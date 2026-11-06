/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file transit_authority.cpp Town transit authorities: budgets, tenders and contracts for lines. */

#include "stdafx.h"
#include "transit_authority.h"
#include "transit_authority_cmd.h"
#include "cargotype.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "core/random_func.hpp"
#include "economy_func.h"
#include "fare.h"
#include "line.h"
#include "news_func.h"
#include "order_base.h"
#include "station_base.h"
#include "strings_func.h"
#include "town.h"
#include "vehicle_base.h"
#include "window_func.h"

#include "table/strings.h"

#include "safeguards.h"

/** Towns from this many people have a transit budget. */
static constexpr uint32_t MIN_TRANSIT_POPULATION = 1500;
/** Bus-years (a town bus's running costs, wear and the operator's share) a town funds per 10,000 people. */
static constexpr uint BUDGET_BUSES_PER_10K = 12;
/** Length of a contract, and of an extension for good service, in years. */
static constexpr int CONTRACT_YEARS = 5;
static constexpr int EXTENSION_YEARS = 3;
/** How long a tender stays open, in days. */
static constexpr int TENDER_DAYS = 60;
/** A town waits this long after a tender before the next, in days. */
static constexpr int TENDER_PAUSE_DAYS = 180;
/** Each month a town that may tender does so with a chance of one in this. */
static constexpr uint TENDER_CHANCE = 4;
/** Share of tenders (in percent) for free lines rather than gross-cost contracts. */
static constexpr uint FREE_LINE_SHARE = 35;
/** Weight of quality against price when a town picks a bid, in percent. */
static constexpr uint QUALITY_WEIGHT = 60;
/** Share of months (in percent) that must meet every target for an extension. */
static constexpr uint RENEW_SHARE = 80;
/** A review ends a contract that costs more per rider than this many standard 20-tile fares. */
static constexpr uint REVIEW_FARES = 4;
/** Ending a contract early costs this many months of fees. */
static constexpr uint EARLY_END_MONTHS = 3;
/** Ended contracts stay in the list this long, in days; a town doesn't tender the line again for twice that. */
static constexpr int KEEP_ENDED_DAYS = 365;

static std::vector<TransitContract> _transit_contracts; ///< All tenders and contracts.
static uint32_t _next_transit_contract = 1; ///< Number of the next contract.

/**
 * All tenders and contracts, for saving and the windows.
 * @return The list.
 */
std::vector<TransitContract> &GetTransitContracts()
{
	return _transit_contracts;
}

/**
 * Find a tender or contract.
 * @param id Its number.
 * @return It, or nullptr.
 */
TransitContract *GetTransitContract(uint32_t id)
{
	for (TransitContract &c : _transit_contracts) {
		if (c.id == id) return &c;
	}
	return nullptr;
}

/**
 * The cost of a bus for a year that a town counts with: the running costs of a town bus, as much again for wear and the operator.
 * @return The cost.
 */
static Money GetBusYearCost()
{
	/* Running cost prices are kept in 1/256 of a pound a year. */
	return (GetPrice(Price::RunningRoadveh, 160, nullptr) >> 8) * 3;
}

/**
 * A town's yearly transit budget.
 * @param t The town.
 * @return The budget; zero for small towns.
 */
Money GetTownTransitBudget(const Town *t)
{
	if (t->cache.population < MIN_TRANSIT_POPULATION) return 0;
	return GetBusYearCost() * t->cache.population * BUDGET_BUSES_PER_10K / 10000;
}

/**
 * What a town's running contracts cost it a year.
 * @param t The town.
 * @return The yearly fees.
 */
Money GetTownTransitCommitted(const Town *t)
{
	Money committed = 0;
	for (const TransitContract &c : _transit_contracts) {
		if (c.town == t->index && c.state == TransitContractState::Active) committed += c.fee * 12;
	}
	return committed;
}

/**
 * What a town could still spend on contracts a year: its budget and last year's fares from its contracts, less its contracts.
 * @param t The town.
 * @return The money.
 */
Money GetTownTransitAvailable(const Town *t)
{
	return GetTownTransitBudget(t) + t->transit.fares_last_year - GetTownTransitCommitted(t);
}

/**
 * The running contract of a line.
 * @param list The line.
 * @return The contract, or nullptr if the line runs on its own account.
 */
const TransitContract *GetLineContract(const OrderList *list)
{
	if (list == nullptr || _transit_contracts.empty()) return nullptr;
	for (const TransitContract &c : _transit_contracts) {
		if (c.state == TransitContractState::Active && c.line == list->index) return &c;
	}
	return nullptr;
}

/**
 * A line's number or name for messages.
 * @param list The line.
 * @return The text.
 */
std::string GetTransitLineDescription(const OrderList *list)
{
	if (list == nullptr) return {};
	const LineInfo &line = list->line;
	if (!line.number.empty() && !line.name.empty()) return GetString(STR_TRANSIT_LINE_NUMBER_NAME, line.number, line.name);
	if (!line.number.empty()) return GetString(STR_TRANSIT_LINE_NUMBER, line.number);
	if (!line.name.empty()) return GetString(STR_TRANSIT_LINE_NAME, line.name);
	const Vehicle *v = list->GetFirstSharedVehicle();
	return v == nullptr ? std::string{} : GetString(STR_TRANSIT_LINE_VEHICLE, v->index);
}

/**
 * What a line costs a month: running costs and wear of its vehicles.
 * @param list The line.
 * @return The cost.
 */
Money GetTransitLineCostBasis(const OrderList *list)
{
	Money yearly = 0;
	for (const Vehicle *v = list->GetFirstSharedVehicle(); v != nullptr; v = v->NextShared()) {
		yearly += v->GetDisplayRunningCost();
		yearly += v->value / std::max(1, v->max_age.base() / EconomyTime::DAYS_IN_YEAR);
	}
	return yearly / 12;
}

/**
 * How long riders wait at a line's first stop beyond what even spacing would give.
 * @param line The line.
 * @return The excess wait in ticks, 0 if not known.
 */
static uint32_t GetExcessWait(const LineInfo &line)
{
	uint32_t gap = GetLineAverageGap(line);
	uint32_t wait = GetLineAverageWait(line);
	return wait > gap / 2 ? wait - gap / 2 : 0;
}

/**
 * How good a line's service is, as a town judges a bid: regularity (40%),
 * reliability of its vehicles (30%) and their age (30%).
 * @param list The line.
 * @return The quality, 0 to 100.
 */
uint GetTransitLineQuality(const OrderList *list)
{
	uint regularity = 50;
	uint32_t gap = GetLineAverageGap(list->line);
	if (gap > 0) regularity = 100 - std::min<uint>(100, GetExcessWait(list->line) * 100 / std::max<uint32_t>(1, gap / 2));

	uint reliability = 0, youth = 0, count = 0;
	for (const Vehicle *v = list->GetFirstSharedVehicle(); v != nullptr; v = v->NextShared()) {
		reliability += ToPercent16(v->reliability);
		youth += 100 - std::min<uint>(100, v->age.base() * 100 / std::max(1, v->max_age.base()));
		count++;
	}
	if (count == 0) return 0;
	return (40 * regularity + 30 * reliability / count + 30 * youth / count) / 100;
}

/**
 * Passengers paid for a journey that ended on a vehicle: under a gross-cost
 * contract the fares go to the town, under a free line there are none.
 * @param front The vehicle (front).
 * @param fares The fares.
 * @return What the company keeps.
 */
Money TakeContractFares(const Vehicle *front, Money fares)
{
	if (front->orders == nullptr || _transit_contracts.empty()) return fares;
	for (TransitContract &c : _transit_contracts) {
		if (c.state != TransitContractState::Active || c.line != front->orders->index) continue;
		c.fares += fares;
		return 0;
	}
	return fares;
}

/**
 * Does a line mostly serve a town: at least half its stops are in the town?
 * @param list The line.
 * @param t The town.
 * @return True if it does.
 */
static bool LineServesTown(const OrderList *list, const Town *t)
{
	uint stops = 0, in_town = 0;
	for (const Order &order : list->GetOrders()) {
		if (!order.IsType(OT_GOTO_STATION)) continue;
		stops++;
		const Station *st = Station::GetIfValid(order.GetDestination().ToStationID());
		if (st != nullptr && st->town == t) in_town++;
	}
	return in_town > 0 && in_town * 2 >= stops;
}

/**
 * Post news about a tender or contract.
 * @param c The tender or contract.
 * @param str The headline.
 * @param args What else goes into the headline after the town and the line.
 */
template <typename... Args>
static void AddContractNews(const TransitContract &c, StringID str, Args &&... args)
{
	const OrderList *list = OrderList::GetIfValid(c.line);
	const Vehicle *v = (list == nullptr) ? nullptr : list->GetFirstSharedVehicle();
	EncodedString headline = GetEncodedString(str, c.town, GetTransitLineDescription(list), std::forward<Args>(args)...);
	AddNewsItem(std::move(headline), NewsType::Subsidies, NewsStyle::Normal, {}, c.town, v == nullptr ? NewsReference{} : NewsReference{v->index});
}

/**
 * End a tender or contract.
 * @param c It.
 * @param why Why.
 * @param today The date.
 */
static void EndContract(TransitContract &c, TransitContractEnd why, TimerGameEconomy::Date today)
{
	c.state = TransitContractState::Ended;
	c.ended_why = why;
	c.end = today;
}

/**
 * Open a tender for a line.
 * @param c The tender, with its town, company and line.
 * @param today The date.
 */
static void OpenTender(TransitContract &c, TimerGameEconomy::Date today)
{
	c.state = TransitContractState::Tender;
	c.ended_why = TransitContractEnd::None;
	c.start = today;
	c.end = today + TENDER_DAYS;
	c.bid = UINT8_MAX;
	const OrderList *list = OrderList::GetIfValid(c.line);
	if (list != nullptr) c.cost_basis = GetTransitLineCostBasis(list);
	Town::Get(c.town)->transit.last_tender = today;
}

/**
 * Close a tender: the town weighs the company's bid against two or three
 * rival operators, 60:40 quality against price.
 * @param c The tender.
 * @param today The date.
 */
static void ResolveTender(TransitContract &c, TimerGameEconomy::Date today)
{
	OrderList *list = OrderList::GetIfValid(c.line);
	const Vehicle *first = (list == nullptr) ? nullptr : list->GetFirstSharedVehicle();
	if (first == nullptr || first->owner != c.company || !Company::IsValidID(c.company)) {
		EndContract(c, TransitContractEnd::LineGone, today);
		return;
	}

	/* The rivals' bids. */
	uint rivals = 2 + RandomRange(2);
	std::array<std::pair<uint, uint>, 3> rival_bids{}; // margin, quality
	for (uint i = 0; i < rivals; i++) rival_bids[i] = {5 + RandomRange(31), 45 + RandomRange(46)};

	if (c.bid == UINT8_MAX) {
		EndContract(c, TransitContractEnd::NoBid, today);
		AddContractNews(c, STR_NEWS_TRANSIT_NO_BID);
		return;
	}

	uint quality = GetTransitLineQuality(list);
	uint lowest = c.bid;
	for (uint i = 0; i < rivals; i++) lowest = std::min(lowest, rival_bids[i].first);
	auto score = [lowest](uint margin, uint q) { return QUALITY_WEIGHT * q + (100 - QUALITY_WEIGHT) * 100 * (100 + lowest) / (100 + margin); };

	uint best = 0;
	for (uint i = 1; i < rivals; i++) {
		if (score(rival_bids[i].first, rival_bids[i].second) > score(rival_bids[best].first, rival_bids[best].second)) best = i;
	}
	c.quality = static_cast<uint8_t>(quality);
	c.rival_bid = static_cast<uint8_t>(rival_bids[best].first);
	c.rival_quality = static_cast<uint8_t>(rival_bids[best].second);

	if (score(c.bid, quality) < score(rival_bids[best].first, rival_bids[best].second)) {
		EndContract(c, TransitContractEnd::LostTender, today);
		AddContractNews(c, STR_NEWS_TRANSIT_LOST, c.rival_bid, c.rival_quality, c.bid, c.quality);
		return;
	}

	c.state = TransitContractState::Active;
	c.start = today;
	c.end = today + CONTRACT_YEARS * EconomyTime::DAYS_IN_YEAR;
	c.fee = c.cost_basis * (100 + c.bid) / 100;
	c.required_departures = static_cast<uint16_t>(std::max<uint>(2, list->line.this_month.departures * 9 / 10));
	c.baseline_wait = GetExcessWait(list->line);
	c.months = 0;
	c.months_met = 0;
	c.idle_months = 0;
	c.paid = 0;
	c.fares = 0;
	AddContractNews(c, c.type == TransitContractType::FreeLine ? STR_NEWS_TRANSIT_WON_FREE : STR_NEWS_TRANSIT_WON, c.company, c.fee);
}

/**
 * A contract's month: check the targets and pay the fee.
 * @param c The contract.
 * @param today The date.
 */
static void RunContractMonth(TransitContract &c, TimerGameEconomy::Date today)
{
	OrderList *list = OrderList::GetIfValid(c.line);
	const Vehicle *first = (list == nullptr) ? nullptr : list->GetFirstSharedVehicle();
	if (first == nullptr || first->owner != c.company || !Company::IsValidID(c.company)) {
		EndContract(c, TransitContractEnd::LineGone, today);
		return;
	}
	Town *t = Town::Get(c.town);
	const LineInfo &line = list->line;

	c.last_departures = static_cast<uint16_t>(std::min<uint32_t>(line.this_month.departures, UINT16_MAX));
	c.last_riders = line.this_month.riders;
	c.last_excess_wait = GetExcessWait(line);
	c.last_on_time = line.this_month.timed > 0 ? static_cast<uint8_t>(line.this_month.on_time * 100 / line.this_month.timed) : UINT8_MAX;

	/* Targets: the departures the town asked for, no more bunching than at the start, punctuality where there is a timetable. */
	int kpi = 0;
	bool met = true;
	if (c.last_departures < c.required_departures) {
		kpi -= 2;
		met = false;
	}
	if (GetLineAverageGap(line) > 0) {
		if (c.last_excess_wait * 10 <= c.baseline_wait * 9) {
			kpi += 2;
		} else if (c.last_excess_wait > c.baseline_wait * 12 / 10 + Ticks::DAY_TICKS) {
			kpi -= 2;
			met = false;
		}
	}
	if (c.last_on_time != UINT8_MAX) {
		if (c.last_on_time >= 85) {
			kpi += 3;
		} else if (c.last_on_time < 70) {
			kpi -= 3;
			met = false;
		}
	}
	c.last_kpi = static_cast<int8_t>(Clamp(kpi, -5, 5));

	/* Pay for the departures made, at most the ones asked for. */
	Money payment = c.fee * std::min(c.last_departures, c.required_departures) / std::max<uint16_t>(1, c.required_departures);
	payment = payment * (100 + c.last_kpi) / 100;
	if (payment > 0) {
		SubtractMoneyFromCompany(c.company, CommandCost(GetFareRevenueType(GetFareMode(first)), -payment));
		/* Show it in the profits of the line's vehicles. */
		uint vehicles = list->GetNumVehicles();
		for (Vehicle *v = list->GetFirstSharedVehicle(); v != nullptr; v = v->NextShared()) v->profit_this_year += (payment << 8) / std::max(1U, vehicles);
	}
	c.last_payment = payment;
	c.paid += payment;
	c.months++;
	if (met) c.months_met++;
	c.idle_months = (c.last_departures == 0) ? c.idle_months + 1 : 0;

	t->transit.fares_this_year += c.fares;
	c.last_fares = c.fares;
	c.fares = 0;

	if (c.idle_months >= 2) {
		EndContract(c, TransitContractEnd::NoService, today);
		AddContractNews(c, STR_NEWS_TRANSIT_ENDED_NO_SERVICE);
		return;
	}
	if (today < c.end) return;

	/* End of the term: a review, then an extension for good service or a new tender. */
	CargoType cargo = INVALID_CARGO;
	for (const CargoSpec *cs : CargoSpec::town_production_cargoes[TownProductionEffect::Passengers]) {
		cargo = cs->Index();
		break;
	}
	Money fare = IsValidCargoType(cargo) ? GetTransportedGoodsIncome(1, 20, 0, cargo) : Money{0};
	if (fare > 0 && c.fee > fare * REVIEW_FARES * std::max<uint32_t>(1, c.last_riders)) {
		EndContract(c, TransitContractEnd::Review, today);
		AddContractNews(c, STR_NEWS_TRANSIT_ENDED_REVIEW);
		return;
	}
	if (c.months_met * 100 >= c.months * RENEW_SHARE && GetTownTransitAvailable(t) >= 0) {
		c.end = c.end + EXTENSION_YEARS * EconomyTime::DAYS_IN_YEAR;
		AddContractNews(c, STR_NEWS_TRANSIT_EXTENDED, EXTENSION_YEARS);
		return;
	}
	OpenTender(c, today);
	AddContractNews(c, STR_NEWS_TRANSIT_RETENDER, c.end);
}

/**
 * Has a line got a tender or contract, or had one with this town lately?
 * @param list The line.
 * @param t The town.
 * @param today The date.
 * @return True if so.
 */
static bool HasRecentContract(const OrderList *list, const Town *t, TimerGameEconomy::Date today)
{
	for (const TransitContract &c : _transit_contracts) {
		if (c.line != list->index) continue;
		if (c.state != TransitContractState::Ended) return true;
		if (c.town == t->index && today - c.end < 2 * KEEP_ENDED_DAYS) return true;
	}
	return false;
}

/**
 * Now and then a town invites tenders for one of the lines that serve it,
 * those losing money first, if its budget allows.
 * @param t The town.
 * @param today The date.
 */
static void MaybeOpenTender(Town *t, TimerGameEconomy::Date today)
{
	if (t->cache.population < MIN_TRANSIT_POPULATION) return;
	for (const TransitContract &c : _transit_contracts) {
		if (c.town == t->index && c.state == TransitContractState::Tender) return;
	}
	if (t->transit.last_tender != TimerGameEconomy::Date{} && today - t->transit.last_tender < TENDER_PAUSE_DAYS) return;
	if (!Chance16(1, TENDER_CHANCE)) return;

	Money available = GetTownTransitAvailable(t);
	std::vector<std::pair<OrderList *, uint>> candidates;
	uint total = 0;
	for (OrderList *list : OrderList::Iterate()) {
		const Vehicle *v = list->GetFirstSharedVehicle();
		if (v == nullptr || !Company::IsValidID(v->owner)) continue;
		if (list->line.this_month.departures < 4 || !LineServesTown(list, t) || HasRecentContract(list, t, today)) continue;
		Money cost = GetTransitLineCostBasis(list);
		if (cost <= 0 || cost * 12 * 12 / 10 > available) continue;

		Money profit = 0;
		for (const Vehicle *u = v; u != nullptr; u = u->NextShared()) profit += u->GetDisplayProfitLastYear();
		uint weight = profit < 0 ? 3 : 1;
		candidates.emplace_back(list, weight);
		total += weight;
	}
	if (candidates.empty()) return;

	uint pick = RandomRange(total);
	OrderList *chosen = candidates.back().first;
	for (const auto &[list, weight] : candidates) {
		if (pick < weight) {
			chosen = list;
			break;
		}
		pick -= weight;
	}

	TransitContract &c = _transit_contracts.emplace_back();
	c.id = _next_transit_contract++;
	c.town = t->index;
	c.company = chosen->GetFirstSharedVehicle()->owner;
	c.line = chosen->index;
	c.type = Chance16(FREE_LINE_SHARE, 100) ? TransitContractType::FreeLine : TransitContractType::GrossCost;
	OpenTender(c, today);
	AddContractNews(c, c.type == TransitContractType::FreeLine ? STR_NEWS_TRANSIT_TENDER_FREE : STR_NEWS_TRANSIT_TENDER, c.cost_basis, c.end);
}

/**
 * The month ended: run the contracts, close tenders, open new ones. Called
 * before the line statistics move on to the next month.
 */
void TransitMonthlyLoop()
{
	TimerGameEconomy::Date today = TimerGameEconomy::date;

	for (TransitContract &c : _transit_contracts) {
		if (c.state == TransitContractState::Active) {
			RunContractMonth(c, today);
		} else if (c.state == TransitContractState::Tender && today >= c.end) {
			ResolveTender(c, today);
		}
	}
	std::erase_if(_transit_contracts, [today](const TransitContract &c) {
		return !Town::IsValidID(c.town) || (c.state == TransitContractState::Ended && today - c.end > KEEP_ENDED_DAYS);
	});

	for (Town *t : Town::Iterate()) MaybeOpenTender(t, today);

	if (TimerGameEconomy::month == 0) {
		for (Town *t : Town::Iterate()) {
			t->transit.fares_last_year = t->transit.fares_this_year;
			t->transit.fares_this_year = 0;
		}
	}

	InvalidateWindowClassesData(WindowClass::TransitAuthority);
	InvalidateWindowClassesData(WindowClass::Line);
}

/** Forget all contracts; used when starting or loading a game. */
void InitializeTransitAuthorities()
{
	_transit_contracts.clear();
	_next_transit_contract = 1;
}

/**
 * Next free contract number, for saving.
 * @return The number.
 */
uint32_t &GetNextTransitContractNumber()
{
	return _next_transit_contract;
}

/**
 * Bid for a town's tender, or take a bid back.
 * @param flags Operation to perform.
 * @param contract The tender.
 * @param margin Margin on top of the line's costs in percent, or UINT8_MAX to take the bid back.
 * @return The cost of this operation or an error.
 */
CommandCost CmdTransitBid(DoCommandFlags flags, uint32_t contract, uint8_t margin)
{
	TransitContract *c = GetTransitContract(contract);
	if (c == nullptr || c->state != TransitContractState::Tender || c->company != _current_company) return CMD_ERROR;
	if (margin != UINT8_MAX && margin > TRANSIT_BID_MAX) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		c->bid = margin;
		InvalidateWindowClassesData(WindowClass::TransitAuthority);
	}
	return CommandCost();
}

/**
 * End a running contract early, for a penalty of a few months' fees.
 * @param flags Operation to perform.
 * @param contract The contract.
 * @return The cost of this operation or an error.
 */
CommandCost CmdTransitEndContract(DoCommandFlags flags, uint32_t contract)
{
	TransitContract *c = GetTransitContract(contract);
	if (c == nullptr || c->state != TransitContractState::Active || c->company != _current_company) return CMD_ERROR;

	CommandCost cost(ExpensesType::Other, c->fee * EARLY_END_MONTHS);
	if (flags.Test(DoCommandFlag::Execute)) {
		EndContract(*c, TransitContractEnd::EndedByCompany, TimerGameEconomy::date);
		InvalidateWindowClassesData(WindowClass::TransitAuthority);
		InvalidateWindowClassesData(WindowClass::Line);
	}
	return cost;
}
