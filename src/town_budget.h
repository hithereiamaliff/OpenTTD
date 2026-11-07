/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file town_budget.h Towns' budgets and ordinances: taxes, spending, reserves, and the rules towns bring in. */

#ifndef TOWN_BUDGET_H
#define TOWN_BUDGET_H

#include "town_budget_type.h"
#include "company_type.h"
#include "strings_type.h"
#include "tile_type.h"
#include "town_type.h"

struct RoadVehicle;
struct Town;

/** Share of its taxes a town spends on running itself (roads, staff, upkeep), in percent. */
static constexpr uint TOWN_RUNNING_SHARE = 60;
/** Share of its taxes a town spends on transit, in percent; more with a transit fund. */
static constexpr uint TOWN_TRANSIT_SHARE = 25;
static constexpr uint TOWN_TRANSIT_FUND_SHARE = 40;

bool AreTownBudgetsOn();
bool HasOrdinance(const Town *t, Ordinance ordinance);
Money GetTownTaxBase(const Town *t);
Money GetTownYearlyTaxes(const Town *t);
uint GetTownTransitSharePercent(const Town *t);
uint GetOrdinanceCostPercent(Ordinance ordinance);
uint GetTownOrdinanceCostPercent(const Town *t);
Money GetTownYearlySpending(const Town *t);

uint GetOrdinanceTrafficPercent(const Town *t, TileIndex tile);
bool IsInLowEmissionZone(const Town *t, TileIndex tile);
bool AnyLowEmissionZones();
void OnRoadVehicleEntersLowEmissionZone(RoadVehicle *front, Town *t, TileIndex from, TileIndex to);
int GetTownBudgetHappiness(const Town *t);
uint GetTownTaxGrowthPercent(const Town *t);

StringID GetOrdinanceName(Ordinance ordinance);
StringID GetOrdinanceRefusal(const Town *t, Ordinance ordinance, CompanyID company);
void UpdateLowEmissionZoneTowns();
void InitializeTownBudgets();
void ShowTownBudgetWindow(TownID town);

#endif /* TOWN_BUDGET_H */
