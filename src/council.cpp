/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file council.cpp Town councils.
 *
 * Every town has a council, as Malaysia's local authorities: a district
 * council (majlis daerah) at first, a municipal council (majlis perbandaran)
 * from 4,000 people with the income to match, a city council (majlis
 * bandaraya) from 12,000 after three years as a municipality. Councils run
 * on the town's budget (town_budget.cpp): its taxes are their assessment tax.
 * Beside them they collect business licences from shops and offices.
 *
 * Councils build city services from a services fund (a tenth of the taxes,
 * out of the town's running costs) and from what the town saves above half a
 * year's taxes: clinics, schools, police and fire stations, parks and hawker
 * centres where the town lacks them; a hospital and a stadium once a
 * municipality, a university once a city, when it can afford one within
 * three years. They build on free land where the
 * new service reaches the most people not yet reached, and run them out of
 * the town's running costs. Companies can co-fund the next one: the council
 * builds it at once and thinks better of them. Companies still build
 * services of their own too.
 *
 * Councils also run the town's estates of flats, as Singapore's town councils
 * run HDB estates: the flats pay service and conservancy charges into a
 * sinking fund, which pays for cleaning, lifts and upkeep (more as the
 * estates age) and repaints the blocks every six years, in new colours. Old
 * estates (20 years on average) are renewed every fifteen years, the
 * government paying three quarters; municipalities and cities rebuild their
 * oldest blocks (35 years) as new ones. Estates in good condition make their
 * residents happier, run-down ones less so.
 *
 * Each January a council reports on its services, estates and finances,
 * green, amber or red, as Singapore's town council management reports do.
 */

#include "stdafx.h"
#include "council.h"
#include "state.h"
#include "council_cmd.h"
#include "city_services.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "core/random_func.hpp"
#include "debug.h"
#include "economy_func.h"
#include "house.h"
#include "map_func.h"
#include "news_func.h"
#include "object_base.h"
#include "object_cmd.h"
#include "object_map.h"
#include "clear_map.h"
#include "settings_type.h"
#include "strings_func.h"
#include "tile_map.h"
#include "town.h"
#include "town_budget.h"
#include "town_map.h"
#include "town_zoning.h"
#include "transit_authority.h"
#include "window_func.h"
#include "timer/timer_game_calendar.h"
#include "timer/timer_game_economy.h"

#include "table/strings.h"

#include "safeguards.h"

/** Income a town needs to become a municipality or a city, in residents' worth of taxes at the normal rate. */
static constexpr uint MUNICIPAL_INCOME_RESIDENTS = 3500;
static constexpr uint CITY_INCOME_RESIDENTS = 10000;
/** Years a municipality must have been one before it becomes a city. */
static constexpr int CITY_AFTER_YEARS = 3;
/** Share of the town's taxes that goes into the services fund. */
static constexpr uint SERVICES_PERCENT = COUNCIL_SERVICES_PERCENT;
/** A council plans a bigger service only when it can pay for it within this many years. */
static constexpr int PLAN_YEARS = 3;
/** Business licences a year, in percent of the taxes at the normal rate, for a town of businesses only. */
static constexpr uint LICENCE_PERCENT = 30;
/** Service and conservancy charges a year for each resident of flats, in percent of a resident's taxes at the normal rate. */
static constexpr uint CHARGES_PERCENT = 20;
/** Routine upkeep of estates a year for each resident of flats, the same way; a half point more for each year of their age above 20. */
static constexpr uint UPKEEP_PERCENT = 16;
/** Repainting the estates, for each resident of flats, the same way; every so many years. */
static constexpr uint REPAINT_PERCENT = 25;
static constexpr int REPAINT_YEARS = 6;
/** Renewing old estates, for each resident of flats, the same way: the council pays a quarter, the government the rest. */
static constexpr uint RENEWAL_PERCENT = 60;
static constexpr uint RENEWAL_AGE = 20;
static constexpr int RENEWAL_YEARS = 15;
/** Renewal's effects last this many years. */
static constexpr int RENEWAL_EFFECT_YEARS = 10;
/** Rating a company gains in a town by co-funding its council's next service, up to "Very good". */
static constexpr int RATING_COFUND_UP = 200;

/**
 * Do towns have councils (setting economy.town_councils)? They need town budgets.
 * @return True if they do.
 */
bool AreTownCouncilsOn()
{
	return _settings_game.economy.town_councils && AreTownBudgetsOn();
}

/**
 * What kind of council runs a town?
 * @param t The town.
 * @return The kind; a district council when councils are off.
 */
CouncilLevel GetCouncilLevel(const Town *t)
{
	return AreTownCouncilsOn() ? t->council.level : CouncilLevel::District;
}

/**
 * Are councils called by their names in English (Funnford Municipal Council)
 * rather than in Malay (Majlis Perbandaran Funnford)? The player's choice
 * (setting gui.council_names).
 * @return True for English.
 */
bool AreCouncilNamesEnglish()
{
	return _settings_client.gui.council_names != 0;
}

/**
 * A council's name, which takes the town as its parameter: "Majlis
 * Perbandaran Funnford", or "Funnford Municipal Council" in English.
 * @param level The kind of council.
 * @return The string.
 */
StringID GetCouncilName(CouncilLevel level)
{
	static constexpr StringID MALAY[] = {STR_COUNCIL_NAME_DISTRICT, STR_COUNCIL_NAME_MUNICIPAL, STR_COUNCIL_NAME_CITY};
	static constexpr StringID ENGLISH[] = {STR_COUNCIL_NAME_DISTRICT_ENGLISH, STR_COUNCIL_NAME_MUNICIPAL_ENGLISH, STR_COUNCIL_NAME_CITY_ENGLISH};
	static_assert(std::size(MALAY) == to_underlying(CouncilLevel::End) && std::size(ENGLISH) == to_underlying(CouncilLevel::End));
	return AreCouncilNamesEnglish() ? ENGLISH[to_underlying(level)] : MALAY[to_underlying(level)];
}

/**
 * What a kind of council is called in English: "municipal council".
 * @param level The kind of council.
 * @return The string.
 */
StringID GetCouncilKind(CouncilLevel level)
{
	static constexpr StringID KINDS[] = {STR_COUNCIL_KIND_DISTRICT, STR_COUNCIL_KIND_MUNICIPAL, STR_COUNCIL_KIND_CITY};
	static_assert(std::size(KINDS) == to_underlying(CouncilLevel::End));
	return KINDS[to_underlying(level)];
}

/**
 * Is a house type flats of an estate: walk-up flats, HDB slab and point
 * blocks, OpenTTD's blocks of flats? Condominiums run themselves.
 * @param hs The house type.
 * @return True if it is.
 */
bool IsEstateFlat(const HouseSpec &hs)
{
	StringID name = hs.building_name;
	if (hs.grf_prop.HasGrfFile()) {
		if (hs.grf_prop.subst_id >= NEW_HOUSE_OFFSET) return false;
		name = HouseSpec::Get(hs.grf_prop.subst_id)->building_name;
	}
	static const StringID flats[] = {
		STR_TOWN_BUILDING_NAME_WALK_UP_FLATS, STR_TOWN_BUILDING_NAME_HDB_BLOCK, STR_TOWN_BUILDING_NAME_HDB_POINT_BLOCK,
		STR_TOWN_BUILDING_NAME_SMALL_BLOCK_OF_FLATS_1, STR_TOWN_BUILDING_NAME_FLATS_1,
	};
	return std::ranges::find(flats, name) != std::end(flats);
}

/**
 * How often a town's estates were repainted: shifts the colours of their blocks.
 * @param t The town.
 * @return The count; 0 when councils are off.
 */
uint8_t GetEstatePaint(const Town *t)
{
	return AreTownCouncilsOn() ? t->council.paint : 0;
}

/**
 * Taxes at the normal rate for so many residents on land worth 50, times a percentage.
 * @param residents_percent Residents times the percentage.
 * @return The money a year.
 */
static Money TaxWorth(uint64_t residents_percent)
{
	return GetTransitBusYearCost() * static_cast<int64_t>(residents_percent) * 48 / 1000000;
}

/** Count what each town has: at the start of each month, so it is the same for every player of a network game. */
static void CountTownBuildings()
{
	for (Town *t : Town::Iterate()) t->council.counts = {};
	for (const auto tile : Map::Iterate()) {
		if (!IsTileType(tile, TileType::House)) continue;
		const HouseSpec *hs = HouseSpec::Get(GetHouseType(tile));
		if (hs->population == 0) continue;
		CouncilCounts &c = Town::GetByTile(tile)->council.counts;
		c.people += hs->population;
		const HouseKind kind = GetHouseKind(*hs);
		if (kind == HouseKind::Business) c.business += hs->population;
		if (IsEstateFlat(*hs)) {
			c.flats += hs->population;
			c.flats_age += static_cast<uint64_t>(GetHouseAge(tile).base()) * hs->population;
		}
	}
	for (const Object *o : Object::Iterate()) {
		if (!IsCityServiceObjectType(o->type) || o->town == nullptr) continue;
		CouncilCounts &c = o->town->council.counts;
		switch (GetCityServiceOfObjectType(o->type)) {
			case CityService::Hospital: c.hospitals++; break;
			case CityService::University: c.universities++; break;
			case CityService::Stadium: c.stadiums++; break;
			default: break;
		}
	}
}

/**
 * What a town has, as counted at the start of the month.
 * @param t The town.
 * @return The counts.
 */
static const CouncilCounts &GetCounts(const Town *t)
{
	return t->council.counts;
}

/**
 * People in a town's flats.
 * @param t The town.
 * @return The people.
 */
uint32_t GetTownFlatsPopulation(const Town *t)
{
	return GetCounts(t).flats;
}

/**
 * Average age of a town's flats, by their people.
 * @param t The town.
 * @return The age in years; 0 without flats.
 */
uint32_t GetTownFlatsAge(const Town *t)
{
	const CouncilCounts &c = GetCounts(t);
	return c.flats == 0 ? 0 : static_cast<uint32_t>(c.flats_age / c.flats);
}

/**
 * Business licences a town collects a year: 30% of its taxes at the normal
 * rate, in the share of its people who work in shops and offices.
 * @param t The town.
 * @return The licences.
 */
Money GetTownYearlyLicences(const Town *t)
{
	if (!AreTownCouncilsOn()) return 0;
	const CouncilCounts &c = GetCounts(t);
	if (c.people == 0) return 0;
	return GetTownTaxBase(t) * LICENCE_PERCENT / 100 * c.business / c.people;
}

/**
 * Service and conservancy charges a town's flats pay a year.
 * @param t The town.
 * @return The charges.
 */
Money GetTownYearlyCharges(const Town *t)
{
	if (!AreTownCouncilsOn()) return 0;
	return TaxWorth(static_cast<uint64_t>(GetCounts(t).flats) * CHARGES_PERCENT);
}

/**
 * Yearly income (taxes and licences) a town needs to become a municipality or a city.
 * @param level The kind of council.
 * @return The income, or 0 for a district council.
 */
Money GetCouncilIncomeNeeded(CouncilLevel level)
{
	switch (level) {
		case CouncilLevel::Municipal: return TaxWorth(MUNICIPAL_INCOME_RESIDENTS * 100);
		case CouncilLevel::City: return TaxWorth(CITY_INCOME_RESIDENTS * 100);
		default: return 0;
	}
}

/**
 * What the council added to or took from the town's reserves last year beside taxes, fees and running costs: licences in, what it drew for new services and estates out.
 * @param t The town.
 * @return The money.
 */
Money GetCouncilSurplusLastYear(const Town *t)
{
	if (!AreTownCouncilsOn()) return 0;
	return t->council.licences_last_year - t->council.drawn_last_year;
}

/**
 * What a council can spend on a new service now: its services fund, what companies gave it, and the town's reserves above half a year's taxes.
 * @param t The town.
 * @return The money.
 */
Money GetCouncilSpendable(const Town *t)
{
	return t->council.services_fund + t->council.cofunded + std::max<Money>(0, t->budget.reserves - GetTownTaxBase(t) / 2);
}

/**
 * What a council puts into its services fund in a year, and gets in licences.
 * @param t The town.
 * @return The money.
 */
static Money GetCouncilServicesIncome(const Town *t)
{
	return GetTownTaxBase(t) * SERVICES_PERCENT / 100 + GetTownYearlyLicences(t);
}

/**
 * May a council build a kind of city service? The bigger ones need a municipality: a hospital or a stadium; a university needs a city.
 * @param t The town.
 * @param service The service.
 * @return True if it may.
 */
bool IsCouncilServiceAvailable(const Town *t, CityService service)
{
	switch (service) {
		case CityService::Hospital:
		case CityService::Stadium:
			return GetCouncilLevel(t) >= CouncilLevel::Municipal;
		case CityService::University:
			return GetCouncilLevel(t) >= CouncilLevel::City;
		default:
			return true;
	}
}

/**
 * What a council would build next: the service the town lacks most, a hospital
 * instead of a clinic in a municipality of 6,000 people without one, a
 * university instead of a school in a city of 12,000 without one. A
 * municipality of 8,000 also wants a hospital and a stadium of its own, a
 * city a university, however well clinics and schools serve it; the bigger
 * ones only when it can pay for them within three years. A council with more
 * than two years' taxes put by aims higher: 15 points more of the town reached.
 * @param t The town.
 * @return The service, or CityService::End for none.
 */
CityService GetCouncilPlannedService(const Town *t)
{
	if (!AreTownCouncilsOn() || t->cache.population < 500) return CityService::End;
	const auto &p = t->service_coverage.percent;
	const CouncilCounts &c = GetCounts(t);
	const uint population = t->cache.population;
	CityService best = CityService::End;
	int best_score = 0;
	const int ambition = t->budget.reserves > 2 * GetTownTaxBase(t) ? 15 : 0;
	auto consider = [&](CityService service, CityService covered, int target, int weight) {
		const int gap = target + ambition - static_cast<int>(p[to_underlying(covered)]);
		if (gap <= 0 || !IsCouncilServiceAvailable(t, service)) return;
		if (gap * weight > best_score) {
			best_score = gap * weight;
			best = service;
		}
	};
	const Money horizon = GetCouncilSpendable(t) + PLAN_YEARS * GetCouncilServicesIncome(t);
	auto affordable = [horizon](CityService service) { return GetCityServiceBuildCost(service) <= horizon; };
	const bool hospital = population >= 6000 && c.hospitals == 0 && IsCouncilServiceAvailable(t, CityService::Hospital) && affordable(CityService::Hospital);
	const bool university = population >= 12000 && c.universities == 0 && IsCouncilServiceAvailable(t, CityService::University) && affordable(CityService::University);
	consider(hospital ? CityService::Hospital : CityService::Health, CityService::Health, 80, 12);
	consider(university ? CityService::University : CityService::Education, CityService::Education, 80, 12);
	consider(CityService::Police, CityService::Police, 70, 10);
	consider(CityService::Fire, CityService::Fire, 70, 10);
	consider(CityService::Park, CityService::Park, 60, 8);
	consider(CityService::Market, CityService::Market, 50, 8);
	/* The bigger towns want one of each bigger service of their own. */
	auto landmark = [&](CityService service, uint have, uint from, int weight) {
		if (population < from || have > 0 || !IsCouncilServiceAvailable(t, service) || !affordable(service)) return;
		if (30 * weight > best_score) {
			best_score = 30 * weight;
			best = service;
		}
	};
	landmark(CityService::Hospital, c.hospitals, 8000, 12);
	landmark(CityService::University, c.universities, COUNCIL_CITY_POPULATION, 12);
	landmark(CityService::Stadium, c.stadiums, 8000, 5);
	return best;
}

/**
 * Where would a council build a new service? On free land (grass, rough land
 * or trees, not zoned for buildings or industry) nearer to this town than to
 * any other, where the service reaches the most of its people not reached by
 * that kind of service yet (at least 100), or for the two by two ones the most
 * of its people (at least 600).
 * @param t The town.
 * @param service The service.
 * @return The (northern) tile, or INVALID_TILE when there is no such place.
 */
static TileIndex FindCouncilServiceSite(const Town *t, CityService service)
{
	const uint town_radius = IntSqrt(t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)]);
	if (town_radius == 0) return INVALID_TILE;
	const uint size = GetCityServiceSize(service);
	const uint reach = GetCityServiceRadius(service);
	const ObjectType type = GetObjectTypeOfCityService(service);
	CityService covers = service;
	if (service == CityService::Hospital) covers = CityService::Health;
	if (service == CityService::University) covers = CityService::Education;

	AutoRestoreBackup cur_company(_current_company, OWNER_TOWN);
	TileIndex best = INVALID_TILE;
	uint best_score = size == 2 ? 599 : 99;
	for (uint attempt = 0; attempt < 40; attempt++) {
		const int dx = static_cast<int>(RandomRange(2 * town_radius + 1)) - static_cast<int>(town_radius);
		const int dy = static_cast<int>(RandomRange(2 * town_radius + 1)) - static_cast<int>(town_radius);
		const TileIndex tile = TileAddWrap(t->xy, dx, dy);
		if (tile == INVALID_TILE || TileX(tile) + size >= Map::MaxX() || TileY(tile) + size >= Map::MaxY()) continue;
		bool free = true;
		for (TileIndex ti : TileArea(tile, size, size)) {
			if (IsTileType(ti, TileType::Clear)) {
				free = !IsClearGround(ti, ClearGround::Fields);
			} else {
				free = IsTileType(ti, TileType::Trees);
			}
			const TownZone zone = GetTownZone(ti);
			if (zone != TownZone::None && zone != TownZone::Park) free = false;
			if (!free) break;
		}
		if (!free || CalcClosestTownFromTile(tile) != t) continue;

		TileArea area(tile, size, size);
		area.Expand(reach);
		uint score = 0;
		for (TileIndex ti : area) {
			if (!IsTileType(ti, TileType::House) || GetTownIndex(ti) != t->index) continue;
			if (size == 1 && HasBit(GetCityServicesCoveringTile(ti), to_underlying(covers))) continue;
			score += HouseSpec::Get(GetHouseType(ti))->population;
		}
		if (score <= best_score) continue;
		if (Command<Commands::BuildObject>::Do({}, tile, type, 0).Failed()) continue;
		best = tile;
		best_score = score;
	}
	return best;
}

/**
 * A council builds a new city service, if it finds a place for it, and pays for it: from what companies gave first, then from its reserves.
 * @param t The town.
 * @param service The service.
 * @param cofunder The company that co-funded it, or CompanyID::Invalid().
 * @return True if it built it.
 */
static bool BuildCouncilService(Town *t, CityService service, CompanyID cofunder)
{
	const TileIndex tile = FindCouncilServiceSite(t, service);
	if (tile == INVALID_TILE) return false;
	CommandCost cost;
	{
		AutoRestoreBackup cur_company(_current_company, OWNER_TOWN);
		cost = Command<Commands::BuildObject>::Do(DoCommandFlag::Execute, tile, GetObjectTypeOfCityService(service), 0);
	}
	if (cost.Failed()) return false;
	TownCouncil &c = t->council;
	const Money price = cost.GetCost();
	Money left = price;
	const Money from_cofund = std::min(left, c.cofunded);
	c.cofunded -= from_cofund;
	left -= from_cofund;
	const Money from_fund = std::min(left, std::max<Money>(0, c.services_fund));
	c.services_fund -= from_fund;
	left -= from_fund;
	t->budget.reserves -= left;
	c.drawn_this_year += left;
	c.built_this_year += price;
	c.services_built++;
	Debug(Facility::Misc, Severity::Trace1, "[council] town {} builds service {} at {} {} for {}{}", t->index, to_underlying(service), TileX(tile), TileY(tile), price,
			Company::IsValidID(cofunder) ? " (co-funded)" : "");
	if (service == CityService::Hospital) c.counts.hospitals++;
	if (service == CityService::University) c.counts.universities++;
	if (service == CityService::Stadium) c.counts.stadiums++;
	if (Company::IsValidID(cofunder)) {
		AddNewsItem(GetEncodedString(STR_NEWS_COUNCIL_SERVICE_COFUNDED, GetTownCouncilName(t), t->index, GetCityServiceShortName(service), cofunder),
				NewsType::General, NewsStyle::Normal, {}, tile);
	} else {
		AddNewsItem(GetEncodedString(STR_NEWS_COUNCIL_SERVICE, GetTownCouncilName(t), t->index, GetCityServiceShortName(service)),
				NewsType::General, NewsStyle::Normal, {}, tile);
	}
	SetWindowDirty(WindowClass::TownBudget, t->index);
	return true;
}

/**
 * Why a council wouldn't take a company's money towards its next service now.
 * @param t The town.
 * @param company The company.
 * @return The reason, or INVALID_STRING_ID if it would.
 */
StringID GetCofundRefusal(const Town *t, CompanyID company)
{
	if (!AreTownCouncilsOn()) return STR_ERROR_COUNCILS_OFF;
	if (!Company::IsValidID(company)) return STR_ERROR_COUNCILS_OFF;
	if (GetCouncilPlannedService(t) == CityService::End) return STR_ERROR_COUNCIL_NO_SERVICE_NEEDED;
	return INVALID_STRING_ID;
}

/**
 * What a company pays to co-fund a council's next service: what the council lacks for it, and at least a quarter of its cost.
 * @param t The town.
 * @return The money; 0 if it plans none.
 */
Money GetCofundCost(const Town *t)
{
	const CityService service = GetCouncilPlannedService(t);
	if (service == CityService::End) return 0;
	const Money cost = GetCityServiceBuildCost(service);
	return std::max(cost - GetCouncilSpendable(t), cost / 4);
}

/**
 * Co-fund a town council's next city service: the company pays what the
 * council lacks for it (at least a quarter of its cost), the council builds
 * it at once and thinks better of the company.
 * @param flags Type of operation.
 * @param town_id The town.
 * @return The cost of this operation or an error.
 */
CommandCost CmdCofundCouncilService(DoCommandFlags flags, TownID town_id)
{
	Town *t = Town::GetIfValid(town_id);
	if (t == nullptr || !Company::IsValidID(_current_company)) return CMD_ERROR;
	const StringID refusal = GetCofundRefusal(t, _current_company);
	if (refusal != INVALID_STRING_ID) return CommandCost(refusal);
	const CityService service = GetCouncilPlannedService(t);
	const Money money = GetCofundCost(t);
	if (FindCouncilServiceSite(t, service) == INVALID_TILE) return CommandCost(STR_ERROR_COUNCIL_NO_SITE);
	if (flags.Test(DoCommandFlag::Execute)) {
		t->council.cofunded += money;
		ChangeTownRating(t, RATING_COFUND_UP, RATING_VERYGOOD, flags);
		BuildCouncilService(t, service, _current_company);
		SetWindowDirty(WindowClass::TownBudget, t->index);
		SetWindowDirty(WindowClass::TownAuthority, t->index);
	}
	return CommandCost(ExpensesType::Other, money);
}

/**
 * Pay for something of the estates: from their fund, and what it lacks from the town's reserves while they last.
 * @param t The town.
 * @param cost The cost.
 * @param use_reserves Whether the council tops up from its reserves; else only what is in the fund is paid.
 * @return What was paid.
 */
static Money PayForEstates(Town *t, Money cost, bool use_reserves)
{
	TownCouncil &c = t->council;
	Money paid = std::min(cost, std::max<Money>(0, c.estates_fund));
	c.estates_fund -= paid;
	if (paid < cost && use_reserves && t->budget.reserves > 0) {
		const Money top_up = std::min(cost - paid, t->budget.reserves);
		t->budget.reserves -= top_up;
		c.drawn_this_year += top_up;
		paid += top_up;
	}
	c.estates_this_year += paid;
	return paid;
}

/**
 * A month of the estates: routine upkeep from the charges, more as they age;
 * their condition heads for 85% when it is paid in full, down to 40% when
 * nothing is.
 * @param t The town.
 */
static void UpdateEstates(Town *t)
{
	TownCouncil &c = t->council;
	const CouncilCounts &n = GetCounts(t);
	if (n.flats == 0) return;
	const uint age = static_cast<uint>(n.flats_age / n.flats);
	const Money upkeep = TaxWorth(static_cast<uint64_t>(n.flats) * (UPKEEP_PERCENT * 2 + (age > 20 ? age - 20 : 0)) / 2) / 12;
	const Money paid = PayForEstates(t, upkeep, true);
	const int funded = upkeep > 0 ? static_cast<int>(paid * 100 / upkeep) : 100;
	/* Under Homes and Services the state's estates programme keeps them better still. */
	const int target = 40 + 45 * funded / 100 + (GetTownGovernment(t) == Platform::HomesAndServices ? 10 : 0);
	const int condition = c.estate_condition;
	int step = (target - condition) / 12;
	if (step == 0 && target != condition) step = target > condition ? 1 : -1;
	c.estate_condition = static_cast<uint8_t>(Clamp(condition + step, 0, 100));
}

/**
 * Has a town the money and the standing for a better council? A municipality
 * from 4,000 people with an income (taxes and licences) of 3,500 residents'
 * taxes at the normal rate; a city from 12,000 with 10,000 residents' worth,
 * after three years as a municipality.
 * @param t The town.
 * @param start Whether the council is only being set up: no waiting, last year's income not known.
 * @return The level it qualifies for.
 */
static CouncilLevel GetQualifyingLevel(const Town *t, bool start)
{
	const TownCouncil &c = t->council;
	const Money income = start ? GetTownYearlyTaxes(t) + GetTownYearlyLicences(t) : t->budget.taxes_last_year + c.licences_last_year;
	const uint population = t->cache.population;
	CouncilLevel level = CouncilLevel::District;
	if (population >= COUNCIL_MUNICIPAL_POPULATION && income >= GetCouncilIncomeNeeded(CouncilLevel::Municipal)) level = CouncilLevel::Municipal;
	if (level == CouncilLevel::Municipal && population >= COUNCIL_CITY_POPULATION && income >= GetCouncilIncomeNeeded(CouncilLevel::City)) {
		const bool waited = start || (c.level >= CouncilLevel::Municipal && TimerGameEconomy::year.base() - c.since >= CITY_AFTER_YEARS);
		if (waited) level = CouncilLevel::City;
	}
	return level;
}

/**
 * Set up a town's council: the standing its size and money give it now, estates in fair condition.
 * @param t The town.
 */
static void StartCouncil(Town *t)
{
	TownCouncil &c = t->council;
	const CouncilCounts counts = c.counts;
	c = {};
	c.counts = counts;
	c.started = true;
	c.level = GetQualifyingLevel(t, true);
	c.since = 0;
	c.estate_condition = 80;
	c.repainted = TimerGameEconomy::year.base() - static_cast<int>(t->index.base() % REPAINT_YEARS);
}

/** Each month: business licences into the town's reserves, a tenth of its taxes into the services fund (out of what the town spends on running itself), charges into the estates' fund, the estates' upkeep, and maybe a new service. */
void UpdateTownCouncilsMonthly()
{
	if (!AreTownCouncilsOn()) return;
	CountTownBuildings();
	for (Town *t : Town::Iterate()) {
		TownCouncil &c = t->council;
		if (!c.started) StartCouncil(t);
		const Money licences = GetTownYearlyLicences(t) / 12;
		c.licences_this_year += licences;
		t->budget.reserves += licences;
		c.services_fund += GetTownTaxBase(t) * SERVICES_PERCENT / 100 / 12;
		const Money charges = GetTownYearlyCharges(t) / 12;
		c.charges_this_year += charges;
		c.estates_fund += charges;
		UpdateEstates(t);

		const CityService plan = GetCouncilPlannedService(t);
		if (plan != CityService::End && GetCouncilSpendable(t) >= GetCityServiceBuildCost(plan)) BuildCouncilService(t, plan, CompanyID::Invalid());
	}
	InvalidateWindowClassesData(WindowClass::TownBudget);
}

/**
 * The year ends: this year's council figures become last year's. Before the town's budget review, which counts them.
 * @param t The town.
 */
void RotateTownCouncilYear(Town *t)
{
	TownCouncil &c = t->council;
	c.licences_last_year = c.licences_this_year;
	c.charges_last_year = c.charges_this_year;
	c.estates_last_year = c.estates_this_year;
	c.built_last_year = c.built_this_year;
	c.drawn_last_year = c.drawn_this_year;
	c.drawn_this_year = 0;
	c.licences_this_year = 0;
	c.charges_this_year = 0;
	c.estates_this_year = 0;
	c.built_this_year = 0;
}

/**
 * How happy a town's estates make it: up to 3 points either way by their
 * condition (70% is neutral), and 2 for ten years after a renewal, in the
 * share of the town's people who live in flats.
 * @param t The town.
 * @return The points.
 */
int GetCouncilHappiness(const Town *t)
{
	if (!AreTownCouncilsOn() || !t->council.started) return 0;
	const CouncilCounts &n = GetCounts(t);
	if (n.flats == 0 || n.people == 0) return 0;
	int points = (static_cast<int>(t->council.estate_condition) - 70) / 10;
	if (t->council.renewed != 0 && TimerGameEconomy::year.base() - t->council.renewed < RENEWAL_EFFECT_YEARS) points += 2;
	points = Clamp(points, -3, 5);
	return points * static_cast<int>(n.flats) / static_cast<int>(n.people);
}

/**
 * How a town's estates change the value of its land: +5 for ten years after a renewal, -5 while they are run down (below 50%).
 * @param t The town.
 * @return The points.
 */
int GetCouncilLandValueBonus(const Town *t)
{
	if (!AreTownCouncilsOn() || !t->council.started || GetCounts(t).flats == 0) return 0;
	int bonus = 0;
	if (t->council.renewed != 0 && TimerGameEconomy::year.base() - t->council.renewed < RENEWAL_EFFECT_YEARS) bonus += 5;
	if (t->council.estate_condition < 50) bonus -= 5;
	return bonus;
}

/**
 * The estates' programmes, once a year: repainting every six years (in new
 * colours), renewal of estates 30 years old every fifteen years (the
 * government pays three quarters), and in municipalities and cities a block
 * 40 years old rebuilt as a new one.
 * @param t The town.
 */
static void ReviewEstates(Town *t)
{
	TownCouncil &c = t->council;
	const CouncilCounts &n = GetCounts(t);
	if (n.flats == 0) return;
	const int year = TimerGameEconomy::year.base();
	const uint age = static_cast<uint>(n.flats_age / n.flats);
	const Money base_reserve = GetTownTaxBase(t) / 4;
	Debug(Facility::Misc, Severity::Trace1, "[council] town {} estates: {} people in flats, {} years old, condition {}, fund {}, repainted {}, renewed {}",
			t->index, n.flats, age, c.estate_condition, c.estates_fund, c.repainted, c.renewed);

	if (year - c.repainted >= REPAINT_YEARS) {
		const Money cost = TaxWorth(static_cast<uint64_t>(n.flats) * REPAINT_PERCENT);
		if (c.estates_fund + std::max<Money>(0, t->budget.reserves - base_reserve) >= cost) {
			PayForEstates(t, cost, true);
			c.estate_condition = 100;
			c.paint++;
			c.repainted = year;
			if (n.flats >= 500) AddNewsItem(GetEncodedString(STR_NEWS_COUNCIL_REPAINT, GetTownCouncilName(t), t->index), NewsType::General, NewsStyle::Small, {}, t->index);
			MarkWholeScreenDirty();
		}
	}

	if (age >= RENEWAL_AGE && (c.renewed == 0 || year - c.renewed >= RENEWAL_YEARS)) {
		const Money share = TaxWorth(static_cast<uint64_t>(n.flats) * RENEWAL_PERCENT) / 4;
		if (c.estates_fund + std::max<Money>(0, t->budget.reserves - base_reserve) >= share) {
			PayForEstates(t, share, true);
			c.estate_condition = 100;
			c.renewed = year;
			Debug(Facility::Misc, Severity::Trace1, "[council] town {} renews its estates", t->index);
			if (n.flats >= 500) AddNewsItem(GetEncodedString(STR_NEWS_COUNCIL_RENEWAL, t->index, age), NewsType::General, NewsStyle::Normal, {}, t->index);
		}
	}

	if (c.level >= CouncilLevel::Municipal) RebuildOldEstateBlock(t);
}

/**
 * The council's yearly report: its services by how much of the town they
 * reach (the ones it may build; 75% green, 45% amber), its estates by their
 * condition (75% green, 55% amber), its finances by its reserves and taxes
 * (in credit with taxes at most 10% over normal green, in credit amber, in
 * debt red).
 * @param t The town.
 */
static void MakeCouncilReport(Town *t)
{
	TownCouncil &c = t->council;
	const auto &p = t->service_coverage.percent;
	uint sum = 0, count = 0;
	for (CityService service : {CityService::Police, CityService::Fire, CityService::Health, CityService::Education, CityService::Park, CityService::Market, CityService::Stadium}) {
		if (!IsCouncilServiceAvailable(t, service)) continue;
		if (service == CityService::Stadium && t->cache.population < 8000) continue;
		sum += p[to_underlying(service)];
		count++;
	}
	const uint services = count == 0 ? 0 : sum / count;
	c.report[to_underlying(ReportItem::Services)] = services >= 75 ? ReportBand::Green : services >= 45 ? ReportBand::Amber : ReportBand::Red;
	if (GetCounts(t).flats == 0) {
		c.report[to_underlying(ReportItem::Estates)] = ReportBand::None;
	} else {
		c.report[to_underlying(ReportItem::Estates)] = c.estate_condition >= 75 ? ReportBand::Green : c.estate_condition >= 55 ? ReportBand::Amber : ReportBand::Red;
	}
	ReportBand finances = ReportBand::Red;
	if (t->budget.reserves >= 0) finances = t->budget.tax_rate <= 110 ? ReportBand::Green : ReportBand::Amber;
	c.report[to_underlying(ReportItem::Finances)] = finances;
}

/**
 * January, after the town's budget review: a better council if the town qualifies, the estates' programmes, the yearly report.
 * @param t The town.
 */
void ReviewTownCouncil(Town *t)
{
	if (!AreTownCouncilsOn()) return;
	TownCouncil &c = t->council;
	if (!c.started) StartCouncil(t);
	const CouncilLevel level = GetQualifyingLevel(t, false);
	if (level > c.level) {
		c.level = level;
		c.since = TimerGameEconomy::year.base();
		Debug(Facility::Misc, Severity::Trace1, "[council] town {} becomes council level {} with {} people", t->index, to_underlying(level), t->cache.population);
		const StringID news = level == CouncilLevel::City ? STR_NEWS_COUNCIL_CITY : STR_NEWS_COUNCIL_MUNICIPAL;
		AddNewsItem(GetEncodedString(news, t->index, GetCouncilName(level), t->index), NewsType::General, NewsStyle::Normal, {}, t->index);
		SetWindowDirty(WindowClass::TownView, t->index);
	}
	/* A council that needs nothing new gives what it saved beyond two years' worth back to the town, which may then cut its taxes. */
	const Money fund_cap = GetTownTaxBase(t) * SERVICES_PERCENT * 2 / 100;
	if (GetCouncilPlannedService(t) == CityService::End && c.services_fund > fund_cap) {
		t->budget.reserves += c.services_fund - fund_cap;
		c.services_fund = fund_cap;
	}
	ReviewEstates(t);
	MakeCouncilReport(t);
	SetWindowDirty(WindowClass::TownBudget, t->index);
}
