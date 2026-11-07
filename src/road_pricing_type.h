/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file road_pricing_type.h Types for road pricing: tolls on expressways and congestion charges of towns. */

#ifndef ROAD_PRICING_TYPE_H
#define ROAD_PRICING_TYPE_H

#include "economy_type.h"
#include "timer/timer_game_economy.h"

/** How a company's toll roads charge town traffic. */
enum class TollScheme : uint8_t {
	PerKm, ///< Closed system: by the distance between the towns a trip links.
	Flat, ///< Open system: the same toll for every trip.
	End, ///< End marker.
};

/** A company's tolls. */
struct CompanyTollPolicy {
	uint8_t level = 100; ///< Toll level in percent of the standard toll.
	TollScheme scheme = TollScheme::PerKm; ///< How town traffic is charged.
	TimerGameEconomy::Date freeze_until{}; ///< Until when the government froze the tolls; in the past when they aren't frozen.
	Money income_last_month = 0; ///< Tolls taken last month, from town traffic and other companies' vehicles.
	Money compensation_last_month = 0; ///< Paid by the government last month for a toll freeze.
};

static constexpr uint8_t TOLL_LEVEL_MIN = 50; ///< Lowest toll level, in percent of the standard toll.
static constexpr uint8_t TOLL_LEVEL_MAX = 150; ///< Highest toll level the regulator allows.
static constexpr uint8_t TOLL_LEVEL_STEP = 10; ///< Steps of the toll level.

/** What can be changed about a company's tolls. */
enum class TollPolicyProperty : uint8_t {
	Level, ///< The toll level; the value is in percent.
	Scheme, ///< The #TollScheme.
	End, ///< End marker.
};

/** What to do with a toll plaza. */
enum class TollPlazaAction : uint8_t {
	Build, ///< Build a toll plaza.
	Remove, ///< Remove it.
	FreeFlow, ///< Replace its booths with a free-flow gantry.
	End, ///< End marker.
};

#endif /* ROAD_PRICING_TYPE_H */
