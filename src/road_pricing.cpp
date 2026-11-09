/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file road_pricing.cpp Road pricing.
 *
 *  - Expressways are a road type (ROADTYPE_EXPRESSWAY) without houses along
 *    them or level crossings; they take 2.5 times the traffic of a town road
 *    (town_traffic.cpp).
 *  - Toll plazas on a company's expressway take tolls from the town traffic
 *    between the towns the expressway links and from other companies' road
 *    vehicles. They run as concessions: after 30 years a concession is
 *    extended by 20, in exchange for a toll cut. Now and then the government
 *    freezes a company's tolls for five years and makes up half the loss.
 *  - Towns may charge road vehicles, except buses, for driving into their
 *    centre. Town traffic there drops, and the money goes to the town's
 *    transit budget.
 */

#include "stdafx.h"
#include "road_pricing.h"
#include "road_pricing_cmd.h"
#include "bus_priority.h"
#include "cargotype.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "core/math_func.hpp"
#include "core/random_func.hpp"
#include "economy_func.h"
#include "house.h"
#include "news_func.h"
#include "road_map.h"
#include "roadveh.h"
#include "strings_func.h"
#include "tile_cmd.h"
#include "tilearea_type.h"
#include "town.h"
#include "town_budget.h"
#include "town_traffic.h"
#include "transparency.h"
#include "tunnelbridge_map.h"
#include "viewport_func.h"
#include "window_func.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

static std::map<TileIndex, TollPlaza> _toll_plazas; ///< Toll plazas by tile.
static uint _charging_towns = 0; ///< Number of towns with a congestion charge; a quick check for vehicles.

/**
 * The toll plazas, for saving and loading.
 * @return The plazas by tile.
 */
std::map<TileIndex, TollPlaza> &GetTollPlazas()
{
	return _toll_plazas;
}

/** Forget all toll plazas; used when starting or loading a game. */
void InitializeRoadPricing()
{
	_toll_plazas.clear();
	_charging_towns = 0;
}

/** Count the towns with a congestion charge again, e.g. after loading a game. */
void UpdateCongestionChargeTowns()
{
	_charging_towns = 0;
	for (const Town *t : Town::Iterate()) {
		if (t->transit.congestion_charge) _charging_towns++;
	}
}

/* Expressways. */

/**
 * Is a tile's road an expressway?
 * @param tile The tile.
 * @return True if its road type is the expressway.
 */
bool IsExpresswayRoad(TileIndex tile)
{
	return MayHaveRoad(tile) && GetRoadTypeRoad(tile) == ROADTYPE_EXPRESSWAY;
}

/**
 * Draw the lanes and the barrier in the middle of an expressway: straight roads, flat or up a slope.
 * @param ti The tile, after its ground and road were drawn.
 * @return True if they were drawn, over any tram track there.
 */
bool DrawExpressway(const TileInfo *ti)
{
	if (!IsNormalRoad(ti->tile) || GetRoadTypeRoad(ti->tile) != ROADTYPE_EXPRESSWAY) return false;
	const SpriteID sprite = GetRoadMarkingSprite(RoadMarking::Expressway, SPR_EXPRESSWAY_X, ti->tileh, GetRoadBits(ti->tile, RoadTramType::Road));
	if (sprite == 0) return false;
	DrawGroundSprite(sprite, PAL_NONE);
	return true;
}

/* Toll plazas. */

/** Highest speed through toll booths: everyone slows down to pay. In internal units (km-ish/h * 2). */
static constexpr int TOLL_PLAZA_SPEED = 40;
/** Largest expressway network that is followed to find the towns a toll plaza's expressway links. */
static constexpr size_t TOLL_NETWORK_MAX_TILES = 8192;
/** A road joining an expressway counts for the closest town this close. */
static constexpr uint ACCESS_TOWN_DISTANCE = 32;

/**
 * Can a tile have a toll plaza: a straight, plain stretch of expressway?
 * @param tile The tile.
 * @return True if it can.
 */
static bool CanHaveTollPlaza(TileIndex tile)
{
	if (!IsTileType(tile, TileType::Road) || !IsNormalRoad(tile) || GetRoadTypeRoad(tile) != ROADTYPE_EXPRESSWAY) return false;
	RoadBits bits = GetRoadBits(tile, RoadTramType::Road);
	return bits == ROAD_X || bits == ROAD_Y;
}

/**
 * The toll plaza on a tile.
 * @param tile The tile.
 * @return The plaza, or nullptr if there is none.
 */
TollPlaza *GetTollPlaza(TileIndex tile)
{
	if (_toll_plazas.empty()) return nullptr;
	auto it = _toll_plazas.find(tile);
	if (it == _toll_plazas.end() || !CanHaveTollPlaza(tile)) return nullptr;
	return &it->second;
}

/**
 * Highest speed of road vehicles on a tile because of toll booths.
 * @param tile The tile.
 * @return The speed in internal road vehicle units, or INT_MAX when there is no limit.
 */
int GetTollPlazaSpeedLimit(TileIndex tile)
{
	const TollPlaza *plaza = GetTollPlaza(tile);
	return (plaza == nullptr || plaza->free_flow) ? INT_MAX : TOLL_PLAZA_SPEED;
}

/**
 * Cost of building a toll plaza.
 * @return The cost.
 */
Money GetTollPlazaCost()
{
	return GetPrice(Price::BuildObject, 300, nullptr);
}

/**
 * Cost of replacing a toll plaza's booths with a free-flow gantry.
 * @return The cost.
 */
Money GetFreeFlowUpgradeCost()
{
	return GetPrice(Price::BuildObject, 200, nullptr);
}

/**
 * Yearly upkeep of a toll plaza.
 * @param free_flow Whether it is a free-flow gantry, without staff in booths.
 * @return The upkeep.
 */
Money GetTollPlazaYearlyUpkeep(bool free_flow)
{
	return GetPrice(Price::BuildObject, free_flow ? 10 : 25, nullptr);
}

/**
 * The standard toll of a car, for one trip (open system) or 40 tiles (closed system).
 * @return The toll.
 */
static Money GetTollUnit()
{
	return GetPrice(Price::BuildObject, 5, nullptr) / 4;
}

/**
 * The toll level a company may charge now: its own, unless the government froze its tolls.
 * @param company The company.
 * @return The level in percent of the standard toll.
 */
uint8_t GetEffectiveTollLevel(CompanyID company)
{
	const Company *c = Company::GetIfValid(company);
	if (c == nullptr) return 100;
	if (TimerGameEconomy::date < c->tolls.freeze_until) return static_cast<uint8_t>(std::min<uint>(c->tolls.level, 100) * 9 / 10);
	return c->tolls.level;
}

/**
 * Toll class of a road vehicle, like the classes of Malaysian tolls.
 * @param front The vehicle (front).
 * @return The class in percent of a car's toll: buses 75%, vans with mail 100%, lorries 200%.
 */
uint GetTollClassPercent(const RoadVehicle *front)
{
	if (front->IsBus()) return 75;
	if (IsCargoInClass(front->cargo_type, CargoClass::Mail)) return 100;
	return 200;
}

/**
 * What a road vehicle pays to pass a toll plaza.
 * @param front The vehicle (front).
 * @param tile The toll plaza.
 * @return The toll.
 */
Money GetVehicleToll(const RoadVehicle *front, TileIndex tile)
{
	const TollPlaza *plaza = GetTollPlaza(tile);
	if (plaza == nullptr) return 0;
	const Owner owner = GetRoadOwner(tile, RoadTramType::Road);
	return GetTollUnit() * GetTollClassPercent(front) * GetEffectiveTollLevel(owner) * (100 - plaza->cut) / 1'000'000;
}

/**
 * Build a toll plaza on an expressway, remove it, or replace its booths with a free-flow gantry.
 * @param flags Type of operation.
 * @param tile The expressway tile.
 * @param action What to do.
 * @return The cost of this operation or an error.
 */
CommandCost CmdBuildTollPlaza(DoCommandFlags flags, TileIndex tile, TollPlazaAction action)
{
	if (!IsValidTile(tile) || action >= TollPlazaAction::End) return CMD_ERROR;
	if (!CanHaveTollPlaza(tile)) return CommandCost(STR_ERROR_TOLL_PLAZA_EXPRESSWAY_ONLY);
	CommandCost ret = CheckOwnership(GetRoadOwner(tile, RoadTramType::Road));
	if (ret.Failed()) return ret;

	TollPlaza *plaza = GetTollPlaza(tile);
	switch (action) {
		case TollPlazaAction::Build:
			if (plaza != nullptr) return CommandCost(STR_ERROR_ALREADY_BUILT);
			if (HasRoadWorks(tile)) return CommandCost(STR_ERROR_ROAD_WORKS_IN_PROGRESS);
			if (flags.Test(DoCommandFlag::Execute)) {
				TollPlaza &p = _toll_plazas[tile];
				p = {};
				p.concession_end = TimerGameEconomy::date + TOLL_CONCESSION_YEARS * EconomyTime::DAYS_IN_YEAR;
				MarkTileDirtyByTile(tile);
				InvalidateWindowClassesData(WindowClass::Tolls);
			}
			return CommandCost(ExpensesType::Construction, GetTollPlazaCost());

		case TollPlazaAction::Remove:
			if (plaza == nullptr) return CommandCost(STR_ERROR_TOLL_PLAZA_NONE);
			if (flags.Test(DoCommandFlag::Execute)) {
				_toll_plazas.erase(tile);
				MarkTileDirtyByTile(tile);
				InvalidateWindowClassesData(WindowClass::Tolls);
			}
			return CommandCost(ExpensesType::Construction, GetPrice(Price::ClearObject, 4, nullptr));

		case TollPlazaAction::FreeFlow:
			if (plaza == nullptr) return CommandCost(STR_ERROR_TOLL_PLAZA_NONE);
			if (plaza->free_flow) return CommandCost(STR_ERROR_ALREADY_BUILT);
			if (flags.Test(DoCommandFlag::Execute)) {
				plaza->free_flow = true;
				MarkTileDirtyByTile(tile);
				InvalidateWindowClassesData(WindowClass::Tolls);
			}
			return CommandCost(ExpensesType::Construction, GetFreeFlowUpgradeCost());

		default: NOT_REACHED();
	}
}

/**
 * Change a company's toll level or toll scheme.
 * @param flags Type of operation.
 * @param property What to change.
 * @param value The new level in percent, or the new #TollScheme.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetTollPolicy(DoCommandFlags flags, TollPolicyProperty property, uint8_t value)
{
	Company *c = Company::GetIfValid(_current_company);
	if (c == nullptr) return CMD_ERROR;

	switch (property) {
		case TollPolicyProperty::Level:
			if (value < TOLL_LEVEL_MIN || value > TOLL_LEVEL_MAX || value % TOLL_LEVEL_STEP != 0) return CommandCost(STR_ERROR_TOLL_LEVEL_CAPPED);
			if (flags.Test(DoCommandFlag::Execute)) c->tolls.level = value;
			break;

		case TollPolicyProperty::Scheme:
			if (value >= to_underlying(TollScheme::End)) return CMD_ERROR;
			if (flags.Test(DoCommandFlag::Execute)) c->tolls.scheme = static_cast<TollScheme>(value);
			break;

		default:
			return CMD_ERROR;
	}
	if (flags.Test(DoCommandFlag::Execute)) InvalidateWindowData(WindowClass::Tolls, c->index.base());
	return CommandCost();
}

/** Sprites of road pricing in city.png, from #SPR_TOLL_PLAZA_BASE. */
enum TollPlazaSprite : uint8_t {
	TPS_CANOPY_X, ///< Roof over the booths, road along X.
	TPS_FAR_X, ///< Booth on the far side, road along X.
	TPS_NEAR_X, ///< Booth on the near side, road along X.
	TPS_CANOPY_Y, ///< Roof over the booths, road along Y.
	TPS_FAR_Y, ///< Booth on the far side, road along Y.
	TPS_NEAR_Y, ///< Booth on the near side, road along Y.
	TPS_GANTRY_X, ///< Free-flow gantry, road along X.
	TPS_GANTRY_Y, ///< Free-flow gantry, road along Y.
};

/**
 * Draw a toll plaza: booths at both sides of the road under a roof, or a free-flow gantry.
 * @param ti The tile, after its road was drawn.
 */
void DrawTollPlaza(const TileInfo *ti)
{
	const TollPlaza *plaza = GetTollPlaza(ti->tile);
	if (plaza == nullptr || ti->tileh != SLOPE_FLAT) return;

	const bool x = GetRoadBits(ti->tile, RoadTramType::Road) == ROAD_X;
	const bool transparent = IsTransparencySet(TransparencyOption::Buildings);
	const Owner owner = GetRoadOwner(ti->tile, RoadTramType::Road);
	const PaletteID pal = Company::IsValidID(owner) ? GetCompanyPalette(owner) : PALETTE_TO_GREY;
	auto sprite = [](uint offset) {
		SpriteID image = SPR_TOLL_PLAZA_BASE + offset;
		SetBit(image, PALETTE_MODIFIER_COLOUR);
		return image;
	};

	if (plaza->free_flow) {
		/* A gantry across the road, above the vehicles. */
		AddSortableSpriteToDraw(sprite(x ? TPS_GANTRY_X : TPS_GANTRY_Y), pal, *ti, {{0, 0, 12}, {TILE_SIZE, TILE_SIZE, 3}, {0, 0, -12}}, transparent);
		return;
	}
	/* Booths on both sides; the sprites are drawn from the tile's north corner. */
	if (x) {
		AddSortableSpriteToDraw(sprite(TPS_FAR_X), pal, *ti, {{0, 0, 0}, {TILE_SIZE, 3, 10}, {0, 0, 0}}, transparent);
		AddSortableSpriteToDraw(sprite(TPS_NEAR_X), pal, *ti, {{0, 13, 0}, {TILE_SIZE, 3, 10}, {0, -13, 0}}, transparent);
		AddSortableSpriteToDraw(sprite(TPS_CANOPY_X), pal, *ti, {{0, 0, 11}, {TILE_SIZE, TILE_SIZE, 3}, {0, 0, -11}}, transparent);
	} else {
		AddSortableSpriteToDraw(sprite(TPS_FAR_Y), pal, *ti, {{0, 0, 0}, {3, TILE_SIZE, 10}, {0, 0, 0}}, transparent);
		AddSortableSpriteToDraw(sprite(TPS_NEAR_Y), pal, *ti, {{13, 0, 0}, {3, TILE_SIZE, 10}, {-13, 0, 0}}, transparent);
		AddSortableSpriteToDraw(sprite(TPS_CANOPY_Y), pal, *ti, {{0, 0, 11}, {TILE_SIZE, TILE_SIZE, 3}, {0, 0, -11}}, transparent);
	}
}

/**
 * A tile was cleared: a toll plaza on it goes too.
 * @param tile The tile.
 */
void OnRoadPricingTileCleared(TileIndex tile)
{
	if (!_toll_plazas.empty() && _toll_plazas.erase(tile) != 0) InvalidateWindowClassesData(WindowClass::Tolls);
}

/* Congestion charges. */

/**
 * Is a tile in the charging zone of a town with a congestion charge: its centre and inner suburbs?
 * @param t The town.
 * @param tile The tile.
 * @return True if it is.
 */
bool IsInCongestionChargeZone(const Town *t, TileIndex tile)
{
	if (!t->transit.congestion_charge) return false;
	HouseZone zone = GetTownRadiusGroup(t, tile);
	return zone == HouseZone::TownCentre || zone == HouseZone::TownInnerSuburb;
}

/**
 * How much of its traffic a road keeps under a congestion charge: 70% in the charging zone (London's charge cut traffic in it by about 30%).
 * @param t The town the road belongs to.
 * @param tile The road tile.
 * @return The share in percent.
 */
uint GetCongestionChargeTrafficPercent(const Town *t, TileIndex tile)
{
	return IsInCongestionChargeZone(t, tile) ? 70 : 100;
}

/**
 * How congested the centre and inner suburbs of a town are: the average congestion level of their roads.
 * @param t The town.
 * @return The level in tenths.
 */
uint GetCongestionChargeZoneLevel(const Town *t)
{
	const uint radius_sq = t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownInnerSuburb)];
	if (radius_sq == 0) return 0;
	const uint radius = IntSqrt(radius_sq) + 1;

	uint roads = 0, levels = 0;
	TileArea area(t->xy, 1, 1);
	area.Expand(radius);
	for (TileIndex tile : area) {
		if (!IsTileType(tile, TileType::Road) || IsRoadDepot(tile) || !HasTileRoadType(tile, RoadTramType::Road)) continue;
		if (DistanceSquare(tile, t->xy) > radius_sq) continue;
		roads++;
		levels += GetRoadCongestion(tile);
	}
	return roads == 0 ? 0 : levels * 10 / roads;
}

/**
 * What a road vehicle pays to drive into a town's charging zone.
 * @param front The vehicle (front).
 * @return The charge; buses and passenger trams go free.
 */
Money GetCongestionCharge(const RoadVehicle *front)
{
	if (front->IsBus()) return 0;
	return GetPrice(Price::BuildObject, 1, nullptr) * GetTollClassPercent(front) / 100;
}

/**
 * Cost of a company's campaign for a congestion charge in a town.
 * @return The cost.
 */
Money GetCongestionChargeProposalCost()
{
	return GetPrice(Price::BuildObject, 125, nullptr);
}

/** Smallest town that brings in a congestion charge on its own. */
static constexpr uint CHARGE_OWN_POPULATION = 15000;
/** Average congestion (in tenths of a level) of its centre at which a town brings in a congestion charge on its own. */
static constexpr uint CHARGE_OWN_LEVEL = 30;
/** Smallest town that agrees to a company's proposal for a congestion charge. */
static constexpr uint CHARGE_PROPOSAL_POPULATION = 5000;
/** Average congestion (in tenths of a level) of its centre at which a town agrees to a company's proposal. */
static constexpr uint CHARGE_PROPOSAL_LEVEL = 20;

/**
 * Why a town would turn down a company's proposal for a congestion charge.
 * @param t The town.
 * @param company The company.
 * @return The reason, or INVALID_STRING_ID if it would agree.
 */
StringID GetCongestionChargeRefusal(const Town *t, CompanyID company)
{
	if (t->transit.congestion_charge) return STR_ERROR_CONGESTION_CHARGE_ALREADY;
	if (t->cache.population < CHARGE_PROPOSAL_POPULATION) return STR_ERROR_CONGESTION_CHARGE_TOWN_TOO_SMALL;
	if (GetCongestionChargeZoneLevel(t) < CHARGE_PROPOSAL_LEVEL) return STR_ERROR_CONGESTION_CHARGE_NOT_CONGESTED;
	if (Company::IsValidID(company) && t->have_ratings.Test(company) && t->ratings[company] < RATING_GOOD) return STR_ERROR_CONGESTION_CHARGE_RATING;
	return INVALID_STRING_ID;
}

/**
 * A town brings in a congestion charge.
 * @param t The town.
 */
static void StartCongestionCharge(Town *t)
{
	t->transit.congestion_charge = true;
	t->transit.charge_since = TimerGameEconomy::date;
	UpdateCongestionChargeTowns();
	AddNewsItem(GetEncodedString(STR_NEWS_CONGESTION_CHARGE, t->index), NewsType::General, NewsStyle::Normal, {}, t->index);
	SetWindowDirty(WindowClass::TransitAuthority, t->index);
}

/**
 * Ask a town to bring in a congestion charge.
 * @param flags Type of operation.
 * @param town_id The town.
 * @return The cost of this operation or an error.
 */
CommandCost CmdProposeCongestionCharge(DoCommandFlags flags, TownID town_id)
{
	Town *t = Town::GetIfValid(town_id);
	if (t == nullptr || !Company::IsValidID(_current_company)) return CMD_ERROR;
	StringID refusal = GetCongestionChargeRefusal(t, _current_company);
	if (refusal != INVALID_STRING_ID) return CommandCost(refusal);
	if (flags.Test(DoCommandFlag::Execute)) StartCongestionCharge(t);
	return CommandCost(ExpensesType::Other, GetCongestionChargeProposalCost());
}

/**
 * A road vehicle drove onto a tile: it pays at toll plazas of other companies, and when it drives into a town's charging zone.
 * @param front The vehicle (front).
 * @param from The tile it came from.
 * @param to The tile it drove onto.
 */
void OnRoadVehicleEntersTile(RoadVehicle *front, TileIndex from, TileIndex to)
{
	if (!_toll_plazas.empty()) {
		TollPlaza *plaza = GetTollPlaza(to);
		const Owner owner = plaza == nullptr ? INVALID_OWNER : GetRoadOwner(to, RoadTramType::Road);
		if (plaza != nullptr && owner != front->owner && Company::IsValidID(owner)) {
			Money toll = GetVehicleToll(front, to);
			if (toll > 0) {
				SubtractMoneyFromCompany(front->owner, CommandCost(ExpensesType::RoadVehRun, toll));
				front->profit_this_year -= toll << 8;
				SubtractMoneyFromCompany(owner, CommandCost(ExpensesType::Other, -toll));
				plaza->vehicles_this_month++;
				plaza->income_this_month += toll;
			}
		}
	}

	if ((_charging_towns == 0 && !AnyLowEmissionZones()) || !IsTileType(to, TileType::Road)) return;
	Town *t = ClosestTownFromTile(to, UINT_MAX);
	if (t == nullptr) return;
	/* Low-emission zones: vehicles that aren't electric pay to drive in. */
	if (AnyLowEmissionZones()) OnRoadVehicleEntersLowEmissionZone(front, t, from, to);
	if (_charging_towns == 0 || front->IsBus() || !IsInCongestionChargeZone(t, to) || (IsValidTile(from) && IsInCongestionChargeZone(t, from))) return;
	Money charge = GetCongestionCharge(front);
	if (charge <= 0) return;
	SubtractMoneyFromCompany(front->owner, CommandCost(ExpensesType::RoadVehRun, charge));
	front->profit_this_year -= charge << 8;
	t->transit.charge_this_year += charge;
	t->transit.charge_income_this_month += charge;
	t->transit.charged_this_month++;
}

/* The monthly and yearly round. */

/** A company's expressway network around a toll plaza. */
struct TollNetwork {
	std::vector<TileIndex> plazas; ///< Toll plazas on it.
	std::set<TownID> towns; ///< Towns it links: a road joins it close to them.
};

/**
 * Follow a company's expressway from a toll plaza: the plazas on it, and the towns whose roads join it.
 * @param start The toll plaza, or any tile of the expressway.
 * @param owner The company.
 * @param[out] net The network.
 * @param[out] visited If not nullptr, the expressway's tiles are added to it.
 */
static void FindTollNetwork(TileIndex start, Owner owner, TollNetwork &net, std::set<TileIndex> *visited = nullptr)
{
	std::vector<TileIndex> queue{start};
	std::set<TileIndex> seen{start};
	auto visit = [&](TileIndex tile) {
		if (seen.size() < TOLL_NETWORK_MAX_TILES && seen.insert(tile).second) queue.push_back(tile);
	};

	while (!queue.empty()) {
		TileIndex tile = queue.back();
		queue.pop_back();
		if (HasTollPlaza(tile)) net.plazas.push_back(tile);

		if (IsTileType(tile, TileType::TunnelBridge)) visit(GetOtherTunnelBridgeEnd(tile));

		RoadBits bits = GetAnyRoadBits(tile, RoadTramType::Road);
		for (DiagDirection dir : EnumRange(DiagDirection::End)) {
			if (!bits.Any(DiagDirToRoadBits(dir))) continue;
			TileIndex next = TileAddByDiagDir(tile, dir);
			if (!IsValidTile(next) || !MayHaveRoad(next) || !HasTileRoadType(next, RoadTramType::Road)) continue;
			if (!GetAnyRoadBits(next, RoadTramType::Road).Any(DiagDirToRoadBits(ReverseDiagDir(dir)))) continue;

			if (GetRoadTypeRoad(next) == ROADTYPE_EXPRESSWAY) {
				if (GetRoadOwner(next, RoadTramType::Road) == owner) visit(next);
			} else {
				/* A road of another type joins the expressway here. */
				const Town *t = ClosestTownFromTile(next, ACCESS_TOWN_DISTANCE);
				if (t != nullptr) net.towns.insert(t->index);
			}
		}
	}
	std::sort(net.plazas.begin(), net.plazas.end());
	if (visited != nullptr) visited->insert(seen.begin(), seen.end());
}

/**
 * Does a company's expressway link two towns: roads from both join it? For the federal government's plan.
 * @param a One town.
 * @param b The other town.
 * @return The company whose expressway links them, or CompanyID::Invalid().
 */
CompanyID FindExpresswayLink(const Town *a, const Town *b)
{
	std::set<TileIndex> done;
	TileArea area(a->xy, 1, 1);
	area.Expand(ACCESS_TOWN_DISTANCE);
	for (TileIndex tile : area) {
		if (!IsExpresswayRoad(tile) || done.contains(tile)) continue;
		const Owner owner = GetRoadOwner(tile, RoadTramType::Road);
		if (!Company::IsValidID(owner)) continue;
		TollNetwork net;
		FindTollNetwork(tile, owner, net, &done);
		if (net.towns.contains(a->index) && net.towns.contains(b->index)) return owner;
	}
	return CompanyID::Invalid();
}

/**
 * What a company's toll plazas take from the town traffic on one expressway network in a month.
 *
 * Trips between each pair of linked towns: their populations multiplied, over
 * 5,000,000, times 60 / (distance + 20) - two towns of 10,000 people 40 tiles
 * apart make 20 trips a month (each standing for many cars). Higher tolls put
 * people off: 150% fewer trips at level 0, none at level 300. Free-flow
 * gantries everywhere make 10% more trips. A trip pays the standard toll for
 * 40 tiles per 40 tiles of distance (closed system), or the standard toll
 * (open system), times 1.1 for the mix of cars, motorbikes and lorries.
 * @param net The network.
 * @param scheme How the company charges.
 * @param level The toll level in percent.
 * @param[out] trips The trips.
 * @return The income.
 */
static Money GetTollNetworkIncome(const TollNetwork &net, TollScheme scheme, uint level, uint64_t &trips)
{
	trips = 0;
	if (net.plazas.empty() || net.towns.size() < 2) return 0;

	uint cut = 0;
	bool all_free_flow = true;
	for (TileIndex tile : net.plazas) {
		const TollPlaza *plaza = GetTollPlaza(tile);
		cut += plaza->cut;
		all_free_flow &= plaza->free_flow;
	}
	cut /= static_cast<uint>(net.plazas.size());

	const Money unit = GetTollUnit();
	Money income = 0;
	for (auto a = net.towns.begin(); a != net.towns.end(); ++a) {
		const Town *ta = Town::Get(*a);
		for (auto b = std::next(a); b != net.towns.end(); ++b) {
			const Town *tb = Town::Get(*b);
			const uint64_t distance = DistanceManhattan(ta->xy, tb->xy);
			/* Thousandths of a trip. */
			uint64_t milli = static_cast<uint64_t>(ta->cache.population) * tb->cache.population * 60 * 1000 / (5'000'000 * (distance + 20));
			milli = milli * (150 - level / 2) / 100;
			if (all_free_flow) milli = milli * 110 / 100;
			const Money toll = scheme == TollScheme::PerKm ? unit * static_cast<int64_t>(distance) / 40 : unit;
			income += toll * static_cast<int64_t>(milli) * 110 * level * (100 - cut) / (1000 * 100 * 100 * 100);
			trips += milli;
		}
	}
	trips /= 1000;
	return income;
}

/** Each month: tolls from town traffic, freeze compensation, upkeep and concessions of toll plazas. */
static void TollsMonthlyLoop()
{
	std::array<Money, MAX_COMPANIES> income{}, compensation{}, upkeep{};

	/* Close the month of other companies' vehicles paying at the plazas. */
	for (auto &[tile, plaza] : _toll_plazas) {
		plaza.vehicles = plaza.vehicles_this_month;
		plaza.income = plaza.income_this_month;
		plaza.vehicles_this_month = 0;
		plaza.income_this_month = 0;
		plaza.trips = 0;
		plaza.towns = 0;
		if (!CanHaveTollPlaza(tile)) continue;
		const Owner owner = GetRoadOwner(tile, RoadTramType::Road);
		if (!Company::IsValidID(owner)) continue;
		income[owner.base()] += plaza.income;
		upkeep[owner.base()] += GetTollPlazaYearlyUpkeep(plaza.free_flow) / 12;
	}

	/* Town traffic on each expressway network with plazas. */
	std::set<TileIndex> done;
	for (auto &[tile, plaza] : _toll_plazas) {
		if (done.contains(tile) || !CanHaveTollPlaza(tile)) continue;
		const Owner owner = GetRoadOwner(tile, RoadTramType::Road);
		if (!Company::IsValidID(owner)) continue;
		const Company *c = Company::Get(owner);

		TollNetwork net;
		FindTollNetwork(tile, owner, net);
		done.insert(net.plazas.begin(), net.plazas.end());

		uint64_t trips;
		const uint level = GetEffectiveTollLevel(owner);
		const Money network_income = GetTollNetworkIncome(net, c->tolls.scheme, level, trips);
		if (level != c->tolls.level) {
			/* The government makes up half of what a toll freeze costs. */
			uint64_t unfrozen_trips;
			const Money unfrozen = GetTollNetworkIncome(net, c->tolls.scheme, c->tolls.level, unfrozen_trips);
			if (unfrozen > network_income) compensation[owner.base()] += (unfrozen - network_income) / 2;
		}
		income[owner.base()] += network_income;

		/* Share the network's figures out over its plazas, for the statistics. */
		const size_t n = net.plazas.size();
		for (size_t i = 0; i < n; i++) {
			TollPlaza *p = GetTollPlaza(net.plazas[i]);
			p->towns = static_cast<uint8_t>(std::min<size_t>(UINT8_MAX, net.towns.size()));
			p->trips = static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, trips / n + (i == 0 ? trips % n : 0)));
			const int64_t total = network_income.base(), count = static_cast<int64_t>(n);
			p->income += total / count + (i == 0 ? total % count : 0);
		}
		/* The vehicles' tolls were paid as they passed; book the town traffic's. */
		if (network_income != 0) SubtractMoneyFromCompany(owner, CommandCost(ExpensesType::Other, -network_income));
	}

	for (Company *c : Company::Iterate()) {
		const size_t i = c->index.base();
		if (compensation[i] != 0) SubtractMoneyFromCompany(c->index, CommandCost(ExpensesType::Other, -compensation[i]));
		if (upkeep[i] != 0) SubtractMoneyFromCompany(c->index, CommandCost(ExpensesType::Property, upkeep[i]));
		c->tolls.income_last_month = income[i];
		c->tolls.compensation_last_month = compensation[i];
	}

	/* Concessions that ran out are extended, for a toll cut. */
	for (auto &[tile, plaza] : _toll_plazas) {
		if (TimerGameEconomy::date < plaza.concession_end) continue;
		plaza.concession_end = plaza.concession_end + TOLL_EXTENSION_YEARS * EconomyTime::DAYS_IN_YEAR;
		plaza.cut = std::min<uint8_t>(TOLL_MAX_CUT, plaza.cut + TOLL_EXTENSION_CUT);
		if (!CanHaveTollPlaza(tile)) continue;
		const Owner owner = GetRoadOwner(tile, RoadTramType::Road);
		const Town *t = ClosestTownFromTile(tile, UINT_MAX);
		if (Company::IsValidID(owner) && t != nullptr) {
			AddNewsItem(GetEncodedString(STR_NEWS_TOLL_CONCESSION_EXTENDED, owner, t->index, plaza.concession_end, TOLL_EXTENSION_CUT), NewsType::Economy, NewsStyle::Normal, {}, tile);
		}
	}

	InvalidateWindowClassesData(WindowClass::Tolls);
}

/** Each month: congestion charges paid by the towns' own drivers, and the statistics. */
static void CongestionChargeMonthlyLoop()
{
	for (Town *t : Town::Iterate()) {
		TownTransit &tr = t->transit;
		if (tr.congestion_charge) {
			/* The town's drivers who still drive into the centre pay too: per thousand people, a car there and back each month. */
			const Money residents = GetPrice(Price::BuildObject, 2, nullptr) * t->cache.population / 1000 * (100 - GetTownCarReduction(t)) / 100;
			tr.charge_this_year += residents;
			tr.charge_income_this_month += residents;
		}
		tr.charged_last_month = tr.charged_this_month;
		tr.charge_income_last_month = tr.charge_income_this_month;
		tr.charged_this_month = 0;
		tr.charge_income_this_month = 0;
		if (TimerGameEconomy::month == 0) {
			tr.charge_last_year = tr.charge_this_year;
			tr.charge_this_year = 0;
		}
	}
}

static const IntervalTimer<TimerGameEconomy> _road_pricing_monthly({TimerGameEconomy::Trigger::Month, TimerGameEconomy::Priority::None}, [](auto)
{
	TollsMonthlyLoop();
	CongestionChargeMonthlyLoop();
	InvalidateWindowClassesData(WindowClass::TransitAuthority);
});

/** Each year: toll freezes, and big congested towns that bring in a congestion charge on their own. */
static const IntervalTimer<TimerGameEconomy> _road_pricing_yearly({TimerGameEconomy::Trigger::Year, TimerGameEconomy::Priority::None}, [](auto)
{
	/* Now and then the government freezes the tolls of a company that has toll plazas (as Malaysia did again and again). */
	std::set<Owner> tolling;
	for (const auto &[tile, plaza] : _toll_plazas) {
		if (CanHaveTollPlaza(tile)) tolling.insert(GetRoadOwner(tile, RoadTramType::Road));
	}
	for (Owner owner : tolling) {
		Company *c = Company::GetIfValid(owner);
		if (c == nullptr || TimerGameEconomy::date < c->tolls.freeze_until || !Chance16(1, 8)) continue;
		c->tolls.freeze_until = TimerGameEconomy::date + TOLL_FREEZE_YEARS * EconomyTime::DAYS_IN_YEAR;
		AddNewsItem(GetEncodedString(STR_NEWS_TOLL_FREEZE, c->index, c->tolls.freeze_until), NewsType::Economy, NewsStyle::Normal, {});
	}

	for (Town *t : Town::Iterate()) {
		if (t->transit.congestion_charge || t->cache.population < CHARGE_OWN_POPULATION) continue;
		if (GetCongestionChargeZoneLevel(t) >= CHARGE_OWN_LEVEL) StartCongestionCharge(t);
	}
	InvalidateWindowClassesData(WindowClass::Tolls);
});
