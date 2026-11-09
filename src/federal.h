/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file federal.h The federal government: its national programmes, its five-year plan of projects and the transit agencies of the federal territories. */

#ifndef FEDERAL_H
#define FEDERAL_H

#include "state_type.h"
#include "strings_type.h"

struct RoadVehicle;
struct Town;

/** Under a Transit First federal government every town's transit budget gets this share of its taxes on top, in percent. */
static constexpr uint FEDERAL_TRANSIT_GRANT_PERCENT = 5;
/** Under Transit First the federal government pays this share of the monthly passes riders buy, in percent. */
static constexpr uint FEDERAL_PASS_SUBSIDY_PERCENT = 50;
/** Under Roads and Growth the fuel subsidy takes this share off the running costs of road vehicles that burn fuel, in percent. */
static constexpr uint FEDERAL_FUEL_SUBSIDY_PERCENT = 20;
/** Under Roads and Growth towns everywhere grow this much faster: the time between new houses, less this, in percent. */
static constexpr uint FEDERAL_GROWTH_PERCENT = 5;
/** Under Homes and Services the federal government builds a hospital in a town of this many people without one, each year. */
static constexpr uint FEDERAL_HOSPITAL_POPULATION = 6000;
/** ... or else a university in a town of this many people without one. */
static constexpr uint FEDERAL_UNIVERSITY_POPULATION = 12000;
/** A federal territory's transit agency adds this share of its taxes to its transit budget, in percent, from federal money. */
static constexpr uint FEDERAL_AGENCY_BUDGET_PERCENT = 25;
/** A federal transit agency invites tenders this many times as often as a town. */
static constexpr uint FEDERAL_AGENCY_TENDER_FACTOR = 3;
/** A federal transit agency picks railway and BRT lines for its tenders this many times as readily as other lines. */
static constexpr uint FEDERAL_AGENCY_LINE_WEIGHT = 4;
/** Each project of the plan a company delivered gives the federal government this many votes in every seat at the next general election. */
static constexpr int FEDERAL_PROJECT_VOTES = 2;
/** The most votes delivered projects give. */
static constexpr int FEDERAL_PROJECT_VOTES_MAX = 8;
/** A line delivered for a project runs at least this many times a month from its first stop. */
static constexpr uint FEDERAL_LINE_DEPARTURES = 8;
/** A metro line for a project stops at this many stations of the town or more. */
static constexpr uint FEDERAL_METRO_STATIONS = 4;
/** A BRT line for a project stops at this many stations of the town or more. */
static constexpr uint FEDERAL_BRT_STOPS = 3;

Platform GetFederalGovernment();
uint GetFederalTransitGrantPercent(const Town *t);
uint GetFederalAgencyBudgetPercent(const Town *t);
bool HasFederalTransitAgency(const Town *t);
bool IsFederalPassSubsidyOn();
void OnFederalPassSubsidy(Money paid);
uint GetFederalFuelCostPercent(const RoadVehicle *v);
uint GetFederalGrowthPercent(const Town *t);
std::string GetFederalProgrammeText(Platform platform);

std::vector<FederalProject> &GetFederalProjects();
std::string GetFederalProjectName(const FederalProject &p);
StringID GetFederalProjectNeed(FederalProjectType type);
uint CountFederalProjects(FederalProjectState state);
int GetFederalProjectVotes();
void StartFederalPlan();
void UpdateFederalMonthly();
void UpdateFederalYearly();
void InitializeFederal();

void ShowFederalPlanWindow();

#endif /* FEDERAL_H */
