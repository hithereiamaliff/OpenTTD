/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file town_traffic.cpp Town traffic: how busy roads are with the cars of the town's people. */

#include "stdafx.h"
#include "town_traffic.h"
#include "commuting.h"
#include "town_budget.h"
#include "city_clock.h"
#include "bus_priority.h"
#include "road_pricing.h"
#include "cargotype.h"
#include "city_services_type.h"
#include "house.h"
#include "object_map.h"
#include "road_map.h"
#include "settings_type.h"
#include "direction_func.h"
#include "map_func.h"
#include "tilearea_type.h"
#include "timer/timer_game_tick.h"
#include "town.h"
#include "town_map.h"
#include "town_zoning.h"
#include "road.h"

#include "safeguards.h"

/** How far around a road tile homes, jobs and services send traffic onto it. */
static constexpr uint TRAFFIC_CATCHMENT_RADIUS = 3;
/**
 * Traffic (after capacity) from which a road reaches each congestion level.
 * With normal traffic, a town of 10,000 people has most of its roads at
 * levels 1-2, a tenth at 4 and up, and its busiest spots at 5-6; towns under
 * 3,000 stay at 0-1; cities of 50,000 and more see 6 and 7.
 */
static constexpr uint64_t TRAFFIC_LEVEL_THRESHOLDS[ROAD_CONGESTION_MAX] = {10000, 18000, 26000, 34000, 44000, 58000, 75000};
/** With commuting, the traffic of the homes and jobs around a road counts for this share, in percent: the rest is commuters, who are routed. */
static constexpr uint64_t LOCAL_TRAFFIC_PERCENT = 50;
/** With commuting, the traffic of one commuter's car a day (one way) on a road. */
static constexpr uint64_t COMMUTE_TRAFFIC_PER_CAR = 25;
/** Traffic a city service tile draws, like a house with this many people. */
static constexpr uint CITY_SERVICE_TRAFFIC = 30;
/** More activity than this around one road is hardly possible: every tile an office block of 150 people. */
static constexpr uint64_t MAX_TRAFFIC_ACTIVITY = 49 * 150 * 13;

/**
 * Share of the town's travel the player's passenger services carry.
 * @param t The town.
 * @return The share, 0 to 256.
 */
static uint GetTownTransitShare(const Town *t)
{
	uint64_t produced = 0, transported = 0;
	for (const CargoSpec *cs : CargoSpec::town_production_cargoes[TownProductionEffect::Passengers]) {
		auto it = t->GetCargoSupplied(cs->Index());
		if (it == std::end(t->supplied)) continue;
		produced += it->history[LAST_MONTH].production;
		transported += it->history[LAST_MONTH].transported;
	}
	if (produced == 0) return 0;
	/* Changing between bus and train is easy at an integrated transport hub: transit takes a quarter more cars off the road. */
	if (t->transit.hubs > 0) transported = transported * 5 / 4;
	return static_cast<uint>(std::min<uint64_t>(256, transported * 256 / produced));
}

/**
 * How much of the town's car traffic passenger services take off the road:
 * with commuting, the share of commuters who take transit; else by the
 * passengers they carry (#GetTownRidershipCarReduction).
 * @param t The town.
 * @return The reduction in percent.
 */
uint GetTownCarReduction(const Town *t)
{
	/* With commuting, the share of commuters who take transit (see commuting.cpp). */
	if (IsTownCommutingKnown(t)) return GetTownCommuteTransitShare(t);
	return GetTownRidershipCarReduction(t);
}

/**
 * How much of the town's car traffic the player's passenger services take
 * off the road by the passengers they carry: up to 60% when they carry all
 * of the town's passengers. With commuting, at least this share of
 * commuters take transit.
 * @param t The town.
 * @return The reduction in percent.
 */
uint GetTownRidershipCarReduction(const Town *t)
{
	return GetTownTransitShare(t) * 60 / 256;
}

/**
 * How much bigger towns make traffic worse: cars go further and more of
 * them pass through. 25% per doubling of the population above 500.
 * @param t The town.
 * @return The factor in percent.
 */
static uint GetTownSizeFactor(const Town *t)
{
	return 25 * FindLastBit(t->cache.population / 500 + 1);
}

/**
 * How central a road is in its town: central roads carry the traffic of more
 * of the town.
 * @param zone The town zone of the road.
 * @return The factor in percent.
 */
static uint GetCentralityFactor(HouseZone zone)
{
	switch (zone) {
		case HouseZone::TownCentre: return 100;
		case HouseZone::TownInnerSuburb: return 80;
		case HouseZone::TownOuterSuburb: return 60;
		case HouseZone::TownOutskirt: return 40;
		default: return 25;
	}
}

/**
 * How much traffic a road tile can take.
 * @param tile The road tile.
 * @return The capacity in percent of an ordinary two-way road.
 */
static uint GetRoadCapacity(TileIndex tile)
{
	uint capacity = 100;
	if (IsNormalRoad(tile)) {
		/* One-way roads have both lanes going the same way. */
		if (GetDisallowedRoadDirections(tile) != DisallowedRoadDirections{}) capacity = 160;
		/* Junctions hold traffic up; traffic lights keep it moving in turns. */
		if (GetRoadBits(tile, RoadTramType::Road).Count() >= 3) capacity = capacity * (HasTrafficLight(tile) ? 85 : 75) / 100;
	}
	/* Road types where towns build no houses are made for through traffic (expressways and the like). */
	RoadType rt = GetRoadTypeRoad(tile);
	if (rt != INVALID_ROADTYPE && GetRoadTypeInfo(rt)->flags.Test(RoadTypeFlag::NoHouses)) capacity = capacity * 250 / 100;
	return capacity;
}

/**
 * The congestion level a road tile is heading for.
 * @param tile The road tile.
 * @param t The town the road belongs to (its closest town).
 * @return The level, 0 to #ROAD_CONGESTION_MAX.
 */
uint8_t GetTownTrafficTarget(TileIndex tile, const Town *t)
{
	static constexpr uint SETTING_FACTOR[] = {0, 60, 100, 150};
	uint setting = _settings_game.economy.town_traffic;
	if (t == nullptr || setting == 0 || setting >= std::size(SETTING_FACTOR)) return 0;
	if (!HasTileRoadType(tile, RoadTramType::Road)) return 0;
	/* Town traffic doesn't use bus-only roads. */
	if (GetRoadTypeRoad(tile) == ROADTYPE_BUS) return 0;

	/* Far outside the town there is nothing to make traffic. */
	uint town_radius_sq = t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)];
	if (DistanceSquare(tile, t->xy) > town_radius_sq * 4 + 64) return 0;

	/* How much of the traffic around the road ends up on it, in millionths. */
	const uint capacity = GetRoadCapacity(tile);
	uint64_t share = static_cast<uint64_t>(GetTownSizeFactor(t)) * GetCentralityFactor(GetTownRadiusGroup(t, tile)) *
			(100 - GetTownCarReduction(t)) * SETTING_FACTOR[setting] / capacity;
	/* A congestion charge keeps cars out of the town centre; so do parking limits and a car-free centre. */
	const uint policy = GetCongestionChargeTrafficPercent(t, tile) * GetOrdinanceTrafficPercent(t, tile) / 100;
	share = share * GetCongestionChargeTrafficPercent(t, tile) / 100;
	share = share * GetOrdinanceTrafficPercent(t, tile) / 100;

	/* With commuting, commuters' cars come along the routes they take, and the homes and jobs around add their errands. */
	uint64_t commute_traffic = 0;
	if (IsTownCommutingKnown(t)) {
		commute_traffic = static_cast<uint64_t>(GetCommuteCars(tile)) * COMMUTE_TRAFFIC_PER_CAR * SETTING_FACTOR[setting] * policy / capacity / 100;
		share = share * LOCAL_TRAFFIC_PERCENT / 100;
	}

	/* Even the densest blocks don't make enough traffic for small towns and their edges to congest. */
	if (share * MAX_TRAFFIC_ACTIVITY / 1'000'000 + commute_traffic < TRAFFIC_LEVEL_THRESHOLDS[0]) return 0;

	/* Trips start and end at the homes, jobs and city services around the road. */
	uint64_t activity = 0;
	TileArea catchment(tile, 1, 1);
	catchment.Expand(TRAFFIC_CATCHMENT_RADIUS);
	for (TileIndex near : catchment) {
		switch (GetTileType(near)) {
			case TileType::House: {
				if (!IsHouseCompleted(near)) break;
				const HouseSpec *hs = HouseSpec::Get(GetHouseType(near));
				switch (GetHouseKind(*hs)) {
					case HouseKind::Home: activity += hs->population * 10; break;
					case HouseKind::Business: activity += hs->population * 13; break;
					case HouseKind::Public: activity += hs->population * 12; break;
				}
				break;
			}

			case TileType::Object:
				if (IsCityServiceObjectType(GetObjectType(near))) activity += CITY_SERVICE_TRAFFIC * 10;
				break;

			default:
				break;
		}
	}
	if (activity == 0 && commute_traffic == 0) return 0;

	uint64_t traffic = activity * share / 1'000'000 + commute_traffic;
	return static_cast<uint8_t>(std::ranges::count_if(TRAFFIC_LEVEL_THRESHOLDS, [traffic](uint64_t threshold) { return traffic >= threshold; }));
}

/**
 * Move the congestion level of a road tile up to two steps towards what the
 * traffic around it calls for. Called from the tile loop, which visits every
 * tile every 256 ticks; to save time only every other visit does the work,
 * so the level follows the town about once a week.
 * @param tile The road tile (not a depot).
 * @param t The closest town, if any.
 */
void UpdateRoadCongestion(TileIndex tile, const Town *t)
{
	if (HasBit(TimerGameTick::counter, 8)) return;

	int level = GetRoadCongestion(tile);
	int target = GetTownTrafficTarget(tile, t);
	if (target != level) SetRoadCongestion(tile, static_cast<uint8_t>(level + Clamp(target - level, -2, 2)));
}

/**
 * Speed that traffic on a congested road moves at.
 * @param level The congestion level.
 * @return The speed in km/h, or 0 when traffic moves freely.
 */
int GetRoadCongestionSpeedLimitKmh(uint8_t level)
{
	static constexpr int SPEED[ROAD_CONGESTION_MAX + 1] = {0, 0, 70, 55, 42, 32, 22, 14};
	return SPEED[std::min(level, ROAD_CONGESTION_MAX)];
}

/**
 * Highest speed of road vehicles on a road tile because of town traffic.
 * Bus lanes and give-way markings help buses (see bus_priority.cpp).
 * @param tile The tile.
 * @param bus Whether the vehicle is a bus.
 * @return The speed in internal road vehicle units (km-ish/h * 2), or INT_MAX when there is no limit.
 */
int GetRoadCongestionSpeedLimit(TileIndex tile, bool bus)
{
	if (!IsTileType(tile, TileType::Road) || IsRoadDepot(tile)) return INT_MAX;
	int kmh = GetRoadCongestionSpeedLimitKmh(GetCongestionLevelFor(tile, bus));
	return kmh == 0 ? INT_MAX : kmh * 2;
}

/**
 * Extra pathfinder cost of driving over a road tile because of town traffic.
 * Bus lanes and give-way markings help buses (see bus_priority.cpp).
 * @param tile The tile.
 * @param bus Whether the vehicle is a bus.
 * @return The penalty, in pathfinder cost units (100 is one tile).
 */
uint GetRoadCongestionPenalty(TileIndex tile, bool bus)
{
	static constexpr uint PENALTY[ROAD_CONGESTION_MAX + 1] = {0, 0, 10, 25, 45, 75, 120, 180};
	if (!IsTileType(tile, TileType::Road) || IsRoadDepot(tile)) return 0;
	return PENALTY[GetCongestionLevelFor(tile, bus)] * _settings_game.pf.yapf.road_congestion_penalty / 100;
}

/**
 * Does town traffic wait at a junction with smart traffic lights to cross
 * along an axis? On busy roads (congestion level 1 and up) there is always
 * someone; on quiet town roads someone turns up at least once the other road
 * had its longest green. Out of town only real vehicles count.
 * @param junction The junction tile.
 * @param axis The road through the junction.
 * @param longest_green_over Whether the other road had its longest green.
 * @return True if town traffic waits.
 */
bool TownTrafficWaitsAt(TileIndex junction, Axis axis, bool longest_green_over)
{
	if (_settings_game.economy.town_traffic == to_underlying(TownTrafficSetting::Off)) return false;

	int level = -1;
	for (DiagDirection side : {AxisToDiagDir(axis), ReverseDiagDir(AxisToDiagDir(axis))}) {
		TileIndex tile = TileAddByDiagDir(junction, side);
		if (!IsValidTile(tile) || !IsTileType(tile, TileType::Road) || IsRoadDepot(tile) || !HasTileRoadType(tile, RoadTramType::Road)) continue;
		level = std::max<int>(level, AdjustCongestionForCityTime(GetRoadCongestion(tile)));
	}
	if (level >= 1) return true;
	if (level < 0 || !longest_green_over) return false;

	const Town *t = ClosestTownFromTile(junction, UINT_MAX);
	return t != nullptr && GetTownRadiusGroup(t, junction) != HouseZone::TownEdge;
}

/**
 * How busy the roads of a town are.
 * @param t The town.
 * @return The number of its road tiles, how many are congested and the worst level.
 */
TownTrafficSummary GetTownTrafficSummary(const Town *t)
{
	TownTrafficSummary summary;
	uint radius = IntSqrt(t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)]) + 2;
	TileArea area(t->xy, 1, 1);
	area.Expand(radius);
	for (TileIndex tile : area) {
		if (!IsTileType(tile, TileType::Road) || IsRoadDepot(tile) || GetTownIndex(tile) != t->index) continue;
		if (!HasTileRoadType(tile, RoadTramType::Road)) continue;
		uint8_t level = GetRoadCongestion(tile);
		summary.roads++;
		if (level >= 4) summary.congested++;
		summary.worst = std::max(summary.worst, level);
	}
	return summary;
}
