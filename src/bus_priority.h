/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file bus_priority.h Bus priority: painted bus lanes, give-way markings at stops and bus-only roads. */

#ifndef BUS_PRIORITY_H
#define BUS_PRIORITY_H

#include "economy_type.h"
#include "tile_type.h"

struct TileInfo;
struct RoadVehicle;

/** A painted bus lane on a road tile. */
enum class BusLane : uint8_t {
	None, ///< No bus lane.
	Peak, ///< A bus lane in the peak hours.
	AllDay, ///< A bus lane all day.
	End, ///< End marker.
};

BusLane GetBusLane(TileIndex tile);
bool HasGiveWayStop(TileIndex tile);
bool IsBusOnlyRoad(TileIndex tile);
bool IsRoadVehicleBus(const RoadVehicle *v);

uint8_t GetCongestionLevelFor(TileIndex tile, bool bus);

Money GetBusLaneCost(BusLane lane);
Money GetGiveWayStopCost();

void DrawBusPriorityRoad(const TileInfo *ti);
void DrawBusPriorityStop(const TileInfo *ti);
void OnBusPriorityTileCleared(TileIndex tile);
void RestoreBusLane(TileIndex tile, BusLane lane);
void InitializeBusPriority();

/** Saveload access. */
std::map<TileIndex, BusLane> &GetBusLanes();
std::set<TileIndex> &GetGiveWayStops();

void ShowBusPriorityToolbar();

#endif /* BUS_PRIORITY_H */
