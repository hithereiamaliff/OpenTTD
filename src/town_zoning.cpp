/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file town_zoning.cpp Zoning: players plan what towns may build where. */

#include "stdafx.h"
#include "town_zoning.h"
#include "town_zoning_cmd.h"
#include "command_func.h"
#include "house.h"
#include "map_func.h"
#include "tilearea_type.h"
#include "town.h"
#include "viewport_func.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

std::unordered_map<TileIndex, TownZone> _town_zones;

/** Largest area that can be zoned in one go, per side. */
static constexpr uint MAX_ZONE_AREA_SIDE = 128;

/**
 * What is a tile zoned for?
 * @param tile The tile.
 * @return The zone, or TownZone::None.
 */
TownZone GetTownZone(TileIndex tile)
{
	if (_town_zones.empty()) return TownZone::None;
	auto it = _town_zones.find(tile);
	return it == _town_zones.end() ? TownZone::None : it->second;
}

/**
 * Is land zoned to stay free of the town: no town buildings and no new town roads?
 * That is land zoned for industry or parks.
 * @param tile The tile.
 * @return True if towns leave the tile alone.
 */
bool IsTownZoneKeptFree(TileIndex tile)
{
	TownZone zone = GetTownZone(tile);
	return zone == TownZone::Industrial || zone == TownZone::Park;
}

/**
 * What kind of building is one of the original house types?
 * @param name The name of the original house type.
 * @return The kind.
 */
static HouseKind GetOriginalHouseKind(StringID name)
{
	static const StringID homes[] = {
		STR_TOWN_BUILDING_NAME_SMALL_BLOCK_OF_FLATS_1, STR_TOWN_BUILDING_NAME_TOWN_HOUSES_1,
		STR_TOWN_BUILDING_NAME_OLD_HOUSES_1, STR_TOWN_BUILDING_NAME_COTTAGES_1,
		STR_TOWN_BUILDING_NAME_HOUSES_1, STR_TOWN_BUILDING_NAME_HOUSES_2,
		STR_TOWN_BUILDING_NAME_FLATS_1, STR_TOWN_BUILDING_NAME_IGLOO_1,
		STR_TOWN_BUILDING_NAME_TEPEES_1, STR_TOWN_BUILDING_NAME_TEAPOT_HOUSE_1,
	};
	static const StringID public_buildings[] = {
		STR_TOWN_BUILDING_NAME_CHURCH_1, STR_TOWN_BUILDING_NAME_STATUE_1,
		STR_TOWN_BUILDING_NAME_FOUNTAIN_1, STR_TOWN_BUILDING_NAME_PARK_1,
		STR_TOWN_BUILDING_NAME_STADIUM_1, STR_TOWN_BUILDING_NAME_STADIUM_2,
	};
	if (std::ranges::find(homes, name) != std::end(homes)) return HouseKind::Home;
	if (std::ranges::find(public_buildings, name) != std::end(public_buildings)) return HouseKind::Public;
	/* Office blocks, shops and offices, hotels, the warehouse, theatre, cinema, shopping centre and piggy-bank. */
	return HouseKind::Business;
}

/**
 * What kind of building is a house type: a home, a business or a public building?
 * Going by what goods a house takes doesn't work: flats take some goods too.
 * @param hs The house type.
 * @return The kind.
 */
HouseKind GetHouseKind(const HouseSpec &hs)
{
	/* Every house from a NewGRF names the original house type it stands in for when
	 * the NewGRF is missing. That is the closest match, so it tells the kind too. */
	if (hs.grf_prop.HasGrfFile()) {
		if (hs.grf_prop.subst_id >= NEW_HOUSE_OFFSET) return HouseKind::Home;
		return GetOriginalHouseKind(HouseSpec::Get(hs.grf_prop.subst_id)->building_name);
	}
	return GetOriginalHouseKind(hs.building_name);
}

/**
 * May a town put (part of) this house type on land with this zone?
 * @param zone The zone of the land.
 * @param hs The house type.
 * @return True if the zone allows it.
 */
bool TownZoneAllowsHouse(TownZone zone, const HouseSpec &hs)
{
	switch (zone) {
		case TownZone::None: return true;
		case TownZone::Residential: return GetHouseKind(hs) != HouseKind::Business;
		case TownZone::Commercial: return GetHouseKind(hs) != HouseKind::Home;
		default: return false;
	}
}

/**
 * Do the zones of all tiles a house would cover allow it?
 * @param tile The northern tile of the house.
 * @param hs The house type.
 * @return True if every tile's zone allows it.
 */
bool TownZonesAllowHouse(TileIndex tile, const HouseSpec &hs)
{
	if (_town_zones.empty()) return true;
	if (!TownZoneAllowsHouse(GetTownZone(tile), hs)) return false;
	if (hs.building_flags.Any(BUILDING_2_TILES_Y) && !TownZoneAllowsHouse(GetTownZone(tile + TileDiffXY(0, 1)), hs)) return false;
	if (hs.building_flags.Any(BUILDING_2_TILES_X) && !TownZoneAllowsHouse(GetTownZone(tile + TileDiffXY(1, 0)), hs)) return false;
	if (hs.building_flags.Any(BUILDING_HAS_4_TILES) && !TownZoneAllowsHouse(GetTownZone(tile + TileDiffXY(1, 1)), hs)) return false;
	return true;
}

/**
 * How many people live and work in a town, and does it want more shops and offices?
 * About one job for every two residents is in balance. People in public buildings
 * count for neither.
 * @param t The town.
 * @return The demand.
 */
TownZoneDemand GetTownZoneDemand(const Town *t)
{
	TownZoneDemand demand;
	for (const HouseSpec &hs : HouseSpec::Specs()) {
		if (hs.population == 0 || hs.Index() >= t->cache.building_counts.id_count.size()) continue;
		uint people = hs.population * t->cache.building_counts.id_count[hs.Index()];
		switch (GetHouseKind(hs)) {
			case HouseKind::Home: demand.residents += people; break;
			case HouseKind::Business: demand.jobs += people; break;
			case HouseKind::Public: break;
		}
	}

	int wanted_jobs = demand.residents / 2;
	int diff = wanted_jobs - static_cast<int>(demand.jobs);
	int scale = std::max(wanted_jobs, static_cast<int>(demand.jobs));
	demand.commercial = scale == 0 ? 0 : Clamp(diff * 100 / scale, -100, 100);
	return demand;
}

/**
 * Colour of a zone on the map.
 * @param zone The zone.
 * @return The recolour palette for the tile overlay.
 */
PaletteID GetTownZonePalette(TownZone zone)
{
	switch (zone) {
		case TownZone::Residential: return PALETTE_TO_GREEN;
		case TownZone::Commercial: return PALETTE_TO_LIGHT_BLUE;
		case TownZone::Industrial: return PALETTE_TO_YELLOW;
		case TownZone::Park: return PALETTE_TO_DARK_GREEN;
		default: return PAL_NONE;
	}
}

/** Whether zones are shown in the main view. */
static bool _show_town_zones = false;

/**
 * Are zones shown in the main view?
 * @return True if they are.
 */
bool IsTownZoneOverlayShown()
{
	return _show_town_zones;
}

/**
 * Show or hide the zones in the main view.
 * @param show Whether to show them.
 */
void ShowTownZoneOverlay(bool show)
{
	if (_show_town_zones == show) return;
	_show_town_zones = show;
	MarkWholeScreenDirty();
}

/**
 * Zone an area for homes, shops and offices, industry or parks, or remove its zoning.
 * @param flags Operation to perform.
 * @param tile One corner of the area.
 * @param start_tile The other corner of the area.
 * @param zone The zone, or TownZone::None to remove zoning.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetTownZone(DoCommandFlags flags, TileIndex tile, TileIndex start_tile, TownZone zone)
{
	if (!IsValidTile(tile) || !IsValidTile(start_tile) || zone >= TownZone::End) return CMD_ERROR;

	TileArea area(start_tile, tile);
	if (area.w > MAX_ZONE_AREA_SIDE || area.h > MAX_ZONE_AREA_SIDE) return CommandCost(STR_ERROR_ZONE_AREA_TOO_LARGE);

	if (flags.Test(DoCommandFlag::Execute)) {
		for (TileIndex t : area) {
			if (!IsValidTile(t)) continue;
			if (zone == TownZone::None) {
				_town_zones.erase(t);
			} else {
				_town_zones[t] = zone;
			}
			if (_show_town_zones) MarkTileDirtyByTile(t);
		}
	}

	return CommandCost();
}

/** Remove all zoning; used when starting or loading a game. */
void InitializeTownZones()
{
	_town_zones.clear();
	_show_town_zones = false;
}

/** After loading a game, drop zones outside the map. */
void AfterLoadTownZones()
{
	for (auto it = _town_zones.begin(); it != _town_zones.end(); /* nothing */) {
		if (it->first >= Map::Size() || it->second == TownZone::None || it->second >= TownZone::End) {
			it = _town_zones.erase(it);
		} else {
			++it;
		}
	}
}
