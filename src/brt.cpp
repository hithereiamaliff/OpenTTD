/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file brt.cpp Bus rapid transit.
 *
 *  - Stations on viaducts: OpenTTD can't put a station on a bridge, so a
 *    drive-through road stop between the ends of two concrete viaducts (or a
 *    run of up to four such stops) is raised to the viaducts' height, and the
 *    viaduct ends next to it are flat instead of ramps. Buses drive through at
 *    the viaduct's height. Nothing is saved for this: it follows from the map.
 *  - BRT stations: a drive-through bus stop with fare gates and level
 *    platforms. Riders pay before they board and walk in through every door,
 *    so buses load twice as fast; low-floor buses (the city buses) three times
 *    as fast.
 *  - The BRT score of a bus line, each month, from the basics of the BRT
 *    standard: dedicated lanes, running apart from the traffic, paying before
 *    boarding, level boarding and priority at junctions, minus points for
 *    sections shared with the traffic. Stations served by a line that makes
 *    the grade rate better, and towns prefer such lines in tenders.
 */

#include "stdafx.h"
#include "brt.h"
#include "bridge.h"
#include "bridge_map.h"
#include "bus_priority.h"
#include "bus_priority_cmd.h"
#include "city_bus.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "economy_func.h"
#include "line_type.h"
#include "order_base.h"
#include "road_junction.h"
#include "road_map.h"
#include "roadveh.h"
#include "station_base.h"
#include "station_map.h"
#include "traffic_light.h"
#include "transparency.h"
#include "tunnelbridge.h"
#include "tunnelbridge_map.h"
#include "tile_cmd.h"
#include "viewport_func.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

static std::set<TileIndex> _brt_stations; ///< Drive-through bus stops that are BRT stations.

/**
 * The BRT stations, for saving and loading.
 * @return The stop tiles.
 */
std::set<TileIndex> &GetBrtStations()
{
	return _brt_stations;
}

/** Forget all BRT stations; used when starting or loading a game. */
void InitializeBrt()
{
	_brt_stations.clear();
}

/* Stations on viaducts. */

/** Longest run of drive-through stops between the ends of two viaducts that is raised onto them. */
static constexpr uint ELEVATED_STOP_MAX_RUN = 4;

/**
 * Is a tile the end of a concrete road viaduct on flat land at a height, leading away in a direction?
 * @param tile The tile.
 * @param dir The direction the viaduct must lead in.
 * @param z The height the end must be at.
 * @return True if it is.
 */
static bool IsViaductEndLeading(TileIndex tile, DiagDirection dir, int z)
{
	if (!IsBridgeTile(tile) || GetBridgeType(tile) != BRIDGE_CONCRETE_VIADUCT) return false;
	if (GetTunnelBridgeTransportType(tile) != TransportType::Road || GetTunnelBridgeDirection(tile) != dir) return false;
	auto [slope, tz] = GetTileSlopeZ(tile);
	return slope == SLOPE_FLAT && tz == z;
}

/**
 * Can a tile be part of a run of stops raised onto a viaduct?
 * @param tile The tile.
 * @param axis The axis of the run.
 * @param z The height of the run.
 * @return True if it can.
 */
static bool IsElevatableStop(TileIndex tile, Axis axis, int z)
{
	if (!IsTileType(tile, TileType::Station) || !IsDriveThroughStopTile(tile) || IsRoadWaypoint(tile)) return false;
	if (GetDriveThroughStopAxis(tile) != axis || IsBridgeAbove(tile)) return false;
	auto [slope, tz] = GetTileSlopeZ(tile);
	return slope == SLOPE_FLAT && tz == z;
}

/**
 * Is a drive-through road stop raised onto a viaduct? It is when it, with the
 * stops next to it along its road (four at most), lies between the flat ends
 * of two concrete road viaducts at its height that lead away from it.
 * @param tile The tile.
 * @return True if it is.
 */
bool IsElevatedRoadStop(TileIndex tile)
{
	if (!IsTileType(tile, TileType::Station) || !IsDriveThroughStopTile(tile) || IsRoadWaypoint(tile) || IsBridgeAbove(tile)) return false;
	auto [slope, z] = GetTileSlopeZ(tile);
	if (slope != SLOPE_FLAT) return false;

	const Axis axis = GetDriveThroughStopAxis(tile);
	uint run = 1;
	for (DiagDirection dir : {AxisToDiagDir(axis), ReverseDiagDir(AxisToDiagDir(axis))}) {
		TileIndex t = tile;
		for (;;) {
			t = TileAddByDiagDir(t, dir);
			if (!IsValidTile(t)) return false;
			if (IsViaductEndLeading(t, dir, z)) break;
			if (++run > ELEVATED_STOP_MAX_RUN || !IsElevatableStop(t, axis, z)) return false;
		}
	}
	return true;
}

/**
 * Is a viaduct end flat at the viaduct's height, because stops raised onto the viaduct are behind it?
 * @param tile The tile.
 * @return True if it is.
 */
bool IsElevatedViaductEnd(TileIndex tile)
{
	if (!IsBridgeTile(tile) || GetBridgeType(tile) != BRIDGE_CONCRETE_VIADUCT || GetTunnelBridgeTransportType(tile) != TransportType::Road) return false;
	if (GetTileSlope(tile) != SLOPE_FLAT) return false;
	const DiagDirection dir = GetTunnelBridgeDirection(tile);
	TileIndex behind = TileAddByDiagDir(tile, ReverseDiagDir(dir));
	return IsValidTile(behind) && IsElevatedRoadStop(behind) && GetDriveThroughStopAxis(behind) == DiagDirToAxis(dir);
}

/**
 * Draw the paving and the deck under a stop raised onto a viaduct. The rest of
 * the stop is then drawn on the deck, like on a foundation.
 * @param ti The stop tile; its height is raised to the deck.
 */
void DrawElevatedStopDeck(TileInfo *ti)
{
	DrawGroundSprite(SPR_CONCRETE_GROUND, PAL_NONE);
	const uint axis = GetDriveThroughStopAxis(ti->tile) == Axis::X ? 0 : 1;
	AddSortableSpriteToDraw(SPR_ELEVATED_STOP_DECK_X + axis, PAL_NONE, *ti, {{}, {TILE_SIZE, TILE_SIZE, TILE_HEIGHT - 1}, {}});
	OffsetGroundSprite(0, -static_cast<int>(TILE_HEIGHT));
	ti->z += TILE_HEIGHT;
}

/* BRT stations. */

/**
 * Is a stop a BRT station?
 * @param tile The stop tile.
 * @return True if it is.
 */
bool HasBrtStation(TileIndex tile)
{
	return !_brt_stations.empty() && _brt_stations.contains(tile);
}

/**
 * Cost of making a stop a BRT station.
 * @return The cost.
 */
Money GetBrtStationCost()
{
	return GetPrice(Price::BuildObject, 100, nullptr);
}

/**
 * Yearly upkeep of a BRT station: staff at the gates and their upkeep.
 * @return The upkeep.
 */
Money GetBrtStationYearlyUpkeep()
{
	return GetPrice(Price::BuildObject, 10, nullptr);
}

/**
 * Make a drive-through bus stop a BRT station, or an ordinary stop again.
 * @param flags Type of operation.
 * @param tile The stop.
 * @param remove Make it an ordinary stop again.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetBrtStation(DoCommandFlags flags, TileIndex tile, bool remove)
{
	if (!IsValidTile(tile) || !IsTileType(tile, TileType::Station) || !IsDriveThroughStopTile(tile) || !IsBusStop(tile)) return CommandCost(STR_ERROR_BRT_STATION_BUS_STOP_ONLY);
	CommandCost ret = CheckTileOwnership(tile);
	if (ret.Failed()) return ret;

	if (remove) {
		if (!HasBrtStation(tile)) return CommandCost(STR_ERROR_BRT_STATION_NONE);
		if (flags.Test(DoCommandFlag::Execute)) {
			_brt_stations.erase(tile);
			MarkTileDirtyByTile(tile);
		}
		return CommandCost(ExpensesType::Construction, GetPrice(Price::ClearObject, 2, nullptr));
	}

	if (HasBrtStation(tile)) return CommandCost(STR_ERROR_ALREADY_BUILT);
	if (flags.Test(DoCommandFlag::Execute)) {
		_brt_stations.insert(tile);
		MarkTileDirtyByTile(tile);
	}
	return CommandCost(ExpensesType::Construction, GetBrtStationCost());
}

/**
 * Draw a BRT station: platforms with glass walls, roofs and fare gates on both sides.
 * They take the place of the stop's shelters.
 * @param ti The stop tile.
 * @return True if a BRT station was drawn.
 */
bool DrawBrtStation(const TileInfo *ti)
{
	if (!HasBrtStation(ti->tile)) return false;

	const bool x = GetDriveThroughStopAxis(ti->tile) == Axis::X;
	const PaletteID pal = GetCompanyPalette(GetTileOwner(ti->tile));
	const bool transparent = IsTransparencySet(TransparencyOption::Buildings);
	SpriteID far_side = SPR_BRT_STATION_BASE + (x ? 0 : 2);
	SpriteID near_side = far_side + 1;
	SetBit(far_side, PALETTE_MODIFIER_COLOUR);
	SetBit(near_side, PALETTE_MODIFIER_COLOUR);
	/* The sprites are drawn from the tile's north corner; the boxes cover the pavements. */
	if (x) {
		AddSortableSpriteToDraw(far_side, pal, *ti, {{0, 0, 0}, {TILE_SIZE, 3, 16}, {0, 0, 0}}, transparent);
		AddSortableSpriteToDraw(near_side, pal, *ti, {{0, 13, 0}, {TILE_SIZE, 3, 16}, {0, -13, 0}}, transparent);
	} else {
		AddSortableSpriteToDraw(far_side, pal, *ti, {{0, 0, 0}, {3, TILE_SIZE, 16}, {0, 0, 0}}, transparent);
		AddSortableSpriteToDraw(near_side, pal, *ti, {{13, 0, 0}, {3, TILE_SIZE, 16}, {-13, 0, 0}}, transparent);
	}
	return true;
}

/**
 * How fast a bus loads and unloads at the stop it is at, because of fare gates and level platforms.
 * @param front The bus (front).
 * @return The speed in percent of the usual.
 */
uint GetBrtBoardingPercent(const RoadVehicle *front)
{
	if (_brt_stations.empty() || !front->IsBus() || !HasBrtStation(front->tile)) return 100;
	/* Riders paid at the gates and board through every door; on a level platform low-floor buses gain most. */
	return GetCityBusSpec(front->GetEngine()) != nullptr ? 300 : 200;
}

/** Charge the upkeep of the BRT stations, each month. */
static const IntervalTimer<TimerGameEconomy> _brt_stations_monthly({TimerGameEconomy::Trigger::Month, TimerGameEconomy::Priority::None}, [](auto)
{
	std::array<Money, MAX_COMPANIES> upkeep{};
	for (TileIndex tile : _brt_stations) {
		Owner owner = GetTileOwner(tile);
		if (Company::IsValidID(owner)) upkeep[owner.base()] += GetBrtStationYearlyUpkeep() / 12;
	}
	for (Company *c : Company::Iterate()) {
		if (upkeep[c->index.base()] != 0) SubtractMoneyFromCompany(c->index, CommandCost(ExpensesType::Property, upkeep[c->index.base()]));
	}
});

/* The BRT score. */

/**
 * The statistics of a bus's line this month.
 * @param front The vehicle (front).
 * @return The statistics, or nullptr if it is no bus or has no orders.
 */
static LineMonthStats *GetBrtStats(const RoadVehicle *front)
{
	if (front->orders == nullptr || !front->IsBus()) return nullptr;
	return &front->orders->line.this_month;
}

/**
 * A bus entered a tile: count it for its line's BRT score.
 * @param front The bus (front).
 * @param tile The tile.
 */
void CountBrtTile(const RoadVehicle *front, TileIndex tile)
{
	LineMonthStats *s = GetBrtStats(front);
	if (s == nullptr || IsRoadDepotTile(tile) || !MayHaveRoad(tile)) return;

	s->brt_tiles++;
	const bool bus_only = GetRoadTypeRoad(tile) == ROADTYPE_BUS;
	if (bus_only) {
		s->brt_busway++;
	} else {
		switch (GetBusLane(tile)) {
			case BusLane::Busway: s->brt_busway++; break;
			case BusLane::AllDay: s->brt_lanes++; break;
			case BusLane::Peak: s->brt_peak++; break;
			default: break;
		}
	}

	/* Junctions where the traffic crosses; nothing else drives on bus-only roads. */
	if (bus_only) return;
	if (IsRoadJunctionTile(tile)) {
		s->brt_crossings++;
		if (HasTrafficLight(tile)) {
			const TrafficLight *light = GetTrafficLight(tile);
			if (light != nullptr && light->bus_priority) {
				s->brt_signals++;
			} else {
				s->brt_lights++;
			}
		}
	} else if (IsLevelCrossingTile(tile)) {
		s->brt_crossings++;
	}
}

/**
 * A bus drove onto a bridge or into a tunnel: count the tiles up to and including the other end.
 * @param front The bus (front).
 * @param head The end it entered at.
 */
void CountBrtSpan(const RoadVehicle *front, TileIndex head)
{
	LineMonthStats *s = GetBrtStats(front);
	if (s == nullptr || !IsTileType(head, TileType::TunnelBridge) || GetTunnelBridgeTransportType(head) != TransportType::Road) return;

	const uint tiles = GetTunnelBridgeLength(head, GetOtherTunnelBridgeEnd(head)) + 1;
	s->brt_tiles += tiles;
	if (GetRoadTypeRoad(head) == ROADTYPE_BUS) s->brt_busway += tiles;
}

/**
 * A bus stopped to load: count the stop for its line's BRT score.
 * @param front The bus (front), at the stop.
 */
void CountBrtStop(const RoadVehicle *front)
{
	LineMonthStats *s = GetBrtStats(front);
	if (s == nullptr) return;

	s->brt_stops++;
	if (!HasBrtStation(front->tile)) return;
	s->brt_gated++;
	if (GetCityBusSpec(front->GetEngine()) != nullptr) s->brt_level++;
}

/** Fewest tiles a line's buses must have driven in a month for a BRT score. */
static constexpr uint BRT_MIN_TILES = 16;

/**
 * Work out a line's BRT score from what its buses did in a month.
 *  - Dedicated lanes, 25 points: the share of the distance on busways counts
 *    in full, on all-day bus lanes 60%, on peak bus lanes 30%.
 *  - Apart from the traffic, 25 points: the share on busways (bus-only roads,
 *    kerbed busways, bus-only viaducts and tunnels).
 *  - Paying before boarding, 15 points: the share of stops at BRT stations.
 *  - Level boarding, 15 points: the share of stops at BRT stations by low-floor buses.
 *  - Priority at junctions, 20 points: the share of junctions crossed that have
 *    bus signals (plain traffic lights count a quarter); full points without junctions.
 *  - Shared sections: a point off for each percent of the distance in mixed
 *    traffic over 10%, up to 30.
 * A line is BRT when at least half its distance is on busways or all-day bus
 * lanes and it scores 40 or more: basic BRT, bronze from 55, silver from 70, gold from 85.
 * @param s The line's statistics of the month.
 * @return The score.
 */
BrtScore CalcBrtScore(const LineMonthStats &s)
{
	BrtScore r;
	if (s.brt_tiles < BRT_MIN_TILES || s.brt_stops == 0) return r;
	r.known = true;

	const uint64_t tiles = s.brt_tiles, stops = s.brt_stops;
	auto percent = [](uint64_t part, uint64_t whole) { return static_cast<uint>(std::min<uint64_t>(100, (part * 100 + whole / 2) / whole)); };
	r.busway = percent(s.brt_busway, tiles);
	r.lanes = percent(s.brt_lanes, tiles);
	r.peak = percent(s.brt_peak, tiles);
	const uint64_t mixed = tiles - std::min<uint64_t>(tiles, static_cast<uint64_t>(s.brt_busway) + s.brt_lanes + s.brt_peak);
	r.shared = percent(mixed, tiles);
	r.gated = percent(s.brt_gated, stops);
	r.level = percent(s.brt_level, stops);
	r.crossings = s.brt_crossings;
	r.signals = s.brt_signals;
	r.lights = s.brt_lights;

	r.dedicated_points = static_cast<uint>((25 * (10 * static_cast<uint64_t>(s.brt_busway) + 6 * static_cast<uint64_t>(s.brt_lanes) + 3 * static_cast<uint64_t>(s.brt_peak)) + 5 * tiles) / (10 * tiles));
	r.alignment_points = static_cast<uint>((25 * static_cast<uint64_t>(s.brt_busway) + tiles / 2) / tiles);
	r.fare_points = static_cast<uint>((15 * static_cast<uint64_t>(s.brt_gated) + stops / 2) / stops);
	r.level_points = static_cast<uint>((15 * static_cast<uint64_t>(s.brt_level) + stops / 2) / stops);
	if (s.brt_crossings == 0) {
		r.junction_points = 20;
	} else {
		const uint64_t crossings = s.brt_crossings;
		r.junction_points = static_cast<uint>(std::min<uint64_t>(20, (20 * (4 * static_cast<uint64_t>(s.brt_signals) + s.brt_lights) + 2 * crossings) / (4 * crossings)));
	}
	r.penalty = r.shared > 10 ? std::min(30u, r.shared - 10) : 0;

	const int score = static_cast<int>(std::min(25u, r.dedicated_points) + std::min(25u, r.alignment_points) + std::min(15u, r.fare_points) + std::min(15u, r.level_points) + r.junction_points) - static_cast<int>(r.penalty);
	r.score = static_cast<uint>(std::clamp(score, 0, 100));

	if (r.busway + r.lanes >= 50 && r.score >= 40) {
		if (r.score >= 85) {
			r.grade = BrtGrade::Gold;
		} else if (r.score >= 70) {
			r.grade = BrtGrade::Silver;
		} else if (r.score >= 55) {
			r.grade = BrtGrade::Bronze;
		} else {
			r.grade = BrtGrade::Basic;
		}
	}
	return r;
}

/**
 * Is a line run with buses?
 * @param list The line's order list.
 * @return True if its vehicles are buses.
 */
bool IsBusLine(const OrderList *list)
{
	const Vehicle *v = list->GetFirstSharedVehicle();
	return v != nullptr && v->type == VehicleType::Road && RoadVehicle::From(v)->IsBus();
}

/**
 * A line's BRT score of last month.
 * @param list The line's order list.
 * @return The score; not known for lines that aren't run with buses.
 */
BrtScore GetLineBrtScore(const OrderList *list)
{
	if (!IsBusLine(list)) return {};
	return CalcBrtScore(list->line.last_month);
}

/**
 * Rating bonus for passengers at a station that a BRT line serves.
 * @param st The station.
 * @return The bonus, on the rating's scale of 255.
 */
int GetStationBrtRatingBonus(const Station *st)
{
	static constexpr std::array<int, to_underlying(BrtGrade::End)> BONUS = {0, 10, 18, 26, 36};
	return BONUS[std::min<uint8_t>(st->brt_grade, to_underlying(BrtGrade::End) - 1)];
}

/**
 * Quality points a town adds in a tender for a BRT line.
 * @param list The line's order list.
 * @return The points.
 */
uint GetBrtTenderQualityBonus(const OrderList *list)
{
	static constexpr std::array<uint, to_underlying(BrtGrade::End)> BONUS = {0, 2, 4, 7, 10};
	return BONUS[to_underlying(GetLineBrtScore(list).grade)];
}

/** Each month, after the lines' statistics moved on: give each station the best grade of the BRT lines that serve it. */
void BrtMonthlyLoop()
{
	for (Station *st : Station::Iterate()) st->brt_grade = 0;

	for (const OrderList *list : OrderList::Iterate()) {
		BrtScore score = GetLineBrtScore(list);
		if (score.grade == BrtGrade::None) continue;
		for (const Order &order : list->GetOrders()) {
			if (!order.IsType(OT_GOTO_STATION)) continue;
			Station *st = Station::GetIfValid(order.GetDestination().ToStationID());
			if (st != nullptr) st->brt_grade = std::max(st->brt_grade, to_underlying(score.grade));
		}
	}
}
