/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file environment.h Air pollution and land value: what traffic, vehicles and industry do to the air, and what land is worth. */

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "gfx_type.h"
#include "strings_type.h"
#include "tile_type.h"

struct HouseSpec;
struct Town;

/** How strong air pollution is (setting economy.environment); Off switches pollution and land value off. */
enum class EnvironmentSetting : uint8_t {
	Off, ///< No pollution and no land value.
	Light, ///< Less pollution.
	Normal, ///< Normal pollution.
	Heavy, ///< More pollution.
};

/** What the main view shows over the land. */
enum class EnvironmentOverlay : uint8_t {
	None, ///< Nothing.
	Pollution, ///< Air pollution.
	LandValue, ///< Land value.
	Commuters, ///< Commuters' cars on the roads (see commuting.cpp).
};

/** The air pollutant index above which the air is unhealthy (Malaysia's API, Singapore's PSI). */
static constexpr uint API_UNHEALTHY = 100;
/** Highest land value. */
static constexpr uint LAND_VALUE_MAX = 100;

bool IsEnvironmentOn();
bool IsEnvironmentKnown();
uint GetTilePollution(TileIndex tile);
uint GetTileLandValue(TileIndex tile);
StringID GetAirQualityName(uint api);
StringID GetLandValueName(uint value);
PixelColour GetPollutionColour(uint api);
PixelColour GetLandValueColour(uint value);

uint GetTownPollutionUnhappiness(const Town *t);
uint GetTownPollutionGrowthPenalty(const Town *t);
uint ScaleHouseProbabilityByLandValue(uint probability, const HouseSpec &hs, TileIndex tile);

void ShowEnvironmentOverlay(EnvironmentOverlay overlay);
EnvironmentOverlay GetEnvironmentOverlay();
PaletteID GetEnvironmentOverlayPalette(TileIndex tile);

void OnEnvironmentSettingChanged();
void InitializeEnvironment();

#endif /* ENVIRONMENT_H */
