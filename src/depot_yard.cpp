/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file depot_yard.cpp Depot yards: facilities around road depots, home depots and charging.
 *
 * A company builds facilities (parking, chargers, a workshop and so on) on
 * tiles next to a road depot, or next to another facility of its yard. Every
 * road vehicle has a home depot: where it was bought, or the depot it was last
 * stopped in. It goes home for service and charging when that isn't far, and
 * its yard decides how many vehicles fit, how fast electric buses charge, how
 * fast vehicles wear and what they cost to run. Yards are worked out from the
 * map and the vehicles, so nothing but the home depots needs saving.
 */

#include "stdafx.h"
#include "depot_yard.h"
#include "city_bus.h"
#include "company_base.h"
#include "company_func.h"
#include "depot_base.h"
#include "depot_map.h"
#include "economy_func.h"
#include "map_func.h"
#include "news_func.h"
#include "newgrf_object.h"
#include "object_base.h"
#include "object_map.h"
#include "road_map.h"
#include "roadveh.h"
#include "tile_cmd.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"
#include "transparency.h"
#include "vehicle_func.h"
#include "viewport_func.h"
#include "window_func.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/** What each depot facility costs and does. */
struct DepotFacilityInfo {
	StringID name; ///< Name of the facility.
	StringID tooltip; ///< What it does, with its costs.
	uint build_cost; ///< Build cost, as a multiple of the base object price.
	uint yearly_upkeep; ///< Upkeep per year, as a multiple of the base object price.
	uint places; ///< Vehicles it holds.
	uint chargers; ///< Chargers in it.
	uint charger_kwh; ///< What each of its chargers gives a day.
	uint grid; ///< Energy it adds to the grid connection a day.
	uint repairs; ///< Vehicles it keeps in shape.
	uint fuel; ///< Vehicles it fuels and washes.
	uint solar; ///< Electric buses it powers.
	uint staff; ///< Vehicles' staff it looks after.
	uint8_t height; ///< Height of the sprite's bounding box, in pixels.
};

static const DepotFacilityInfo _depot_facility_info[DEPOT_FACILITY_COUNT] = {
	/* name                                     tooltip                                            cost upkeep places chargers kWh               grid           repairs fuel solar staff height */
	{STR_DEPOT_FACILITY_BUS_PARK,          STR_DEPOT_FACILITY_BUS_PARK_TOOLTIP,           50,   5,  8, 0, 0,                0,              0,  0, 0,  0,  2},
	{STR_DEPOT_FACILITY_SOLAR_BUS_PARK,    STR_DEPOT_FACILITY_SOLAR_BUS_PARK_TOOLTIP,    150,   8,  8, 0, 0,                0,              0,  0, 6,  0, 15},
	{STR_DEPOT_FACILITY_MULTI_STOREY,      STR_DEPOT_FACILITY_MULTI_STOREY_TOOLTIP,      500,  40, 24, 0, 0,                0,              0,  0, 0,  0, 26},
	{STR_DEPOT_FACILITY_CHARGING_BAY,      STR_DEPOT_FACILITY_CHARGING_BAY_TOOLTIP,      150,  20,  2, 2, SLOW_CHARGER_KWH, 0,              0,  0, 0,  0,  8},
	{STR_DEPOT_FACILITY_FAST_CHARGING_BAY, STR_DEPOT_FACILITY_FAST_CHARGING_BAY_TOOLTIP, 400,  50,  2, 2, FAST_CHARGER_KWH, 0,              0,  0, 0,  0, 16},
	{STR_DEPOT_FACILITY_SUBSTATION,        STR_DEPOT_FACILITY_SUBSTATION_TOOLTIP,        300,  30,  0, 0, 0,                SUBSTATION_GRID, 0,  0, 0,  0, 12},
	{STR_DEPOT_FACILITY_REPAIR_BAY,        STR_DEPOT_FACILITY_REPAIR_BAY_TOOLTIP,        200,  40,  0, 0, 0,                0,             20,  0, 0,  0, 18},
	{STR_DEPOT_FACILITY_WASH_FUEL,         STR_DEPOT_FACILITY_WASH_FUEL_TOOLTIP,         150,  30,  0, 0, 0,                0,              0, 50, 0,  0, 14},
	{STR_DEPOT_FACILITY_STAFF,             STR_DEPOT_FACILITY_STAFF_TOOLTIP,             200,  30,  0, 0, 0,                0,              0,  0, 0, 40, 16},
};

#define ALL LandscapeTypes({LandscapeType::Temperate, LandscapeType::Arctic, LandscapeType::Tropic, LandscapeType::Toyland})
#define M(name, height) {{ObjectClassID::Invalid(), 0}, StandardGRFFileProps{}, AnimationInfo<ObjectAnimationTriggers>{}, name, ALL, 0x11, 0, 50, TimerGameCalendar::Date{}, CalendarTime::MAX_DATE + 1, ObjectFlags{}, ObjectCallbackMasks{}, height, 1, 0, {}}
static const ObjectSpec _depot_facility_specs[DEPOT_FACILITY_COUNT] = {
	M(STR_DEPOT_FACILITY_BUS_PARK, 1),
	M(STR_DEPOT_FACILITY_SOLAR_BUS_PARK, 2),
	M(STR_DEPOT_FACILITY_MULTI_STOREY, 4),
	M(STR_DEPOT_FACILITY_CHARGING_BAY, 1),
	M(STR_DEPOT_FACILITY_FAST_CHARGING_BAY, 2),
	M(STR_DEPOT_FACILITY_SUBSTATION, 2),
	M(STR_DEPOT_FACILITY_REPAIR_BAY, 3),
	M(STR_DEPOT_FACILITY_WASH_FUEL, 2),
	M(STR_DEPOT_FACILITY_STAFF, 2),
};
#undef M
#undef ALL

static constexpr uint OVERFLOW_SURCHARGE = 15; ///< Extra running costs, in percent, when all of a depot's vehicles park on the street.
static constexpr uint WEAR_PENALTY = 50; ///< Faster wear, in percent, when none of a depot's vehicles are looked after.
static constexpr uint FUEL_DISCOUNT = 8; ///< Lower running costs for vehicles fuelled at their depot's own lane, in percent.
static constexpr uint SOLAR_DISCOUNT = 15; ///< Lower running costs for electric buses powered by their depot's solar roofs, in percent.
static constexpr uint STAFF_PENALTY = 25; ///< Faster wear, in percent, when none of a depot's vehicles' staff have room (with staff budgets).

static const DepotFacilityInfo &GetInfo(DepotFacility facility)
{
	return _depot_facility_info[to_underlying(facility)];
}

/**
 * Name of a depot facility.
 * @param facility The facility.
 * @return Its name.
 */
StringID GetDepotFacilityName(DepotFacility facility)
{
	return GetInfo(facility).name;
}

/**
 * Tooltip of a depot facility: what it does; takes its build cost and upkeep.
 * @param facility The facility.
 * @return The tooltip.
 */
StringID GetDepotFacilityTooltip(DepotFacility facility)
{
	return GetInfo(facility).tooltip;
}

/**
 * Cost of building a depot facility.
 * @param facility The facility.
 * @return The cost.
 */
Money GetDepotFacilityBuildCost(DepotFacility facility)
{
	return GetPrice(Price::BuildObject, GetInfo(facility).build_cost, nullptr);
}

/**
 * Yearly upkeep of a depot facility.
 * @param facility The facility.
 * @return The upkeep.
 */
Money GetDepotFacilityYearlyUpkeep(DepotFacility facility)
{
	return GetPrice(Price::BuildObject, GetInfo(facility).yearly_upkeep, nullptr);
}

/**
 * Object spec of a depot facility object type.
 * @param type The object type.
 * @pre IsDepotFacilityObjectType(type)
 * @return The spec.
 */
const ObjectSpec *GetDepotFacilityObjectSpec(ObjectType type)
{
	return &_depot_facility_specs[to_underlying(GetDepotFacilityOfObjectType(type))];
}

/**
 * Is an object spec one of the depot facilities?
 * @param spec The spec.
 * @return True if it is.
 */
bool IsDepotFacilityObjectSpec(const ObjectSpec *spec)
{
	return spec >= std::begin(_depot_facility_specs) && spec < std::end(_depot_facility_specs);
}

/**
 * Object type of a depot facility's object spec.
 * @param spec The spec.
 * @pre IsDepotFacilityObjectSpec(spec)
 * @return The object type.
 */
uint GetDepotFacilityObjectSpecIndex(const ObjectSpec *spec)
{
	return OBJECT_DEPOT_FACILITY_FIRST + static_cast<uint>(spec - std::begin(_depot_facility_specs));
}

/**
 * Is a tile a depot facility of a company?
 * @param tile The tile.
 * @param owner The company.
 * @return True if it is.
 */
static bool IsDepotFacilityTileOf(TileIndex tile, Owner owner)
{
	return IsTileType(tile, TileType::Object) && IsDepotFacilityObjectType(GetObjectType(tile)) && IsTileOwner(tile, owner);
}

/**
 * Can a company build a depot facility here? It must be next to one of its road depots, or next to another of its facilities.
 * @param tile The tile.
 * @param owner The company.
 * @return An error, or success.
 */
CommandCost CheckDepotFacilityLocation(TileIndex tile, Owner owner)
{
	for (DiagDirection dir : EnumRange(DiagDirection::End)) {
		TileIndex near = TileAddByDiagDir(tile, dir);
		if (!IsValidTile(near)) continue;
		if (IsRoadDepotTile(near) && IsTileOwner(near, owner)) return CommandCost();
		if (IsDepotFacilityTileOf(near, owner)) return CommandCost();
	}
	return CommandCost(STR_ERROR_DEPOT_FACILITY_NEXT_TO_DEPOT);
}

/** A facility tile in a yard: its depot, and its place among the yard's facilities of its kind. */
struct YardTile {
	DepotID depot; ///< The depot.
	uint16_t index; ///< Its number among the yard's facilities of the same kind, in the order they were found.
};

static std::vector<DepotYardInfo> _yards; ///< Yards by depot index.
static std::unordered_map<TileIndex, YardTile> _yard_tiles; ///< Facility tiles that belong to a yard.
static bool _yards_dirty = true; ///< Do the yards need working out again?
static bool _homes_dirty = true; ///< Do the counts of vehicles based at each depot need working out again?

/** Facilities or depots were built, removed or changed owner: work the yards out again. */
void OnDepotYardsChanged()
{
	_yards_dirty = true;
	_homes_dirty = true;
	InvalidateWindowClassesData(WindowClass::DepotYard);
}

/** Vehicles were bought or sold, or got a new home: count the vehicles based at each depot again. */
void OnDepotHomesChanged()
{
	_homes_dirty = true;
	InvalidateWindowClassesData(WindowClass::DepotYard);
}

/** Forget the yards; used when starting or loading a game. */
void InitializeDepotYards()
{
	_yards.clear();
	_yard_tiles.clear();
	_yards_dirty = true;
	_homes_dirty = true;
}

/**
 * Work out every depot's yard: facilities belong to the nearest road depot of
 * their owner they connect to, through other facilities.
 */
static void RebuildYards()
{
	_yards.assign(Depot::GetPoolSize(), DepotYardInfo{});
	_yard_tiles.clear();

	/* Every depot's yard starts with what the depot has itself. */
	std::deque<std::pair<TileIndex, DepotID>> queue;
	for (const Depot *d : Depot::Iterate()) {
		if (!IsRoadDepotTile(d->xy) || GetDepotIndex(d->xy) != d->index) continue;
		DepotYardInfo &yard = _yards[d->index.base()];
		yard.places = DEPOT_BASE_PLACES;
		yard.repair_capacity = DEPOT_BASE_REPAIRS;
		yard.staff_capacity = DEPOT_BASE_STAFF;
		yard.grid = DEPOT_BASE_GRID;
		yard.chargers.assign(DEPOT_WALL_CHARGERS, WALL_CHARGER_KWH);
		queue.emplace_back(d->xy, d->index);
	}

	/* Spread out from the depots, one tile at a time, so a facility between two yards goes to the nearer depot. */
	while (!queue.empty()) {
		auto [tile, depot] = queue.front();
		queue.pop_front();
		Owner owner = GetTileOwner(tile);
		for (DiagDirection dir : EnumRange(DiagDirection::End)) {
			TileIndex near = TileAddByDiagDir(tile, dir);
			if (!IsValidTile(near) || !IsDepotFacilityTileOf(near, owner) || _yard_tiles.contains(near)) continue;

			DepotFacility facility = GetDepotFacilityOfObjectType(GetObjectType(near));
			const DepotFacilityInfo &info = GetInfo(facility);
			DepotYardInfo &yard = _yards[depot.base()];
			_yard_tiles[near] = {depot, yard.facilities[to_underlying(facility)]};
			yard.facilities[to_underlying(facility)]++;
			yard.places += info.places;
			yard.repair_capacity += info.repairs;
			yard.fuel_capacity += info.fuel;
			yard.solar_capacity += info.solar;
			yard.staff_capacity += info.staff;
			yard.grid += info.grid;
			for (uint i = 0; i < info.chargers; i++) yard.chargers.push_back(static_cast<uint16_t>(info.charger_kwh));
			queue.emplace_back(near, depot);
		}
	}

	for (DepotYardInfo &yard : _yards) std::ranges::sort(yard.chargers, std::greater{});
	_yards_dirty = false;
	_homes_dirty = true;
}

/** Count the vehicles based at each depot. */
static void RecountHomes()
{
	for (DepotYardInfo &yard : _yards) {
		yard.homed = 0;
		yard.homed_electric = 0;
		yard.homed_fuel = 0;
	}
	for (const RoadVehicle *rv : RoadVehicle::Iterate()) {
		if (!rv->IsFrontEngine() || rv->home_depot == DepotID::Invalid() || rv->home_depot.base() >= _yards.size()) continue;
		DepotYardInfo &yard = _yards[rv->home_depot.base()];
		yard.homed++;
		if (IsElectricBus(rv)) {
			yard.homed_electric++;
		} else {
			yard.homed_fuel++;
		}
	}
	_homes_dirty = false;
}

/**
 * Get a depot's yard, and how many vehicles are based there.
 * @param depot The depot.
 * @return The yard; empty for depots that aren't road depots.
 */
const DepotYardInfo &GetDepotYard(DepotID depot)
{
	static const DepotYardInfo empty{};
	if (_yards_dirty) RebuildYards();
	if (_homes_dirty) RecountHomes();
	if (depot == DepotID::Invalid() || depot.base() >= _yards.size()) return empty;
	return _yards[depot.base()];
}

/**
 * The depot whose yard a facility tile belongs to.
 * @param tile The facility tile.
 * @return The depot, or DepotID::Invalid() if the facility isn't part of a yard.
 */
DepotID GetDepotOfFacility(TileIndex tile)
{
	if (_yards_dirty) RebuildYards();
	auto it = _yard_tiles.find(tile);
	return it == _yard_tiles.end() ? DepotID::Invalid() : it->second.depot;
}

/**
 * Yearly upkeep of a depot's yard.
 * @param yard The yard.
 * @return The upkeep.
 */
Money GetDepotYardYearlyUpkeep(const DepotYardInfo &yard)
{
	Money upkeep = 0;
	for (DepotFacility f : EnumRange(DepotFacility::End)) {
		upkeep += GetDepotFacilityYearlyUpkeep(f) * yard.facilities[to_underlying(f)];
	}
	return upkeep;
}

/**
 * The home depot of a road vehicle.
 * @param v The vehicle (any part).
 * @return The depot, or DepotID::Invalid() if it has none yet.
 */
DepotID GetHomeDepot(const RoadVehicle *v)
{
	return v->First()->home_depot;
}

/**
 * Base a road vehicle at a depot.
 * @param v The vehicle (front).
 * @param depot The depot.
 */
void SetHomeDepot(RoadVehicle *v, DepotID depot)
{
	if (v->home_depot == depot) return;
	v->home_depot = depot;
	OnDepotHomesChanged();
	SetWindowDirty(WindowClass::VehicleDetails, v->index);
}

/**
 * A depot is going: its vehicles lose their home, and find a new one at the next depot they enter.
 * @param depot The depot.
 */
void ForgetHomeDepot(DepotID depot)
{
	for (RoadVehicle *rv : RoadVehicle::Iterate()) {
		if (rv->home_depot == depot) rv->home_depot = DepotID::Invalid();
	}
	OnDepotYardsChanged();
}

/**
 * Tile of a road vehicle's home depot.
 * @param v The vehicle.
 * @return The tile, or INVALID_TILE if it has no home.
 */
TileIndex GetHomeDepotTile(const RoadVehicle *v)
{
	const Depot *d = Depot::GetIfValid(GetHomeDepot(v));
	if (d == nullptr || !IsRoadDepotTile(d->xy) || GetTileOwner(d->xy) != v->owner) return INVALID_TILE;
	return d->xy;
}

/**
 * Extra running costs of vehicles parked on the street because their yard is full.
 * @param yard The yard.
 * @return The extra costs in percent.
 */
uint GetYardOverflowPercent(const DepotYardInfo &yard)
{
	if (yard.homed <= yard.places) return 0;
	return OVERFLOW_SURCHARGE * (yard.homed - yard.places) / yard.homed;
}

/**
 * How much faster vehicles wear when their depot's workshops can't keep up.
 * @param yard The yard.
 * @return The extra wear in percent.
 */
uint GetYardWearPercent(const DepotYardInfo &yard)
{
	if (yard.homed <= yard.repair_capacity) return 0;
	return WEAR_PENALTY * (yard.homed - yard.repair_capacity) / yard.homed;
}

/**
 * How much cheaper fuel-powered vehicles run with their depot's fuel lanes.
 * @param yard The yard.
 * @return The saving in percent.
 */
uint GetYardFuelDiscountPercent(const DepotYardInfo &yard)
{
	if (yard.homed_fuel == 0) return yard.fuel_capacity > 0 ? FUEL_DISCOUNT : 0;
	return FUEL_DISCOUNT * std::min(yard.fuel_capacity, yard.homed_fuel) / yard.homed_fuel;
}

/**
 * How much cheaper electric buses run with their depot's solar roofs.
 * @param yard The yard.
 * @return The saving in percent.
 */
uint GetYardSolarDiscountPercent(const DepotYardInfo &yard)
{
	if (yard.homed_electric == 0) return yard.solar_capacity > 0 ? SOLAR_DISCOUNT : 0;
	return SOLAR_DISCOUNT * std::min(yard.solar_capacity, yard.homed_electric) / yard.homed_electric;
}

/**
 * A company's staff budget.
 * @param owner The company.
 * @return Its staff pay, or #SB_OFF if it has no staff budgets.
 */
StaffBudget GetStaffBudget(Owner owner)
{
	const Company *c = Company::GetIfValid(owner);
	if (c == nullptr || c->settings.staff_budget > SB_GENEROUS) return SB_OFF;
	return static_cast<StaffBudget>(c->settings.staff_budget);
}

/**
 * How much faster vehicles wear when their depot's staff rooms are too small; only with staff budgets.
 * @param yard The yard.
 * @return The extra wear in percent.
 */
uint GetYardStaffWearPercent(const DepotYardInfo &yard)
{
	if (yard.homed <= yard.staff_capacity) return 0;
	return STAFF_PENALTY * (yard.homed - yard.staff_capacity) / yard.homed;
}

/** Running costs (staff are about 40% of them) and wear, in percent, by staff pay. */
static constexpr std::array<std::pair<int, int>, 4> STAFF_PAY_EFFECTS = {{
	{0, 0}, // no staff budgets
	{-6, 25}, // lean: 15% less pay
	{0, 0}, // standard
	{6, -15}, // generous: 15% more pay
}};

/**
 * Running costs of a road vehicle against its standard costs, from its home depot's yard and its company's staff pay.
 * @param v The vehicle (front).
 * @return The running costs in percent.
 */
uint GetHomeDepotRunningCostPercent(const RoadVehicle *v)
{
	int percent = 100 + STAFF_PAY_EFFECTS[GetStaffBudget(v->owner)].first;
	if (v->home_depot == DepotID::Invalid()) return percent;
	const DepotYardInfo &yard = GetDepotYard(v->home_depot);
	percent += GetYardOverflowPercent(yard);
	percent -= IsElectricBus(v) ? GetYardSolarDiscountPercent(yard) : GetYardFuelDiscountPercent(yard);
	return percent;
}

/**
 * How fast a road vehicle wears against the standard, from its home depot's workshops and staff rooms, and its company's staff pay.
 * @param v The vehicle (front).
 * @return The wear in percent.
 */
uint GetHomeDepotWearPercent(const RoadVehicle *v)
{
	const StaffBudget budget = GetStaffBudget(v->owner);
	int percent = 100 + STAFF_PAY_EFFECTS[budget].second;
	if (v->home_depot == DepotID::Invalid()) return percent;
	const DepotYardInfo &yard = GetDepotYard(v->home_depot);
	percent += GetYardWearPercent(yard);
	if (budget != SB_OFF) percent += GetYardStaffWearPercent(yard);
	return percent;
}

/**
 * The electric buses in a depot that want charge, in the order they get a charger:
 * the emptiest batteries first.
 * @param depot_tile The depot.
 * @return The buses (front parts).
 */
static std::vector<RoadVehicle *> GetBusesToCharge(TileIndex depot_tile)
{
	std::vector<RoadVehicle *> buses;
	for (Vehicle *v : VehiclesOnTile(depot_tile)) {
		if (v->type != VehicleType::Road || !v->IsFrontEngine()) continue;
		RoadVehicle *rv = RoadVehicle::From(v);
		if (!IsElectricBus(rv) || !rv->IsChainInDepot() || rv->battery_charge >= GetBatteryCapacityWh(rv)) continue;
		buses.push_back(rv);
	}
	std::ranges::sort(buses, [](const RoadVehicle *a, const RoadVehicle *b) {
		uint pa = GetBatteryPercent(a), pb = GetBatteryPercent(b);
		return pa != pb ? pa < pb : a->index < b->index;
	});
	return buses;
}

/**
 * Is an electric bus at one of its depot's chargers, rather than waiting for one?
 * @param v The bus (front), in a depot.
 * @return True if it is at a charger.
 */
bool IsAtDepotCharger(const RoadVehicle *v)
{
	if (!v->IsChainInDepot()) return false;
	const DepotYardInfo &yard = GetDepotYard(GetDepotIndex(v->tile));
	std::vector<RoadVehicle *> buses = GetBusesToCharge(v->tile);
	auto it = std::ranges::find(buses, v);
	return it != buses.end() && static_cast<size_t>(it - buses.begin()) < yard.chargers.size();
}

/**
 * Charge the electric buses standing in depots, once a day. Each depot has
 * its own two chargers plus those of its charging bays; the buses with the
 * emptiest batteries get the fastest chargers, and together they can't draw
 * more than the depot's grid connection gives.
 */
void ChargeBusesInDepots()
{
	std::set<TileIndex> depots;
	for (const RoadVehicle *rv : RoadVehicle::Iterate()) {
		if (rv->IsFrontEngine() && rv->IsChainInDepot() && IsElectricBus(rv)) depots.insert(rv->tile);
	}

	for (TileIndex depot_tile : depots) {
		const DepotYardInfo &yard = GetDepotYard(GetDepotIndex(depot_tile));
		std::vector<RoadVehicle *> buses = GetBusesToCharge(depot_tile);

		const size_t n = std::min(buses.size(), yard.chargers.size());
		uint total = 0;
		for (size_t i = 0; i < n; i++) total += yard.chargers[i];
		for (size_t i = 0; i < n; i++) {
			RoadVehicle *rv = buses[i];
			uint64_t energy = static_cast<uint64_t>(yard.chargers[i]) * 1000;
			if (total > yard.grid) energy = energy * yard.grid / total;
			if (rv->battery_charge == 0) DeleteVehicleNews(rv->index, AdviceType::BatteryFlat);
			rv->battery_charge = static_cast<uint32_t>(std::min<uint64_t>(GetBatteryCapacityWh(rv), rv->battery_charge + energy));
			SetWindowDirty(WindowClass::VehicleDetails, rv->index);
			SetWindowDirty(WindowClass::VehicleView, rv->index);
		}
	}
}

static const IntervalTimer<TimerGameEconomy> _depot_yards_daily({TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [](auto)
{
	ChargeBusesInDepots();
});

/** Charge the upkeep of the depot facilities, each month. */
static const IntervalTimer<TimerGameEconomy> _depot_yards_monthly({TimerGameEconomy::Trigger::Month, TimerGameEconomy::Priority::None}, [](auto)
{
	std::array<Money, MAX_COMPANIES> upkeep{};
	for (const Object *o : Object::Iterate()) {
		if (!IsDepotFacilityObjectType(o->type)) continue;
		Owner owner = GetTileOwner(o->location.tile);
		if (!Company::IsValidID(owner)) continue;
		upkeep[owner.base()] += GetDepotFacilityYearlyUpkeep(GetDepotFacilityOfObjectType(o->type)) / 12;
	}
	for (Company *c : Company::Iterate()) {
		if (upkeep[c->index.base()] == 0) continue;
		SubtractMoneyFromCompany(c->index, CommandCost(ExpensesType::Property, upkeep[c->index.base()]));
	}
	InvalidateWindowClassesData(WindowClass::DepotYard);
});

/**
 * The vehicles standing in a depot, by unit number.
 * @param depot_tile The depot.
 * @return Their front parts.
 */
static std::vector<const RoadVehicle *> GetParkedVehicles(TileIndex depot_tile)
{
	std::vector<const RoadVehicle *> parked;
	for (const Vehicle *v : VehiclesOnTile(depot_tile)) {
		if (v->type != VehicleType::Road || !v->IsFrontEngine()) continue;
		const RoadVehicle *rv = RoadVehicle::From(v);
		if (rv->IsChainInDepot()) parked.push_back(rv);
	}
	std::ranges::sort(parked, [](const RoadVehicle *a, const RoadVehicle *b) { return a->unitnumber != b->unitnumber ? a->unitnumber < b->unitnumber : a->index < b->index; });
	return parked;
}

/**
 * Draw a parked vehicle on a facility tile, along the X axis.
 * @param ti The tile.
 * @param v The vehicle (front).
 * @param y Distance of its centre line from the tile's north-east edge, in world units.
 */
static void DrawParkedVehicle(const TileInfo *ti, const RoadVehicle *v, int y)
{
	uint total = 0;
	for (const RoadVehicle *u = v; u != nullptr; u = u->Next()) total += u->gcache.cached_veh_length;

	PaletteID pal = GetVehiclePalette(v);
	int x = static_cast<int>(TILE_SIZE / 2) - static_cast<int>(total) / 2;
	for (const RoadVehicle *u = v; u != nullptr; u = u->Next()) {
		int len = u->gcache.cached_veh_length;
		VehicleSpriteSeq seq;
		u->GetImage(Direction::NE, EngineImageType::OnMap, &seq);
		SpriteBounds bounds({static_cast<int8_t>(-len / 2), -1, 0}, {static_cast<uint8_t>(len), 2, 6}, {});
		for (uint i = 0; i < seq.count; i++) {
			AddSortableSpriteToDraw(seq.seq[i].sprite, seq.seq[i].pal != PAL_NONE ? seq.seq[i].pal : pal, ti->x + x + len / 2, ti->y + y, ti->z, bounds);
		}
		x += len;
	}
}

/**
 * Draw the vehicles parked on a facility tile: buses that are charging stand
 * at the charging bays (fast ones first), the others on the bus parks.
 * @param ti The tile.
 * @param facility The facility.
 */
static void DrawParkedVehicles(const TileInfo *ti, DepotFacility facility)
{
	if (_yards_dirty) RebuildYards();
	auto it = _yard_tiles.find(ti->tile);
	if (it == _yard_tiles.end()) return;
	const Depot *d = Depot::GetIfValid(it->second.depot);
	if (d == nullptr) return;
	const DepotYardInfo &yard = _yards[it->second.depot.base()];

	/* The buses at the depot's chargers: the first ones in charging order. */
	std::vector<RoadVehicle *> to_charge = GetBusesToCharge(d->xy);
	if (to_charge.size() > yard.chargers.size()) to_charge.resize(yard.chargers.size());

	std::vector<const RoadVehicle *> charging, parked;
	for (const RoadVehicle *rv : GetParkedVehicles(d->xy)) (std::ranges::find(to_charge, rv) != to_charge.end() ? charging : parked).push_back(rv);

	static constexpr std::array<int, 4> PARK_LANES = {2, 6, 10, 14};
	static constexpr std::array<int, 2> BAY_LANES = {4, 12};
	switch (facility) {
		case DepotFacility::FastChargingBay:
		case DepotFacility::ChargingBay: {
			size_t first = it->second.index * BAY_LANES.size();
			if (facility == DepotFacility::ChargingBay) first += yard.facilities[to_underlying(DepotFacility::FastChargingBay)] * BAY_LANES.size();
			for (size_t i = 0; i < BAY_LANES.size() && first + i < charging.size(); i++) DrawParkedVehicle(ti, charging[first + i], BAY_LANES[i]);
			break;
		}

		case DepotFacility::SolarBusPark:
		case DepotFacility::BusPark: {
			/* Buses charging at the depot's own chargers stand on the parks too. */
			size_t bays = (yard.facilities[to_underlying(DepotFacility::FastChargingBay)] + yard.facilities[to_underlying(DepotFacility::ChargingBay)]) * BAY_LANES.size();
			for (size_t i = bays; i < charging.size(); i++) parked.push_back(charging[i]);
			size_t first = it->second.index * PARK_LANES.size();
			if (facility == DepotFacility::SolarBusPark) first += yard.facilities[to_underlying(DepotFacility::BusPark)] * PARK_LANES.size();
			for (size_t i = 0; i < PARK_LANES.size() && first + i < parked.size(); i++) DrawParkedVehicle(ti, parked[first + i], PARK_LANES[i]);
			break;
		}

		default: break;
	}
}

/** Sprites of the depot yards in city.png, from #SPR_DEPOT_YARD_BASE. */
enum YardSprite : uint8_t {
	YS_PARK_GROUND, ///< Parking lanes on the ground.
	YS_BAY_GROUND, ///< Charging bays on the ground.
	YS_SOLAR_ROOF, ///< Solar roof over a bus park.
	YS_MULTI_STOREY, ///< Multi-storey bus park.
	YS_CHARGERS, ///< Two chargers.
	YS_FAST_CHARGERS, ///< Gantry with two fast chargers.
	YS_SUBSTATION, ///< Substation.
	YS_REPAIR_BAY, ///< Workshop.
	YS_WASH_FUEL, ///< Wash and fuel lane.
	YS_STAFF, ///< Staff building.
};

/**
 * Draw a structure of a depot facility.
 * @param ti The tile.
 * @param sprite The sprite.
 * @param palette The owner's colours.
 * @param bounds Its bounding box.
 */
static void DrawYardStructure(const TileInfo *ti, YardSprite sprite, PaletteID palette, const SpriteBounds &bounds)
{
	if (IsInvisibilitySet(TransparencyOption::Structures)) return;
	SpriteID image = SPR_DEPOT_YARD_BASE + sprite;
	SetBit(image, PALETTE_MODIFIER_COLOUR);
	AddSortableSpriteToDraw(image, palette, *ti, bounds, IsTransparencySet(TransparencyOption::Structures));
}

/**
 * Draw a depot facility tile.
 * @param ti The tile.
 * @param type The object type.
 */
void DrawDepotFacilityTile(TileInfo *ti, ObjectType type)
{
	DepotFacility facility = GetDepotFacilityOfObjectType(type);
	Owner owner = GetTileOwner(ti->tile);
	PaletteID palette = owner == OWNER_NONE ? PAL_NONE : GetCompanyPalette(owner);
	const uint8_t height = GetInfo(facility).height;

	DrawGroundSprite(SPR_CONCRETE_GROUND, PAL_NONE);
	switch (facility) {
		case DepotFacility::BusPark:
			DrawGroundSprite(SPR_DEPOT_YARD_BASE + YS_PARK_GROUND, PAL_NONE);
			break;

		case DepotFacility::SolarBusPark:
			DrawGroundSprite(SPR_DEPOT_YARD_BASE + YS_PARK_GROUND, PAL_NONE);
			/* The roof is above the parked buses. */
			DrawYardStructure(ti, YS_SOLAR_ROOF, palette, SpriteBounds({0, 0, 11}, {TILE_SIZE, TILE_SIZE, 3}, {0, 0, -11}));
			break;

		case DepotFacility::ChargingBay:
			DrawGroundSprite(SPR_DEPOT_YARD_BASE + YS_BAY_GROUND, PAL_NONE);
			/* The chargers stand at the head of the bays, past the buses. */
			DrawYardStructure(ti, YS_CHARGERS, palette, SpriteBounds({13, 0, 0}, {3, TILE_SIZE, 9}, {-13, 0, 0}));
			break;

		case DepotFacility::FastChargingBay:
			DrawGroundSprite(SPR_DEPOT_YARD_BASE + YS_BAY_GROUND, PAL_NONE);
			DrawYardStructure(ti, YS_FAST_CHARGERS, palette, SpriteBounds({13, 0, 0}, {3, TILE_SIZE, 15}, {-13, 0, 0}));
			break;

		case DepotFacility::MultiStoreyPark: DrawYardStructure(ti, YS_MULTI_STOREY, palette, SpriteBounds({0, 0, 0}, {TILE_SIZE, TILE_SIZE, height}, {})); break;
		case DepotFacility::Substation: DrawYardStructure(ti, YS_SUBSTATION, palette, SpriteBounds({0, 0, 0}, {TILE_SIZE, TILE_SIZE, height}, {})); break;
		case DepotFacility::RepairBay: DrawYardStructure(ti, YS_REPAIR_BAY, palette, SpriteBounds({0, 0, 0}, {TILE_SIZE, TILE_SIZE, height}, {})); break;
		case DepotFacility::WashFuelLane: DrawYardStructure(ti, YS_WASH_FUEL, palette, SpriteBounds({0, 0, 0}, {TILE_SIZE, TILE_SIZE, height}, {})); break;
		case DepotFacility::StaffBuilding: DrawYardStructure(ti, YS_STAFF, palette, SpriteBounds({0, 0, 0}, {TILE_SIZE, TILE_SIZE, height}, {})); break;
		default: NOT_REACHED();
	}

	DrawParkedVehicles(ti, facility);
}
