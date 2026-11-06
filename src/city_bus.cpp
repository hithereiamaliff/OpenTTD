/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file city_bus.cpp Built-in modern buses of the city fork: models, batteries and charging. */

#include "stdafx.h"
#include "city_bus.h"
#include "engine_base.h"
#include "roadveh.h"
#include "news_func.h"
#include "company_func.h"
#include "strings_func.h"
#include "window_func.h"
#include "settings_type.h"
#include "timer/timer_game_calendar.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * The city buses, in the order of the engine tables in table/engines.h.
 * A tile counts as about 2 km for the batteries: a 12 m bus that does 1.1 kWh per km
 * uses 2.2 kWh per tile, so a new 350 kWh battery lasts about 160 tiles.
 */
static constexpr CityBusSpec _city_bus_specs[] = {
	/* name                                  battery Wh/tile doors rear */
	{ STR_CITY_BUS_TANJUNG_MINIBUS,             0,     0, 1, CITY_ENGINE_NONE }, //  0
	{ STR_CITY_BUS_SERI_M9,                     0,     0, 2, CITY_ENGINE_NONE }, //  1
	{ STR_CITY_BUS_KENARI_CITY_12,              0,     0, 2, CITY_ENGINE_NONE }, //  2
	{ STR_CITY_BUS_MERBOK_TWIN_DECKER,          0,     0, 2, CITY_ENGINE_NONE }, //  3
	{ STR_CITY_BUS_KENARI_BENDI_18,             0,     0, 3, 5                }, //  4
	{ STR_CITY_BUS_KENARI_BENDI_18,             0,     0, 3, CITY_ENGINE_NONE }, //  5 rear section
	{ STR_CITY_BUS_TANJUNG_E_MINIBUS,         110,   660, 1, CITY_ENGINE_NONE }, //  6
	{ STR_CITY_BUS_SERI_M9E,                  230,  1500, 2, CITY_ENGINE_NONE }, //  7
	{ STR_CITY_BUS_KENARI_CITY_12E,           350,  2200, 2, CITY_ENGINE_NONE }, //  8
	{ STR_CITY_BUS_MERBOK_TWIN_DECKER_E,      450,  2800, 2, CITY_ENGINE_NONE }, //  9
	{ STR_CITY_BUS_KENARI_BENDI_18E,          500,  3200, 3, 11               }, // 10
	{ STR_CITY_BUS_KENARI_BENDI_18E,            0,     0, 3, CITY_ENGINE_NONE }, // 11 rear section
	{ STR_CITY_BUS_LAJU_BRT_18E,              500,  3100, 4, 13               }, // 12
	{ STR_CITY_BUS_LAJU_BRT_18E,                0,     0, 4, CITY_ENGINE_NONE }, // 13 rear section
};
static_assert(lengthof(_city_bus_specs) == CITY_ENGINE_COUNT);

/**
 * Get the city bus model of an engine.
 * @param e The engine, may be nullptr.
 * @return The model, or nullptr if the engine is no city bus.
 */
const CityBusSpec *GetCityBusSpec(const Engine *e)
{
	if (e == nullptr || !e->IsCityEngine()) return nullptr;
	return &_city_bus_specs[e->city_index];
}

/**
 * Get the city bus model of an engine.
 * @param engine The engine.
 * @return The model, or nullptr if the engine is no city bus.
 */
const CityBusSpec *GetCityBusSpec(EngineID engine)
{
	return GetCityBusSpec(Engine::GetIfValid(engine));
}

/**
 * Get the rear section of an articulated city bus.
 * @param front The engine of the front section.
 * @return The engine of the rear section, or EngineID::Invalid() if the bus has none.
 */
EngineID GetCityEngineRearPart(EngineID front)
{
	const CityBusSpec *spec = GetCityBusSpec(front);
	if (spec == nullptr || spec->rear == CITY_ENGINE_NONE) return EngineID::Invalid();
	return _engine_mngr.GetID(VehicleType::Road, CITY_ENGINE_FIRST_ID + spec->rear, CITY_ENGINE_GRFID);
}

/**
 * Get the battery size of a bus model.
 * @param engine The engine.
 * @return Battery size when new in kWh, 0 if the engine has no battery.
 */
uint GetBatteryCapacityKwh(EngineID engine)
{
	const CityBusSpec *spec = GetCityBusSpec(engine);
	return spec == nullptr ? 0 : spec->battery_kwh;
}

/**
 * Is today's air-con or heating load heating?
 * @return True if buses heat, false if they cool (or do neither).
 */
bool IsHvacHeating()
{
	switch (_settings_game.game_creation.landscape) {
		case LandscapeType::Arctic: return true;
		case LandscapeType::Temperate: return TimerGameCalendar::month == 11 || TimerGameCalendar::month <= 1;
		default: return false;
	}
}

/**
 * Extra energy electric buses use for air-con or heating, from the climate and the time of year.
 * Air-con adds 13-29% in the tropics; heating costs more in a cold winter.
 * @return The extra energy in percent.
 */
uint GetHvacLoadPercent()
{
	const uint month = TimerGameCalendar::month; // 0 is January.
	switch (_settings_game.game_creation.landscape) {
		case LandscapeType::Tropic: return 20; // Air-con all year.
		case LandscapeType::Arctic: return (month >= 10 || month <= 2) ? 25 : 5; // Heating from November to March.
		case LandscapeType::Temperate:
			if (month >= 5 && month <= 7) return 10; // Air-con from June to August.
			if (month == 11 || month <= 1) return 15; // Heating from December to February.
			return 0;
		default: return 0;
	}
}

/**
 * Energy a bus uses per tile today.
 * @param spec The bus model.
 * @return Wh per tile, air-con or heating included.
 */
static uint GetWhPerTile(const CityBusSpec *spec)
{
	return spec->wh_per_tile * (100 + GetHvacLoadPercent()) / 100;
}

/**
 * How far a bus model gets on a full battery today.
 * @param engine The engine.
 * @param health_percent What is left of the battery, in percent.
 * @return The range in tiles, 0 if the engine has no battery.
 */
uint GetBatteryRangeTiles(EngineID engine, uint health_percent)
{
	const CityBusSpec *spec = GetCityBusSpec(engine);
	if (spec == nullptr || spec->battery_kwh == 0) return 0;
	return static_cast<uint>(static_cast<uint64_t>(spec->battery_kwh) * 10 * health_percent / GetWhPerTile(spec));
}

/**
 * Get the model of a bus if it runs on a battery.
 * @param v The bus, any section.
 * @return The model, or nullptr if the bus is no electric city bus.
 */
static const CityBusSpec *GetElectricBusSpec(const RoadVehicle *v)
{
	const CityBusSpec *spec = GetCityBusSpec(v->First()->engine_type);
	return (spec != nullptr && spec->battery_kwh != 0) ? spec : nullptr;
}

/**
 * Does a bus run on a battery?
 * @param v The bus, any section.
 * @return True for the electric city buses.
 */
bool IsElectricBus(const RoadVehicle *v)
{
	return GetElectricBusSpec(v) != nullptr;
}

/**
 * What is left of a bus's battery after fading with age.
 * @param v The bus (front section).
 * @return The health in per mille of the size when new.
 */
static uint GetBatteryHealthPermille(const RoadVehicle *v)
{
	const int64_t days = std::max<int64_t>(0, v->First()->age.base());
	const int64_t fade = days * BATTERY_FADE_PERCENT_PER_YEAR * 10 / CalendarTime::DAYS_IN_YEAR;
	return static_cast<uint>(std::max<int64_t>(BATTERY_MIN_HEALTH_PERCENT * 10, 1000 - fade));
}

/**
 * What is left of a bus's battery after fading with age.
 * @param v The bus.
 * @return The health in percent of the size when new.
 */
uint GetBatteryHealthPercent(const RoadVehicle *v)
{
	return GetBatteryHealthPermille(v) / 10;
}

/**
 * How much a bus's battery holds now.
 * @param v The bus.
 * @return The capacity in Wh, 0 for buses without a battery.
 */
uint32_t GetBatteryCapacityWh(const RoadVehicle *v)
{
	const CityBusSpec *spec = GetElectricBusSpec(v);
	if (spec == nullptr) return 0;
	return spec->battery_kwh * GetBatteryHealthPermille(v); // kWh times per mille is Wh.
}

/**
 * How full a bus's battery is.
 * @param v The bus.
 * @return The charge in percent of what the battery holds now.
 */
uint GetBatteryPercent(const RoadVehicle *v)
{
	const uint32_t capacity = GetBatteryCapacityWh(v);
	if (capacity == 0) return 0;
	return static_cast<uint>(std::min<uint64_t>(100, static_cast<uint64_t>(v->First()->battery_charge) * 100 / capacity));
}

/**
 * How far a bus gets on what is left in its battery, today.
 * @param v The bus.
 * @return The distance in tiles.
 */
uint GetBatteryTilesLeft(const RoadVehicle *v)
{
	const CityBusSpec *spec = GetElectricBusSpec(v);
	if (spec == nullptr) return 0;
	return v->First()->battery_charge / GetWhPerTile(spec);
}

/**
 * Has a bus run its battery flat?
 * @param v The bus.
 * @return True for electric buses without charge.
 */
bool IsBatteryFlat(const RoadVehicle *v)
{
	return v->First()->battery_charge == 0 && IsElectricBus(v);
}

/**
 * Is a bus waiting in a depot for its battery to charge?
 * @param v The bus (front section).
 * @return True if it may not leave yet.
 */
bool IsChargingInDepot(const RoadVehicle *v)
{
	return IsElectricBus(v) && v->IsChainInDepot() && GetBatteryPercent(v) < BATTERY_LEAVE_DEPOT_PERCENT;
}

/**
 * Give a new bus a full battery.
 * @param v The new bus (front section).
 */
void InitBattery(RoadVehicle *v)
{
	v->battery_charge = GetBatteryCapacityWh(v);
}

/**
 * Take the energy for driving from a bus's battery.
 * @param v The bus (front section).
 * @param steps Steps driven; a tile has 16.
 */
void UseBattery(RoadVehicle *v, uint steps)
{
	const CityBusSpec *spec = GetElectricBusSpec(v);
	if (spec == nullptr || v->battery_charge == 0) return;

	const uint32_t use = (GetWhPerTile(spec) * steps + 8) / 16;
	if (use < v->battery_charge) {
		v->battery_charge -= use;
		return;
	}

	v->battery_charge = 0;
	if (v->owner == _local_company) {
		AddVehicleAdviceNewsItem(AdviceType::BatteryFlat, GetEncodedString(STR_NEWS_VEHICLE_BATTERY_FLAT, v->index), v->index);
	}
	SetWindowDirty(WindowClass::VehicleDetails, v->index);
}

/**
 * Charge a bus standing in a depot, once a day.
 * @param v The bus (front section), in a depot.
 */
void ChargeBatteryInDepot(RoadVehicle *v)
{
	const uint32_t capacity = GetBatteryCapacityWh(v);
	if (capacity == 0 || v->battery_charge >= capacity) return;

	if (v->battery_charge == 0) DeleteVehicleNews(v->index, AdviceType::BatteryFlat);
	v->battery_charge = static_cast<uint32_t>(std::min<uint64_t>(capacity, static_cast<uint64_t>(v->battery_charge) + DEPOT_CHARGE_KWH_PER_DAY * 1000));
	SetWindowDirty(WindowClass::VehicleDetails, v->index);
	SetWindowDirty(WindowClass::VehicleView, v->index);
}
