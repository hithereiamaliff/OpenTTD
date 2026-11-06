/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file stop_charger.h Fast chargers at bus stops: electric buses top up while they stand at the stop. */

#ifndef STOP_CHARGER_H
#define STOP_CHARGER_H

#include "economy_type.h"
#include "tile_type.h"

struct TileInfo;
struct Vehicle;
struct RoadVehicle;

static constexpr uint STOP_CHARGER_KWH = 200; ///< What a charger at a stop gives a bus standing under it, per day.
static constexpr uint STOP_CHARGE_HOLD_PERCENT = 40; ///< An electric bus below this charge waits under a stop's charger until it gets there.

bool HasStopCharger(TileIndex tile);
Money GetStopChargerBuildCost();
Money GetStopChargerYearlyUpkeep();
void OnRoadStopRemoved(TileIndex tile);
void ChargeAtStop(RoadVehicle *v);
bool MustWaitToCharge(const Vehicle *v);
void DrawStopCharger(const TileInfo *ti);
void InitializeStopChargers();

/** Saveload access to the chargers. */
std::set<TileIndex> &GetStopChargers();

#endif /* STOP_CHARGER_H */
