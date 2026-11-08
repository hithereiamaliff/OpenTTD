/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file transit_authority.cpp Town transit authorities: budgets, tenders and contracts for lines. */

#include "stdafx.h"
#include "transit_authority.h"
#include "brt.h"
#include "city_bus.h"
#include "roadveh.h"
#include "transit_authority_cmd.h"
#include "cargotype.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "core/random_func.hpp"
#include "economy_func.h"
#include "fare.h"
#include "line.h"
#include "map_func.h"
#include "satisfaction.h"
#include "state.h"
#include "town_map.h"
#include "news_func.h"
#include "order_base.h"
#include "station_base.h"
#include "strings_func.h"
#include "town.h"
#include "town_budget.h"
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
/** Length of a contract for a line run mostly with electric buses: they pay off over a longer term (Bogota and Santiago give 14-15 years). */
static constexpr int ELECTRIC_CONTRACT_YEARS = 8;
/** Quality points a town adds for a line run only with electric buses. */
static constexpr uint ELECTRIC_QUALITY_BONUS = 10;
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

/** A town asks for a feeder line with a chance of one in this a month. */
static constexpr uint FEEDER_CHANCE = 6;
/** A town asks for a new feeder line at most this often, in days. */
static constexpr int FEEDER_PAUSE_DAYS = 540;
/** How long a town waits for someone to run the feeder line it asked for, in days. */
static constexpr int FEEDER_OFFER_DAYS = 365;
/** Margin a town pays on top of the costs of a feeder line, in percent. */
static constexpr uint FEEDER_MARGIN = 20;
/** A feeder line needs a stop at most this far from the railway station, in tiles. */
static constexpr uint FEEDER_STATION_REACH = 4;
/** The houses a feeder line is wanted for are this far from the railway station, in tiles. */
static constexpr uint FEEDER_MIN_DISTANCE = 8;
static constexpr uint FEEDER_MAX_DISTANCE = 24;
/** Departures a month a feeder line needs before the town signs. */
static constexpr uint FEEDER_MIN_DEPARTURES = 4;

static std::vector<TransitContract> _transit_contracts; ///< All tenders and contracts.

static bool LineFeedsStation(const OrderList *list, StationID station, TileIndex target);
static bool IsFeederStationThere(const TransitContract &c);
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
Money GetTransitBusYearCost()
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
	/* With town budgets, a share of the town's taxes: as much as before on land of medium value and at normal taxes. */
	if (AreTownBudgetsOn()) return GetTownTaxBase(t) * (GetTownTransitSharePercent(t) + GetStateTransitGrantPercent(t)) / 100;
	return GetTransitBusYearCost() * t->cache.population * BUDGET_BUSES_PER_10K / 10000;
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
 * What a town could still spend on contracts a year: its budget, last year's fares from its contracts and congestion charges, less its contracts.
 * @param t The town.
 * @return The money.
 */
Money GetTownTransitAvailable(const Town *t)
{
	return GetTownTransitBudget(t) + t->transit.fares_last_year + t->transit.charge_last_year - GetTownTransitCommitted(t);
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
 * Is this a town's open request for a feeder line, that no line runs yet?
 * @param c The tender or contract.
 * @return True if it is.
 */
bool IsFeederOffer(const TransitContract &c)
{
	return c.type == TransitContractType::Feeder && c.state == TransitContractState::Tender && c.line == OrderListID::Invalid();
}

/**
 * A town's open request for a feeder line.
 * @param town The town.
 * @return The request, or nullptr if there is none.
 */
const TransitContract *GetTownFeederOffer(TownID town)
{
	for (const TransitContract &c : _transit_contracts) {
		if (c.town == town && IsFeederOffer(c)) return &c;
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
 * Share of a line's vehicles that are electric buses.
 * @param list The line.
 * @return The share in percent.
 */
static uint GetLineElectricShare(const OrderList *list)
{
	uint electric = 0, count = 0;
	for (const Vehicle *v = list->GetFirstSharedVehicle(); v != nullptr; v = v->NextShared()) {
		if (v->type == VehicleType::Road && IsElectricBus(RoadVehicle::From(v))) electric++;
		count++;
	}
	return count == 0 ? 0 : electric * 100 / count;
}

/**
 * How good a line's service is, as a town judges a bid: regularity (40%),
 * reliability of its vehicles (30%) and their age (30%), plus up to 10
 * points for electric buses.
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
	const uint quality = (40 * regularity + 30 * reliability / count + 30 * youth / count) / 100;
	return std::min<uint>(100, quality + ELECTRIC_QUALITY_BONUS * GetLineElectricShare(list) / 100 + GetBrtTenderQualityBonus(list));
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
	/* Lines run mostly with electric buses get a longer term. */
	const int years = GetLineElectricShare(list) >= 50 ? ELECTRIC_CONTRACT_YEARS : CONTRACT_YEARS;
	c.end = today + years * EconomyTime::DAYS_IN_YEAR;
	c.fee = c.cost_basis * (100 + c.bid) / 100;
	c.required_departures = static_cast<uint16_t>(std::max<uint>(2, list->line.this_month.departures * 9 / 10));
	c.baseline_wait = GetExcessWait(list->line);
	c.months = 0;
	c.months_met = 0;
	c.idle_months = 0;
	c.paid = 0;
	c.fares = 0;
	AddContractNews(c, c.type == TransitContractType::FreeLine ? STR_NEWS_TRANSIT_WON_FREE : STR_NEWS_TRANSIT_WON, c.company, c.fee, years);
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

	/* A feeder line must keep linking its railway station with the houses. */
	if (c.type == TransitContractType::Feeder) {
		if (!IsFeederStationThere(c)) {
			EndContract(c, TransitContractEnd::StationGone, today);
			return;
		}
		if (!LineFeedsStation(list, c.station, c.target)) {
			EndContract(c, TransitContractEnd::NoService, today);
			AddContractNews(c, STR_NEWS_TRANSIT_ENDED_NO_SERVICE);
			return;
		}
	}

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
 * The stations with railway platforms that passenger trains stopped at last month.
 * @return Whether each station (by index) did.
 */
static std::vector<bool> GetStationsWithPassengerTrains()
{
	std::vector<bool> served(Station::GetPoolSize(), false);
	for (const OrderList *list : OrderList::Iterate()) {
		const Vehicle *v = list->GetFirstSharedVehicle();
		if (v == nullptr || v->type != VehicleType::Train || list->line.last_month.load_count == 0 || !IsPassengerLine(list)) continue;
		for (const Order &order : list->GetOrders()) {
			if (!order.IsType(OT_GOTO_STATION)) continue;
			const Station *st = Station::GetIfValid(order.GetDestination().ToStationID());
			if (st != nullptr && !st->train_station.IsEmpty()) served[st->index.base()] = true;
		}
	}
	return served;
}

/**
 * Do the bus stops or railway stations of any company reach a tile?
 * @param tile The tile.
 * @return True if one does.
 */
static bool IsReachedByPassengerStation(TileIndex tile)
{
	bool reached = false;
	ForAllStationsAroundTiles(TileArea(tile, 1, 1), [&reached](Station *st, TileIndex) {
		if (st->facilities.Any({StationFacility::BusStop, StationFacility::Train})) reached = true;
		return true;
	});
	return reached;
}

/**
 * Find houses of a town that no stop reaches, a bus ride from a railway station.
 * @param t The town.
 * @param rail The railway station.
 * @return The middle of the houses, or INVALID_TILE if there are none.
 */
static TileIndex FindFeederHouses(const Town *t, const Station *rail)
{
	const int radius = std::max<int>(FEEDER_MIN_DISTANCE, IntSqrt(t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)]));
	TileIndex best = INVALID_TILE;
	uint best_houses = 0;
	for (uint i = 0; i < 64; i++) {
		const int dx = static_cast<int>(RandomRange(2 * radius + 1)) - radius;
		const int dy = static_cast<int>(RandomRange(2 * radius + 1)) - radius;
		const TileIndex tile = TileAddWrap(t->xy, dx, dy);
		if (tile == INVALID_TILE || !IsTileType(tile, TileType::House) || GetTownIndex(tile) != t->index) continue;
		const uint distance = DistanceManhattan(tile, rail->xy);
		if (distance < FEEDER_MIN_DISTANCE || distance > FEEDER_MAX_DISTANCE || IsReachedByPassengerStation(tile)) continue;
		uint houses = 0;
		for (TileIndex near : TileArea(tile, 1, 1).Expand(2)) {
			if (IsTileType(near, TileType::House) && GetTownIndex(near) == t->index) houses++;
		}
		if (houses > best_houses) {
			best = tile;
			best_houses = houses;
		}
	}
	return best_houses >= 6 ? best : INVALID_TILE;
}

/**
 * Is the railway station of a feeder line still there: a station with
 * railway platforms in the town (its number may have gone to another
 * station since)?
 * @param c The feeder request or contract.
 * @return True if it is.
 */
static bool IsFeederStationThere(const TransitContract &c)
{
	const Station *rail = Station::GetIfValid(c.station);
	return rail != nullptr && !rail->train_station.IsEmpty() && rail->town != nullptr && rail->town->index == c.town;
}

/**
 * Does a bus line feed a railway station: a stop at it or next to it and,
 * if given, a stop near the houses?
 * @param list The line.
 * @param station The railway station.
 * @param target The houses, or INVALID_TILE.
 * @return True if it does.
 */
static bool LineFeedsStation(const OrderList *list, StationID station, TileIndex target)
{
	const Station *rail = Station::GetIfValid(station);
	if (rail == nullptr || !IsBusLine(list)) return false;
	bool at_station = false, at_houses = target == INVALID_TILE;
	for (const Order &order : list->GetOrders()) {
		if (!order.IsType(OT_GOTO_STATION) || order.GetNonStopType().Test(OrderNonStopFlag::GoVia)) continue;
		const Station *st = Station::GetIfValid(order.GetDestination().ToStationID());
		if (st == nullptr) continue;
		if (st == rail || DistanceManhattan(st->xy, rail->xy) <= FEEDER_STATION_REACH) at_station = true;
		if (target != INVALID_TILE && (st->TileIsInCatchment(target) || DistanceManhattan(st->xy, target) <= 3)) at_houses = true;
	}
	return at_station && at_houses;
}

/**
 * Now and then a town with a railway station asks for a feeder bus line
 * between the station and houses no stop reaches, as Rapid KL runs feeder
 * buses to the MRT, if its budget allows. The first company to run one gets
 * a contract.
 * @param t The town.
 * @param today The date.
 * @param rail_served The stations passenger trains stopped at last month.
 */
static void MaybeOfferFeeder(Town *t, TimerGameEconomy::Date today, const std::vector<bool> &rail_served)
{
	if (t->cache.population < MIN_TRANSIT_POPULATION) return;
	for (const TransitContract &c : _transit_contracts) {
		if (c.town == t->index && c.type == TransitContractType::Feeder && (IsFeederOffer(c) || today - c.start < FEEDER_PAUSE_DAYS)) return;
	}

	/* Only towns with passenger trains roll the dice, so games without them run as before. */
	std::vector<const Station *> stations;
	for (const Station *st : Station::Iterate()) {
		if (st->town == t && rail_served[st->index.base()]) stations.push_back(st);
	}
	if (stations.empty() || !Chance16(1, FEEDER_CHANCE)) return;
	const Station *rail = stations[RandomRange(static_cast<uint32_t>(stations.size()))];
	const TileIndex target = FindFeederHouses(t, rail);
	if (target == INVALID_TILE) return;

	/* Two to four buses, by how far it is; the town must afford a year of it. */
	const uint buses = Clamp<uint>(DistanceManhattan(target, rail->xy) / 8 + 1, 2, 4);
	const Money estimate = GetTransitBusYearCost() * buses / 24 * (100 + FEEDER_MARGIN) / 100;
	if (estimate * 12 > GetTownTransitAvailable(t)) return;

	TransitContract &c = _transit_contracts.emplace_back();
	c.id = _next_transit_contract++;
	c.town = t->index;
	c.type = TransitContractType::Feeder;
	c.state = TransitContractState::Tender;
	c.station = rail->index;
	c.target = target;
	c.start = today;
	c.end = today + FEEDER_OFFER_DAYS;
	c.cost_basis = estimate;
	AddNewsItem(GetEncodedString(STR_NEWS_TRANSIT_FEEDER_WANTED, t->index, rail->index, DistanceManhattan(target, rail->xy), estimate, c.end),
			NewsType::Subsidies, NewsStyle::Normal, {}, target, rail->index);
}

/**
 * A town's request for a feeder line: sign with the busiest line that runs it, or give up when no one does in time.
 * @param c The request.
 * @param today The date.
 */
static void CheckFeederOffer(TransitContract &c, TimerGameEconomy::Date today)
{
	if (!IsFeederStationThere(c)) {
		EndContract(c, TransitContractEnd::StationGone, today);
		return;
	}

	OrderList *best = nullptr;
	for (OrderList *list : OrderList::Iterate()) {
		const Vehicle *v = list->GetFirstSharedVehicle();
		if (v == nullptr || !Company::IsValidID(v->owner) || list->line.this_month.departures < FEEDER_MIN_DEPARTURES) continue;
		if (best != nullptr && list->line.this_month.departures <= best->line.this_month.departures) continue;
		if (!IsPassengerLine(list) || !LineFeedsStation(list, c.station, c.target)) continue;
		if (std::ranges::any_of(_transit_contracts, [list](const TransitContract &o) { return o.line == list->index && o.state != TransitContractState::Ended; })) continue;
		best = list;
	}
	if (best == nullptr) {
		if (today >= c.end) EndContract(c, TransitContractEnd::NoTaker, today);
		return;
	}

	c.company = best->GetFirstSharedVehicle()->owner;
	c.line = best->index;
	c.state = TransitContractState::Active;
	c.start = today;
	c.end = today + CONTRACT_YEARS * EconomyTime::DAYS_IN_YEAR;
	c.cost_basis = GetTransitLineCostBasis(best);
	c.bid = FEEDER_MARGIN;
	c.fee = c.cost_basis * (100 + FEEDER_MARGIN) / 100;
	c.required_departures = static_cast<uint16_t>(std::max<uint>(2, best->line.this_month.departures * 9 / 10));
	c.baseline_wait = GetExcessWait(best->line);
	c.months = 0;
	c.months_met = 0;
	c.idle_months = 0;
	c.paid = 0;
	c.fares = 0;
	AddContractNews(c, STR_NEWS_TRANSIT_FEEDER_SIGNED, c.company, c.station, c.fee, CONTRACT_YEARS);
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
 * A town that grew big enough decides once whether it gives its residents a
 * free pass (four towns in ten) and whether it pays for concession fares
 * (half of them).
 * @param t The town.
 */
static void DecideTownSchemes(Town *t)
{
	if (t->transit.policy_set || t->cache.population < MIN_TRANSIT_POPULATION) return;
	t->transit.policy_set = true;
	t->transit.resident_pass = Chance16(40, 100);
	t->transit.pays_concessions = Chance16(50, 100);
	if (t->transit.resident_pass) {
		AddNewsItem(GetEncodedString(STR_NEWS_TRANSIT_RESIDENT_PASS, t->index), NewsType::Subsidies, NewsStyle::Normal, {}, t->index);
	}
}

/**
 * Pay the operators for the town's passes and concessions of the month,
 * as far as the town's budget goes.
 * @param t The town.
 */
static void PayTownSchemes(Town *t)
{
	TownTransit &transit = t->transit;
	transit.last_pass_holders = 0;
	transit.last_pass_paid = 0;
	transit.last_concessions_paid = 0;
	if (!transit.resident_pass && !transit.pays_concessions) return;

	CargoType cargo = INVALID_CARGO;
	for (const CargoSpec *cs : CargoSpec::town_production_cargoes[TownProductionEffect::Passengers]) {
		cargo = cs->Index();
		break;
	}
	Money pass_price = IsValidCargoType(cargo) ? GetTransportedGoodsIncome(1, 20, 0, cargo) * RESIDENT_PASS_FARES : Money{0};

	bool short_of_money = false;
	for (uint company = 0; company < MAX_COMPANIES; company++) {
		uint32_t holders = (transit.pass_riders[company] + 50 * 40) / (100 * 40);
		Money owed = pass_price * holders + transit.concessions_owed[company];
		Money pass_part = pass_price * holders;
		transit.pass_riders[company] = 0;
		Money concessions = transit.concessions_owed[company];
		transit.concessions_owed[company] = 0;
		if (owed <= 0 || !Company::IsValidID(company)) continue;

		Money left = GetTownTransitAvailable(t) - transit.spent_this_year;
		if (left < owed) {
			short_of_money = true;
			if (left <= 0) continue;
			int64_t share_left = static_cast<int64_t>(left), share_owed = static_cast<int64_t>(owed);
			pass_part = Money{static_cast<int64_t>(pass_part) * share_left / share_owed};
			concessions = Money{static_cast<int64_t>(concessions) * share_left / share_owed};
			owed = left;
		}
		SubtractMoneyFromCompany(static_cast<CompanyID>(company), CommandCost(ExpensesType::RoadVehRevenue, -owed));
		transit.spent_this_year += owed;
		transit.last_pass_holders += holders;
		transit.last_pass_paid += pass_part;
		transit.last_concessions_paid += concessions;
	}
	if (short_of_money && !transit.told_budget_gone) {
		transit.told_budget_gone = true;
		AddNewsItem(GetEncodedString(STR_NEWS_TRANSIT_BUDGET_GONE, t->index), NewsType::Subsidies, NewsStyle::Normal, {}, t->index);
	}
}

/**
 * The month ended: run the contracts, close tenders, open new ones, pay for
 * the towns' passes and concessions. Called before the line statistics move
 * on to the next month.
 */
void TransitMonthlyLoop()
{
	TimerGameEconomy::Date today = TimerGameEconomy::date;

	for (TransitContract &c : _transit_contracts) {
		if (IsFeederOffer(c)) {
			CheckFeederOffer(c, today);
		} else if (c.state == TransitContractState::Active) {
			RunContractMonth(c, today);
		} else if (c.state == TransitContractState::Tender && today >= c.end) {
			ResolveTender(c, today);
		}
	}
	std::erase_if(_transit_contracts, [today](const TransitContract &c) {
		return !Town::IsValidID(c.town) || (c.state == TransitContractState::Ended && today - c.end > KEEP_ENDED_DAYS);
	});

	const std::vector<bool> rail_served = GetStationsWithPassengerTrains();
	for (Town *t : Town::Iterate()) {
		MaybeOpenTender(t, today);
		MaybeOfferFeeder(t, today, rail_served);
		DecideTownSchemes(t);
		PayTownSchemes(t);
	}

	if (TimerGameEconomy::month == 0) {
		for (Town *t : Town::Iterate()) {
			t->transit.fares_last_year = t->transit.fares_this_year;
			t->transit.fares_this_year = 0;
			t->transit.spent_this_year = 0;
			t->transit.told_budget_gone = false;
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

/**
 * Set a town's resident pass and concession schemes; for game scripts.
 * @param flags Operation to perform.
 * @param town The town.
 * @param resident_pass Whether residents ride free with the town's pass.
 * @param pays_concessions Whether the town pays back concession fares.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetTownTransitSchemes(DoCommandFlags flags, TownID town, bool resident_pass, bool pays_concessions)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	Town *t = Town::GetIfValid(town);
	if (t == nullptr) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		t->transit.policy_set = true;
		t->transit.resident_pass = resident_pass;
		t->transit.pays_concessions = pays_concessions;
		InvalidateWindowClassesData(WindowClass::TransitAuthority);
	}
	return CommandCost();
}
