/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file road_traffic.h How busy and congested roads are, for the traffic map. */

#ifndef ROAD_TRAFFIC_H
#define ROAD_TRAFFIC_H

#include "tile_type.h"

/** How busy a road tile is lately. */
enum class RoadTrafficLevel : uint8_t {
	Quiet, ///< (Almost) no traffic.
	Flowing, ///< Traffic drives on freely.
	Waiting, ///< Vehicles have to wait now and then.
	Slow, ///< Vehicles wait about half the time.
	Jammed, ///< Vehicles stand still most of the time.
};

RoadTrafficLevel GetRoadTrafficLevel(TileIndex tile);
void InitializeRoadTraffic();

#endif /* ROAD_TRAFFIC_H */
