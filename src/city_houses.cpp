/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file city_houses.cpp Malaysian and Singaporean town buildings.
 *
 * Towns in the temperate and sub-tropical climates build the way Malaysian
 * and Singaporean towns grew: kampung houses on stilts and rows of
 * shophouses first; terrace (link) houses from the 1950s, walk-up flats and
 * then HDB slab and point blocks around the new town centres; office blocks
 * from the 1960s, glass towers, shopping malls and mixed developments from
 * the 1980s and 1990s, condominiums, and in the biggest cities a landmark
 * tower. Mosques and Chinese temples stand among them.
 *
 * They are built-in house types with the last house IDs (NewGRF houses get
 * the ones below), drawn with the city fork's sprites. Towns pick them like
 * any house: by zone, year and the value of the land (part 14), so dear
 * land near good stations gets the towers, and as towns rebuild old houses
 * the kampungs give way to terraces and the terraces to flats. The setting
 * "Town buildings" picks these, OpenTTD's or both.
 */

#include "stdafx.h"
#include "city_houses.h"
#include "cargo_type.h"
#include "house.h"
#include "settings_type.h"
#include "sprite.h"
#include "tile_cmd.h"
#include "town.h"
#include "town_map.h"
#include "town_zoning.h"
#include "tile_map.h"
#include "newgrf_house.h"
#include "sprite.h"
#include "transparency.h"
#include "viewport_func.h"
#include "landscape.h"
#include "debug.h"
#include "map_func.h"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/** A house type's look: its sprite, the box it fills and how it is coloured. */
struct CityHouseLook {
	uint8_t sprite; ///< Sprite, from SPR_CITY_HOUSE_BASE.
	uint8_t x0, y0, x1, y1; ///< Footprint, in 1/16 tile.
	uint8_t height; ///< Height in pixels.
	bool grass; ///< On grass instead of paving.
	uint8_t colours; ///< Index into COLOUR_SETS: which facade colours it comes in.
};

/** Facade colours a building comes in, one picked per building; Colours::Invalid for its own colours. */
static constexpr Colours COLOUR_SETS[][8] = {
	{Colours::Invalid, Colours::Invalid, Colours::Invalid, Colours::Invalid, Colours::Invalid, Colours::Invalid, Colours::Invalid, Colours::Invalid}, ///< Its own colours.
	{Colours::Cream, Colours::White, Colours::PaleGreen, Colours::LightBlue, Colours::Cream, Colours::Yellow, Colours::White, Colours::Pink}, ///< Houses: light colours.
	{Colours::PaleGreen, Colours::Yellow, Colours::LightBlue, Colours::Orange, Colours::Pink, Colours::Cream, Colours::Green, Colours::Red}, ///< HDB blocks and shophouses: bright ones.
	{Colours::Blue, Colours::Grey, Colours::DarkGreen, Colours::Brown, Colours::White, Colours::LightBlue, Colours::DarkBlue, Colours::Mauve}, ///< Towers: quieter ones.
};

/**
 * The facade colour of a building.
 * @param look The building's look.
 * @param x X of its northern tile, in world units.
 * @param y Y of its northern tile, in world units.
 * @return The recolour palette, or PAL_NONE.
 */
static PaletteID GetCityHousePalette(const CityHouseLook &look, uint x, uint y)
{
	const Colours colour = COLOUR_SETS[look.colours][GB(TileHash(x, y), 0, 3)];
	return colour == Colours::Invalid ? PAL_NONE : GetColourPalette(colour);
}

/** Looks of the house types, in house ID order from CITY_HOUSE_OFFSET. */
static constexpr CityHouseLook CITY_HOUSE_LOOKS[NUM_CITY_HOUSES] = {
	{ 0, 1, 3, 15, 14, 22, true, 0},   // kampung house
	{ 1, 2, 3, 14, 15, 20, true, 1},   // terrace houses
	{ 2, 2, 3, 14, 13, 34, false, 2},  // walk-up flats
	{ 3, 1, 4, 16, 12, 68, false, 2},  // HDB slab along X, north part
	{ 4, 0, 4, 15, 12, 62, false, 2},  // HDB slab along X, south part
	{ 5, 4, 1, 12, 16, 68, false, 2},  // HDB slab along Y, north part
	{ 6, 4, 0, 12, 15, 62, false, 2},  // HDB slab along Y, south part
	{ 7, 3, 3, 13, 13, 100, false, 2}, // HDB point block
	{ 8, 1, 1, 15, 15, 136, false, 3}, // condominium
	{ 9, 1, 2, 15, 13, 24, false, 2},  // shophouses
	{10, 3, 3, 13, 13, 79, false, 0},  // office block
	{11, 3, 3, 13, 13, 178, false, 0}, // glass office tower
	{12, 1, 1, 16, 16, 26, false, 3},  // shopping mall, north part
	{13, 1, 0, 16, 15, 26, false, 3},  // shopping mall, part at (0, 1)
	{14, 0, 1, 15, 16, 26, false, 3},  // shopping mall, part at (1, 0)
	{15, 0, 0, 15, 15, 26, false, 3},  // shopping mall, south part
	{16, 3, 3, 13, 13, 196, false, 0}, // landmark tower
	{17, 1, 1, 15, 15, 106, false, 3}, // mixed development
	{18, 1, 3, 13, 14, 38, false, 0},  // mosque
	{19, 2, 3, 14, 15, 20, false, 0},  // Chinese temple
};

/** Heights of the three stages of construction, in pixels. */
static constexpr uint8_t CONSTRUCTION_HEIGHTS[] = {3, 22, 64};

/* House specs; fields as in table/town_land.h. */
#define CH(mnd, mxd, pop, rc, bn, rr, mg, ca1, ca2, ca3, bf, ba, prob, life) \
	{TimerGameCalendar::Year{mnd}, TimerGameCalendar::Year{mxd}, pop, rc, bn, rr, mg, \
	{ca1, ca2, ca3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, \
	{INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO, INVALID_CARGO}, \
	bf, ba, true, SubstituteGRFFileProps(INVALID_HOUSE_ID), HouseCallbackMasks{}, {Colours::Begin, Colours::Begin, Colours::Begin, Colours::Begin}, \
	prob, HouseExtraFlags{}, HOUSE_NO_CLASS, AnimationInfo<void>{}, 0, life, CargoTypes{}, {}, {CT_PASSENGERS, CT_MAIL, CT_GOODS}, }

/** The extra part of a bigger building: no size, no people, not built on its own. */
#define CH_PART(bn, rc, rr) CH(0, CalendarTime::MAX_YEAR, 0, rc, bn, rr, 0, 8, 2, 1, {}, {}, 0, 0)

using enum HouseZone;
static constexpr HouseZone TEMP = ClimateTemperate, TROPIC = ClimateSubtropic;

/** The Malaysian and Singaporean house types, in house ID order from CITY_HOUSE_OFFSET. */
static const HouseSpec _city_house_specs[NUM_CITY_HOUSES] = {
	CH(1900, 1985,  14,  70, STR_TOWN_BUILDING_NAME_KAMPUNG_HOUSE,        70,   6, 3, 1, 0, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownEdge, TownOutskirt, TownOuterSuburb}), 16, 0),
	CH(1950, CalendarTime::MAX_YEAR, 36, 90, STR_TOWN_BUILDING_NAME_TERRACE_HOUSES, 80, 14, 4, 1, 0, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownEdge, TownOutskirt, TownOuterSuburb, TownInnerSuburb}), 16, 10),
	CH(1955, CalendarTime::MAX_YEAR, 70, 110, STR_TOWN_BUILDING_NAME_WALK_UP_FLATS, 100, 25, 6, 2, 1, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownOuterSuburb, TownInnerSuburb}), 12, 15),
	CH(1965, CalendarTime::MAX_YEAR, 200, 150, STR_TOWN_BUILDING_NAME_HDB_BLOCK, 150, 60, 8, 3, 1, BuildingFlag::Size2x1,
	   HouseZones({TEMP, TROPIC, TownOuterSuburb, TownInnerSuburb, TownCentre}), 8, 30),
	CH_PART(STR_TOWN_BUILDING_NAME_HDB_BLOCK, 150, 150),
	CH(1965, CalendarTime::MAX_YEAR, 200, 150, STR_TOWN_BUILDING_NAME_HDB_BLOCK, 150, 60, 8, 3, 1, BuildingFlag::Size1x2,
	   HouseZones({TEMP, TROPIC, TownOuterSuburb, TownInnerSuburb, TownCentre}), 8, 30),
	CH_PART(STR_TOWN_BUILDING_NAME_HDB_BLOCK, 150, 150),
	CH(1975, CalendarTime::MAX_YEAR, 160, 160, STR_TOWN_BUILDING_NAME_HDB_POINT_BLOCK, 150, 50, 8, 3, 1, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownOuterSuburb, TownInnerSuburb, TownCentre}), 8, 30),
	CH(1990, CalendarTime::MAX_YEAR, 180, 180, STR_TOWN_BUILDING_NAME_CONDOMINIUM, 160, 55, 8, 3, 2, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownInnerSuburb, TownCentre}), 6, 30),
	CH(1900, CalendarTime::MAX_YEAR, 60, 100, STR_TOWN_BUILDING_NAME_SHOPHOUSES, 100, 35, 6, 2, 3, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownOuterSuburb, TownInnerSuburb, TownCentre}), 16, 0),
	CH(1965, CalendarTime::MAX_YEAR, 140, 150, STR_TOWN_BUILDING_NAME_OFFICE_BLOCK_KL, 140, 60, 8, 3, 4, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownInnerSuburb, TownCentre}), 10, 25),
	CH(1985, CalendarTime::MAX_YEAR, 220, 200, STR_TOWN_BUILDING_NAME_GLASS_TOWER, 180, 80, 8, 3, 4, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownCentre}), 6, 40),
	CH(1985, CalendarTime::MAX_YEAR, 240, 220, STR_TOWN_BUILDING_NAME_SHOPPING_MALL, 220, 70, 8, 3, 8, BuildingFlag::Size2x2,
	   HouseZones({TEMP, TROPIC, TownInnerSuburb, TownCentre}), 4, 30),
	CH_PART(STR_TOWN_BUILDING_NAME_SHOPPING_MALL, 220, 220),
	CH_PART(STR_TOWN_BUILDING_NAME_SHOPPING_MALL, 220, 220),
	CH_PART(STR_TOWN_BUILDING_NAME_SHOPPING_MALL, 220, 220),
	CH(1998, CalendarTime::MAX_YEAR, 255, 255, STR_TOWN_BUILDING_NAME_LANDMARK_TOWER, 255, 100, 8, 4, 4, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownCentre}), 4, 60),
	CH(1995, CalendarTime::MAX_YEAR, 200, 200, STR_TOWN_BUILDING_NAME_MIXED_DEVELOPMENT, 170, 70, 8, 3, 4, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownInnerSuburb, TownCentre}), 4, 30),
	CH(1900, CalendarTime::MAX_YEAR, 6, 120, STR_TOWN_BUILDING_NAME_MOSQUE, 230, 2, 2, 0, 0, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownEdge, TownOutskirt, TownOuterSuburb, TownInnerSuburb}), 3, 60),
	CH(1900, CalendarTime::MAX_YEAR, 5, 120, STR_TOWN_BUILDING_NAME_CHINESE_TEMPLE, 230, 2, 2, 0, 0, BuildingFlag::Size1x1,
	   HouseZones({TEMP, TROPIC, TownOuterSuburb, TownInnerSuburb, TownCentre}), 3, 60),
};

#undef CH
#undef CH_PART

/**
 * Town population from which (and up to which) towns build each house type, in
 * house ID order: towns develop from kampungs and shophouses to HDB estates,
 * towers and malls as they grow.
 */
static constexpr std::pair<uint32_t, uint32_t> CITY_HOUSE_POPULATION[NUM_CITY_HOUSES] = {
	{0, 20000}, // kampung house
	{0, UINT32_MAX}, // terrace houses
	{0, UINT32_MAX}, // walk-up flats
	{1000, UINT32_MAX}, {0, UINT32_MAX}, // HDB slab along X
	{1000, UINT32_MAX}, {0, UINT32_MAX}, // HDB slab along Y
	{2500, UINT32_MAX}, // HDB point block
	{4000, UINT32_MAX}, // condominium
	{0, UINT32_MAX}, // shophouses
	{1500, UINT32_MAX}, // office block
	{8000, UINT32_MAX}, // glass office tower
	{6000, UINT32_MAX}, {0, UINT32_MAX}, {0, UINT32_MAX}, {0, UINT32_MAX}, // shopping mall
	{40000, UINT32_MAX}, // landmark tower
	{12000, UINT32_MAX}, // mixed development
	{0, UINT32_MAX}, // mosque
	{0, UINT32_MAX}, // Chinese temple
};

/** The landmark tower: one in a town, and only in a big one. */
static constexpr HouseID LANDMARK_TOWER = CITY_HOUSE_OFFSET + 16;
static constexpr uint LANDMARK_POPULATION = 40000;
/** Mosques and temples: one for each so many people. */
static constexpr HouseID MOSQUE = CITY_HOUSE_OFFSET + 18;
static constexpr HouseID CHINESE_TEMPLE = CITY_HOUSE_OFFSET + 19;
static constexpr uint PEOPLE_PER_PLACE_OF_WORSHIP = 6000;

/** Put the Malaysian and Singaporean house types in the house list, after the room kept for NewGRF houses. */
void AddCityHouseSpecs()
{
	std::vector<HouseSpec> &specs = HouseSpec::Specs();
	specs.resize(CITY_HOUSE_OFFSET);
	specs.insert(specs.end(), std::begin(_city_house_specs), std::end(_city_house_specs));
}

/**
 * Draw a Malaysian or Singaporean house tile.
 * @param ti The tile.
 * @param house_id Its house type.
 */
void DrawCityHouseTile(TileInfo *ti, HouseID house_id)
{
	const CityHouseLook &look = CITY_HOUSE_LOOKS[house_id - CITY_HOUSE_OFFSET];
	const uint8_t stage = GetHouseBuildingStage(ti->tile);
	const bool done = stage == TOWN_HOUSE_COMPLETED;

	if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, Foundation::Leveled);
	DrawGroundSprite(done && look.grass ? SPR_FLAT_GRASS_TILE : SPR_CONCRETE_GROUND, PAL_NONE);
	if (IsInvisibilitySet(TransparencyOption::Houses)) return;

	SpriteID image;
	PaletteID palette = PAL_NONE;
	SpriteBounds bounds({static_cast<int8_t>(look.x0), static_cast<int8_t>(look.y0), 0}, {static_cast<uint8_t>(look.x1 - look.x0), static_cast<uint8_t>(look.y1 - look.y0), look.height}, {0, 0, 0});
	if (done) {
		image = SPR_CITY_HOUSE_BASE + look.sprite;
		/* All parts of a bigger building in the colour of its northern tile. */
		HouseID north = house_id;
		const TileIndex base = ti->tile + GetHouseNorthPart(north);
		palette = GetCityHousePalette(look, TileX(base) * TILE_SIZE, TileY(base) * TILE_SIZE);
		if (palette != PAL_NONE) SetBit(image, PALETTE_MODIFIER_COLOUR);
	} else {
		image = SPR_CITY_HOUSE_CONSTRUCTION + std::min<uint8_t>(stage, 2);
		bounds = SpriteBounds({2, 2, 0}, {12, 12, CONSTRUCTION_HEIGHTS[std::min<uint8_t>(stage, 2)]}, {0, 0, 0});
	}
	AddSortableSpriteToDraw(image, palette, *ti, bounds, IsTransparencySet(TransparencyOption::Houses));
}

/**
 * Draw a Malaysian or Singaporean house in a window, finished.
 * @param x Position x of the image.
 * @param y Position y of the image.
 * @param house_id The house type.
 * @param view The house's 'view' (picks the facade colour).
 */
void DrawCityHouseInGUI(int x, int y, HouseID house_id, int view)
{
	const CityHouseLook &look = CITY_HOUSE_LOOKS[house_id - CITY_HOUSE_OFFSET];
	DrawSprite(look.grass ? SPR_FLAT_GRASS_TILE : SPR_CONCRETE_GROUND, PAL_NONE, x, y);
	SpriteID image = SPR_CITY_HOUSE_BASE + look.sprite;
	const Colours colour = COLOUR_SETS[look.colours][view & 7];
	const PaletteID palette = colour == Colours::Invalid ? PAL_NONE : GetColourPalette(colour);
	if (palette != PAL_NONE) SetBit(image, PALETTE_MODIFIER_COLOUR);
	DrawSprite(image, palette, x, y);
}

/**
 * Do the towns of this game build a house type, by the setting "Town buildings"?
 * Where the climate has the Malaysian and Singaporean buildings and towns
 * build only those, OpenTTD's own homes and businesses stay away, and its
 * statues and fountains, but not its other public buildings (churches,
 * parks, stadiums), nor those that take food or water (towns in the desert
 * need them). NewGRF houses are always built: the player chose them.
 * @param hs The house type.
 * @return True if towns build it.
 */
bool IsHouseAllowedByTownStyle(const HouseSpec &hs)
{
	const auto style = static_cast<TownBuildingStyle>(_settings_game.economy.town_building_style);
	const HouseID id = hs.Index();
	if (IsCityHouse(id)) return style != TownBuildingStyle::Original;
	if (style != TownBuildingStyle::Malaysian || id >= NEW_HOUSE_OFFSET) return true;
	const LandscapeType climate = _settings_game.game_creation.landscape;
	if (climate != LandscapeType::Temperate && climate != LandscapeType::Tropic) return true;
	if (GetHouseKind(hs) == HouseKind::Public) {
		/* Statues and fountains: the city services' parks and the mosques and temples do instead. */
		return hs.building_name != STR_TOWN_BUILDING_NAME_STATUE_1 && hs.building_name != STR_TOWN_BUILDING_NAME_FOUNTAIN_1;
	}
	for (const CargoLabel &label : hs.accepts_cargo_label) {
		if (label == CT_FOOD || label == CT_WATER) return true;
	}
	return false;
}

/**
 * May a town build a Malaysian or Singaporean building now? Taller ones come
 * as towns grow: HDB slab blocks from 1,000 people, office blocks from 1,500,
 * HDB point blocks from 2,500, condominiums from 4,000, malls from 6,000,
 * glass towers from 8,000, mixed developments from 12,000 and a landmark
 * tower (only one) from 40,000; no new kampungs in a town of more than
 * 20,000; a mosque or a Chinese temple once for each 6,000 people. Beyond
 * that they go by zone, like OpenTTD's: the bigger the town, the bigger its
 * centre.
 * @param t The town.
 * @param hs The house type.
 * @return True if it may.
 */
bool CanTownBuildCityHouse(const Town *t, const HouseSpec &hs)
{
	const HouseID id = hs.Index();
	if (!IsCityHouse(id)) return true;
	const auto &counts = t->cache.building_counts.id_count;
	const uint have = id < counts.size() ? counts[id] : 0;
	const auto [lowest, highest] = CITY_HOUSE_POPULATION[id - CITY_HOUSE_OFFSET];
	if (t->cache.population < lowest || t->cache.population > highest) return false;
	if (id == LANDMARK_TOWER) return have == 0 && t->cache.population >= LANDMARK_POPULATION;
	if (id == MOSQUE || id == CHINESE_TEMPLE) return have < 1 + t->cache.population / PEOPLE_PER_PLACE_OF_WORSHIP;
	return true;
}

/** For testing: once a year, with misc debugging at trace level 1, count the houses of each kind on the map. */
static const IntervalTimer<TimerGameCalendar> _city_houses_census({TimerGameCalendar::Trigger::Year, TimerGameCalendar::Priority::None}, [](auto)
{
	if (!IsVisibleSeverity(Facility::Misc, Severity::Trace1)) return;
	std::array<uint, NUM_CITY_HOUSES> count{};
	uint original = 0;
	for (const auto tile : Map::Iterate()) {
		if (!IsTileType(tile, TileType::House)) continue;
		const HouseSpec *hs = HouseSpec::Get(GetHouseType(tile));
		if (!hs->building_flags.Any(BUILDING_HAS_1_TILE)) continue;
		if (IsCityHouse(hs->Index())) {
			count[hs->Index() - CITY_HOUSE_OFFSET]++;
		} else {
			original++;
		}
	}
	std::string line;
	for (uint i = 0; i < NUM_CITY_HOUSES; i++) line += fmt::format(" {}", count[i]);
	Debug(Facility::Misc, Severity::Trace1, "[houses] year {} original {} city{}", TimerGameCalendar::year, original, line);
	std::vector<const Town *> towns;
	for (const Town *t : Town::Iterate()) towns.push_back(t);
	std::ranges::sort(towns, [](const Town *a, const Town *b) { return a->cache.population > b->cache.population; });
	for (size_t i = 0; i < std::min<size_t>(3, towns.size()); i++) {
		Debug(Facility::Misc, Severity::Trace1, "[houses] town {} pop {} at {} {}", towns[i]->index, towns[i]->cache.population, TileX(towns[i]->xy), TileY(towns[i]->xy));
	}
});
