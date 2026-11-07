/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file council.h Town councils: district, municipal and city councils, the services they build and the estates they run. */

#ifndef COUNCIL_H
#define COUNCIL_H

#include "council_type.h"
#include "city_services_type.h"
#include "company_type.h"
#include "house_type.h"
#include "strings_type.h"
#include "town_type.h"

struct Town;
struct HouseSpec;

/** A town becomes a municipality with this many people (and the income to match). */
static constexpr uint COUNCIL_MUNICIPAL_POPULATION = 4000;
/** A municipality becomes a city with this many people (and the income to match, after some years). */
static constexpr uint COUNCIL_CITY_POPULATION = 12000;
/** Share of the town's taxes at the normal rate that goes into the council's services fund, out of its running costs, in percent. */
static constexpr uint COUNCIL_SERVICES_PERCENT = 10;

bool AreTownCouncilsOn();
CouncilLevel GetCouncilLevel(const Town *t);
StringID GetCouncilPrefix(CouncilLevel level);
StringID GetCouncilKind(CouncilLevel level);
bool IsEstateFlat(const HouseSpec &hs);
uint8_t GetEstatePaint(const Town *t);

Money GetTownYearlyLicences(const Town *t);
Money GetTownYearlyCharges(const Town *t);
Money GetCouncilIncomeNeeded(CouncilLevel level);
Money GetCouncilSurplusLastYear(const Town *t);
Money GetCouncilSpendable(const Town *t);
CityService GetCouncilPlannedService(const Town *t);
bool IsCouncilServiceAvailable(const Town *t, CityService service);
uint32_t GetTownFlatsPopulation(const Town *t);
uint32_t GetTownFlatsAge(const Town *t);
int GetCouncilHappiness(const Town *t);
int GetCouncilLandValueBonus(const Town *t);

StringID GetCofundRefusal(const Town *t, CompanyID company);
Money GetCofundCost(const Town *t);

void UpdateTownCouncilsMonthly();
void RotateTownCouncilYear(Town *t);
void ReviewTownCouncil(Town *t);

#endif /* COUNCIL_H */
