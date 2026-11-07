/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file council_type.h Types for town councils: district, municipal and city councils, and the estates they run. */

#ifndef COUNCIL_TYPE_H
#define COUNCIL_TYPE_H

#include "economy_type.h"
#include "timer/timer_game_economy.h"

/** What kind of council runs a town, as Malaysia's local authorities. */
enum class CouncilLevel : uint8_t {
	District, ///< District council (majlis daerah): small towns and the country around them.
	Municipal, ///< Municipal council (majlis perbandaran).
	City, ///< City council (majlis bandaraya).
	End, ///< End marker.
};

/** Bands of a council's yearly report, as Singapore's town council management reports. */
enum class ReportBand : uint8_t {
	None, ///< Nothing to judge (no report yet, or no estates).
	Green, ///< Good.
	Amber, ///< Fair.
	Red, ///< Poor.
};

/** What a council's yearly report judges. */
enum class ReportItem : uint8_t {
	Services, ///< How much of the town its services reach.
	Estates, ///< The condition of its estates.
	Finances, ///< Its money and taxes.
	End, ///< End marker.
};

/** Number of items a council's yearly report judges. */
static constexpr uint REPORT_ITEM_COUNT = static_cast<uint>(ReportItem::End);

/** What a town has, counted at the start of each month from its buildings (saved, so every player in a network game has the same). */
struct CouncilCounts {
	uint32_t people = 0; ///< People in all its houses.
	uint32_t business = 0; ///< People in its shops and offices.
	uint32_t flats = 0; ///< People in its flats (the estates).
	uint64_t flats_age = 0; ///< Sum of the flats' ages times their people.
	uint16_t hospitals = 0; ///< Hospitals in the town, whoever owns them.
	uint16_t universities = 0; ///< Universities, the same.
	uint16_t stadiums = 0; ///< Stadiums, the same.
};

/** A town's council: its standing, its money beside taxes, the services it builds and the estates it runs (saved in chunk TCNL). */
struct TownCouncil {
	bool started = false; ///< The council was set up (its standing worked out once).
	CouncilLevel level = CouncilLevel::District; ///< What kind of council it is.
	int32_t since = 0; ///< Year it got its standing; 0 when it had it from the start.
	Money licences_this_year = 0; ///< Business licences collected this year.
	Money licences_last_year = 0; ///< The same last year.
	Money charges_this_year = 0; ///< Service and conservancy charges from flats this year.
	Money charges_last_year = 0; ///< The same last year.
	Money estates_this_year = 0; ///< Spent on its estates this year: upkeep, repainting, renewal.
	Money estates_last_year = 0; ///< The same last year.
	Money built_this_year = 0; ///< Spent on new city services this year.
	Money built_last_year = 0; ///< The same last year.
	Money drawn_this_year = 0; ///< Taken from the town's reserves this year, for new services and estates.
	Money drawn_last_year = 0; ///< The same last year.
	Money services_fund = 0; ///< Saved for new city services: a tenth of the taxes, out of the running costs.
	Money cofunded = 0; ///< What companies gave towards its next service, not spent yet.
	Money estates_fund = 0; ///< Its estates' sinking fund.
	uint8_t estate_condition = 80; ///< Condition of its estates, in percent.
	uint8_t paint = 0; ///< How often its estates were repainted; picks the colours of their blocks.
	int32_t repainted = 0; ///< Year its estates were last repainted.
	int32_t renewed = 0; ///< Year of its last estate renewal.
	uint16_t services_built = 0; ///< City services it has built.
	std::array<ReportBand, REPORT_ITEM_COUNT> report{}; ///< Last year's report.
	CouncilCounts counts{}; ///< What the town has, as of the start of this month.
};

#endif /* COUNCIL_TYPE_H */
