/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file transit_authority_type.h Types for town transit authorities and their contracts. */

#ifndef TRANSIT_AUTHORITY_TYPE_H
#define TRANSIT_AUTHORITY_TYPE_H

#include "company_type.h"
#include "economy_type.h"
#include "order_type.h"
#include "timer/timer_game_economy.h"
#include "town_type.h"

/** Kinds of contract a town gives for a line. */
enum class TransitContractType : uint8_t {
	GrossCost, ///< The town keeps the fares and pays a monthly fee (Singapore's BCM, BAS.MY).
	FreeLine, ///< Riders ride free and the town pays a monthly fee (Penang's CAT, Bas Muafakat Johor).
	End, ///< End marker.
};

/** Stages of a contract. */
enum class TransitContractState : uint8_t {
	Tender, ///< The town invites bids until the deadline.
	Active, ///< The contract runs.
	Ended, ///< Over; kept for a while so it shows in the town's list.
	End, ///< End marker.
};

/** Why a contract or tender ended. */
enum class TransitContractEnd : uint8_t {
	None, ///< It didn't.
	LostTender, ///< A rival operator won the tender.
	NoBid, ///< The company didn't bid.
	Expired, ///< The term ran out and the town didn't renew it.
	Review, ///< The town found the line too dear for its riders.
	EndedByCompany, ///< The company ended it early.
	NoService, ///< The line stopped running.
	LineGone, ///< The line no longer exists.
	End, ///< End marker.
};

/** A tender or contract between a town and a company for one of the company's lines. */
struct TransitContract {
	uint32_t id = 0; ///< Unique number.
	TownID town = TownID::Invalid(); ///< The town.
	CompanyID company = CompanyID::Invalid(); ///< The company running the line.
	OrderListID line = OrderListID::Invalid(); ///< The line (its shared order list).
	TransitContractType type = TransitContractType::GrossCost; ///< What kind of contract.
	TransitContractState state = TransitContractState::Tender; ///< Its stage.
	TransitContractEnd ended_why = TransitContractEnd::None; ///< Why it ended.
	TimerGameEconomy::Date start{}; ///< Tender: when it opened. Contract: when it began.
	TimerGameEconomy::Date end{}; ///< Tender: the deadline. Contract: the end of its term. Ended: when it ended.
	Money cost_basis = 0; ///< Monthly cost of the line when tendered: running costs and wear of its vehicles.
	uint8_t bid = UINT8_MAX; ///< The company's margin on top of the cost basis in percent, UINT8_MAX for no bid.
	uint8_t rival_bid = 0; ///< Tender result: the margin of the best rival bid.
	uint8_t rival_quality = 0; ///< Tender result: the quality of the best rival bid (0-100).
	uint8_t quality = 0; ///< Tender result: the quality of the company's line (0-100).
	Money fee = 0; ///< Monthly fee at full service.
	uint16_t required_departures = 0; ///< Departures from the first stop the town wants a month.
	uint32_t baseline_wait = 0; ///< Excess wait (in ticks) when the contract began, for the regularity target.
	uint16_t months = 0; ///< Months run.
	uint16_t months_met = 0; ///< Months that met every target.
	uint16_t idle_months = 0; ///< Months in a row without departures.
	uint16_t last_departures = 0; ///< Departures last month.
	uint32_t last_excess_wait = 0; ///< Excess wait last month, in ticks.
	uint8_t last_on_time = UINT8_MAX; ///< On-time share last month in percent, UINT8_MAX if not timetabled.
	int8_t last_kpi = 0; ///< Bonus (positive) or penalty (negative) last month, in percent of the fee.
	Money last_payment = 0; ///< Paid last month.
	Money paid = 0; ///< Paid over the contract.
	Money fares = 0; ///< Fares collected for the town this month (gross-cost contracts).
	Money last_fares = 0; ///< Fares collected for the town last month.
	uint32_t last_riders = 0; ///< Riders last month.
};

/** A town's transit authority. */
struct TownTransit {
	Money fares_this_year = 0; ///< Fares its contracts brought in this year.
	Money fares_last_year = 0; ///< Fares its contracts brought in last year.
	TimerGameEconomy::Date last_tender{}; ///< When it last invited tenders.

	bool policy_set = false; ///< Whether the town decided on its schemes (once it is big enough).
	bool resident_pass = false; ///< Residents ride free with the town's pass; the town pays operators per pass holder.
	bool pays_concessions = false; ///< The town pays back what concession riders don't pay on journeys from the town.
	Money spent_this_year = 0; ///< Spent on passes and concessions this year.
	std::array<uint32_t, MAX_COMPANIES> pass_riders{}; ///< Riders with the town's pass this month per company, in hundredths.
	std::array<Money, MAX_COMPANIES> concessions_owed{}; ///< Concession fares owed this month per company.
	uint32_t last_pass_holders = 0; ///< Passes paid for last month.
	Money last_pass_paid = 0; ///< Paid for passes last month.
	Money last_concessions_paid = 0; ///< Paid back for concessions last month.
	bool told_budget_gone = false; ///< Whether the news that this year's budget is used up came out.

	bool congestion_charge = false; ///< The town charges road vehicles, except buses, that drive into its centre (see road_pricing.cpp).
	TimerGameEconomy::Date charge_since{}; ///< When it brought the congestion charge in.
	Money charge_this_year = 0; ///< Congestion charges taken this year; they go to the transit budget the year after.
	Money charge_last_year = 0; ///< Congestion charges taken last year.
	uint32_t charged_this_month = 0; ///< Company road vehicles charged this month.
	uint32_t charged_last_month = 0; ///< Company road vehicles charged last month.
	Money charge_income_this_month = 0; ///< Charges taken this month from company road vehicles.
	Money charge_income_last_month = 0; ///< Charges taken last month, from company road vehicles and the town's own drivers.

	uint8_t hubs = 0; ///< Integrated transport hubs in the town last month (see interchange.cpp).
	uint8_t satisfaction = UINT8_MAX; ///< Passengers' satisfaction with the town's stations last month, 0 to 100 or UINT8_MAX if none (see satisfaction.cpp).
};

static constexpr uint RESIDENT_PASS_SHARE = 33; ///< Share of riders (in percent) boarding in a town with a resident pass that hold one.
static constexpr uint RESIDENT_PASS_FARES = 30; ///< What a town pays per pass holder a month, in standard 20-tile fares.

static constexpr uint8_t TRANSIT_BID_MAX = 40; ///< Highest margin a company may bid, in percent.

#endif /* TRANSIT_AUTHORITY_TYPE_H */
