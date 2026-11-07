/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file line_type.h Types for lines: the vehicles that share one order list, with a number, name and colour. */

#ifndef LINE_TYPE_H
#define LINE_TYPE_H

#include "fare_type.h"
#include "gfx_type.h"
#include "timer/timer_game_tick.h"

/** Longest line number, in characters, like "101", "T420" or "CAT13". */
static constexpr uint MAX_LENGTH_LINE_NUMBER_CHARS = 8;
/** Longest line name, in characters. */
static constexpr uint MAX_LENGTH_LINE_NAME_CHARS = 32;
/** How many recent gaps between departures a line remembers. */
static constexpr uint LINE_GAP_HISTORY = 16;

/** Parts of the satisfaction of a line's passengers (see satisfaction.cpp). */
enum class SatisfactionPart : uint8_t {
	Waiting, ///< How long they wait, against how long the trip takes.
	Crowding, ///< How full the vehicles are.
	Punctuality, ///< Departures on time, or evenly spaced.
	Fare, ///< What they pay.
	Condition, ///< How reliable the vehicles are.
	Comfort, ///< Air-conditioning, newer and quieter vehicles.
	End, ///< End marker.
};

/** Satisfaction that isn't known yet. */
static constexpr uint8_t SATISFACTION_UNKNOWN = UINT8_MAX;

/** What a line did in one month. */
struct LineMonthStats {
	uint32_t riders = 0; ///< Passengers who got off (delivered or transferred) the line's vehicles.
	uint32_t departures = 0; ///< Departures from the first stop.
	uint32_t load_sum = 0; ///< Sum of how full (in percent) vehicles were when leaving any stop.
	uint32_t load_count = 0; ///< Number of departures counted in load_sum.
	uint8_t max_load = 0; ///< Fullest departure, in percent.
	uint32_t timed = 0; ///< Departures from the first stop that had a running timetable to judge.
	uint32_t on_time = 0; ///< Of those, the ones that left on time.
	/* For the BRT score of bus lines (brt.cpp). */
	uint32_t brt_tiles = 0; ///< Tiles driven by the line's buses.
	uint32_t brt_busway = 0; ///< Of those, on busways: bus-only roads, kerbed busways, bus-only viaducts and tunnels.
	uint32_t brt_lanes = 0; ///< On all-day bus lanes.
	uint32_t brt_peak = 0; ///< On peak bus lanes.
	uint32_t brt_crossings = 0; ///< Junctions and level crossings crossed.
	uint32_t brt_lights = 0; ///< Of those, with traffic lights but no bus signals.
	uint32_t brt_signals = 0; ///< Of those, with bus signals.
	uint32_t brt_stops = 0; ///< Stops made to load.
	uint32_t brt_gated = 0; ///< Of those, at BRT stations.
	uint32_t brt_level = 0; ///< Of those, by low-floor buses at BRT stations.
};

/** The line of a shared order list. */
struct LineInfo {
	std::string number; ///< Line number shown on badges; empty if not set.
	std::string name; ///< Line name; empty if not set.
	Colours colour = Colours::Invalid; ///< Line colour; Colours::Invalid picks one automatically.
	TimerGameTick::Ticks target_gap = 0; ///< Wanted time between departures from the first stop; 0 for none.
	bool hold_for_spacing = false; ///< Hold vehicles at the first stop to keep the target gap.
	FareScheme fare_scheme = FareScheme::Inherit; ///< The line's own fare scheme, or Inherit for the company's.
	uint16_t fare_price = 0; ///< The line's own price level in percent, or 0 for the company's.

	TimerGameTick::TickCounter last_departure = 0; ///< When a vehicle last left the first stop; 0 for never.
	std::array<uint32_t, LINE_GAP_HISTORY> gaps{}; ///< Recent gaps between departures from the first stop, in ticks.
	uint8_t num_gaps = 0; ///< How many entries of gaps are filled.
	uint8_t next_gap = 0; ///< Where the next gap goes in gaps.

	LineMonthStats this_month{}; ///< Statistics of the current month.
	LineMonthStats last_month{}; ///< Statistics of the previous month.

	uint8_t satisfaction = SATISFACTION_UNKNOWN; ///< How satisfied its passengers were last month, 0 to 100.
	std::array<uint8_t, to_underlying(SatisfactionPart::End)> satisfaction_parts{}; ///< The parts of it, 0 to 100 each.

	uint8_t offpeak_vehicles = UINT8_MAX; ///< Vehicles that keep running between the peaks of the city clock; UINT8_MAX for all of them.
	bool on_demand = false; ///< Its vehicles pick their next stop by demand instead of running the stops in order.

	/**
	 * Remember the gap before a departure from the first stop.
	 * @param gap Ticks since the previous departure.
	 */
	void AddGap(uint32_t gap)
	{
		this->gaps[this->next_gap] = gap;
		this->next_gap = (this->next_gap + 1) % LINE_GAP_HISTORY;
		if (this->num_gaps < LINE_GAP_HISTORY) this->num_gaps++;
	}
};

/** Properties of a line that can be changed by command. */
enum class LineProperty : uint8_t {
	Number, ///< The line number; uses the text.
	Name, ///< The line name; uses the text.
	Colour, ///< The line colour; uses the value, Colours::Invalid for automatic.
	TargetGap, ///< Wanted ticks between departures; uses the value, 0 for none.
	HoldForSpacing, ///< Whether to hold vehicles at the first stop; uses the value.
	FareScheme, ///< The line's own fare scheme; uses the value, FareScheme::Inherit for the company's.
	FarePrice, ///< The line's own price level in percent; uses the value, 0 for the company's.
	OffPeakVehicles, ///< Vehicles kept running between the peaks of the city clock; uses the value, UINT8_MAX for all.
	OnDemand, ///< Whether vehicles pick their next stop by demand; uses the value.
	End, ///< End marker.
};

#endif /* LINE_TYPE_H */
