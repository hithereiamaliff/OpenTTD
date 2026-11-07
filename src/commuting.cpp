/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file commuting.cpp Commuting: where a town's people work, how they get there, and the roads they take.
 *
 * Once a month (each town on its own day) a town works out its commutes.
 * Its area is cut into cells of 4 by 4 tiles (8 by 8 in big towns). Of the
 * town's people, 45% go to work on a day: from the cells by the people
 * living there (homes; offices, shops and public buildings have a few flats
 * too), to the cells by their jobs (offices, shops, public buildings, city
 * services, industries), fewer the further those are (a gravity model).
 *
 * Each commuter can drive or take transit. Times are in ticks, counting a
 * town tile as about 75 m and a tick as about a third of a second:
 * - By car: the quickest way over the town's roads, a tile in 52 ticks at
 *   60 km/h (town driving: corners, crossings, lights), slower in town
 *   traffic (part 2's congestion levels), with time lost at junctions,
 *   level crossings and drive-through stops; then parking and walking to
 *   work, five minutes in the centre down to half a minute at the edge, half
 *   as long again with parking limits, five more from the edge of a car-free
 *   centre; a congestion charge feels like three more.
 * - By transit: walking to a stop within reach (a station's catchment), 130
 *   ticks a tile; waiting (the line's measured wait, longer when its
 *   vehicles leave full); boarding; the fare (none when changing: one fare a
 *   journey); riding (the line's measured driving time between its stops,
 *   and up to 120 ticks standing at each stop on the way); changing; and
 *   walking from the stop. Walking feels twice as long and waiting half as
 *   long again, as transport planners count them.
 * Of those who could take either, the share taking transit falls with the
 * ratio of the two times (1 / (1 + ratio squared): half when transit takes
 * as long as the car, a fifth when it takes twice as long), at most 85%.
 * At least as many take transit as the share of the town's passengers its
 * services carry says (as town traffic counted before commuting); those
 * riders leave their cars at home all over town.
 *
 * The drivers' routes give every road tile its commuters' cars a day, which
 * town traffic adds to the traffic of the homes and jobs around the road
 * (town_traffic.cpp); routes follow congestion and are averaged over the
 * months, so traffic spreads out over parallel roads. The share taking
 * transit is what transit takes off the road for the whole town
 * (GetTownCarReduction). Homes and workplaces whose commuters take transit
 * a lot (journey times alone) send up to twice the passengers.
 * Everything is worked out from the game state and kept in the save, so it
 * is the same for every player in a network game.
 */

#include "stdafx.h"
#include "commuting.h"
#include "city_services_type.h"
#include "core/math_func.hpp"
#include "debug.h"
#include "fare.h"
#include "house.h"
#include "industry.h"
#include "line.h"
#include "map_func.h"
#include "object_map.h"
#include "order_base.h"
#include "road.h"
#include "road_map.h"
#include "road_pricing.h"
#include "satisfaction.h"
#include "settings_type.h"
#include "station_base.h"
#include "station_map.h"
#include "town.h"
#include "town_budget.h"
#include "town_traffic.h"
#include "town_zoning.h"
#include "tunnelbridge.h"
#include "tunnelbridge_map.h"
#include "vehicle_base.h"
#include "window_func.h"
#include "environment.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"

#include "table/sprites.h"

#include <chrono>
#include <queue>
#include <unordered_map>

#include "safeguards.h"

/** Towns smaller than this don't work out commutes; their traffic stays as in part 2. */
static constexpr uint COMMUTE_MIN_POPULATION = 500;
/** Share of a town's people who go to work on a day, in percent. */
static constexpr uint COMMUTE_RATE = 45;
/**
 * Where commuters live and work, in percent of a building's people: most of
 * a home's live there; offices, shops and public buildings are mostly jobs,
 * with some flats above (OpenTTD counts their people in the population too).
 */
static constexpr uint HOME_RESIDENTS = 100, HOME_JOBS = 10;
static constexpr uint BUSINESS_RESIDENTS = 25, BUSINESS_JOBS = 100;
static constexpr uint PUBLIC_RESIDENTS = 10, PUBLIC_JOBS = 100;
/** Days between two workings-out of a town's commutes. */
static constexpr uint COMMUTE_DAYS = 30;
/** Tiles around the town's edge that still belong to its area. */
static constexpr uint COMMUTE_AREA_MARGIN = 6;
/** With more cells than this with homes, a town uses cells of 8 tiles instead of 4. */
static constexpr uint COMMUTE_MAX_ORIGINS = 200;
/** Distance (squared, in tiles) at which a job draws half as many commuters as one next door. */
static constexpr uint64_t GRAVITY_DISTANCE_SQ = 100;
/** Fixed-point factor for commuter counts while working them out. */
static constexpr uint64_t TRIP_UNIT = 1024;

/** Speed of cars on town roads, and on road types without houses (expressways), in km/h. */
static constexpr uint CAR_SPEED_TOWN = 60;
static constexpr uint CAR_SPEED_FAST = 100;
/** A road vehicle crosses a tile in this many ticks divided by its speed in km/h. */
static constexpr uint TICKS_PER_TILE_KMH = 1536;
/** Time lost at a junction, a junction with traffic lights, a level crossing and a drive-through stop, in ticks. */
static constexpr uint JUNCTION_TICKS = 15;
static constexpr uint LIGHTS_TICKS = 25;
static constexpr uint CROSSING_TICKS = 20;
static constexpr uint ROAD_STOP_TICKS = 10;

/**
 * Time to walk a tile, in ticks. Commuting counts a town tile as about 75 m
 * and a tick as about a third of a second: a car crosses a tile in 9 seconds
 * on town roads (30 km/h with the junctions), a walker in 54 (5 km/h).
 */
static constexpr uint WALK_TICKS_PER_TILE = 130;
/** Walking feels this many times as long as it takes. */
static constexpr uint WALK_WEIGHT = 2;
/** Waiting feels this long, in percent of the time. */
static constexpr uint WAIT_WEIGHT_PERCENT = 150;
/** What boarding a vehicle, and changing on top of it, feel like, in ticks: a minute each. */
static constexpr uint BOARDING_TICKS = 170;
static constexpr uint TRANSFER_TICKS = 170;
/** What paying the standard fare feels like, in ticks: two minutes. */
static constexpr uint FARE_TICKS = 340;
/** What paying a congestion charge feels like, in ticks: three minutes. */
static constexpr uint CHARGE_TICKS = 510;
/** Walk from the edge of a car-free centre, in ticks: five minutes. */
static constexpr uint CAR_FREE_WALK_TICKS = 860;
/** Riders who stay on feel at most this much of a vehicle standing at a stop, in ticks. */
static constexpr uint DWELL_CAP_TICKS = 120;
/** Cars on town roads take this long, in percent of a road vehicle at the same speed (turning, crossings, lights). */
static constexpr uint CAR_TOWN_PERCENT = 200;
/** Highest share of commuters taking transit, in percent. */
static constexpr uint MAX_TRANSIT_SHARE = 85;
/** Up to this share of commuters taking transit, their homes and workplaces send the usual passengers, in percent. */
static constexpr uint RIDERS_BASE_SHARE = 15;

static constexpr uint32_t UNREACHED = UINT32_MAX;

/** All towns' commuters' cars a day on each road tile, in one direction. */
static std::unordered_map<uint32_t, uint32_t> _commute_flows;

/**
 * Do towns work out their commutes (setting economy.commuting, with town traffic on)?
 * @return True if they do.
 */
bool IsCommutingOn()
{
	return _settings_game.economy.commuting && _settings_game.economy.town_traffic != to_underlying(TownTrafficSetting::Off);
}

/**
 * Are a town's commutes worked out?
 * @param t The town.
 * @return True if they are.
 */
bool IsTownCommutingKnown(const Town *t)
{
	return t != nullptr && IsCommutingOn() && t->commuting.valid;
}

/**
 * Commuters' cars on a road tile.
 * @param tile The tile.
 * @return Cars a day in one direction; as many come back.
 */
uint GetCommuteCars(TileIndex tile)
{
	if (_commute_flows.empty()) return 0;
	auto it = _commute_flows.find(tile.base());
	return it == _commute_flows.end() ? 0 : it->second;
}

/**
 * Share of a town's commuters who take transit.
 * @param t The town.
 * @return The share in percent.
 */
uint GetTownCommuteTransitShare(const Town *t)
{
	const TownCommuting &c = t->commuting;
	return c.commuters == 0 ? 0 : static_cast<uint>(static_cast<uint64_t>(c.by_transit) * 100 / c.commuters);
}

/**
 * A commute's time in minutes, at commuting's scale of about a third of a second a tick.
 * @param ticks The time in ticks.
 * @return The minutes, rounded.
 */
uint GetCommuteMinutes(uint ticks)
{
	return (ticks + 85) / 170;
}

/**
 * Share of the commutes of a house's people (a home) or to its jobs (any other building) taken by transit.
 * @param t The town of the house.
 * @param tile A tile of the house.
 * @param hs The house.
 * @param appeal True for the share the journey times alone send to transit,
 *               without the riders the town's passengers account for beyond them.
 * @return The share in percent.
 */
uint GetHouseCommuteShare(const Town *t, TileIndex tile, const HouseSpec &hs, bool appeal)
{
	if (!IsTownCommutingKnown(t)) return 0;
	const TownCommuting &c = t->commuting;
	const int cx = static_cast<int>(TileX(tile) / c.cell_size) - c.grid_x;
	const int cy = static_cast<int>(TileY(tile) / c.cell_size) - c.grid_y;
	if (cx < 0 || cy < 0 || cx >= c.grid_w || cy >= c.grid_h) return 0;
	const size_t cell = static_cast<size_t>(cy) * c.grid_w + cx;
	const bool home = GetHouseKind(hs) == HouseKind::Home;
	const std::vector<uint8_t> &shares = appeal ? (home ? c.home_appeal : c.job_appeal) : (home ? c.home_share : c.job_share);
	return cell < shares.size() ? shares[cell] : 0;
}

/**
 * How many passengers a house sends compared with the usual: more when its
 * commuters take transit a lot (twice as many at 65%), never fewer.
 * @param t The town of the house.
 * @param tile A tile of the house.
 * @param hs The house.
 * @return The amount in percent of the usual.
 */
uint GetCommuterPassengerPercent(const Town *t, TileIndex tile, const HouseSpec &hs)
{
	/* Journey times alone: the riders the town's passengers account for are already passengers. */
	const uint share = GetHouseCommuteShare(t, tile, hs, true);
	if (share <= RIDERS_BASE_SHARE) return 100;
	return std::min<uint>(200, 100 + (share - RIDERS_BASE_SHARE) * 2);
}

/**
 * Recolour palette for a road tile in the commuter traffic map: by commuters' cars a day, both ways.
 * @param tile The tile.
 * @return The palette, or PAL_NONE for no tint.
 */
PaletteID GetCommuteOverlayPalette(TileIndex tile)
{
	if (!IsCommutingOn()) return PAL_NONE;
	const uint cars = GetCommuteCars(tile) * 2;
	if (cars < 100) return PAL_NONE;
	if (cars < 400) return PALETTE_TO_GREEN;
	if (cars < 1000) return PALETTE_TO_YELLOW;
	if (cars < 2000) return PALETTE_TO_ORANGE;
	return PALETTE_TO_RED;
}

/**
 * Add a town's commuters' cars to the cars on each road tile, or take them away.
 * @param c The town's commuting.
 * @param add True to add them, false to take them away.
 */
static void ApplyTownFlows(const TownCommuting &c, bool add)
{
	for (size_t i = 0; i < c.flow_tiles.size(); i++) {
		const uint32_t tile = c.flow_tiles[i];
		const uint32_t cars = c.flow_cars[i];
		if (add) {
			_commute_flows[tile] += cars;
		} else {
			auto it = _commute_flows.find(tile);
			if (it == _commute_flows.end()) continue;
			if (it->second <= cars) {
				_commute_flows.erase(it);
			} else {
				it->second -= cars;
			}
		}
	}
}

/**
 * Forget a town's commutes, e.g. when it is removed or towns stop working them out.
 * @param t The town.
 */
void OnTownCommutingRemoved(Town *t)
{
	ApplyTownFlows(t->commuting, false);
	t->commuting = {};
}

/** Count all towns' commuters' cars on the roads again, e.g. after loading a game. */
void RebuildCommuteFlows()
{
	_commute_flows.clear();
	for (const Town *t : Town::Iterate()) {
		if (t->commuting.flow_tiles.size() != t->commuting.flow_cars.size()) continue;
		ApplyTownFlows(t->commuting, true);
	}
}

/* --- Transit: the lines everyone can ride, worked out on days when a town needs them. --- */

/** A passenger line as commuters see it. */
struct CommuteLine {
	std::vector<StationID> stops; ///< Its stops, in order; it goes round.
	std::vector<uint32_t> distance; ///< Tiles along the route from the first stop to each stop; one more for the way back to the first.
	uint32_t ticks_per_tile16 = 16; ///< Driving time per tile of route, in 1/16 ticks.
	uint32_t dwell = 0; ///< Time standing at each stop, in ticks, as riders who stay on feel it.
	uint32_t wait = 0; ///< Average wait for a vehicle, in ticks, longer when they leave full.
	uint32_t fare = 0; ///< What its fare feels like, in ticks.
};

static std::vector<CommuteLine> _commute_lines; ///< The lines, worked out on a day a town needs them.

/** Work out the passenger lines, as commuters see them. */
static void BuildCommuteLines()
{
	_commute_lines.clear();
	for (const OrderList *list : OrderList::Iterate()) {
		const Vehicle *v = list->GetFirstSharedVehicle();
		if (v == nullptr || v->type == VehicleType::Aircraft || !IsPassengerLine(list)) continue;

		CommuteLine line;
		for (const Order &order : list->GetOrders()) {
			if (!order.IsType(OT_GOTO_STATION) && !order.IsType(OT_IMPLICIT)) continue;
			if (order.IsType(OT_GOTO_STATION) && order.GetNonStopType().Test(OrderNonStopFlag::GoVia)) continue;
			const StationID station = order.GetDestination().ToStationID();
			if (!Station::IsValidID(station)) continue;
			if (!line.stops.empty() && line.stops.back() == station) continue;
			line.stops.push_back(station);
		}
		if (line.stops.size() >= 2 && line.stops.front() == line.stops.back()) line.stops.pop_back();
		if (line.stops.size() < 2) continue;

		const size_t n = line.stops.size();
		uint32_t total = 0;
		line.distance.push_back(0);
		for (size_t i = 0; i < n; i++) {
			total += std::max<uint>(1, DistanceManhattan(Station::Get(line.stops[i])->xy, Station::Get(line.stops[(i + 1) % n])->xy));
			line.distance.push_back(total);
		}

		/*
		 * Riding time from the measured times of the orders: driving between
		 * the stops, and standing at them. OpenTTD's vehicles take a long time
		 * to load; riders who stay on feel only up to DWELL_CAP_TICKS of it
		 * at each stop. Lines without measured times go by their vehicles' speed.
		 */
		uint64_t drive = 0, dwell = 0;
		uint stations = 0;
		for (const Order &order : list->GetOrders()) {
			if (order.IsType(OT_IMPLICIT)) continue;
			drive += order.GetTravelTime();
			if (order.IsType(OT_GOTO_STATION)) {
				dwell += order.GetWaitTime();
				stations++;
			}
		}
		if (drive > 0) {
			line.ticks_per_tile16 = static_cast<uint32_t>(std::max<uint64_t>(16, drive * 16 / total));
		} else {
			const uint speed = std::max(1, v->GetDisplayMaxSpeed());
			line.ticks_per_tile16 = TICKS_PER_TILE_KMH * 16 * 3 / 2 / speed;
		}
		line.dwell = stations == 0 ? 0 : std::min<uint32_t>(DWELL_CAP_TICKS, static_cast<uint32_t>(dwell / stations));
		const TimerGameTick::Ticks round_trip = list->GetTotalDuration();

		uint32_t wait = GetLineAverageWait(list->line);
		if (wait == 0) wait = static_cast<uint32_t>(static_cast<uint64_t>(total) * line.ticks_per_tile16 / 16 / (2 * std::max(1U, list->GetNumVehicles())));
		/* People can't get on full vehicles: waits grow when they leave more than 70% full on average. */
		const LineMonthStats &stats = list->line.last_month;
		if (stats.load_count > 0) {
			const uint load = stats.load_sum / stats.load_count;
			if (load > 70) wait = wait * (100 + (load - 70) * 10) / 100;
		}
		line.wait = wait;

		const FarePolicy fare = GetVehicleFare(v);
		line.fare = fare.scheme == FareScheme::Free ? 0 : FARE_TICKS * fare.price / 100;
		Debug(Facility::Misc, Severity::Trace1, "[commuting] line {} stops {} tiles {} round trip {} drive {} dwell {} ticks/tile {} counted dwell {} wait {} (measured {}) load {} fare {} vehicles {}",
				list->index, line.stops.size(), total, round_trip, drive, dwell, line.ticks_per_tile16 / 16, line.dwell, line.wait, GetLineAverageWait(list->line),
				stats.load_count > 0 ? stats.load_sum / stats.load_count : 0, line.fare, list->GetNumVehicles());
		_commute_lines.push_back(std::move(line));
	}
}

/* --- One town's commutes. --- */

/** A tile of road cars can drive on. */
struct CommuteRoadNode {
	TileIndex tile; ///< The tile.
	uint32_t cost; ///< Ticks to drive across it.
	DiagDirections exits; ///< Sides cars can leave it by to the next tile.
	int32_t far = -1; ///< The other end of a bridge or tunnel starting here, or -1.
	uint32_t far_cost = 0; ///< Ticks to drive over or through it.
};

/** A stop's reach in a cell: walking time to a station from the cell's homes and jobs it covers. */
struct CommuteAccess {
	uint32_t station; ///< Index of the station in the town's transit graph.
	uint64_t weight = 0; ///< Homes and jobs it covers in the cell.
	uint64_t walk_sum = 0; ///< Their tiles to the station, weighted.
};

/** A cell of a town's grid. */
struct CommuteCell {
	uint32_t residents = 0; ///< People living there.
	uint32_t jobs = 0; ///< Jobs there.
	uint64_t sum_x = 0; ///< For the centre of its homes and jobs.
	uint64_t sum_y = 0; ///< For the centre of its homes and jobs.
	uint64_t weight = 0; ///< Homes and jobs, for the centre and for transit coverage.
	uint64_t covered = 0; ///< Homes and jobs within reach of a stop.
	TileIndex centre = INVALID_TILE; ///< Centre of its homes and jobs.
	int32_t road = -1; ///< Road node where its car trips start and end, or -1.
	uint32_t arrive = 0; ///< Parking and walking to work by car, in ticks.
	uint32_t charge = 0; ///< Extra cost of driving there: a congestion charge, in ticks.
	std::vector<CommuteAccess> access; ///< Stops within reach.
};

/** An edge of the transit graph. */
struct CommuteEdge {
	uint32_t to; ///< Node it leads to.
	uint32_t cost; ///< How long it feels.
	uint32_t time; ///< How long it takes.
};

/** Dijkstra queue entry. */
using CommuteQueueItem = std::pair<uint32_t, uint32_t>;
using CommuteQueue = std::priority_queue<CommuteQueueItem, std::vector<CommuteQueueItem>, std::greater<CommuteQueueItem>>;

/**
 * Share of commuters taking transit, out of a thousand, when they could do either.
 * @param car How long driving feels, or UNREACHED.
 * @param transit How long transit feels, or UNREACHED.
 * @return The share in thousandths.
 */
static uint TransitSharePermille(uint64_t car, uint64_t transit)
{
	if (transit == UNREACHED) return 0;
	if (car == UNREACHED) return 1000;
	if (car == 0) return 0;
	const uint64_t ratio = transit * 1000 / car;
	if (ratio >= 5000) return 0;
	const uint64_t share = 1'000'000'000ULL / (1'000'000ULL + ratio * ratio);
	return static_cast<uint>(std::min<uint64_t>(share, MAX_TRANSIT_SHARE * 10));
}

/**
 * Parking and walking to work after driving into a part of town, in ticks.
 * @param t The town.
 * @param zone The part of town.
 * @return The ticks.
 */
static uint32_t GetParkingTicks(const Town *t, HouseZone zone)
{
	uint32_t ticks;
	switch (zone) {
		case HouseZone::TownCentre: ticks = 860; break; // five minutes
		case HouseZone::TownInnerSuburb: ticks = 510; break; // three
		case HouseZone::TownOuterSuburb: ticks = 340; break; // two
		case HouseZone::TownOutskirt: ticks = 170; break; // one
		default: ticks = 85; break;
	}
	const bool central = zone == HouseZone::TownCentre || zone == HouseZone::TownInnerSuburb;
	if (central && HasOrdinance(t, Ordinance::ParkingLimits)) ticks = ticks * 3 / 2;
	if (zone == HouseZone::TownCentre && HasOrdinance(t, Ordinance::CarFreeCentre)) ticks += CAR_FREE_WALK_TICKS;
	return ticks;
}

/**
 * The sides of a tile cars can drive off it by, for the road graph.
 * @param tile The tile.
 * @param[out] bridge_end The other end of a bridge or tunnel starting here, or INVALID_TILE.
 * @return The sides; none if cars can't use the tile.
 */
static DiagDirections GetCommuteRoadExits(TileIndex tile, TileIndex &bridge_end)
{
	bridge_end = INVALID_TILE;
	switch (GetTileType(tile)) {
		case TileType::Road: {
			if (IsRoadDepot(tile) || !HasTileRoadType(tile, RoadTramType::Road) || GetRoadTypeRoad(tile) == ROADTYPE_BUS) return {};
			if (IsLevelCrossing(tile)) return AxisToDiagDirs(GetCrossingRoadAxis(tile));
			const RoadBits bits = GetRoadBits(tile, RoadTramType::Road);
			DiagDirections exits{};
			for (DiagDirection d = DiagDirection::Begin; d < DiagDirection::End; d++) {
				if (bits.Any(DiagDirToRoadBits(d))) exits.Set(d);
			}
			return exits;
		}

		case TileType::Station:
			if (!IsDriveThroughStopTile(tile) || !HasTileRoadType(tile, RoadTramType::Road) || GetRoadTypeRoad(tile) == ROADTYPE_BUS) return {};
			return AxisToDiagDirs(GetDriveThroughStopAxis(tile));

		case TileType::TunnelBridge:
			if (GetTunnelBridgeTransportType(tile) != TransportType::Road || !HasTileRoadType(tile, RoadTramType::Road) || GetRoadTypeRoad(tile) == ROADTYPE_BUS) return {};
			bridge_end = GetOtherTunnelBridgeEnd(tile);
			return DiagDirections{ReverseDiagDir(GetTunnelBridgeDirection(tile))};

		default:
			return {};
	}
}

/**
 * Ticks for a car to cross a tile at the speed its road type and town traffic allow.
 * @param tile The tile.
 * @return The ticks.
 */
static uint32_t GetCommuteTileTicks(TileIndex tile)
{
	uint speed = CAR_SPEED_TOWN;
	const RoadType rt = GetRoadTypeRoad(tile);
	if (rt != INVALID_ROADTYPE) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
		if (rti->flags.Test(RoadTypeFlag::NoHouses)) speed = CAR_SPEED_FAST;
		if (rti->max_speed > 0) speed = std::min<uint>(speed, std::max(1, rti->max_speed / 2));
	}
	if (IsTileType(tile, TileType::Road)) {
		const int limit = GetRoadCongestionSpeedLimitKmh(GetRoadCongestion(tile));
		if (limit > 0) speed = std::min<uint>(speed, limit);
	}
	return CeilDiv(TICKS_PER_TILE_KMH * CAR_TOWN_PERCENT / 100, speed);
}

/** Offsets of the neighbouring tile on each side, in the order of #DiagDirection. */
static constexpr int COMMUTE_DX[] = {-1, 0, 1, 0};
static constexpr int COMMUTE_DY[] = {0, 1, 0, -1};

/**
 * Work out a town's commutes: who works where, who drives and who takes
 * transit, and the roads the drivers take.
 * @param t The town.
 */
void UpdateTownCommuting(Town *t)
{
	const auto started = std::chrono::steady_clock::now();
	if (!IsCommutingOn() || t->cache.population < COMMUTE_MIN_POPULATION) {
		if (t->commuting.valid || !t->commuting.flow_tiles.empty()) OnTownCommutingRemoved(t);
		return;
	}

	/* The town's area. */
	const uint radius = IntSqrt(t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)]) + COMMUTE_AREA_MARGIN;
	const uint x0 = static_cast<uint>(std::max<int>(1, static_cast<int>(TileX(t->xy)) - static_cast<int>(radius)));
	const uint y0 = static_cast<uint>(std::max<int>(1, static_cast<int>(TileY(t->xy)) - static_cast<int>(radius)));
	const uint x1 = std::min<uint>(Map::MaxX() - 1, TileX(t->xy) + radius);
	const uint y1 = std::min<uint>(Map::MaxY() - 1, TileY(t->xy) + radius);
	if (x1 <= x0 || y1 <= y0) return;
	const uint w = x1 - x0 + 1;
	const uint h = y1 - y0 + 1;
	auto local = [&](uint x, uint y) { return (y - y0) * w + (x - x0); };

	/* Homes and jobs on each tile; roads. */
	std::vector<uint32_t> tile_residents(static_cast<size_t>(w) * h, 0);
	std::vector<uint32_t> tile_jobs(static_cast<size_t>(w) * h, 0);
	std::vector<int32_t> node_of(static_cast<size_t>(w) * h, -1);
	std::vector<CommuteRoadNode> nodes;
	std::vector<TileIndex> bridge_ends;
	for (uint y = y0; y <= y1; y++) {
		for (uint x = x0; x <= x1; x++) {
			const TileIndex tile = TileXY(x, y);
			switch (GetTileType(tile)) {
				case TileType::House: {
					if (GetTownIndex(tile) != t->index || !IsHouseCompleted(tile)) break;
					const HouseSpec *hs = HouseSpec::Get(GetHouseType(tile));
					switch (GetHouseKind(*hs)) {
						case HouseKind::Home:
							tile_residents[local(x, y)] = hs->population * HOME_RESIDENTS;
							tile_jobs[local(x, y)] = hs->population * HOME_JOBS;
							break;
						case HouseKind::Business:
							tile_residents[local(x, y)] = hs->population * BUSINESS_RESIDENTS;
							tile_jobs[local(x, y)] = hs->population * BUSINESS_JOBS;
							break;
						case HouseKind::Public:
							tile_residents[local(x, y)] = hs->population * PUBLIC_RESIDENTS;
							tile_jobs[local(x, y)] = hs->population * PUBLIC_JOBS;
							break;
					}
					break;
				}

				case TileType::Object:
					if (IsCityServiceObjectType(GetObjectType(tile))) tile_jobs[local(x, y)] = 30 * 100;
					break;

				case TileType::Industry:
					if (Industry::GetByTile(tile)->town == t) tile_jobs[local(x, y)] = 10 * 100;
					break;

				default: {
					TileIndex bridge_end;
					const DiagDirections exits = GetCommuteRoadExits(tile, bridge_end);
					if (exits.None()) break;
					CommuteRoadNode node{tile, GetCommuteTileTicks(tile), exits};
					if (IsTileType(tile, TileType::Road) && IsNormalRoad(tile) && GetRoadBits(tile, RoadTramType::Road).Count() >= 3) {
						node.cost += HasTrafficLight(tile) ? LIGHTS_TICKS : JUNCTION_TICKS;
					} else if (IsTileType(tile, TileType::Road) && IsLevelCrossing(tile)) {
						node.cost += CROSSING_TICKS;
					} else if (IsTileType(tile, TileType::Station)) {
						node.cost += ROAD_STOP_TICKS;
					}
					node_of[local(x, y)] = static_cast<int32_t>(nodes.size());
					nodes.push_back(node);
					bridge_ends.push_back(bridge_end);
					break;
				}
			}
		}
	}
	/* Bridges and tunnels join their two ends. */
	for (size_t i = 0; i < nodes.size(); i++) {
		const TileIndex end = bridge_ends[i];
		if (end == INVALID_TILE) continue;
		const uint ex = TileX(end), ey = TileY(end);
		if (ex < x0 || ex > x1 || ey < y0 || ey > y1) continue;
		const int32_t other = node_of[local(ex, ey)];
		if (other < 0) continue;
		nodes[i].far = other;
		nodes[i].far_cost = (GetTunnelBridgeLength(nodes[i].tile, end) + 1) * GetCommuteTileTicks(nodes[i].tile);
	}

	/* The grid: cells of 4 tiles, or 8 in big towns. */
	uint cell_size = 4;
	uint origins = 0;
	for (int attempt = 0; attempt < 2; attempt++) {
		std::set<uint> with_homes;
		for (uint y = y0; y <= y1; y++) {
			for (uint x = x0; x <= x1; x++) {
				if (tile_residents[local(x, y)] > 0) with_homes.insert((y / cell_size) * Map::SizeX() + x / cell_size);
			}
		}
		origins = static_cast<uint>(with_homes.size());
		if (origins <= COMMUTE_MAX_ORIGINS) break;
		cell_size = 8;
	}
	const uint gx = x0 / cell_size, gy = y0 / cell_size;
	const uint gw = x1 / cell_size - gx + 1, gh = y1 / cell_size - gy + 1;
	auto cell_of = [&](uint x, uint y) { return (y / cell_size - gy) * gw + (x / cell_size - gx); };

	std::vector<CommuteCell> cells(static_cast<size_t>(gw) * gh);
	for (uint y = y0; y <= y1; y++) {
		for (uint x = x0; x <= x1; x++) {
			const uint32_t residents = tile_residents[local(x, y)];
			const uint32_t jobs = tile_jobs[local(x, y)];
			if (residents == 0 && jobs == 0) continue;
			CommuteCell &cell = cells[cell_of(x, y)];
			cell.residents += residents;
			cell.jobs += jobs;
			cell.sum_x += static_cast<uint64_t>(x) * (residents + jobs);
			cell.sum_y += static_cast<uint64_t>(y) * (residents + jobs);
			cell.weight += residents + jobs;
		}
	}

	/* Each cell's centre, its road and what arriving there by car costs. */
	std::vector<std::vector<int32_t>> cell_nodes(cells.size());
	for (size_t i = 0; i < nodes.size(); i++) cell_nodes[cell_of(TileX(nodes[i].tile), TileY(nodes[i].tile))].push_back(static_cast<int32_t>(i));
	const bool charging = t->transit.congestion_charge;
	for (uint cy = 0; cy < gh; cy++) {
		for (uint cx = 0; cx < gw; cx++) {
			CommuteCell &cell = cells[cy * gw + cx];
			if (cell.weight == 0) continue;
			const uint mx = static_cast<uint>(cell.sum_x / cell.weight), my = static_cast<uint>(cell.sum_y / cell.weight);
			cell.centre = TileXY(mx, my);
			uint best = UINT_MAX;
			for (int ny = static_cast<int>(cy) - 1; ny <= static_cast<int>(cy) + 1; ny++) {
				for (int nx = static_cast<int>(cx) - 1; nx <= static_cast<int>(cx) + 1; nx++) {
					if (nx < 0 || ny < 0 || nx >= static_cast<int>(gw) || ny >= static_cast<int>(gh)) continue;
					for (int32_t n : cell_nodes[ny * gw + nx]) {
						const uint d = DistanceManhattan(cell.centre, nodes[n].tile);
						if (d < best) {
							best = d;
							cell.road = n;
						}
					}
				}
			}
			const HouseZone zone = GetTownRadiusGroup(t, cell.centre);
			cell.arrive = GetParkingTicks(t, zone) + (best == UINT_MAX ? 0 : best * WALK_TICKS_PER_TILE);
			if (charging && IsInCongestionChargeZone(t, cell.centre)) cell.charge = CHARGE_TICKS;
		}
	}

	/* Transit: the stations in and near the town, and the lines between them. */
	std::map<StationID, uint32_t> station_index;
	std::vector<const Station *> stations;
	auto near_town = [&](TileIndex tile) {
		return TileX(tile) + 10 >= x0 && TileX(tile) <= x1 + 10 && TileY(tile) + 10 >= y0 && TileY(tile) <= y1 + 10;
	};
	struct LineStops {
		const CommuteLine *line;
		std::vector<uint32_t> at; ///< Indices into line->stops of its stops near the town.
	};
	std::vector<LineStops> town_lines;
	for (const CommuteLine &line : _commute_lines) {
		LineStops ls{&line, {}};
		for (uint32_t i = 0; i < line.stops.size(); i++) {
			const Station *st = Station::GetIfValid(line.stops[i]);
			if (st != nullptr && near_town(st->xy)) ls.at.push_back(i);
		}
		if (ls.at.size() < 2) continue;
		for (uint32_t i : ls.at) {
			if (station_index.emplace(line.stops[i], static_cast<uint32_t>(stations.size())).second) stations.push_back(Station::Get(line.stops[i]));
		}
		town_lines.push_back(std::move(ls));
	}

	/*
	 * Graph: per station a node before riding (reached on foot) and one after
	 * (got off); per line and stop a node for leaving the stop on board and
	 * one for arriving there on board, so nobody gets off where they got on.
	 */
	const uint32_t station_nodes = static_cast<uint32_t>(stations.size()) * 2;
	std::vector<std::vector<CommuteEdge>> transit_edges(station_nodes);
	for (const LineStops &ls : town_lines) {
		const CommuteLine &line = *ls.line;
		const uint32_t first = static_cast<uint32_t>(transit_edges.size());
		const uint32_t k = static_cast<uint32_t>(ls.at.size());
		transit_edges.resize(first + 2 * k);
		const uint32_t wait_cost = line.wait * WAIT_WEIGHT_PERCENT / 100;
		const uint32_t route = line.distance.back();
		for (uint32_t j = 0; j < k; j++) {
			const uint32_t s = station_index[line.stops[ls.at[j]]];
			const uint32_t depart = first + 2 * j;
			const uint32_t arrive = first + 2 * j + 1;
			/* Boarding from the street pays the fare; changing doesn't (one fare a journey). */
			transit_edges[2 * s].push_back({depart, BOARDING_TICKS + line.fare + wait_cost, line.wait});
			transit_edges[2 * s + 1].push_back({depart, BOARDING_TICKS + TRANSFER_TICKS + wait_cost, line.wait});
			/* Arriving: get off, or stay on while it stands at the stop. */
			transit_edges[arrive].push_back({2 * s + 1, 0, 0});
			transit_edges[arrive].push_back({depart, line.dwell, line.dwell});
			/* Riding on to the next stop near the town, round the end of the route if need be. */
			const uint32_t from = line.distance[ls.at[j]];
			const uint32_t to = line.distance[ls.at[(j + 1) % k]];
			const uint32_t tiles = (j + 1 < k) ? to - from : route - from + to;
			const uint32_t ride = static_cast<uint32_t>(static_cast<uint64_t>(tiles) * line.ticks_per_tile16 / 16);
			transit_edges[depart].push_back({first + 2 * ((j + 1) % k) + 1, ride, ride});
		}
	}

	/* Which homes and jobs are within reach of which stations. */
	std::vector<uint8_t> covered(static_cast<size_t>(w) * h, 0);
	for (uint32_t s = 0; s < stations.size(); s++) {
		const Station *st = stations[s];
		for (TileIndex tile : st->catchment_tiles) {
			const uint x = TileX(tile), y = TileY(tile);
			if (x < x0 || x > x1 || y < y0 || y > y1) continue;
			const uint64_t weight = tile_residents[local(x, y)] + tile_jobs[local(x, y)];
			if (weight == 0) continue;
			CommuteCell &cell = cells[cell_of(x, y)];
			if (covered[local(x, y)] == 0) {
				covered[local(x, y)] = 1;
				cell.covered += weight;
			}
			auto it = std::ranges::find_if(cell.access, [s](const CommuteAccess &a) { return a.station == s; });
			if (it == cell.access.end()) it = cell.access.insert(cell.access.end(), {s});
			it->weight += weight;
			it->walk_sum += weight * DistanceManhattan(tile, st->xy);
		}
	}

	/* The commuters: a share of the town's people, from each cell by the people living there. */
	uint64_t all_residents = 0;
	for (const CommuteCell &cell : cells) all_residents += cell.residents;
	if (all_residents == 0) return;
	const uint64_t all_leaving = static_cast<uint64_t>(t->cache.population) * COMMUTE_RATE * TRIP_UNIT / 100;

	/* The commutes, origin by origin: first how each would go, then the drivers' routes. */
	const uint32_t transit_nodes = static_cast<uint32_t>(transit_edges.size());
	std::vector<uint32_t> car_dist(nodes.size()), car_pred_order;
	std::vector<int32_t> car_pred(nodes.size());
	std::vector<uint32_t> transit_cost(transit_nodes), transit_time(transit_nodes);

	/* Driving: the quickest way from a cell to every road. */
	auto drive_from = [&](const CommuteCell &from) {
		std::ranges::fill(car_dist, UNREACHED);
		car_pred_order.clear();
		if (from.road < 0) return;
		CommuteQueue queue;
		car_dist[from.road] = 0;
		car_pred[from.road] = -1;
		queue.emplace(0, static_cast<uint32_t>(from.road));
		while (!queue.empty()) {
			const auto [cost, u] = queue.top();
			queue.pop();
			if (cost != car_dist[u]) continue;
			car_pred_order.push_back(u);
			const CommuteRoadNode &node = nodes[u];
			const uint ux = TileX(node.tile), uy = TileY(node.tile);
			auto relax = [&](int32_t v, uint32_t c) {
				if (c < car_dist[v]) {
					car_dist[v] = c;
					car_pred[v] = static_cast<int32_t>(u);
					queue.emplace(c, static_cast<uint32_t>(v));
				}
			};
			for (DiagDirection dir = DiagDirection::Begin; dir < DiagDirection::End; dir++) {
				if (!node.exits.Test(dir)) continue;
				const int nx = static_cast<int>(ux) + COMMUTE_DX[to_underlying(dir)];
				const int ny = static_cast<int>(uy) + COMMUTE_DY[to_underlying(dir)];
				if (nx < static_cast<int>(x0) || ny < static_cast<int>(y0) || nx > static_cast<int>(x1) || ny > static_cast<int>(y1)) continue;
				const int32_t v = node_of[local(static_cast<uint>(nx), static_cast<uint>(ny))];
				if (v < 0 || !nodes[v].exits.Test(ReverseDiagDir(dir))) continue;
				relax(v, cost + nodes[v].cost);
			}
			if (node.far >= 0) relax(node.far, cost + node.far_cost + nodes[node.far].cost);
		}
	};

	/** The commuters from one cell to another. */
	struct CommutePair {
		uint32_t from; ///< Cell they live in.
		uint32_t to; ///< Cell they work in.
		uint64_t trips; ///< How many, in TRIP_UNIT.
		uint32_t car; ///< How long driving feels, or UNREACHED.
		uint32_t car_time; ///< How long driving takes.
		uint32_t ride_time; ///< How long transit takes, or 0 if it doesn't go there.
		uint64_t room; ///< How many more could take transit (those within reach of stops, up to the highest share), in TRIP_UNIT.
		uint64_t transit; ///< How many take transit, in TRIP_UNIT.
		uint64_t chosen; ///< How many of those the journey times alone send to transit, in TRIP_UNIT.
	};
	std::vector<CommutePair> pairs;
	uint64_t commuters = 0, model_riders = 0;
	/* Each origin's quickest ways, kept for the drivers' routes. */
	std::vector<std::vector<int32_t>> origin_pred(cells.size());
	std::vector<std::vector<uint32_t>> origin_order(cells.size());

	for (uint32_t o = 0; o < cells.size(); o++) {
		const CommuteCell &from = cells[o];
		if (from.residents == 0) continue;

		/* Where they work: jobs, fewer the further away. */
		uint64_t total_pull = 0;
		std::vector<uint64_t> pull(cells.size(), 0);
		for (uint32_t d = 0; d < cells.size(); d++) {
			const CommuteCell &to = cells[d];
			if (d == o || to.jobs == 0) continue;
			const uint64_t dist = DistanceManhattan(from.centre, to.centre);
			pull[d] = static_cast<uint64_t>(to.jobs) * 16 * GRAVITY_DISTANCE_SQ / (GRAVITY_DISTANCE_SQ + dist * dist);
			total_pull += pull[d];
		}
		if (total_pull == 0) continue;
		const uint64_t leaving = all_leaving * from.residents / all_residents;

		drive_from(from);
		const size_t pairs_before = pairs.size();

		/* Transit: the best way to every station, from the stops within reach. */
		std::ranges::fill(transit_cost, UNREACHED);
		std::ranges::fill(transit_time, 0);
		if (!from.access.empty() && transit_nodes > 0) {
			CommuteQueue queue;
			for (const CommuteAccess &a : from.access) {
				const uint32_t walk = static_cast<uint32_t>(a.walk_sum / a.weight) * WALK_TICKS_PER_TILE + WALK_TICKS_PER_TILE / 2;
				const uint32_t n = 2 * a.station;
				if (walk * WALK_WEIGHT < transit_cost[n]) {
					transit_cost[n] = walk * WALK_WEIGHT;
					transit_time[n] = walk;
					queue.emplace(transit_cost[n], n);
				}
			}
			while (!queue.empty()) {
				const auto [cost, u] = queue.top();
				queue.pop();
				if (cost != transit_cost[u]) continue;
				for (const CommuteEdge &e : transit_edges[u]) {
					if (cost + e.cost < transit_cost[e.to]) {
						transit_cost[e.to] = cost + e.cost;
						transit_time[e.to] = transit_time[u] + e.time;
						queue.emplace(transit_cost[e.to], e.to);
					}
				}
			}
		}

		/* Each destination: drive or ride? */
		for (uint32_t d = 0; d < cells.size(); d++) {
			if (pull[d] == 0) continue;
			const CommuteCell &to = cells[d];
			const uint64_t trips = leaving * pull[d] / total_pull;
			if (trips == 0) continue;

			CommutePair pair{o, d, trips, UNREACHED, 0, 0, 0, 0, 0};
			if (to.road >= 0 && car_dist[to.road] != UNREACHED) {
				pair.car_time = car_dist[to.road] + to.arrive;
				pair.car = pair.car_time + to.charge;
			}
			uint32_t transit = UNREACHED;
			for (const CommuteAccess &a : to.access) {
				const uint32_t post = 2 * a.station + 1;
				if (transit_cost[post] == UNREACHED) continue;
				const uint32_t walk = static_cast<uint32_t>(a.walk_sum / a.weight) * WALK_TICKS_PER_TILE + WALK_TICKS_PER_TILE / 2;
				const uint32_t cost = transit_cost[post] + walk * WALK_WEIGHT;
				if (cost < transit) {
					transit = cost;
					pair.ride_time = transit_time[post] + walk;
				}
			}
			if (pair.car == UNREACHED && transit == UNREACHED) continue;

			if (pair.car == UNREACHED) {
				/* Without a road, everyone rides. */
				pair.transit = trips;
			} else if (transit != UNREACHED) {
				/* Only the homes and jobs within reach of stops can choose transit. */
				const uint64_t reach = static_cast<uint64_t>(1000) * from.covered / from.weight * to.covered / to.weight;
				pair.transit = trips * TransitSharePermille(pair.car, transit) / 1000 * reach / 1000;
				const uint64_t most = trips * MAX_TRANSIT_SHARE / 100 * reach / 1000;
				pair.room = most > pair.transit ? most - pair.transit : 0;
			}
			pair.chosen = pair.transit;
			commuters += trips;
			model_riders += pair.transit;
			pairs.push_back(pair);
		}
		if (pairs.size() > pairs_before) {
			origin_pred[o] = car_pred;
			origin_order[o] = car_pred_order;
		}
	}

	/*
	 * At least as many commuters take transit as the share of the town's
	 * passengers its services carry says (as town traffic counted before
	 * commuting). Those beyond the ones the journey times account for are
	 * found among the drivers within reach of stops; if there aren't enough
	 * of them, the rest leave their cars at home all over town.
	 */
	const uint64_t floor = commuters * GetTownRidershipCarReduction(t) / 100;
	if (floor > model_riders) {
		uint64_t extra = floor - model_riders;
		uint64_t room = 0;
		for (const CommutePair &pair : pairs) room += pair.room;
		if (room > 0) {
			const uint64_t used = std::min(extra, room);
			for (CommutePair &pair : pairs) pair.transit += pair.room * used / room;
			extra -= used;
		}
		uint64_t drivers = 0;
		for (const CommutePair &pair : pairs) {
			if (pair.car != UNREACHED) drivers += pair.trips - pair.transit;
		}
		if (extra > 0 && drivers > 0) {
			for (CommutePair &pair : pairs) {
				if (pair.car != UNREACHED) pair.transit += (pair.trips - pair.transit) * std::min(extra, drivers) / drivers;
			}
		}
	}

	/* The drivers' routes: back from each destination along the quickest ways. */
	std::vector<uint64_t> car_acc(nodes.size(), 0), flow(nodes.size(), 0);
	std::vector<uint64_t> home_total(cells.size(), 0), home_transit(cells.size(), 0), job_total(cells.size(), 0), job_transit(cells.size(), 0);
	std::vector<uint64_t> home_appeal(cells.size(), 0), job_appeal(cells.size(), 0);
	uint64_t by_transit = 0, by_car = 0, car_time_sum = 0, timed_riders = 0, transit_time_sum = 0, could_ride = 0, could_ride_time_sum = 0;
	for (size_t i = 0; i < pairs.size();) {
		const uint32_t o = pairs[i].from;
		const std::vector<int32_t> &pred = origin_pred[o];
		const std::vector<uint32_t> &order = origin_order[o];
		for (; i < pairs.size() && pairs[i].from == o; i++) {
			const CommutePair &pair = pairs[i];
			const uint64_t car_trips = pair.car == UNREACHED ? 0 : pair.trips - pair.transit;
			by_transit += pair.transit;
			by_car += car_trips;
			car_time_sum += car_trips * pair.car_time;
			if (pair.ride_time > 0) {
				timed_riders += pair.transit;
				transit_time_sum += pair.transit * pair.ride_time;
				could_ride += pair.trips;
				could_ride_time_sum += pair.trips * pair.ride_time;
			}
			home_total[o] += pair.trips;
			home_transit[o] += pair.transit;
			home_appeal[o] += pair.chosen;
			job_total[pair.to] += pair.trips;
			job_transit[pair.to] += pair.transit;
			job_appeal[pair.to] += pair.chosen;
			if (car_trips > 0) car_acc[cells[pair.to].road] += car_trips;
		}
		for (auto it = order.rbegin(); it != order.rend(); ++it) {
			const uint32_t u = *it;
			if (car_acc[u] == 0) continue;
			flow[u] += car_acc[u];
			if (pred[u] >= 0) car_acc[pred[u]] += car_acc[u];
			car_acc[u] = 0;
		}
	}
	/* Cars on each road, averaged with last month's so routes settle as traffic moves them about. */
	TownCommuting &c = t->commuting;
	std::vector<std::pair<uint32_t, uint32_t>> fresh;
	for (size_t i = 0; i < nodes.size(); i++) {
		const uint32_t cars = static_cast<uint32_t>((flow[i] + TRIP_UNIT / 2) / TRIP_UNIT);
		if (cars > 0) fresh.emplace_back(nodes[i].tile.base(), cars);
	}
	std::ranges::sort(fresh);
	std::vector<uint32_t> tiles;
	std::vector<uint16_t> cars;
	{
		size_t i = 0, j = 0;
		const bool smooth = c.valid && c.flow_tiles.size() == c.flow_cars.size();
		const size_t old_n = smooth ? c.flow_tiles.size() : 0;
		while (i < old_n || j < fresh.size()) {
			uint32_t tile;
			uint32_t value;
			if (j >= fresh.size() || (i < old_n && c.flow_tiles[i] < fresh[j].first)) {
				tile = c.flow_tiles[i];
				value = c.flow_cars[i] / 2;
				i++;
			} else if (i >= old_n || fresh[j].first < c.flow_tiles[i]) {
				tile = fresh[j].first;
				value = smooth ? (fresh[j].second + 1) / 2 : fresh[j].second;
				j++;
			} else {
				tile = fresh[j].first;
				value = (c.flow_cars[i] + fresh[j].second + 1) / 2;
				i++;
				j++;
			}
			if (value == 0) continue;
			tiles.push_back(tile);
			cars.push_back(static_cast<uint16_t>(std::min<uint32_t>(value, UINT16_MAX)));
		}
	}
	ApplyTownFlows(c, false);
	c.flow_tiles = std::move(tiles);
	c.flow_cars = std::move(cars);
	ApplyTownFlows(c, true);

	c.valid = true;
	c.cell_size = static_cast<uint8_t>(cell_size);
	c.commuters = static_cast<uint32_t>(commuters / TRIP_UNIT);
	c.by_transit = static_cast<uint32_t>(by_transit / TRIP_UNIT);
	c.car_time = by_car == 0 ? 0 : static_cast<uint32_t>(car_time_sum / by_car);
	/* Transit's time: of those who take it where it goes; else of all the trips it could serve. */
	if (timed_riders > 0) {
		c.transit_time = static_cast<uint32_t>(transit_time_sum / timed_riders);
	} else {
		c.transit_time = could_ride == 0 ? 0 : static_cast<uint32_t>(could_ride_time_sum / could_ride);
	}
	c.grid_x = static_cast<uint16_t>(gx);
	c.grid_y = static_cast<uint16_t>(gy);
	c.grid_w = static_cast<uint16_t>(gw);
	c.grid_h = static_cast<uint16_t>(gh);
	c.home_share.assign(cells.size(), 0);
	c.job_share.assign(cells.size(), 0);
	c.home_appeal.assign(cells.size(), 0);
	c.job_appeal.assign(cells.size(), 0);
	for (size_t i = 0; i < cells.size(); i++) {
		if (home_total[i] > 0) {
			c.home_share[i] = static_cast<uint8_t>(home_transit[i] * 100 / home_total[i]);
			c.home_appeal[i] = static_cast<uint8_t>(home_appeal[i] * 100 / home_total[i]);
		}
		if (job_total[i] > 0) {
			c.job_share[i] = static_cast<uint8_t>(job_transit[i] * 100 / job_total[i]);
			c.job_appeal[i] = static_cast<uint8_t>(job_appeal[i] * 100 / job_total[i]);
		}
	}

	Debug(Facility::Misc, Severity::Debug2, "[commuting] town {} pop {} cells {}x{} of {} origins {} roads {} stations {} lines {} commuters {} transit {} (journey times {}) car {} pt {} in {} us",
			t->index, t->cache.population, gw, gh, cell_size, origins, nodes.size(), stations.size(), town_lines.size(), c.commuters, c.by_transit, model_riders / TRIP_UNIT, c.car_time, c.transit_time,
			std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
	if (IsVisibleSeverity(Facility::Misc, Severity::Trace1)) {
		for (size_t i = 0; i < cells.size(); i++) {
			const CommuteCell &cell = cells[i];
			if (cell.weight == 0) continue;
			Debug(Facility::Misc, Severity::Trace1, "[commuting]   cell {},{} res {} jobs {} covered {}% stops {} road {} home {}% job {}% out {} in {}",
					i % gw, i / gw, cell.residents / 100, cell.jobs / 100, cell.covered * 100 / cell.weight, cell.access.size(), cell.road >= 0,
					c.home_share[i], c.job_share[i], home_total[i] / TRIP_UNIT, job_total[i] / TRIP_UNIT);
		}
	}
}

/** Each day, the towns whose turn it is work out their commutes. */
static const IntervalTimer<TimerGameEconomy> _commuting_daily({TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [](auto)
{
	if (!IsCommutingOn()) return;
	const uint day = static_cast<uint>(TimerGameEconomy::date.base());
	bool any = false;
	for (Town *t : Town::Iterate()) {
		if ((t->index.base() + day) % COMMUTE_DAYS != 0) continue;
		if (!any) {
			BuildCommuteLines();
			any = true;
		}
		UpdateTownCommuting(t);
	}
	if (!any) return;
	_commute_lines.clear();
	SetWindowClassesDirty(WindowClass::TownView);
	if (GetEnvironmentOverlay() == EnvironmentOverlay::Commuters) MarkWholeScreenDirty();
});

/** The setting changed: towns forget their commutes when it goes off, and work them out at once when it comes on. */
void OnCommutingSettingChanged()
{
	if (!IsCommutingOn()) {
		for (Town *t : Town::Iterate()) t->commuting = {};
		_commute_flows.clear();
	} else {
		BuildCommuteLines();
		for (Town *t : Town::Iterate()) UpdateTownCommuting(t);
		_commute_lines.clear();
	}
	SetWindowClassesDirty(WindowClass::TownView);
	MarkWholeScreenDirty();
}

/** Forget all commutes; used when starting or loading a game. */
void InitializeCommuting()
{
	_commute_flows.clear();
	_commute_lines.clear();
}
