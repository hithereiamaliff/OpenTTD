/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file fare.h Passenger fares.
 *
 * Every company sets fares per transport type (bus, tram, train, ship,
 * aircraft): how they are worked out (OpenTTD's classic payment, a distance
 * fare, a flat fare, zones or free), a price level, what students, seniors
 * and disabled riders pay, and an optional monthly pass. A line can have its
 * own fare. A journey pays one fare however often the passengers change.
 * Riders weigh the price: each station keeps what riders who boarded there
 * paid against what OpenTTD's classic fare would have been, and its passenger
 * rating rises for cheap fares and falls for dear ones. The defaults pay
 * exactly as OpenTTD does.
 */

#ifndef FARE_H
#define FARE_H

#include "cargo_type.h"
#include "company_type.h"
#include "fare_type.h"
#include "station_type.h"

struct Station;
struct Vehicle;

FareMode GetFareMode(const Vehicle *v);
FarePolicy GetVehicleFare(const Vehicle *front);
uint GetPassUptake(uint8_t pass);
uint GetFarePayingShare(const FarePolicy &fare);
Money GetFare(const FarePolicy &fare, uint count, uint distance, uint16_t periods, CargoType cargo, bool other_town);
Money GetFareExample(const FarePolicy &fare, uint distance, bool other_town);
Money GetPassengerIncome(const Vehicle *front, uint count, uint distance, uint16_t periods, CargoType cargo, StationID first_station, const Station *at);
int GetFareRatingBonus(uint level);
int GetStationFareRatingBonus(const Station *st);

void OnFarePaid(StationID first_station, Money paid, Money standard);
void FaresMonthlyLoop();

void ShowFaresWindow(CompanyID company);

#endif /* FARE_H */
