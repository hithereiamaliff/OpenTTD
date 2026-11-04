/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file city_services.h City services: police, fire, health, schools and parks.
 *
 * Companies build city services as objects near towns. Each covers the houses
 * within its radius. Once a month every town works out how much of its
 * population is covered by each service; coverage makes the town happier,
 * which makes it grow faster. The owner pays upkeep every month and gains
 * standing with the town the service belongs to. Towns without services work
 * as in regular OpenTTD.
 */

#ifndef CITY_SERVICES_H
#define CITY_SERVICES_H

#include "city_services_type.h"
#include "economy_type.h"
#include "gfx_type.h"
#include "strings_type.h"
#include "tile_type.h"
#include "town_type.h"

struct ObjectSpec;
struct TileInfo;

/** Happiness of a town without any city services, in percent. */
static constexpr uint TOWN_HAPPINESS_BASE = 40;

uint GetCityServiceRadius(CityService service);
Money GetCityServiceBuildCost(CityService service);
Money GetCityServiceYearlyUpkeep(CityService service);
StringID GetCityServiceName(CityService service);
StringID GetCityServiceShortName(CityService service);
SpriteID GetCityServiceIcon(CityService service);
PaletteID GetCityServicePalette(CityService service);

const ObjectSpec *GetCityServiceObjectSpec(ObjectType type);
bool IsCityServiceObjectSpec(const ObjectSpec *spec);
uint GetCityServiceObjectSpecIndex(const ObjectSpec *spec);

uint GetTownHappiness(const Town *t);
uint GetTownServiceGrowthBonus(const Town *t);
uint8_t GetCityServicesCoveringTile(TileIndex tile);

void DrawCityServiceTile(TileInfo *ti, ObjectType type);
void OnCityServicesChanged();
void UpdateCityServicesMonthly();
void InitializeCityServices();

void ShowCityServicesToolbar();
void ShowCityServiceOverlay(CityService service);
CityService GetCityServiceOverlay();

#endif /* CITY_SERVICES_H */
