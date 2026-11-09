/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file federal.cpp The federal government, besides running the federal territories and paying the states their grants.
 *
 * - National programmes: the platform the general election put in power works across the whole country, on top of
 *   each state's own. Transit First pays a national transit fund into every town's transit budget and half of the
 *   monthly passes riders buy (as Malaysia's My50 pass); Roads and Growth subsidises fuel for road vehicles that burn
 *   it and makes towns grow faster; Homes and Services builds a hospital or a university each year in a big town
 *   that lacks one.
 * - A five-year plan (as the Rancangan Malaysia): when the results of a general election are declared, the
 *   government offers projects that match its platform, metro and BRT lines, expressways, hospitals and
 *   universities. The first company to deliver one is paid; what isn't delivered lapses when the next plan begins.
 *   Delivered projects count for the government at the next general election.
 * - Federal transit agencies (as Prasarana in Kuala Lumpur): a federal territory's transit budget gets a federal
 *   grant on top, and it tenders railway and BRT lines more often than towns do (see transit_authority.cpp).
 */

#include "stdafx.h"
#include "federal.h"
#include "brt.h"
#include "city_bus.h"
#include "city_services.h"
#include "city_services_type.h"
#include "company_base.h"
#include "company_func.h"
#include "council.h"
#include "debug.h"
#include "economy_func.h"
#include "map_func.h"
#include "metro.h"
#include "news_func.h"
#include "object_base.h"
#include "order_base.h"
#include "road.h"
#include "road_pricing.h"
#include "roadveh.h"
#include "state.h"
#include "station_base.h"
#include "strings_func.h"
#include "tile_map.h"
#include "town.h"
#include "town_budget.h"
#include "transit_authority.h"
#include "window_func.h"
#include "timer/timer_game_economy.h"

#include "table/strings.h"

#include "safeguards.h"

extern Federation _federation;
std::vector<FederalProject> _federal_projects; ///< The projects of the federal government's five-year plan and of the plan before it (saved in chunk SFPR).

/** Towns this big or bigger may get a metro line in the plan. */
static constexpr uint METRO_TOWN_POPULATION = 8000;
/** Towns this big or bigger may get a BRT line in the plan. */
static constexpr uint BRT_TOWN_POPULATION = 2500;
/** Towns this big or bigger may get a hospital in the plan. */
static constexpr uint HOSPITAL_TOWN_POPULATION = 5000;
/** Towns this big or bigger may get a university in the plan. */
static constexpr uint UNIVERSITY_TOWN_POPULATION = 10000;
/** Expressways in the plan join capitals at least this far apart, in tiles... */
static constexpr uint EXPRESSWAY_MIN_DISTANCE = 24;
/** ... and at most this far. */
static constexpr uint EXPRESSWAY_MAX_DISTANCE = 200;
/** A plan has at most this many projects. */
static constexpr uint PLAN_MAX_PROJECTS = 4;

/**
 * Who governs the federation.
 * @return The platform in power; None before the first general election or without states.
 */
Platform GetFederalGovernment()
{
	return AreStatesFormed() ? _federation.government : Platform::None;
}

/**
 * The national transit fund: what a Transit First federal government adds to a town's transit budget.
 * Federal territories already have the federal platform, and a transit agency of their own.
 * @param t The town.
 * @return The grant, in percent of the town's taxes.
 */
uint GetFederalTransitGrantPercent(const Town *t)
{
	if (GetFederalGovernment() != Platform::TransitFirst || GetTownState(t) == nullptr || IsFederalTerritory(t)) return 0;
	return FEDERAL_TRANSIT_GRANT_PERCENT;
}

/**
 * Does a town's transit come under a federal transit agency? The federal territories' does.
 * @param t The town.
 * @return True if it does.
 */
bool HasFederalTransitAgency(const Town *t)
{
	return IsFederalTerritory(t);
}

/**
 * What a federal transit agency adds to its territory's transit budget, from federal money.
 * @param t The town.
 * @return The grant, in percent of the town's taxes.
 */
uint GetFederalAgencyBudgetPercent(const Town *t)
{
	return HasFederalTransitAgency(t) ? FEDERAL_AGENCY_BUDGET_PERCENT : 0;
}

/**
 * Does the federal government pay part of the monthly passes? Under Transit First it does, so more riders buy one.
 * @return True if it does.
 */
bool IsFederalPassSubsidyOn()
{
	return GetFederalGovernment() == Platform::TransitFirst;
}

/**
 * The federal government paid its share of monthly passes.
 * @param paid How much.
 */
void OnFederalPassSubsidy(Money paid)
{
	_federation.programmes_this_year += paid;
}

/**
 * What the fuel subsidy leaves of a road vehicle's running costs: under Roads and Growth it takes a share off for
 * road vehicles that burn fuel; trams and electric buses don't.
 * @param v The road vehicle.
 * @return Its running costs, in percent.
 */
uint GetFederalFuelCostPercent(const RoadVehicle *v)
{
	if (GetFederalGovernment() != Platform::RoadsAndGrowth) return 100;
	if (RoadTypeIsTram(v->roadtype) || (v->IsBus() && IsElectricBus(v))) return 100;
	return 100 - FEDERAL_FUEL_SUBSIDY_PERCENT;
}

/**
 * How much faster a town grows under a Roads and Growth federal government, on top of what its state does.
 * @param t The town.
 * @return Less time between new houses, in percent.
 */
uint GetFederalGrowthPercent(const Town *t)
{
	if (GetFederalGovernment() != Platform::RoadsAndGrowth || GetTownState(t) == nullptr || IsFederalTerritory(t)) return 0;
	return FEDERAL_GROWTH_PERCENT;
}

/**
 * What the national programmes of a platform do, in words.
 * @param platform The platform.
 * @return The words.
 */
std::string GetFederalProgrammeText(Platform platform)
{
	switch (platform) {
		case Platform::TransitFirst: return GetString(STR_FEDERAL_PROGRAMME_TRANSIT_FIRST, FEDERAL_TRANSIT_GRANT_PERCENT, FEDERAL_PASS_SUBSIDY_PERCENT);
		case Platform::RoadsAndGrowth: return GetString(STR_FEDERAL_PROGRAMME_ROADS_AND_GROWTH, FEDERAL_FUEL_SUBSIDY_PERCENT, FEDERAL_GROWTH_PERCENT);
		case Platform::HomesAndServices: return GetString(STR_FEDERAL_PROGRAMME_HOMES_AND_SERVICES, FEDERAL_HOSPITAL_POPULATION, FEDERAL_UNIVERSITY_POPULATION);
		default: return GetString(STR_FEDERAL_PROGRAMME_NONE);
	}
}

/* ------------------------------------------------------------------------
 * The five-year plan.
 * ------------------------------------------------------------------------ */

/**
 * The projects of the plan, and of the plan before it.
 * @return The projects.
 */
std::vector<FederalProject> &GetFederalProjects()
{
	return _federal_projects;
}

/**
 * What a project is, in words: "a metro line for Kuala Lumpur".
 * @param p The project.
 * @return The words.
 */
std::string GetFederalProjectName(const FederalProject &p)
{
	if (!Town::IsValidID(p.town) || (p.type == FederalProjectType::Expressway && !Town::IsValidID(p.town2))) return GetString(STR_FEDERAL_PROJECT_GONE);
	switch (p.type) {
		case FederalProjectType::MetroLine: return GetString(STR_FEDERAL_PROJECT_METRO, p.town);
		case FederalProjectType::BrtLine: return GetString(STR_FEDERAL_PROJECT_BRT, p.town);
		case FederalProjectType::Expressway: return GetString(STR_FEDERAL_PROJECT_EXPRESSWAY, p.town, p.town2);
		case FederalProjectType::Hospital: return GetString(STR_FEDERAL_PROJECT_HOSPITAL, p.town);
		case FederalProjectType::University: return GetString(STR_FEDERAL_PROJECT_UNIVERSITY, p.town);
		default: NOT_REACHED();
	}
}

/**
 * What a company must build to deliver a kind of project; takes the numbers of #FEDERAL_LINE_DEPARTURES and the stops.
 * @param type The kind of project.
 * @return The text.
 */
StringID GetFederalProjectNeed(FederalProjectType type)
{
	switch (type) {
		case FederalProjectType::MetroLine: return STR_FEDERAL_NEED_METRO;
		case FederalProjectType::BrtLine: return STR_FEDERAL_NEED_BRT;
		case FederalProjectType::Expressway: return STR_FEDERAL_NEED_EXPRESSWAY;
		case FederalProjectType::Hospital: return STR_FEDERAL_NEED_HOSPITAL;
		case FederalProjectType::University: return STR_FEDERAL_NEED_UNIVERSITY;
		default: NOT_REACHED();
	}
}

/**
 * How many projects of the current plan are at a stage.
 * @param state The stage.
 * @return How many.
 */
uint CountFederalProjects(FederalProjectState state)
{
	uint count = 0;
	for (const FederalProject &p : _federal_projects) {
		if (p.plan == _federation.plan && p.state == state) count++;
	}
	return count;
}

/**
 * What the projects companies delivered give the federal government at the general election.
 * @return Votes in every seat.
 */
int GetFederalProjectVotes()
{
	return std::min(FEDERAL_PROJECT_VOTES_MAX, FEDERAL_PROJECT_VOTES * static_cast<int>(CountFederalProjects(FederalProjectState::Delivered)));
}

/**
 * How many stations of a town a line stops at.
 * @param list The line.
 * @param town The town.
 * @return The stations.
 */
static uint CountLineStationsIn(const OrderList *list, TownID town)
{
	std::vector<StationID> seen;
	for (const Order &o : list->GetOrders()) {
		if (!o.IsType(OT_GOTO_STATION)) continue;
		const Station *st = Station::GetIfValid(o.GetDestination().ToStationID());
		if (st == nullptr || st->town == nullptr || st->town->index != town) continue;
		if (std::ranges::find(seen, st->index) == seen.end()) seen.push_back(st->index);
	}
	return static_cast<uint>(seen.size());
}

/**
 * The company whose line delivers a metro or BRT line project: a railway line with #FEDERAL_METRO_STATIONS stations
 * in the town, or a bus line graded BRT with #FEDERAL_BRT_STOPS stops there, that ran #FEDERAL_LINE_DEPARTURES
 * times or more last month.
 * @param type MetroLine or BrtLine.
 * @param town The town.
 * @return The company, or CompanyID::Invalid() if no line does.
 */
static CompanyID FindLineDelivery(FederalProjectType type, TownID town)
{
	for (const OrderList *list : OrderList::Iterate()) {
		const Vehicle *v = list->GetFirstSharedVehicle();
		if (v == nullptr || !Company::IsValidID(v->owner)) continue;
		if (list->line.last_month.departures < FEDERAL_LINE_DEPARTURES) continue;
		if (type == FederalProjectType::MetroLine) {
			if (!IsTrainLine(list) || CountLineStationsIn(list, town) < FEDERAL_METRO_STATIONS) continue;
		} else {
			if (!IsBusLine(list) || CountLineStationsIn(list, town) < FEDERAL_BRT_STOPS) continue;
			const BrtScore score = GetLineBrtScore(list);
			if (!score.known || score.grade == BrtGrade::None) continue;
		}
		return v->owner;
	}
	return CompanyID::Invalid();
}

/**
 * Who built a kind of city service in a town.
 * @param town The town.
 * @param service Hospital or University.
 * @param[out] other Set if the town has one that no company built.
 * @return The company that built one, or CompanyID::Invalid() if none did.
 */
static CompanyID FindServiceBuilder(TownID town, CityService service, bool &other)
{
	other = false;
	const ObjectType type = GetObjectTypeOfCityService(service);
	for (const Object *o : Object::Iterate()) {
		if (o->type != type || o->town == nullptr || o->town->index != town) continue;
		const Owner owner = GetTileOwner(o->location.tile);
		if (Company::IsValidID(owner)) return owner;
		other = true;
	}
	return CompanyID::Invalid();
}

/**
 * Does a town have a kind of city service, whoever built it?
 * @param town The town.
 * @param service Hospital or University.
 * @return True if it has one.
 */
static bool HasCityService(TownID town, CityService service)
{
	bool other = false;
	return Company::IsValidID(FindServiceBuilder(town, service, other)) || other;
}

/**
 * The towns, biggest first.
 * @return The towns.
 */
static std::vector<const Town *> GetTownsBySize()
{
	std::vector<const Town *> towns;
	for (const Town *t : Town::Iterate()) towns.push_back(t);
	std::ranges::sort(towns, [](const Town *a, const Town *b) {
		return a->cache.population != b->cache.population ? a->cache.population > b->cache.population : a->index < b->index;
	});
	return towns;
}

/**
 * Is a town a state's capital or a federal territory's city?
 * @param t The town.
 * @return True if it is.
 */
static bool IsCapital(const Town *t)
{
	for (const State &s : GetStates()) {
		if (s.capital == t->index) return true;
	}
	return false;
}

/**
 * What the federal government pays for a kind of project.
 * @param type The kind.
 * @param distance For an expressway, the distance between its towns in tiles.
 * @return The payment.
 */
static Money GetProjectPayment(FederalProjectType type, uint distance)
{
	switch (type) {
		case FederalProjectType::MetroLine: return GetPrice(Price::BuildObject, 30000, nullptr);
		case FederalProjectType::BrtLine: return GetPrice(Price::BuildObject, 12000, nullptr);
		case FederalProjectType::Expressway: return GetPrice(Price::BuildObject, std::max<uint>(6000, distance * 80), nullptr);
		case FederalProjectType::Hospital: return GetCityServiceBuildCost(CityService::Hospital) * 3 / 2;
		case FederalProjectType::University: return GetCityServiceBuildCost(CityService::University) * 3 / 2;
		default: NOT_REACHED();
	}
}

/**
 * Is a project offered for a town in the current plan?
 * @param type The kind of project.
 * @param town The town.
 * @return True if one is open.
 */
static bool IsProjectOpen(FederalProjectType type, TownID town)
{
	for (const FederalProject &p : _federal_projects) {
		if (p.state == FederalProjectState::Open && p.type == type && (p.town == town || p.town2 == town)) return true;
	}
	return false;
}

/**
 * Offer a project of the plan.
 * @param type The kind of project.
 * @param t The town it is for.
 * @param t2 An expressway's other town.
 */
static void OfferProject(FederalProjectType type, const Town *t, const Town *t2 = nullptr)
{
	FederalProject &p = _federal_projects.emplace_back();
	p.type = type;
	p.state = FederalProjectState::Open;
	p.plan = _federation.plan;
	p.town = t->index;
	p.town2 = t2 == nullptr ? TownID::Invalid() : t2->index;
	p.payment = GetProjectPayment(type, t2 == nullptr ? 0 : DistanceManhattan(t->xy, t2->xy));
	p.offered = TimerGameEconomy::year.base();
	Debug(Facility::Misc, Severity::Trace1, "[federal] plan {} offers project {} for town {} ({}) paying {}", p.plan, to_underlying(type), t->index, t2 == nullptr ? -1 : static_cast<int>(t2->index.base()), p.payment);
}

/** Transit First: a metro line for the biggest city without one (a federal territory's first), BRT lines for the biggest capitals. */
static void PlanTransitProjects()
{
	const std::vector<const Town *> towns = GetTownsBySize();
	const Town *metro = nullptr;
	for (int pass = 0; pass < 2 && metro == nullptr; pass++) {
		for (const Town *t : towns) {
			if (t->cache.population < METRO_TOWN_POPULATION) break;
			if (pass == 0 && !IsFederalTerritory(t)) continue;
			if (Company::IsValidID(FindLineDelivery(FederalProjectType::MetroLine, t->index))) continue;
			metro = t;
			break;
		}
	}
	if (metro != nullptr) OfferProject(FederalProjectType::MetroLine, metro);

	uint planned = metro != nullptr ? 1 : 0;
	for (int pass = 0; pass < 2 && planned < PLAN_MAX_PROJECTS; pass++) {
		for (const Town *t : towns) {
			if (planned >= PLAN_MAX_PROJECTS || t->cache.population < BRT_TOWN_POPULATION) break;
			/* Capitals first, then any big town. */
			if (t == metro || (pass == 0 && !IsCapital(t)) || (pass == 1 && IsCapital(t))) continue;
			if (Company::IsValidID(FindLineDelivery(FederalProjectType::BrtLine, t->index))) continue;
			OfferProject(FederalProjectType::BrtLine, t);
			planned++;
		}
	}
}

/** Roads and Growth: expressways joining the capitals closest to each other that no expressway joins yet, on the same land. */
static void PlanRoadProjects()
{
	std::vector<const Town *> capitals;
	for (const State &s : GetStates()) {
		if (Town::IsValidID(s.capital)) capitals.push_back(Town::Get(s.capital));
	}
	struct Pair {
		const Town *a;
		const Town *b;
		uint distance;
	};
	std::vector<Pair> pairs;
	for (size_t i = 0; i < capitals.size(); i++) {
		for (size_t j = i + 1; j < capitals.size(); j++) {
			const Town *a = capitals[i], *b = capitals[j];
			/* The main land, or one region: expressways don't cross the sea. */
			const State *sa = GetTownState(a), *sb = GetTownState(b);
			if (sa == nullptr || sb == nullptr) continue;
			if ((sa->region || sb->region) && a->state != b->state) continue;
			const uint d = DistanceManhattan(a->xy, b->xy);
			if (d < EXPRESSWAY_MIN_DISTANCE || d > EXPRESSWAY_MAX_DISTANCE) continue;
			if (Company::IsValidID(FindExpresswayLink(a, b))) continue;
			pairs.push_back({a, b, d});
		}
	}
	std::ranges::sort(pairs, [](const Pair &x, const Pair &y) { return x.distance != y.distance ? x.distance < y.distance : x.a->index < y.a->index; });
	uint planned = 0;
	for (const Pair &p : pairs) {
		if (planned >= PLAN_MAX_PROJECTS - 1) break;
		OfferProject(FederalProjectType::Expressway, p.a, p.b);
		planned++;
	}
}

/** Homes and Services: hospitals for the biggest towns without one, and a university for the biggest town without one. */
static void PlanServiceProjects()
{
	const std::vector<const Town *> towns = GetTownsBySize();
	uint hospitals = 0, universities = 0;
	for (const Town *t : towns) {
		if (universities >= 1 || t->cache.population < UNIVERSITY_TOWN_POPULATION) break;
		if (HasCityService(t->index, CityService::University)) continue;
		OfferProject(FederalProjectType::University, t);
		universities++;
	}
	for (const Town *t : towns) {
		if (hospitals + universities >= PLAN_MAX_PROJECTS - 1 || t->cache.population < HOSPITAL_TOWN_POPULATION) break;
		if (HasCityService(t->index, CityService::Hospital)) continue;
		OfferProject(FederalProjectType::Hospital, t);
		hospitals++;
	}
}

/**
 * The federal government elected makes its five-year plan: projects matching its platform, which the first company
 * to deliver is paid for. What the plan before didn't deliver lapses.
 */
void StartFederalPlan()
{
	if (!AreStatesFormed() || _federation.government == Platform::None) return;
	const int year = TimerGameEconomy::year.base();
	std::erase_if(_federal_projects, [](const FederalProject &p) { return p.plan < _federation.plan; });
	for (FederalProject &p : _federal_projects) {
		if (p.state != FederalProjectState::Open) continue;
		p.state = FederalProjectState::Lapsed;
		p.done = year;
	}
	_federation.plan++;
	_federation.plan_year = year;
	switch (_federation.government) {
		case Platform::TransitFirst: PlanTransitProjects(); break;
		case Platform::RoadsAndGrowth: PlanRoadProjects(); break;
		case Platform::HomesAndServices: PlanServiceProjects(); break;
		default: break;
	}

	std::string list;
	uint count = 0;
	TownID where = TownID::Invalid();
	for (const FederalProject &p : _federal_projects) {
		if (p.plan != _federation.plan) continue;
		if (!list.empty()) list += "; ";
		list += GetString(STR_FEDERAL_PROJECT_WITH_PAYMENT, GetFederalProjectName(p), p.payment);
		if (count++ == 0) where = p.town;
	}
	EncodedString headline = count == 0 ?
			GetEncodedString(STR_NEWS_FEDERAL_PLAN_NONE, _federation.plan, GetPlatformName(_federation.government)) :
			GetEncodedString(STR_NEWS_FEDERAL_PLAN, _federation.plan, GetPlatformName(_federation.government), count, list);
	if (Town::IsValidID(where)) {
		AddNewsItem(std::move(headline), NewsType::General, NewsStyle::Normal, {}, where);
	} else {
		AddNewsItem(std::move(headline), NewsType::General, NewsStyle::Normal, {});
	}
	InvalidateWindowClassesData(WindowClass::FederalPlan);
}

/** Pay the companies that delivered a project of the plan; lapse projects no longer wanted. */
static void CheckFederalProjects()
{
	const int year = TimerGameEconomy::year.base();
	for (FederalProject &p : _federal_projects) {
		if (p.state != FederalProjectState::Open) continue;
		if (!Town::IsValidID(p.town) || (p.type == FederalProjectType::Expressway && !Town::IsValidID(p.town2))) {
			p.state = FederalProjectState::Lapsed;
			p.done = year;
			continue;
		}
		CompanyID company = CompanyID::Invalid();
		bool other = false;
		switch (p.type) {
			case FederalProjectType::MetroLine:
			case FederalProjectType::BrtLine:
				company = FindLineDelivery(p.type, p.town);
				break;
			case FederalProjectType::Expressway:
				company = FindExpresswayLink(Town::Get(p.town), Town::Get(p.town2));
				break;
			case FederalProjectType::Hospital:
				company = FindServiceBuilder(p.town, CityService::Hospital, other);
				break;
			case FederalProjectType::University:
				company = FindServiceBuilder(p.town, CityService::University, other);
				break;
			default: NOT_REACHED();
		}
		if (Company::IsValidID(company)) {
			p.state = FederalProjectState::Delivered;
			p.company = company;
			p.done = year;
			SubtractMoneyFromCompany(company, CommandCost(ExpensesType::Other, -p.payment));
			_federation.projects_this_year += p.payment;
			Debug(Facility::Misc, Severity::Trace1, "[federal] company {} delivers project {} for town {}: paid {}", company, to_underlying(p.type), p.town, p.payment);
			AddNewsItem(GetEncodedString(STR_NEWS_FEDERAL_PROJECT_DELIVERED, company, GetFederalProjectName(p), p.payment), NewsType::Subsidies, NewsStyle::Normal, {}, p.town);
		} else if (other) {
			/* The town got its hospital or university from its council, or the federal programme. */
			p.state = FederalProjectState::Lapsed;
			p.done = year;
		}
	}
}

/** What the national programmes and the transit agencies cost the federal government each month, as far as it isn't counted when paid. */
static void CountProgrammes()
{
	const Platform government = GetFederalGovernment();
	for (const Town *t : Town::Iterate()) {
		if (GetTownTransitBudget(t) <= 0) continue;
		const uint percent = GetFederalTransitGrantPercent(t) + GetFederalAgencyBudgetPercent(t);
		if (percent > 0) _federation.programmes_this_year += GetTownTaxBase(t) / 12 * percent / 100;
	}
	if (government == Platform::RoadsAndGrowth) {
		Money fuel = 0;
		for (const RoadVehicle *rv : RoadVehicle::Iterate()) {
			if (!rv->IsFrontEngine() || !Company::IsValidID(rv->owner) || rv->vehstatus.Test(VehState::Stopped)) continue;
			if (GetFederalFuelCostPercent(rv) == 100) continue;
			/* The running costs are with the subsidy already: it is its share of what they were. */
			fuel += rv->GetRunningCost() * FEDERAL_FUEL_SUBSIDY_PERCENT / (100 - FEDERAL_FUEL_SUBSIDY_PERCENT) / 12;
		}
		_federation.programmes_this_year += fuel;
	}
}

/** Each month: the plan's projects, and what the programmes cost. A game with a federal government but no plan yet gets one. */
void UpdateFederalMonthly()
{
	if (!AreStatesFormed()) return;
	if (_federation.government != Platform::None && _federation.plan == 0 && !IsElectionCampaign(INVALID_STATE)) StartFederalPlan();
	CheckFederalProjects();
	CountProgrammes();
	InvalidateWindowClassesData(WindowClass::FederalPlan);
}

/**
 * Each January under Homes and Services: the federal government builds a hospital in the biggest town of
 * #FEDERAL_HOSPITAL_POPULATION people without one, or else a university in the biggest town of
 * #FEDERAL_UNIVERSITY_POPULATION people without one. Towns waiting for a project of the plan are left to companies.
 */
void UpdateFederalYearly()
{
	if (GetFederalGovernment() != Platform::HomesAndServices || IsElectionCampaign(INVALID_STATE)) return;
	const std::vector<const Town *> towns = GetTownsBySize();
	for (CityService service : {CityService::Hospital, CityService::University}) {
		const uint min_population = service == CityService::Hospital ? FEDERAL_HOSPITAL_POPULATION : FEDERAL_UNIVERSITY_POPULATION;
		const FederalProjectType type = service == CityService::Hospital ? FederalProjectType::Hospital : FederalProjectType::University;
		for (const Town *t : towns) {
			if (t->cache.population < min_population) break;
			if (HasCityService(t->index, service) || IsProjectOpen(type, t->index)) continue;
			Money cost = 0;
			if (!BuildFederalCityService(Town::Get(t->index), service, cost)) continue;
			_federation.programmes_this_year += cost;
			return;
		}
	}
}

/** Forget the plan; used when starting or loading a game. */
void InitializeFederal()
{
	_federal_projects.clear();
}
