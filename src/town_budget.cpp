/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file town_budget.cpp Towns' budgets and ordinances.
 *
 * Towns collect property and business taxes, more from bigger towns and
 * dearer land. What it spends is reckoned from its taxes at the normal rate:
 * a quarter goes to transit (the transit budget of the town's transit
 * authority, which pays for contracts, passes and concessions), 60% to
 * running the town; the rest pays for ordinances or builds reserves. Each
 * year a town cuts its tax rate after a year in surplus with more than a
 * year's taxes put by, raises it after a year in deficit with less than a
 * quarter put by (or any time it is in debt), and repeals its dearest
 * ordinance when it is deep in debt; so taxes settle where they pay for what
 * the town spends. High taxes make a town less happy and slow its growth.
 *
 * Towns bring in ordinances on their own when their problems call for them
 * and they can afford them: parking limits for a congested centre, a
 * low-emission zone or tree planting for dirty air, a car-free centre where
 * many people ride transit, transit-oriented development where trains stop,
 * a transit fund. Companies can propose them too; a town agrees if the
 * problem is there, the money is there and it rates the company well enough.
 */

#include "stdafx.h"
#include "town_budget.h"
#include "town_budget_cmd.h"
#include "cargotype.h"
#include "city_bus.h"
#include "bridge_map.h"
#include "clear_map.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "core/random_func.hpp"
#include "economy_func.h"
#include "environment.h"
#include "map_func.h"
#include "news_func.h"
#include "road.h"
#include "road_pricing.h"
#include "roadveh.h"
#include "settings_type.h"
#include "station_base.h"
#include "strings_func.h"
#include "town.h"
#include "town_traffic.h"
#include "town_zoning.h"
#include "transit_authority.h"
#include "tree_cmd.h"
#include "window_func.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"

#include "table/strings.h"

#include "safeguards.h"

/** What each ordinance costs a year, in percent of the town's taxes. A transit fund moves 15% more of them to transit. */
static constexpr std::array<uint, ORDINANCE_COUNT> ORDINANCE_COST = {15, 1, 2, 6, 3, 0};
/** Parking levies bring in this share of the town's taxes, in percent. */
static constexpr uint PARKING_LEVY_PERCENT = 3;
/** An ordinance stays at least this long before a town repeals it to save money, in days. */
static constexpr int ORDINANCE_MIN_DAYS = 2 * EconomyTime::DAYS_IN_YEAR;
/** Lowest and highest tax rates, and the step a town changes them by in a year, in percent. */
static constexpr uint TAX_MIN = 70, TAX_MAX = 150, TAX_STEP = 10;

static uint _lez_towns = 0; ///< Towns with a low-emission zone, to skip looking when there are none.

/**
 * Do towns keep budgets and bring in ordinances (setting economy.town_budgets)?
 * @return True if they do.
 */
bool AreTownBudgetsOn()
{
	return _settings_game.economy.town_budgets;
}

/**
 * Has a town an ordinance in force?
 * @param t The town.
 * @param ordinance The ordinance.
 * @return True if it has.
 */
bool HasOrdinance(const Town *t, Ordinance ordinance)
{
	return AreTownBudgetsOn() && HasBit(t->budget.ordinances, to_underlying(ordinance));
}

/**
 * A town's taxes a year at the normal rate: about £10.80 a resident at
 * normal costs on land worth 50, in proportion to 50 plus its land value
 * (+30% on land worth 80, -30% on land worth 20). What the town spends is
 * reckoned from it.
 * @param t The town.
 * @return The taxes at the normal rate.
 */
Money GetTownTaxBase(const Town *t)
{
	const uint value = (IsEnvironmentKnown() && t->environment.land_value != 0) ? t->environment.land_value : 50;
	return GetTransitBusYearCost() * t->cache.population * 48 / 10000 * (50 + value) / 100;
}

/**
 * A town's taxes a year at its current rate.
 * @param t The town.
 * @return The taxes.
 */
Money GetTownYearlyTaxes(const Town *t)
{
	return GetTownTaxBase(t) * t->budget.tax_rate / 100;
}

/**
 * Share of its taxes a town spends on transit.
 * @param t The town.
 * @return The share in percent.
 */
uint GetTownTransitSharePercent(const Town *t)
{
	return HasOrdinance(t, Ordinance::TransitFund) ? TOWN_TRANSIT_FUND_SHARE : TOWN_TRANSIT_SHARE;
}

/**
 * What an ordinance costs a year.
 * @param ordinance The ordinance.
 * @return The cost in percent of the town's taxes.
 */
uint GetOrdinanceCostPercent(Ordinance ordinance)
{
	return ORDINANCE_COST[to_underlying(ordinance)];
}

/**
 * What a town's ordinances cost it a year.
 * @param t The town.
 * @return The cost in percent of its taxes.
 */
uint GetTownOrdinanceCostPercent(const Town *t)
{
	uint cost = 0;
	for (uint o = 0; o < ORDINANCE_COUNT; o++) {
		if (HasOrdinance(t, static_cast<Ordinance>(o))) cost += ORDINANCE_COST[o];
	}
	return cost;
}

/**
 * What a town spends a year: running itself, transit and its ordinances,
 * reckoned from its taxes at the normal rate, whatever its tax rate.
 * @param t The town.
 * @return The spending.
 */
Money GetTownYearlySpending(const Town *t)
{
	return GetTownTaxBase(t) * (TOWN_RUNNING_SHARE + TOWN_TRANSIT_SHARE + GetTownOrdinanceCostPercent(t)) / 100;
}

/**
 * How much of its traffic a road keeps under the town's ordinances: 85% in
 * the centre and inner suburbs with parking limits, 40% in the centre when
 * it is car-free.
 * @param t The town the road belongs to.
 * @param tile The road tile.
 * @return The share in percent.
 */
uint GetOrdinanceTrafficPercent(const Town *t, TileIndex tile)
{
	if (t == nullptr || !AreTownBudgetsOn() || t->budget.ordinances == 0) return 100;
	const bool parking = HasOrdinance(t, Ordinance::ParkingLimits);
	const bool car_free = HasOrdinance(t, Ordinance::CarFreeCentre);
	if (!parking && !car_free) return 100;
	const HouseZone zone = GetTownRadiusGroup(t, tile);
	uint percent = 100;
	if (car_free && zone == HouseZone::TownCentre) percent = percent * 40 / 100;
	if (parking && (zone == HouseZone::TownCentre || zone == HouseZone::TownInnerSuburb)) percent = percent * 85 / 100;
	return percent;
}

/**
 * Is a tile in a town's low-emission zone: its centre and inner suburbs?
 * @param t The town.
 * @param tile The tile.
 * @return True if it is.
 */
bool IsInLowEmissionZone(const Town *t, TileIndex tile)
{
	if (!HasOrdinance(t, Ordinance::LowEmissionZone)) return false;
	const HouseZone zone = GetTownRadiusGroup(t, tile);
	return zone == HouseZone::TownCentre || zone == HouseZone::TownInnerSuburb;
}

/**
 * Has any town a low-emission zone?
 * @return True if one has.
 */
bool AnyLowEmissionZones()
{
	return _lez_towns > 0;
}

/** Count the towns with a low-emission zone again, e.g. after loading a game. */
void UpdateLowEmissionZoneTowns()
{
	_lez_towns = 0;
	for (const Town *t : Town::Iterate()) {
		if (HasOrdinance(t, Ordinance::LowEmissionZone)) _lez_towns++;
	}
}

/**
 * A road vehicle drove onto a tile of a town: one that isn't electric pays when it drives into the town's low-emission zone.
 * @param front The vehicle (front).
 * @param t The closest town.
 * @param from The tile it came from.
 * @param to The tile it drove onto.
 */
void OnRoadVehicleEntersLowEmissionZone(RoadVehicle *front, Town *t, TileIndex from, TileIndex to)
{
	if (!IsInLowEmissionZone(t, to) || (IsValidTile(from) && IsInLowEmissionZone(t, from))) return;
	if (!Company::IsValidID(front->owner)) return;
	if (IsElectricBus(front) || GetRoadTypeInfo(front->roadtype)->flags.Test(RoadTypeFlag::Catenary)) return;
	const Money fee = GetPrice(Price::BuildObject, 1, nullptr) * GetTollClassPercent(front) / 100;
	if (fee <= 0) return;
	SubtractMoneyFromCompany(front->owner, CommandCost(ExpensesType::RoadVehRun, fee));
	front->profit_this_year -= fee << 8;
	t->budget.fees_this_year += fee;
	t->budget.reserves += fee;
	t->budget.lez_charged_this_month++;
}

/**
 * How taxes and ordinances change a town's happiness: a point per 5% of
 * taxes above or below normal, +3 for a car-free centre, -1 for parking limits.
 * @param t The town.
 * @return The points.
 */
int GetTownBudgetHappiness(const Town *t)
{
	if (!AreTownBudgetsOn()) return 0;
	int happiness = -(static_cast<int>(t->budget.tax_rate) - 100) / 5;
	if (HasOrdinance(t, Ordinance::CarFreeCentre)) happiness += 3;
	if (HasOrdinance(t, Ordinance::ParkingLimits)) happiness -= 1;
	return happiness;
}

/**
 * How taxes change the time between a town's new houses: in proportion to the tax rate.
 * @param t The town.
 * @return The time in percent of normal.
 */
uint GetTownTaxGrowthPercent(const Town *t)
{
	return AreTownBudgetsOn() ? t->budget.tax_rate : 100;
}

/**
 * The name of an ordinance.
 * @param ordinance The ordinance.
 * @return The name.
 */
StringID GetOrdinanceName(Ordinance ordinance)
{
	static constexpr StringID NAMES[] = {
		STR_ORDINANCE_TRANSIT_FUND, STR_ORDINANCE_PARKING_LIMITS, STR_ORDINANCE_LOW_EMISSION_ZONE,
		STR_ORDINANCE_CAR_FREE_CENTRE, STR_ORDINANCE_TREE_PLANTING, STR_ORDINANCE_TRANSIT_ORIENTED_DEVELOPMENT,
	};
	static_assert(std::size(NAMES) == ORDINANCE_COUNT);
	return NAMES[to_underlying(ordinance)];
}

/**
 * Does a company run passenger services from a town: a bus stop or railway station there?
 * @param t The town.
 * @param company The company.
 * @return True if it does.
 */
static bool CompanyServesTown(const Town *t, CompanyID company)
{
	for (const Station *st : Station::Iterate()) {
		if (st->owner == company && st->town == t && st->facilities.Any({StationFacility::BusStop, StationFacility::Train})) return true;
	}
	return false;
}

/**
 * Has a town a railway station where passenger trains stop?
 * @param t The town.
 * @return True if it has.
 */
static bool HasRailwayWithPassengers(const Town *t)
{
	for (const Station *st : Station::Iterate()) {
		if (st->town != t || !st->facilities.Test(StationFacility::Train)) continue;
		for (const CargoSpec *cs : CargoSpec::Iterate()) {
			if (cs->IsValid() && IsCargoInClass(cs->Index(), CargoClass::Passengers) && st->goods[cs->Index()].HasRating()) return true;
		}
	}
	return false;
}

/**
 * Can a town afford an ordinance: from what it has left of its taxes, or from two years' worth of reserves?
 * @param t The town.
 * @param ordinance The ordinance.
 * @return True if it can.
 */
static bool CanAffordOrdinance(const Town *t, Ordinance ordinance)
{
	const uint cost = GetOrdinanceCostPercent(ordinance);
	if (cost == 0) return true;
	const uint used = GetTownOrdinanceCostPercent(t);
	const uint free = 100 - TOWN_RUNNING_SHARE - TOWN_TRANSIT_SHARE;
	if (used + cost <= free) return true;
	return t->budget.reserves >= GetTownTaxBase(t) * cost / 100 * 2;
}

/**
 * Why a town wouldn't bring in an ordinance a company proposes, or now on its own.
 * @param t The town.
 * @param ordinance The ordinance.
 * @param company The company proposing it, or INVALID_COMPANY for the town itself.
 * @return The reason, or INVALID_STRING_ID if it would.
 */
StringID GetOrdinanceRefusal(const Town *t, Ordinance ordinance, CompanyID company)
{
	if (!AreTownBudgetsOn()) return STR_ERROR_ORDINANCES_OFF;
	if (HasOrdinance(t, ordinance)) return STR_ERROR_ORDINANCE_ALREADY;
	const uint population = t->cache.population;
	switch (ordinance) {
		case Ordinance::TransitFund:
			if (population < 1500) return STR_ERROR_ORDINANCE_TOWN_TOO_SMALL;
			if (Company::IsValidID(company) && !CompanyServesTown(t, company)) return STR_ERROR_ORDINANCE_NO_SERVICE;
			break;

		case Ordinance::ParkingLimits:
			if (population < 3000) return STR_ERROR_ORDINANCE_TOWN_TOO_SMALL;
			if (GetCongestionChargeZoneLevel(t) < 15) return STR_ERROR_ORDINANCE_NOT_CONGESTED;
			break;

		case Ordinance::LowEmissionZone:
			if (population < 3000) return STR_ERROR_ORDINANCE_TOWN_TOO_SMALL;
			if (!IsEnvironmentKnown() || t->environment.api <= 50) return STR_ERROR_ORDINANCE_AIR_CLEAN;
			break;

		case Ordinance::CarFreeCentre:
			if (population < 5000) return STR_ERROR_ORDINANCE_TOWN_TOO_SMALL;
			if (GetTownCarReduction(t) < 15) return STR_ERROR_ORDINANCE_TRANSIT_SHARE;
			break;

		case Ordinance::TreePlanting:
			if (population < 1000) return STR_ERROR_ORDINANCE_TOWN_TOO_SMALL;
			break;

		case Ordinance::TransitOrientedDevelopment:
			if (!HasRailwayWithPassengers(t)) return STR_ERROR_ORDINANCE_NO_RAILWAY;
			break;

		default:
			return STR_ERROR_ORDINANCES_OFF;
	}
	if (!CanAffordOrdinance(t, ordinance)) return STR_ERROR_ORDINANCE_CANT_AFFORD;

	/* The dearer and stricter ordinances need a company the town thinks well of. */
	const bool strict = ordinance == Ordinance::TransitFund || ordinance == Ordinance::LowEmissionZone || ordinance == Ordinance::CarFreeCentre;
	if (Company::IsValidID(company) && t->have_ratings.Test(company) && t->ratings[company] < (strict ? RATING_GOOD : RATING_MEDIOCRE)) {
		return strict ? STR_ERROR_ORDINANCE_RATING_GOOD : STR_ERROR_ORDINANCE_RATING_MEDIOCRE;
	}
	return INVALID_STRING_ID;
}

/**
 * A town brings in an ordinance.
 * @param t The town.
 * @param ordinance The ordinance.
 * @param proposer The company that proposed it, or INVALID_COMPANY.
 */
static void StartOrdinance(Town *t, Ordinance ordinance, CompanyID proposer)
{
	SetBit(t->budget.ordinances, to_underlying(ordinance));
	t->budget.since[to_underlying(ordinance)] = TimerGameEconomy::date;
	if (ordinance == Ordinance::LowEmissionZone) UpdateLowEmissionZoneTowns();
	if (Company::IsValidID(proposer)) {
		AddNewsItem(GetEncodedString(STR_NEWS_ORDINANCE_PROPOSED, t->index, GetOrdinanceName(ordinance), proposer), NewsType::General, NewsStyle::Normal, {}, t->index);
	} else {
		AddNewsItem(GetEncodedString(STR_NEWS_ORDINANCE_ADOPTED, t->index, GetOrdinanceName(ordinance)), NewsType::General, NewsStyle::Normal, {}, t->index);
	}
	SetWindowDirty(WindowClass::TownBudget, t->index);
	SetWindowDirty(WindowClass::TownView, t->index);
	SetWindowDirty(WindowClass::TransitAuthority, t->index);
}

/**
 * A town repeals an ordinance.
 * @param t The town.
 * @param ordinance The ordinance.
 * @param news Whether to say so in the news.
 */
static void EndOrdinance(Town *t, Ordinance ordinance, bool news)
{
	ClrBit(t->budget.ordinances, to_underlying(ordinance));
	t->budget.since[to_underlying(ordinance)] = {};
	if (ordinance == Ordinance::LowEmissionZone) UpdateLowEmissionZoneTowns();
	if (news) AddNewsItem(GetEncodedString(STR_NEWS_ORDINANCE_REPEALED, t->index, GetOrdinanceName(ordinance)), NewsType::General, NewsStyle::Normal, {}, t->index);
	SetWindowDirty(WindowClass::TownBudget, t->index);
	SetWindowDirty(WindowClass::TownView, t->index);
	SetWindowDirty(WindowClass::TransitAuthority, t->index);
}

/**
 * Plant trees on free land in and around a town: on grass and rough land
 * that isn't zoned for buildings, a few a month by the size of the town.
 * Towns can still build on them later.
 * @param t The town.
 */
static void PlantTownTrees(Town *t)
{
	const uint wanted = std::min<uint>(8, t->cache.population / 2500 + 2);
	const int radius = static_cast<int>(IntSqrt(t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)])) + 2;
	uint planted = 0;
	for (uint attempt = 0; attempt < 40 && planted < wanted; attempt++) {
		const int dx = static_cast<int>(RandomRange(2 * radius + 1)) - radius;
		const int dy = static_cast<int>(RandomRange(2 * radius + 1)) - radius;
		const TileIndex tile = TileAddWrap(t->xy, dx, dy);
		if (tile == INVALID_TILE || !IsTileType(tile, TileType::Clear) || IsBridgeAbove(tile)) continue;
		if (IsClearGround(tile, ClearGround::Fields) || IsClearGround(tile, ClearGround::Rocks) || IsClearGround(tile, ClearGround::Desert)) continue;
		const TownZone zone = GetTownZone(tile);
		if (zone != TownZone::None && zone != TownZone::Park) continue;
		/* Young trees: no growth stage bits. */
		PlaceTree(tile, Random() & ~(7U << 16));
		planted++;
	}
	t->budget.trees_planted += planted;
}

/**
 * At the year's end a town looks at its money: taxes down when it has a
 * year's worth put by, up when its reserves are gone, back towards normal in
 * between, and its dearest ordinance repealed when reserves run low. Then it
 * may bring in an ordinance for its biggest problem.
 * @param t The town.
 */
static void ReviewTownBudget(Town *t)
{
	TownBudget &b = t->budget;
	const Money taxes = GetTownTaxBase(t);
	if (b.reserves < 0) {
		b.tax_rate = static_cast<uint8_t>(std::min<uint>(TAX_MAX, b.tax_rate + TAX_STEP));
		if (b.reserves < -taxes / 2) {
			/* Repeal the dearest ordinance that has had its time. */
			Ordinance dearest = Ordinance::End;
			for (uint o = 0; o < ORDINANCE_COUNT; o++) {
				const Ordinance ordinance = static_cast<Ordinance>(o);
				if (!HasOrdinance(t, ordinance) || ORDINANCE_COST[o] == 0) continue;
				if (TimerGameEconomy::date - b.since[o] < ORDINANCE_MIN_DAYS) continue;
				if (dearest == Ordinance::End || ORDINANCE_COST[o] > GetOrdinanceCostPercent(dearest)) dearest = ordinance;
			}
			if (dearest != Ordinance::End) EndOrdinance(t, dearest, true);
		}
		return;
	}
	/* Cut taxes only after a year in surplus, raise them only after a year in deficit. */
	const Money surplus = b.taxes_last_year + b.fees_last_year - b.spent_last_year;
	if (b.reserves > taxes && surplus >= 0) {
		b.tax_rate = static_cast<uint8_t>(std::max<uint>(TAX_MIN, b.tax_rate - TAX_STEP));
	} else if (b.reserves < taxes / 4 && surplus < 0) {
		b.tax_rate = static_cast<uint8_t>(std::min<uint>(TAX_MAX, b.tax_rate + TAX_STEP));
	}

	/* A town with a problem may bring in an ordinance for it, one a year. */
	if (t->cache.population < 1000) return;
	struct Candidate {
		Ordinance ordinance;
		bool problem;
		uint chance;
	};
	const uint api = IsEnvironmentKnown() ? t->environment.api : 0;
	const uint cars_off = GetTownCarReduction(t);
	const Candidate candidates[] = {
		{Ordinance::LowEmissionZone, api >= 100 && t->cache.population >= 3000, 2},
		{Ordinance::TreePlanting, api >= 60, 2},
		{Ordinance::ParkingLimits, t->cache.population >= 6000 && GetCongestionChargeZoneLevel(t) >= 25, 2},
		{Ordinance::CarFreeCentre, cars_off >= 25 && t->cache.population >= 8000, 3},
		{Ordinance::TransitOrientedDevelopment, true, 4},
		{Ordinance::TransitFund, cars_off >= 20 && b.reserves > taxes, 3},
	};
	for (const Candidate &c : candidates) {
		if (!c.problem || HasOrdinance(t, c.ordinance)) continue;
		if (GetOrdinanceRefusal(t, c.ordinance, CompanyID::Invalid()) != INVALID_STRING_ID) continue;
		if (!Chance16(1, c.chance)) continue;
		StartOrdinance(t, c.ordinance, CompanyID::Invalid());
		return;
	}
}

/** Each month: taxes, fees and spending; each year, the towns' reviews. */
static const IntervalTimer<TimerGameEconomy> _town_budgets_monthly({TimerGameEconomy::Trigger::Month, TimerGameEconomy::Priority::None}, [](auto)
{
	if (!AreTownBudgetsOn()) return;
	for (Town *t : Town::Iterate()) {
		TownBudget &b = t->budget;
		const Money base = GetTownTaxBase(t);
		if (!b.started) {
			b.started = true;
			b.reserves = base / 2;
		}
		const Money month_taxes = GetTownYearlyTaxes(t) / 12;
		const Money month_spending = GetTownYearlySpending(t) / 12;
		const Money month_fees = HasOrdinance(t, Ordinance::ParkingLimits) ? base * PARKING_LEVY_PERCENT / 100 / 12 : Money{0};
		b.taxes_this_year += month_taxes;
		b.spent_this_year += month_spending;
		b.fees_this_year += month_fees;
		b.reserves += month_taxes + month_fees - month_spending;
		b.lez_charged_last_month = b.lez_charged_this_month;
		b.lez_charged_this_month = 0;
		if (HasOrdinance(t, Ordinance::TreePlanting)) PlantTownTrees(t);
	}

	if (TimerGameEconomy::month == 0) {
		for (Town *t : Town::Iterate()) {
			TownBudget &b = t->budget;
			b.taxes_last_year = b.taxes_this_year;
			b.fees_last_year = b.fees_this_year;
			b.spent_last_year = b.spent_this_year;
			b.taxes_this_year = 0;
			b.fees_this_year = 0;
			b.spent_this_year = 0;
			ReviewTownBudget(t);
		}
	}
	InvalidateWindowClassesData(WindowClass::TownBudget);
});

/**
 * Propose an ordinance to a town.
 * @param flags Type of operation.
 * @param town_id The town.
 * @param ordinance The ordinance.
 * @return The cost of this operation or an error.
 */
CommandCost CmdProposeOrdinance(DoCommandFlags flags, TownID town_id, Ordinance ordinance)
{
	Town *t = Town::GetIfValid(town_id);
	if (t == nullptr || ordinance >= Ordinance::End || !Company::IsValidID(_current_company)) return CMD_ERROR;
	const StringID refusal = GetOrdinanceRefusal(t, ordinance, _current_company);
	if (refusal != INVALID_STRING_ID) return CommandCost(refusal);
	if (flags.Test(DoCommandFlag::Execute)) StartOrdinance(t, ordinance, _current_company);
	return CommandCost(ExpensesType::Other, GetCongestionChargeProposalCost());
}

/**
 * Bring in or repeal an ordinance in a town; for game scripts.
 * @param flags Operation to perform.
 * @param town_id The town.
 * @param ordinance The ordinance.
 * @param in_force Whether it is in force.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetTownOrdinance(DoCommandFlags flags, TownID town_id, Ordinance ordinance, bool in_force)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	Town *t = Town::GetIfValid(town_id);
	if (t == nullptr || ordinance >= Ordinance::End || !AreTownBudgetsOn()) return CMD_ERROR;
	if (flags.Test(DoCommandFlag::Execute) && HasOrdinance(t, ordinance) != in_force) {
		if (in_force) {
			StartOrdinance(t, ordinance, CompanyID::Invalid());
		} else {
			EndOrdinance(t, ordinance, false);
		}
	}
	return CommandCost();
}

/** Forget the count of low-emission zones; used when starting or loading a game. */
void InitializeTownBudgets()
{
	_lez_towns = 0;
}
