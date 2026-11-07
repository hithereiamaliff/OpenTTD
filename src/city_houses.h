/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file city_houses.h Malaysian and Singaporean town buildings: kampung houses to HDB blocks and towers. */

#ifndef CITY_HOUSES_H
#define CITY_HOUSES_H

#include "house.h"
#include "tile_cmd.h"

struct Town;

/** Which buildings towns build (setting economy.town_building_style). */
enum class TownBuildingStyle : uint8_t {
	Original, ///< OpenTTD's own.
	Malaysian, ///< Malaysian and Singaporean ones, where the climate has them.
	Both, ///< Both.
};

/** Number of Malaysian and Singaporean house types (with the extra parts of the bigger ones). */
static constexpr uint NUM_CITY_HOUSES = 20;
static_assert(CITY_HOUSE_OFFSET + NUM_CITY_HOUSES <= NUM_HOUSES);

void AddCityHouseSpecs();
void DrawCityHouseTile(TileInfo *ti, HouseID house_id);
void DrawCityHouseInGUI(int x, int y, HouseID house_id, int view);
bool IsHouseAllowedByTownStyle(const HouseSpec &hs);
bool CanTownBuildCityHouse(const Town *t, const HouseSpec &hs);

#endif /* CITY_HOUSES_H */
