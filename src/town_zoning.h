/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file town_zoning.h Zoning: players plan what towns may build where.
 *
 * Every tile can be zoned for homes, shops and offices, industry or parks.
 * Towns only build matching buildings on zoned tiles. Industrial and park
 * zones stay free of town buildings and new town roads. Public buildings such
 * as churches fit in both residential and commercial zones. Unzoned land works
 * as in regular OpenTTD.
 */

#ifndef TOWN_ZONING_H
#define TOWN_ZONING_H

#include "gfx_type.h"
#include "tile_type.h"
#include "town_type.h"

struct HouseSpec;

/** What the player planned a tile for. */
enum class TownZone : uint8_t {
	None, ///< Not zoned: towns build as usual.
	Residential, ///< Homes and public buildings only.
	Commercial, ///< Businesses and public buildings only.
	Industrial, ///< Kept free of town buildings and roads, for industry.
	Park, ///< Kept free of town buildings and roads.
	End, ///< End marker.
};

/** Zoned tiles; unzoned tiles are left out. */
extern std::unordered_map<TileIndex, TownZone> _town_zones;

/** What kind of building a town house is, as zoning sees it. */
enum class HouseKind : uint8_t {
	Home, ///< Houses and flats.
	Business, ///< Shops and offices, hotels, theatres, cinemas and the like.
	Public, ///< Churches, stadiums, statues, fountains and parks.
};

TownZone GetTownZone(TileIndex tile);
bool IsTownZoneKeptFree(TileIndex tile);
HouseKind GetHouseKind(const HouseSpec &hs);
bool TownZoneAllowsHouse(TownZone zone, const HouseSpec &hs);
bool TownZonesAllowHouse(TileIndex tile, const HouseSpec &hs);

/** Homes and jobs in a town, for the zoning demand. */
struct TownZoneDemand {
	uint residents = 0; ///< People living in homes.
	uint jobs = 0; ///< People working in businesses.
	int commercial = 0; ///< Demand for shops and offices: positive wants more, negative has too many (-100 to 100).
};
TownZoneDemand GetTownZoneDemand(const Town *t);

PaletteID GetTownZonePalette(TownZone zone);
bool IsTownZoneOverlayShown();
void ShowTownZoneOverlay(bool show);
void ShowTownZoningToolbar();

void InitializeTownZones();
void AfterLoadTownZones();

#endif /* TOWN_ZONING_H */
