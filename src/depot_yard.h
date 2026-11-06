/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file depot_yard.h Depot yards: facilities around road depots, home depots and charging. */

#ifndef DEPOT_YARD_H
#define DEPOT_YARD_H

#include "depot_yard_type.h"
#include "command_type.h"
#include "company_type.h"
#include "depot_type.h"
#include "economy_type.h"
#include "strings_type.h"
#include "tile_type.h"

struct ObjectSpec;
struct TileInfo;
struct RoadVehicle;

/** What a depot's yard offers and how many vehicles it serves. Derived from the map and the vehicles, never saved. */
struct DepotYardInfo {
	std::array<uint16_t, DEPOT_FACILITY_COUNT> facilities{}; ///< Number of each facility in the yard.
	uint places = 0; ///< Vehicles that fit in the depot and its yard.
	uint repair_capacity = 0; ///< Vehicles its workshops keep in shape.
	uint fuel_capacity = 0; ///< Vehicles its fuel and wash lanes serve.
	uint solar_capacity = 0; ///< Electric buses its solar roofs power.
	uint staff_capacity = 0; ///< Vehicles its staff buildings serve.
	uint grid = 0; ///< Energy its grid connection gives the chargers, in kWh a day.
	std::vector<uint16_t> chargers; ///< Energy of each charger, in kWh a day, fastest first.

	uint homed = 0; ///< Vehicles based here.
	uint homed_electric = 0; ///< Electric buses based here.
	uint homed_fuel = 0; ///< Other vehicles based here: they need fuel.
};

static constexpr uint DEPOT_BASE_PLACES = 4; ///< Vehicles a depot holds without a yard.
static constexpr uint DEPOT_BASE_REPAIRS = 5; ///< Vehicles a depot's own workshop keeps in shape.
static constexpr uint DEPOT_BASE_STAFF = 10; ///< Vehicles a depot's own staff room serves.
static constexpr uint DEPOT_WALL_CHARGERS = 2; ///< Chargers in every road depot.
static constexpr uint WALL_CHARGER_KWH = 60; ///< What a depot's own charger gives a day.
static constexpr uint SLOW_CHARGER_KWH = 180; ///< What a charger of a charging bay gives a day.
static constexpr uint FAST_CHARGER_KWH = 500; ///< What a charger of a fast charging bay gives a day.
static constexpr uint DEPOT_BASE_GRID = 600; ///< Energy a depot's own grid connection gives its chargers a day.
static constexpr uint SUBSTATION_GRID = 3000; ///< Energy a substation adds a day.
static constexpr uint HOME_DEPOT_REACH = 48; ///< Vehicles go home for service or charging from up to this many tiles away.

StringID GetDepotFacilityName(DepotFacility facility);
StringID GetDepotFacilityTooltip(DepotFacility facility);
Money GetDepotFacilityBuildCost(DepotFacility facility);
Money GetDepotFacilityYearlyUpkeep(DepotFacility facility);
const ObjectSpec *GetDepotFacilityObjectSpec(ObjectType type);
bool IsDepotFacilityObjectSpec(const ObjectSpec *spec);
uint GetDepotFacilityObjectSpecIndex(const ObjectSpec *spec);
CommandCost CheckDepotFacilityLocation(TileIndex tile, Owner owner);

void OnDepotYardsChanged();
void OnDepotHomesChanged();
void InitializeDepotYards();

DepotID GetDepotOfFacility(TileIndex tile);
const DepotYardInfo &GetDepotYard(DepotID depot);
Money GetDepotYardYearlyUpkeep(const DepotYardInfo &yard);

DepotID GetHomeDepot(const RoadVehicle *v);
void SetHomeDepot(RoadVehicle *v, DepotID depot);
void ForgetHomeDepot(DepotID depot);
TileIndex GetHomeDepotTile(const RoadVehicle *v);

uint GetYardOverflowPercent(const DepotYardInfo &yard);
uint GetYardWearPercent(const DepotYardInfo &yard);
uint GetYardFuelDiscountPercent(const DepotYardInfo &yard);
uint GetYardSolarDiscountPercent(const DepotYardInfo &yard);
/** Pay of a company's road vehicle staff, when it has staff budgets (CompanySettings::staff_budget). */
enum StaffBudget : uint8_t {
	SB_OFF, ///< No staff budgets: staff are part of the running costs, and staff buildings don't matter.
	SB_LEAN, ///< Lower pay: cheaper to run, but vehicles wear faster.
	SB_STANDARD, ///< Standard pay.
	SB_GENEROUS, ///< Higher pay: dearer to run, but vehicles are kept in better shape.
};

uint GetYardStaffWearPercent(const DepotYardInfo &yard);
StaffBudget GetStaffBudget(Owner owner);
uint GetHomeDepotRunningCostPercent(const RoadVehicle *v);
uint GetHomeDepotWearPercent(const RoadVehicle *v);

void ChargeBusesInDepots();
bool IsAtDepotCharger(const RoadVehicle *v);

void DrawDepotFacilityTile(TileInfo *ti, ObjectType type);
void ShowDepotYardWindow(DepotID depot);

#endif /* DEPOT_YARD_H */
