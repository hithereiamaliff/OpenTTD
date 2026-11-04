/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file traffic_light.h Traffic lights at road junctions.
 *
 * A junction with traffic lights gives right of way to one road axis at a time.
 * The lights are actuated: they stay green while traffic keeps flowing and only
 * switch when vehicles wait on the other road. Between two green phases all
 * directions are red for a short while, so the junction can clear.
 *
 * The list of lights (#_traffic_lights) is the source of truth and is saved in
 * its own savegame chunk. The map bit read by #HasTrafficLight is only a fast
 * cache of that list, rebuilt after loading a game.
 */

#ifndef TRAFFIC_LIGHT_H
#define TRAFFIC_LIGHT_H

#include "tile_type.h"
#include "track_type.h"
#include "direction_type.h"
#include "company_type.h"

/** The traffic lights controlling one road junction. */
struct TrafficLight {
	TileIndex tile = INVALID_TILE; ///< The junction tile.
	Owner owner = INVALID_OWNER; ///< Company that built the lights, or #OWNER_NONE.
	uint8_t green_axis = 0; ///< #Axis that has (or, while clearing, last had) right of way.
	bool clearing = false; ///< All directions are red while the junction clears.
	uint16_t phase_ticks = 0; ///< Ticks spent in the current phase.
	uint16_t gap_ticks = 0; ///< Ticks since a vehicle last entered on the green axis.

	/* Not saved: these only live within a single game tick. */
	uint8_t demand = 0; ///< Bit per #Axis: a vehicle is waiting at a red light.
	bool entered = false; ///< A vehicle entered the junction on green this tick.

	bool IsGreenFor(Axis axis) const;
};

using TrafficLightMap = std::map<TileIndex, TrafficLight>;
extern TrafficLightMap _traffic_lights;

bool CanHaveTrafficLight(TileIndex tile);
TrafficLight *GetTrafficLight(TileIndex tile);
TrackdirBits GetTrafficLightRedTrackdirs(TileIndex tile);
bool TrafficLightShowsGreen(TileIndex tile, Axis axis);

void TrafficLightsOnRoadChanged(TileIndex tile);
void ChangeTrafficLightOwner(Owner old_owner, Owner new_owner);

void InitializeTrafficLights();
void AfterLoadTrafficLights();
void OnTick_TrafficLights();

#endif /* TRAFFIC_LIGHT_H */
