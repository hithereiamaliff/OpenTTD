/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file city_bus.h Built-in modern buses of the city fork: diesel and battery-electric models, batteries and charging. */

#ifndef CITY_BUS_H
#define CITY_BUS_H

#include "engine_type.h"
#include "newgrf_type.h"
#include "strings_type.h"

struct Engine;
struct RoadVehicle;

/**
 * Pseudo NewGRF ID under which the built-in city buses are kept in the engine map.
 * NewGRF vehicle sets can only take over unreserved slots of the original vehicles,
 * so the city buses stay available next to them.
 */
static const GrfID CITY_ENGINE_GRFID{"\xFF" "CTY"};
static constexpr uint16_t CITY_ENGINE_FIRST_ID = 88; ///< Internal id of the first city bus, right after the 88 original road vehicles.
static constexpr uint8_t CITY_ENGINE_COUNT = 14; ///< Number of city bus engines, articulated rear sections included.
static constexpr uint8_t CITY_ENGINE_NONE = UINT8_MAX; ///< Engine::city_index of engines that are no city bus.

/** What the game knows about a built-in city bus beyond the engine tables. */
struct CityBusSpec {
	StringID name; ///< Model name, also used for articulated rear sections (never shown).
	uint16_t battery_kwh; ///< Battery size when new, 0 for diesel buses.
	uint16_t wh_per_tile; ///< Energy used per tile driven, without air-con or heating.
	uint8_t doors; ///< Number of doors, for the purchase list.
	uint8_t rear; ///< City index of the articulated rear section, or #CITY_ENGINE_NONE.
};

static constexpr uint BATTERY_SEEK_DEPOT_PERCENT = 25; ///< Below this charge an electric bus heads for a depot.
static constexpr uint BATTERY_LEAVE_DEPOT_PERCENT = 95; ///< An electric bus in a depot stays until it is charged this far.
static constexpr uint BATTERY_FADE_PERCENT_PER_YEAR = 2; ///< Battery capacity lost each year of age.
static constexpr uint BATTERY_MIN_HEALTH_PERCENT = 60; ///< Batteries never fade below this.
static constexpr uint FLAT_BATTERY_SPEED = 30; ///< Top speed with a flat battery, in internal road vehicle units (15 km/h).

const CityBusSpec *GetCityBusSpec(const Engine *e);
const CityBusSpec *GetCityBusSpec(EngineID engine);
EngineID GetCityEngineRearPart(EngineID front);

uint GetBatteryCapacityKwh(EngineID engine);
bool IsHvacHeating();
uint GetHvacLoadPercent();
uint GetBatteryRangeTiles(EngineID engine, uint health_percent);

bool IsElectricBus(const RoadVehicle *v);
uint GetBatteryHealthPercent(const RoadVehicle *v);
uint32_t GetBatteryCapacityWh(const RoadVehicle *v);
uint GetBatteryPercent(const RoadVehicle *v);
uint GetBatteryTilesLeft(const RoadVehicle *v);
bool IsBatteryFlat(const RoadVehicle *v);
bool IsChargingInDepot(const RoadVehicle *v);

void InitBattery(RoadVehicle *v);
void UseBattery(RoadVehicle *v, uint steps);

#endif /* CITY_BUS_H */
