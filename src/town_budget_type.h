/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file town_budget_type.h Types for towns' budgets and ordinances. */

#ifndef TOWN_BUDGET_TYPE_H
#define TOWN_BUDGET_TYPE_H

#include "economy_type.h"
#include "timer/timer_game_economy.h"

/** Rules a town can bring in (see town_budget.cpp). */
enum class Ordinance : uint8_t {
	TransitFund, ///< More of the budget for transit.
	ParkingLimits, ///< Fewer parking places in the centre, and a parking levy.
	LowEmissionZone, ///< Older cars kept out of the centre; vehicles that aren't electric pay to drive in.
	CarFreeCentre, ///< No through traffic in the centre.
	TreePlanting, ///< The town plants trees.
	TransitOrientedDevelopment, ///< Building close to railway stations.
	End, ///< End marker.
};

/** Number of kinds of ordinance. */
static constexpr uint ORDINANCE_COUNT = to_underlying(Ordinance::End);

/** A town's money: taxes, fees, spending and reserves, and its ordinances (saved in chunk TBUD). */
struct TownBudget {
	bool started = false; ///< The budget runs: reserves were set up.
	uint8_t tax_rate = 100; ///< Taxes in percent of the normal rate.
	Money reserves = 0; ///< Money in the town's coffers.
	Money taxes_this_year = 0; ///< Taxes collected this year.
	Money taxes_last_year = 0; ///< Taxes collected last year.
	Money fees_this_year = 0; ///< Parking levies and low-emission zone fees this year.
	Money fees_last_year = 0; ///< The same last year.
	Money spent_this_year = 0; ///< Spent this year: transit, running the town, ordinances.
	Money spent_last_year = 0; ///< The same last year.
	uint8_t ordinances = 0; ///< The ordinances in force, a bit per #Ordinance.
	std::array<TimerGameEconomy::Date, ORDINANCE_COUNT> since{}; ///< When each ordinance in force came in.
	uint32_t trees_planted = 0; ///< Trees the town planted.
	uint32_t lez_charged_this_month = 0; ///< Vehicles that paid to drive into its low-emission zone this month.
	uint32_t lez_charged_last_month = 0; ///< The same last month.
};

#endif /* TOWN_BUDGET_TYPE_H */
