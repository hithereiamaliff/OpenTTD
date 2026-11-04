/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file road_junction.h Right of way for road vehicles at junctions. */

#ifndef ROAD_JUNCTION_H
#define ROAD_JUNCTION_H

#include "direction_type.h"
#include "tile_type.h"

struct RoadVehicle;

bool RoadVehicleMayEnterTile(RoadVehicle *v, TileIndex tile, DiagDirection enterdir);

#endif /* ROAD_JUNCTION_H */
