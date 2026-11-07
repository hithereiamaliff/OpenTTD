/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file metro.h Metro and rail: platform crowding, platform screen doors and driverless lines. */

#ifndef METRO_H
#define METRO_H

#include "economy_type.h"
#include "station_type.h"
#include "strings_type.h"
#include "vehicle_type.h"

struct OrderList;
struct Station;
struct TileInfo;
struct Train;

/** Passengers who fit comfortably on one tile of railway platform. */
static constexpr uint PLATFORM_PLACES_PER_TILE = 120;
/** Passengers who fit at one bus or tram stop of a station that has railway platforms. */
static constexpr uint PLATFORM_PLACES_PER_ROAD_STOP = 30;
/** Above this crowding (in percent of the places) a platform counts as crowded. */
static constexpr uint PLATFORM_CROWDED = 100;
/** Running costs of a driverless train, in percent of the same train with a driver. */
static constexpr uint DRIVERLESS_RUNNING_COST_PERCENT = 80;
/** Boarding speed of a driverless train (automatic doors, precise stops), in percent. */
static constexpr uint DRIVERLESS_BOARDING_PERCENT = 110;

uint CountPlatformTiles(const Station *st);
uint GetStationPlatformPlaces(const Station *st);
uint GetStationPassengersWaiting(const Station *st);
uint GetStationPlatformCrowding(const Station *st);
void UpdateStationCrowding(Station *st);
int GetPlatformCrowdingRatingPenalty(const Station *st);
uint GetTrainBoardingPercent(const Vehicle *v);

Money GetScreenDoorsCost(const Station *st);
Money GetScreenDoorsYearlyUpkeep(const Station *st);
void DrawScreenDoors(const TileInfo *ti);

bool IsTrainLine(const OrderList *list);
StringID GetDriverlessProblem(const OrderList *list);
Money GetDriverlessFittingCost(const OrderList *list);
uint GetLineStopsWithScreenDoors(const OrderList *list, uint *stops);
uint GetTrainRunningCostPercent(const Train *front);
void OnTrainEntersLevelCrossing(Train *front);
void MetroMonthlyLoop();

#endif /* METRO_H */
