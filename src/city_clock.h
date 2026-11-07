/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file city_clock.h The optional city clock with rush hours, lines that park vehicles off-peak, and on-demand lines. */

#ifndef CITY_CLOCK_H
#define CITY_CLOCK_H

#include "cargo_type.h"
#include "station_type.h"
#include "vehicle_type.h"

struct OrderList;
struct Vehicle;

/** How long a day of the city clock lasts (setting economy.rush_hours). */
enum class CityClockSetting : uint8_t {
	Off, ///< No city clock: traffic and passengers are the same all day, as in OpenTTD.
	Year, ///< A city day per game year: vehicles keep pace with it.
	Month, ///< A city day per game month: faster to watch, but rush hours last only a few days.
	End, ///< End marker.
};

/** The parts of a city day. */
enum class CityPeriod : uint8_t {
	Night, ///< 00:00 to 06:00.
	MorningPeak, ///< 06:00 to 09:00.
	Day, ///< 09:00 to 16:00.
	EveningPeak, ///< 16:00 to 19:00.
	Evening, ///< 19:00 to 24:00.
	End, ///< End marker.
};

bool IsCityClockOn();
int GetCityMinuteOfDay();
CityPeriod GetCityPeriod();
bool IsRushHour();
uint GetCityPassengerPercent();
uint8_t AdjustCongestionForCityTime(uint8_t level);

void ScalePassengersForCityTime(CargoType cargo, uint &amount);

void ApplyOffPeakVehicles(OrderList *list);
uint CountOffPeakParked(const OrderList *list);
bool CanLineParkBetweenPeaks(const OrderList *list);
bool IsWaitingForPeak(const Vehicle *v);
void ForgetOffPeakVehicle(const Vehicle *v);
void ReplaceOffPeakVehicle(const Vehicle *old_vehicle, const Vehicle *new_vehicle);

bool ChooseOnDemandStop(Vehicle *v);
/* GetOnDemandNextStations() is declared in vehicle_base.h, which uses it. */

void InitializeCityClock();

/** Saveload access. */
std::vector<VehicleID> &GetOffPeakParkedVehicles();

#endif /* CITY_CLOCK_H */
