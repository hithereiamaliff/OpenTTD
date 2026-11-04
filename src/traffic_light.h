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

/** How traffic lights decide when to switch. */
enum class TrafficLightMode : uint8_t {
	Smart, ///< Stay green while traffic flows; switch when vehicles wait on the other road.
	Fixed, ///< Switch on a fixed cycle; offsets let neighbouring lights form a green wave.
	End, ///< End marker.
};

/* Timings of traffic lights, in ticks. */
static constexpr uint16_t TRAFFIC_LIGHT_CLEARANCE = 30; ///< All-red time between two green phases, so the junction can clear.
static constexpr uint16_t TRAFFIC_LIGHT_GAP = 45; ///< Smart lights: time without traffic on the green road after which they may switch.
static constexpr uint16_t TRAFFIC_LIGHT_MIN_TIME = 18; ///< Shortest green that can be set.
static constexpr uint16_t TRAFFIC_LIGHT_MAX_TIME = 1480; ///< Longest green that can be set (20 days).
static constexpr uint16_t TRAFFIC_LIGHT_DEFAULT_GREEN = 450; ///< Default (longest) green per road.
static constexpr uint16_t TRAFFIC_LIGHT_DEFAULT_MIN_GREEN = 90; ///< Default shortest green of smart lights.

/** The traffic lights controlling one road junction. */
struct TrafficLight {
	TileIndex tile = INVALID_TILE; ///< The junction tile.
	Owner owner = INVALID_OWNER; ///< Company that built the lights, or #OWNER_NONE.
	TrafficLightMode mode = TrafficLightMode::Smart; ///< How the lights switch.
	uint16_t green_x = TRAFFIC_LIGHT_DEFAULT_GREEN; ///< Green for the road along the X axis: longest green for smart lights, green time for fixed ones.
	uint16_t green_y = TRAFFIC_LIGHT_DEFAULT_GREEN; ///< Green for the road along the Y axis: longest green for smart lights, green time for fixed ones.
	uint16_t min_green = TRAFFIC_LIGHT_DEFAULT_MIN_GREEN; ///< Smart lights: shortest green.
	uint16_t offset = 0; ///< Fixed lights: how far into the cycle they are when the tick counter is a multiple of the cycle length.

	uint8_t green_axis = 0; ///< #Axis that has (or, while clearing, last had) right of way.
	bool clearing = false; ///< All directions are red while the junction clears.
	uint16_t phase_ticks = 0; ///< Ticks spent in the current phase.
	uint16_t gap_ticks = 0; ///< Ticks since a vehicle last entered on the green axis.

	/* Not saved: these only live within a single game tick. */
	uint8_t demand = 0; ///< Bit per #Axis: a vehicle is waiting at a red light.
	bool entered = false; ///< A vehicle entered the junction on green this tick.

	bool IsGreenFor(Axis axis) const;

	/**
	 * Get the (longest) green time of one road.
	 * @param axis The axis of the road.
	 * @return The time in ticks.
	 */
	uint16_t GetGreenTime(Axis axis) const { return axis == Axis::X ? this->green_x : this->green_y; }

	/**
	 * Get the length of one cycle of fixed lights.
	 * @return The time in ticks.
	 */
	uint32_t GetCycleLength() const { return this->green_x + this->green_y + 2 * TRAFFIC_LIGHT_CLEARANCE; }

	bool Update();
};

using TrafficLightMap = std::map<TileIndex, TrafficLight>;
extern TrafficLightMap _traffic_lights;

bool CanHaveTrafficLight(TileIndex tile);
TrafficLight *GetTrafficLight(TileIndex tile);
TrackdirBits GetTrafficLightRedTrackdirs(TileIndex tile);
bool TrafficLightShowsGreen(TileIndex tile, Axis axis);

void TrafficLightsOnRoadChanged(TileIndex tile);
void ChangeTrafficLightOwner(Owner old_owner, Owner new_owner);

void ShowTrafficLightWindow(TileIndex tile);

void InitializeTrafficLights();
void AfterLoadTrafficLights();
void OnTick_TrafficLights();

#endif /* TRAFFIC_LIGHT_H */
