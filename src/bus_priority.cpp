/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file bus_priority.cpp Bus priority.
 *
 * Town traffic makes congested roads slow (see town_traffic.cpp). Bus
 * priority lets buses past it:
 *  - a bus lane painted all day lets buses drive at full speed, but the other
 *    traffic, squeezed into fewer lanes, is one congestion level worse;
 *  - a bus lane in the peak hours takes two levels off for buses and leaves
 *    the other traffic alone (it only takes the lane when the roads are busiest);
 *  - a kerbed busway in the middle of the road works like a bus lane all day,
 *    but kerbs keep the traffic out: it counts as a busway for BRT (brt.cpp);
 *  - give-way markings at a stop let buses pull out of it ahead of the queue;
 *  - bus-only roads (a road type) carry no town traffic, and only buses use them.
 * Bus signals at traffic lights live with the lights (traffic_light.cpp).
 */

#include "stdafx.h"
#include "bus_priority.h"
#include "bus_priority_cmd.h"
#include "brt.h"
#include "command_func.h"
#include "company_func.h"
#include "economy_func.h"
#include "road_map.h"
#include "roadveh.h"
#include "station_map.h"
#include "town_traffic.h"
#include "viewport_func.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

static std::map<TileIndex, BusLane> _bus_lanes; ///< Road tiles with a bus lane.
static std::set<TileIndex> _give_way_stops; ///< Bus stops with give-way markings.

/**
 * The bus lanes, for saving and loading.
 * @return The lanes by tile.
 */
std::map<TileIndex, BusLane> &GetBusLanes()
{
	return _bus_lanes;
}

/**
 * The stops with give-way markings, for saving and loading.
 * @return The stop tiles.
 */
std::set<TileIndex> &GetGiveWayStops()
{
	return _give_way_stops;
}

/** Forget all bus priority; used when starting or loading a game. */
void InitializeBusPriority()
{
	_bus_lanes.clear();
	_give_way_stops.clear();
}

/**
 * Is a tile a bus-only road?
 * @param tile The tile.
 * @return True if its road is the bus-only road type.
 */
bool IsBusOnlyRoad(TileIndex tile)
{
	return MayHaveRoad(tile) && GetRoadTypeRoad(tile) == ROADTYPE_BUS;
}

/**
 * Can a tile have a bus lane: a road (no depot) or a drive-through stop, with road on it that isn't bus-only already.
 * @param tile The tile.
 * @return True if it can.
 */
static bool CanHaveBusLane(TileIndex tile)
{
	if (IsTileType(tile, TileType::Road)) {
		if (IsRoadDepot(tile)) return false;
	} else if (!IsDriveThroughStopTile(tile)) {
		return false;
	}
	return HasTileRoadType(tile, RoadTramType::Road) && !IsBusOnlyRoad(tile);
}

/**
 * The bus lane on a tile.
 * @param tile The tile.
 * @return The lane, #BusLane::None if there is none.
 */
BusLane GetBusLane(TileIndex tile)
{
	if (_bus_lanes.empty()) return BusLane::None;
	auto it = _bus_lanes.find(tile);
	if (it == _bus_lanes.end() || !CanHaveBusLane(tile)) return BusLane::None;
	return it->second;
}

/**
 * Does a bus stop have give-way markings?
 * @param tile The stop tile.
 * @return True if it has.
 */
bool HasGiveWayStop(TileIndex tile)
{
	return !_give_way_stops.empty() && _give_way_stops.contains(tile);
}

/**
 * Is a road vehicle a bus?
 * @param v The vehicle (any part).
 * @return True for buses.
 */
bool IsRoadVehicleBus(const RoadVehicle *v)
{
	return v->First()->IsBus();
}

/**
 * Is a road tile right next to a stop with give-way markings, where buses pull out ahead of the queue?
 * @param tile The road tile.
 * @return True if it is.
 */
static bool IsNextToGiveWayStop(TileIndex tile)
{
	if (_give_way_stops.empty()) return false;
	for (DiagDirection dir : EnumRange(DiagDirection::End)) {
		TileIndex near = TileAddByDiagDir(tile, dir);
		if (IsValidTile(near) && HasGiveWayStop(near)) return true;
	}
	return false;
}

/**
 * How congested a road tile is for a road vehicle, with bus lanes and give-way markings.
 * @param tile A road tile (no depot).
 * @param bus Whether the vehicle is a bus.
 * @return The congestion level, 0 to #ROAD_CONGESTION_MAX.
 */
uint8_t GetCongestionLevelFor(TileIndex tile, bool bus)
{
	uint8_t level = GetRoadCongestion(tile);
	if (level == 0 || (_bus_lanes.empty() && _give_way_stops.empty())) return level;

	BusLane lane = GetBusLane(tile);
	if (bus) {
		if (lane == BusLane::AllDay || lane == BusLane::Busway || IsNextToGiveWayStop(tile)) return 0;
		if (lane == BusLane::Peak) return level > 2 ? level - 2 : 0;
		return level;
	}
	/* The others lost a lane. */
	if (lane == BusLane::AllDay || lane == BusLane::Busway) return std::min<uint8_t>(ROAD_CONGESTION_MAX, level + 1);
	return level;
}

/**
 * Cost of painting a bus lane on a tile.
 * @param lane The lane.
 * @return The cost.
 */
Money GetBusLaneCost(BusLane lane)
{
	switch (lane) {
		case BusLane::Busway: return GetPrice(Price::BuildObject, 10, nullptr);
		case BusLane::AllDay: return GetPrice(Price::BuildObject, 4, nullptr);
		default: return GetPrice(Price::BuildObject, 3, nullptr);
	}
}

/**
 * Cost of give-way markings at a stop.
 * @return The cost.
 */
Money GetGiveWayStopCost()
{
	return GetPrice(Price::BuildObject, 10, nullptr);
}

/**
 * May a company paint a tile's road? Its own roads, town roads and roads nobody owns.
 * @param tile The tile.
 * @return An error, or success.
 */
static CommandCost CheckBusLaneOwnership(TileIndex tile)
{
	Owner owner = GetRoadOwner(tile, RoadTramType::Road);
	if (owner == OWNER_TOWN || owner == OWNER_NONE) return CommandCost();
	return CheckOwnership(owner);
}

/**
 * Paint or remove a bus lane along a straight stretch of road.
 * @param flags Type of operation.
 * @param end_tile One end of the stretch.
 * @param start_tile The other end.
 * @param lane The lane to paint.
 * @param remove Remove the lanes instead.
 * @return The cost of this operation or an error.
 */
CommandCost CmdBuildBusLane(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, BusLane lane, bool remove)
{
	if (!IsValidTile(end_tile) || !IsValidTile(start_tile) || lane == BusLane::None || lane >= BusLane::End) return CMD_ERROR;
	if (TileX(start_tile) != TileX(end_tile) && TileY(start_tile) != TileY(end_tile)) return CommandCost(STR_ERROR_BUS_LANE_STRAIGHT_ONLY);

	CommandCost cost(ExpensesType::Construction);
	CommandCost last_error(remove ? STR_ERROR_BUS_LANE_NONE : STR_ERROR_BUS_LANE_ROAD_ONLY);
	for (TileIndex tile : OrthogonalTileArea(start_tile, end_tile)) {
		if (!CanHaveBusLane(tile)) continue;
		CommandCost ret = CheckBusLaneOwnership(tile);
		if (ret.Failed()) {
			last_error = ret;
			continue;
		}
		auto it = _bus_lanes.find(tile);
		if (remove) {
			if (it == _bus_lanes.end()) continue;
			cost.AddCost(GetPrice(Price::ClearObject, 1, nullptr));
			if (flags.Test(DoCommandFlag::Execute)) {
				_bus_lanes.erase(it);
				MarkTileDirtyByTile(tile);
			}
		} else {
			if (it != _bus_lanes.end() && it->second == lane) {
				last_error = CommandCost(STR_ERROR_ALREADY_BUILT);
				continue;
			}
			cost.AddCost(GetBusLaneCost(lane));
			if (flags.Test(DoCommandFlag::Execute)) {
				_bus_lanes[tile] = lane;
				MarkTileDirtyByTile(tile);
			}
		}
	}
	return cost.GetCost() == 0 ? last_error : cost;
}

/**
 * Paint or remove give-way markings at a bus stop.
 * @param flags Type of operation.
 * @param tile The bus stop.
 * @param remove Remove the markings instead.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetGiveWayStop(DoCommandFlags flags, TileIndex tile, bool remove)
{
	if (!IsValidTile(tile) || !IsAnyRoadStopTile(tile) || !IsBusStop(tile)) return CommandCost(STR_ERROR_GIVE_WAY_BUS_STOP_ONLY);
	CommandCost ret = CheckTileOwnership(tile);
	if (ret.Failed()) return ret;

	if (remove) {
		if (!HasGiveWayStop(tile)) return CommandCost(STR_ERROR_GIVE_WAY_NONE);
		if (flags.Test(DoCommandFlag::Execute)) {
			_give_way_stops.erase(tile);
			MarkTileDirtyByTile(tile);
		}
		return CommandCost(ExpensesType::Construction, GetPrice(Price::ClearObject, 1, nullptr));
	}

	if (HasGiveWayStop(tile)) return CommandCost(STR_ERROR_ALREADY_BUILT);
	if (flags.Test(DoCommandFlag::Execute)) {
		_give_way_stops.insert(tile);
		MarkTileDirtyByTile(tile);
	}
	return CommandCost(ExpensesType::Construction, GetGiveWayStopCost());
}

/**
 * A tile was cleared, or a stop removed: its lane, markings and BRT station go too.
 * @param tile The tile.
 */
void OnBusPriorityTileCleared(TileIndex tile)
{
	if (!_bus_lanes.empty()) _bus_lanes.erase(tile);
	if (!_give_way_stops.empty()) _give_way_stops.erase(tile);
	if (!GetBrtStations().empty()) GetBrtStations().erase(tile);
}

/**
 * Paint a lane again on a road that was rebuilt, e.g. when a drive-through stop is removed and its road stays.
 * @param tile The tile.
 * @param lane The lane it had.
 */
void RestoreBusLane(TileIndex tile, BusLane lane)
{
	if (lane != BusLane::None && CanHaveBusLane(tile)) _bus_lanes[tile] = lane;
}

/** Sprites of bus priority in city.png, from #SPR_BUS_PRIORITY_BASE. */
enum BusPrioritySprite : uint8_t {
	BPS_LANE_X, ///< All-day bus lane, road along X.
	BPS_LANE_Y, ///< All-day bus lane, road along Y.
	BPS_PEAK_X, ///< Peak bus lane, road along X.
	BPS_PEAK_Y, ///< Peak bus lane, road along Y.
	BPS_BUS_ROAD_X, ///< Bus-only road, along X.
	BPS_BUS_ROAD_Y, ///< Bus-only road, along Y.
	BPS_GIVE_WAY_X, ///< Give-way markings at a stop, road along X.
	BPS_GIVE_WAY_Y, ///< Give-way markings at a stop, road along Y.
};

/**
 * Draw bus lanes and the red of bus-only roads on a plain road tile; straight, flat roads only.
 * @param ti The tile, after its ground and road were drawn.
 */
void DrawBusPriorityRoad(const TileInfo *ti)
{
	if (ti->tileh != SLOPE_FLAT || !IsNormalRoad(ti->tile)) return;
	RoadBits bits = GetRoadBits(ti->tile, RoadTramType::Road);
	if (bits != ROAD_X && bits != ROAD_Y) return;
	const uint axis = bits == ROAD_X ? 0 : 1;

	if (IsBusOnlyRoad(ti->tile)) {
		DrawGroundSprite(SPR_BUS_PRIORITY_BASE + BPS_BUS_ROAD_X + axis, PAL_NONE);
		return;
	}
	switch (GetBusLane(ti->tile)) {
		case BusLane::AllDay: DrawGroundSprite(SPR_BUS_PRIORITY_BASE + BPS_LANE_X + axis, PAL_NONE); break;
		case BusLane::Peak: DrawGroundSprite(SPR_BUS_PRIORITY_BASE + BPS_PEAK_X + axis, PAL_NONE); break;
		case BusLane::Busway: DrawGroundSprite(SPR_BUSWAY_X + axis, PAL_NONE); break;
		default: break;
	}
}

/**
 * Draw bus lanes and give-way markings on a drive-through bus stop.
 * @param ti The stop tile, after its ground and road were drawn.
 */
void DrawBusPriorityStop(const TileInfo *ti)
{
	if (ti->tileh != SLOPE_FLAT || !IsDriveThroughStopTile(ti->tile)) return;
	const uint axis = GetDriveThroughStopAxis(ti->tile) == Axis::X ? 0 : 1;

	if (IsBusOnlyRoad(ti->tile)) {
		DrawGroundSprite(SPR_BUS_PRIORITY_BASE + BPS_BUS_ROAD_X + axis, PAL_NONE);
	} else {
		switch (GetBusLane(ti->tile)) {
			case BusLane::AllDay: DrawGroundSprite(SPR_BUS_PRIORITY_BASE + BPS_LANE_X + axis, PAL_NONE); break;
			case BusLane::Peak: DrawGroundSprite(SPR_BUS_PRIORITY_BASE + BPS_PEAK_X + axis, PAL_NONE); break;
			case BusLane::Busway: DrawGroundSprite(SPR_BUSWAY_X + axis, PAL_NONE); break;
			default: break;
		}
	}
	if (HasGiveWayStop(ti->tile)) DrawGroundSprite(SPR_BUS_PRIORITY_BASE + BPS_GIVE_WAY_X + axis, PAL_NONE);
}
