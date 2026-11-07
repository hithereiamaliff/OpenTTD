/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file town_traffic.h Town traffic: how busy roads are with the cars of the town's people.
 *
 * Towns don't send real vehicles onto the roads. Instead every road tile has
 * a congestion level, worked out now and then from the homes, jobs and city
 * services around it, the size of the town, how central the road is, how
 * much of the town's travel the player's passenger services carry, and what
 * the road can take; with commuting (commuting.cpp), also from the
 * commuters' cars on the routes they take. Congested roads slow down road vehicles and the
 * pathfinder avoids them. The level is kept in the map, so it is saved and
 * the same for every player in a network game. Cars and pedestrians drawn on
 * the roads only show it.
 */

#ifndef TOWN_TRAFFIC_H
#define TOWN_TRAFFIC_H

#include "direction_type.h"
#include "tile_type.h"
#include "town_type.h"

/** Settings for how much traffic towns make. */
enum class TownTrafficSetting : uint8_t {
	Off, ///< No town traffic.
	Light, ///< Less traffic.
	Normal, ///< Normal traffic.
	Heavy, ///< More traffic.
};

/** Highest congestion level of a road tile. */
static constexpr uint8_t ROAD_CONGESTION_MAX = 7;

void UpdateRoadCongestion(TileIndex tile, const Town *t);
uint8_t GetTownTrafficTarget(TileIndex tile, const Town *t);
int GetRoadCongestionSpeedLimit(TileIndex tile, bool bus = false);
int GetRoadCongestionSpeedLimitKmh(uint8_t level);
uint GetRoadCongestionPenalty(TileIndex tile, bool bus = false);
uint GetTownCarReduction(const Town *t);
uint GetTownRidershipCarReduction(const Town *t);
bool TownTrafficWaitsAt(TileIndex junction, Axis axis, bool longest_green_over);

/** How busy the roads of a town are. */
struct TownTrafficSummary {
	uint roads = 0; ///< Road tiles of the town.
	uint congested = 0; ///< Of those, the ones where traffic crawls (level 4 and up).
	uint8_t worst = 0; ///< Highest congestion level.
};

TownTrafficSummary GetTownTrafficSummary(const Town *t);

#endif /* TOWN_TRAFFIC_H */
