/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file interchange.h Bus interchanges and integrated transport hubs: stations upgraded for many bus lines and for changing to the train. */

#ifndef INTERCHANGE_H
#define INTERCHANGE_H

#include "economy_type.h"
#include "station_type.h"

struct Station;
struct TileInfo;
struct Town;

/** How far a station was upgraded. */
enum class InterchangeLevel : uint8_t {
	None, ///< An ordinary station.
	Interchange, ///< A bus interchange: a roof over its bus stops.
	Hub, ///< An integrated transport hub: an air-conditioned interchange with a railway station.
	End, ///< End marker.
};

/** Bus stops a station needs to be a bus interchange. */
static constexpr uint INTERCHANGE_MIN_BUS_STOPS = 6;

uint CountBusStops(const Station *st);
bool IsActiveInterchange(const Station *st);
bool IsActiveTransportHub(const Station *st);
int GetStationInterchangeRatingBonus(const Station *st);
Money GetInterchangeCost(const Station *st);
Money GetTransportHubCost();
Money GetInterchangeYearlyUpkeep(const Station *st);
Money GetTownShareOfUpgrade(const Town *t, Money cost);
void DrawInterchangeRoof(const TileInfo *ti);

#endif /* INTERCHANGE_H */
