/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file city_services.cpp City services: police, fire, health, schools and parks;
 * hospitals, universities, stadiums and hawker centres.
 *
 * The bigger services of the second part serve a whole town: a hospital's
 * reach counts as health care, a university's as schooling, so they raise
 * the same coverage (and happiness, growth and land value) as clinics and
 * schools, over a much wider area. Stadiums and hawker centres reach their
 * own, and add a little happiness. Unlike the first ones, the bigger
 * services draw people: they send passengers (visitors, students, shoppers;
 * crowds on a stadium's match days) to stations within reach, take
 * passengers in, and their jobs draw commuters.
 */

#include "stdafx.h"
#include "satisfaction.h"
#include "city_services.h"
#include "council.h"
#include "environment.h"
#include "town_budget.h"
#include "company_base.h"
#include "company_func.h"
#include "core/backup_type.hpp"
#include "economy_func.h"
#include "gfx_func.h"
#include "house.h"
#include "tile_cmd.h"
#include "map_func.h"
#include "newgrf_object.h"
#include "object_base.h"
#include "tilearea_type.h"
#include "town.h"
#include "object_map.h"
#include "cargotype.h"
#include "timer/timer_game_economy.h"
#include "transparency.h"
#include "viewport_func.h"
#include "window_func.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/** What makes one kind of city service. */
struct CityServiceInfo {
	StringID name; ///< Name of the building.
	StringID short_name; ///< Name of the service in lists, like "police".
	uint radius; ///< Houses within this many tiles are covered.
	uint build_cost; ///< Build cost, as a multiple of the base object price.
	uint yearly_upkeep; ///< Upkeep per year, as a multiple of the base object price.
	uint8_t height; ///< Height of the sprite's bounding box, in pixels.
	PaletteID overlay; ///< Colour of its coverage in the main view.
	uint8_t size; ///< Tiles along each side: 1 or 2.
	uint8_t passengers; ///< Passengers it sends, like a house with this many people on each tile; 0 for none.
	uint8_t jobs; ///< Jobs on each tile, for commuters.
	uint8_t sprite; ///< First sprite, from SPR_CIVIC_SERVICE_BASE (bigger services only).
};

/** The city services; costs are multiples of the base object price (about one pound at the start). */
static const CityServiceInfo _city_service_info[CITY_SERVICE_COUNT] = {
	{STR_CITY_SERVICE_POLICE, STR_CITY_SERVICE_POLICE_SHORT,          10,  750, 150, 30, PALETTE_TO_BLUE,   1,   0, 30,  0},
	{STR_CITY_SERVICE_FIRE, STR_CITY_SERVICE_FIRE_SHORT,              10,  750, 150, 42, PALETTE_TO_RED,    1,   0, 30,  0},
	{STR_CITY_SERVICE_HEALTH, STR_CITY_SERVICE_HEALTH_SHORT,          10,  900, 180, 30, PALETTE_TO_WHITE,  1,   0, 30,  0},
	{STR_CITY_SERVICE_EDUCATION, STR_CITY_SERVICE_EDUCATION_SHORT,    10,  750, 150, 24, PALETTE_TO_ORANGE, 1,   0, 30,  0},
	{STR_CITY_SERVICE_PARK, STR_CITY_SERVICE_PARK_SHORT,               5,  125,  25, 16, PALETTE_TO_GREEN,  1,   0, 30,  0},
	{STR_CITY_SERVICE_HOSPITAL, STR_CITY_SERVICE_HOSPITAL_SHORT,      22, 6000, 900, 46, PALETTE_TO_PINK,   2,  70, 60,  0},
	{STR_CITY_SERVICE_UNIVERSITY, STR_CITY_SERVICE_UNIVERSITY_SHORT,  22, 5000, 700, 40, PALETTE_TO_PURPLE, 2, 110, 45,  4},
	{STR_CITY_SERVICE_STADIUM, STR_CITY_SERVICE_STADIUM_SHORT,        16, 4000, 400, 41, PALETTE_TO_YELLOW, 2,  30, 12,  8},
	{STR_CITY_SERVICE_MARKET, STR_CITY_SERVICE_MARKET_SHORT,           8,  600, 100, 15, PALETTE_TO_CREAM,  1,  60, 40, 12},
};
static_assert(std::size(_city_service_info) == CITY_SERVICE_COUNT);

/** Climate all. */
#define ALL LandscapeTypes({LandscapeType::Temperate, LandscapeType::Arctic, LandscapeType::Tropic, LandscapeType::Toyland})
/** Object spec of a city service; like _original_objects, see table/object_land.h. */
#define M(name, height, size) {{ObjectClassID::Invalid(), 0}, StandardGRFFileProps{}, AnimationInfo<ObjectAnimationTriggers>{}, name, ALL, size, 0, 50, TimerGameCalendar::Date{}, CalendarTime::MAX_DATE + 1, ObjectFlags{}, ObjectCallbackMasks{}, height, 1, 0, {}}
/** The object specs of the city services, in CityService order. */
static const ObjectSpec _city_service_specs[CITY_SERVICE_COUNT] = {
	M(STR_CITY_SERVICE_POLICE, 4, 0x11),
	M(STR_CITY_SERVICE_FIRE, 6, 0x11),
	M(STR_CITY_SERVICE_HEALTH, 4, 0x11),
	M(STR_CITY_SERVICE_EDUCATION, 3, 0x11),
	M(STR_CITY_SERVICE_PARK, 2, 0x11),
	M(STR_CITY_SERVICE_HOSPITAL, 6, 0x22),
	M(STR_CITY_SERVICE_UNIVERSITY, 5, 0x22),
	M(STR_CITY_SERVICE_STADIUM, 5, 0x22),
	M(STR_CITY_SERVICE_MARKET, 2, 0x11),
};
#undef M
#undef ALL

/**
 * Coverage of the map: per tile, one bit per city service that reaches it.
 * Empty while there are no city services. Derived from the objects on the
 * map only, so rebuilding it gives the same result on every client.
 */
static std::vector<uint16_t> _service_cover;
static bool _service_cover_dirty = true; ///< Does #_service_cover need rebuilding?

/** The city service whose coverage the main view shows, or CityService::End for none. */
static CityService _service_overlay = CityService::End;

/**
 * How far does a city service reach?
 * @param service The service.
 * @return Houses within this many tiles (in a square around it) are covered.
 */
uint GetCityServiceRadius(CityService service)
{
	return _city_service_info[to_underlying(service)].radius;
}

/**
 * What does building a city service cost?
 * @param service The service.
 * @return The cost.
 */
Money GetCityServiceBuildCost(CityService service)
{
	return GetPrice(Price::BuildObject, _city_service_info[to_underlying(service)].build_cost, nullptr);
}

/**
 * What does a city service cost to run for a year? It is charged monthly.
 * @param service The service.
 * @return The yearly upkeep.
 */
Money GetCityServiceYearlyUpkeep(CityService service)
{
	return GetPrice(Price::BuildObject, _city_service_info[to_underlying(service)].yearly_upkeep, nullptr);
}

/**
 * Name of the building of a city service, like "Police station".
 * @param service The service.
 * @return The name.
 */
StringID GetCityServiceName(CityService service)
{
	return _city_service_info[to_underlying(service)].name;
}

/**
 * Name of a city service in lists, like "police".
 * @param service The service.
 * @return The name.
 */
StringID GetCityServiceShortName(CityService service)
{
	return _city_service_info[to_underlying(service)].short_name;
}

/**
 * Icon of a city service.
 * @param service The service.
 * @return The sprite.
 */
SpriteID GetCityServiceIcon(CityService service)
{
	if (to_underlying(service) >= BASIC_CITY_SERVICE_COUNT) return SPR_IMG_CIVIC_SERVICE_BASE + (to_underlying(service) - BASIC_CITY_SERVICE_COUNT);
	return SPR_IMG_CITY_SERVICE_BASE + to_underlying(service);
}

/**
 * Tiles along each side of a city service's building.
 * @param service The service.
 * @return 1 or 2.
 */
uint GetCityServiceSize(CityService service)
{
	return _city_service_info[to_underlying(service)].size;
}

/**
 * Jobs on each tile of a city service, for commuters.
 * @param service The service.
 * @return The jobs.
 */
uint GetCityServiceJobs(CityService service)
{
	return _city_service_info[to_underlying(service)].jobs;
}

/**
 * Colour of a city service's coverage in the main view.
 * @param service The service.
 * @return The recolour palette for the tile overlay.
 */
PaletteID GetCityServicePalette(CityService service)
{
	return _city_service_info[to_underlying(service)].overlay;
}

/**
 * Object spec of a city service object type.
 * @param type The object type.
 * @pre IsCityServiceObjectType(type)
 * @return The spec.
 */
const ObjectSpec *GetCityServiceObjectSpec(ObjectType type)
{
	return &_city_service_specs[to_underlying(GetCityServiceOfObjectType(type))];
}

/**
 * Is an object spec one of the city services?
 * @param spec The spec.
 * @return True if it is.
 */
bool IsCityServiceObjectSpec(const ObjectSpec *spec)
{
	return spec >= std::begin(_city_service_specs) && spec < std::end(_city_service_specs);
}

/**
 * Object type of a city service's object spec.
 * @param spec The spec.
 * @pre IsCityServiceObjectSpec(spec)
 * @return The object type.
 */
uint GetCityServiceObjectSpecIndex(const ObjectSpec *spec)
{
	return GetObjectTypeOfCityService(static_cast<CityService>(spec - std::begin(_city_service_specs)));
}

/**
 * How happy is a town? Without city services it is #TOWN_HAPPINESS_BASE;
 * full coverage by all of them makes it 100. Riders' satisfaction with its
 * stations adds or takes up to 10 points; polluted air takes up to 15; taxes
 * and ordinances add or take a few.
 * @param t The town.
 * @return Happiness in percent.
 */
uint GetTownHappiness(const Town *t)
{
	/* Hospitals and universities count for health and education: their reach is in those percentages. */
	uint sum = 0;
	for (uint s = 0; s < BASIC_CITY_SERVICE_COUNT; s++) sum += t->service_coverage.percent[s];
	/* Hawker centres within reach add up to 4 points, a stadium up to 3. */
	const auto &p = t->service_coverage.percent;
	const int extras = p[to_underlying(CityService::Market)] * 4 / 100 + p[to_underlying(CityService::Stadium)] * 3 / 100;
	/* How satisfied riders are with the town's stations adds or takes up to 10 points. */
	const int happiness = static_cast<int>(TOWN_HAPPINESS_BASE + sum * (100 - TOWN_HAPPINESS_BASE) / (100 * BASIC_CITY_SERVICE_COUNT)) + GetTownSatisfactionHappiness(t)
			- static_cast<int>(GetTownPollutionUnhappiness(t)) + GetTownBudgetHappiness(t) + extras + GetCouncilHappiness(t);
	return static_cast<uint>(std::clamp(happiness, 0, 100));
}

/**
 * How much faster does a town grow thanks to its city services?
 * @param t The town.
 * @return Percentage to take off the time between new houses, 0 to 33.
 */
uint GetTownServiceGrowthBonus(const Town *t)
{
	uint sum = 0;
	for (uint s = 0; s < BASIC_CITY_SERVICE_COUNT; s++) sum += t->service_coverage.percent[s];
	return sum / 15;
}

/** Rebuild the coverage of the map from the city service objects on it. */
static void RebuildServiceCoverage()
{
	_service_cover_dirty = false;
	_service_cover.clear();

	for (const Object *o : Object::Iterate()) {
		if (!IsCityServiceObjectType(o->type)) continue;
		if (_service_cover.empty()) _service_cover.assign(Map::Size(), 0);

		CityService service = GetCityServiceOfObjectType(o->type);
		TileArea area = o->location;
		area.Expand(GetCityServiceRadius(service));
		/* A hospital's reach is health care too, a university's schooling. */
		uint16_t bits = 1U << to_underlying(service);
		if (service == CityService::Hospital) bits |= 1U << to_underlying(CityService::Health);
		if (service == CityService::University) bits |= 1U << to_underlying(CityService::Education);
		for (TileIndex tile : area) _service_cover[tile.base()] |= bits;
	}

	if (_service_cover.empty()) _service_cover.shrink_to_fit();
}

/**
 * Which city services reach a tile?
 * @param tile The tile.
 * @return Bit mask with a bit per #CityService.
 */
uint16_t GetCityServicesCoveringTile(TileIndex tile)
{
	if (_service_cover_dirty) RebuildServiceCoverage();
	return _service_cover.empty() ? 0 : _service_cover[tile.base()];
}

/** A city service was built or removed. */
void OnCityServicesChanged()
{
	_service_cover_dirty = true;
	if (_service_overlay != CityService::End) MarkWholeScreenDirty();
}

/**
 * Monthly update, before the towns' own monthly update: work out each town's
 * coverage, give owners standing with their towns, and charge the upkeep.
 */
void UpdateCityServicesMonthly()
{
	RebuildServiceCoverage();

	/* People per town, and how many of them each service reaches. */
	struct Count {
		uint32_t total = 0;
		std::array<uint32_t, CITY_SERVICE_COUNT> covered{};
	};
	std::vector<Count> counts(Town::GetPoolSize());
	if (!_service_cover.empty()) {
		for (uint index = 0; index < Map::Size(); index++) {
			TileIndex tile{index};
			if (!IsTileType(tile, TileType::House)) continue;
			uint population = HouseSpec::Get(GetHouseType(tile))->population;
			if (population == 0) continue;

			Count &count = counts[GetTownIndex(tile).base()];
			count.total += population;
			uint16_t cover = _service_cover[tile.base()];
			for (uint s = 0; s < CITY_SERVICE_COUNT; s++) {
				if (HasBit(cover, s)) count.covered[s] += population;
			}
		}
	}
	for (Town *t : Town::Iterate()) {
		const Count &count = counts[t->index.base()];
		for (uint s = 0; s < CITY_SERVICE_COUNT; s++) {
			t->service_coverage.percent[s] = count.total == 0 ? 0 : static_cast<uint8_t>(count.covered[s] * 100 / count.total);
		}
	}

	/* Upkeep per owner, and which kinds of service each company runs for each town. */
	std::array<Money, MAX_COMPANIES> upkeep{};
	std::vector<std::array<uint16_t, MAX_COMPANIES>> kinds(Town::GetPoolSize());
	for (const Object *o : Object::Iterate()) {
		if (!IsCityServiceObjectType(o->type)) continue;
		Owner owner = GetTileOwner(o->location.tile);
		if (!Company::IsValidID(owner)) continue;

		CityService service = GetCityServiceOfObjectType(o->type);
		upkeep[owner.base()] += GetCityServiceYearlyUpkeep(service) / 12;
		SetBit(kinds[o->town->index.base()][owner.base()], to_underlying(service));
	}

	/* Standing with the town grows with each kind of service a company runs for it;
	 * more of the same kind doesn't count, so a row of parks buys no favours. */
	Backup<CompanyID> cur_company(_current_company);
	for (Town *t : Town::Iterate()) {
		for (const Company *c : Company::Iterate()) {
			uint16_t mask = kinds[t->index.base()][c->index.base()];
			if (mask == 0) continue;
			cur_company.Change(c->index);
			ChangeTownRating(t, CountBits(mask) * RATING_SERVICE_UP_STEP, RATING_SERVICE_MAXIMUM, DoCommandFlag::Execute);
		}
	}
	cur_company.Restore();

	for (const Company *c : Company::Iterate()) {
		if (upkeep[c->index.base()] == 0) continue;
		SubtractMoneyFromCompany(c->index, CommandCost(ExpensesType::Property, upkeep[c->index.base()]));
	}
}

/**
 * Draw a city service tile, after its foundation.
 * @param ti The tile.
 * @param type Its object type.
 */
void DrawCityServiceTile(TileInfo *ti, ObjectType type)
{
	CityService service = GetCityServiceOfObjectType(type);
	Owner owner = GetTileOwner(ti->tile);
	/* The council's own services fly its colours: dark blue. */
	PaletteID palette = owner == OWNER_TOWN ? GetColourPalette(Colours::DarkBlue) : (Company::IsValidID(owner) ? GetCompanyPalette(owner) : PAL_NONE);

	const bool grass = service == CityService::Park || service == CityService::Stadium;
	DrawGroundSprite(grass ? SPR_FLAT_GRASS_TILE : SPR_CONCRETE_GROUND, PAL_NONE);
	if (IsInvisibilitySet(TransparencyOption::Structures)) return;

	const CityServiceInfo &info = _city_service_info[to_underlying(service)];
	SpriteBounds bounds({0, 0, 0}, {TILE_SIZE, TILE_SIZE, info.height}, {0, 0, 0});
	SpriteID image = SPR_CITY_SERVICE_BASE + to_underlying(service);
	if (to_underlying(service) >= BASIC_CITY_SERVICE_COUNT) {
		/* Bigger buildings: a sprite per tile, (0,0), (1,0), (0,1), (1,1). */
		const TileIndex north = Object::GetByTile(ti->tile)->location.tile;
		const uint part = (TileX(ti->tile) - TileX(north)) + 2 * (TileY(ti->tile) - TileY(north));
		image = SPR_CIVIC_SERVICE_BASE + info.sprite + (info.size == 2 ? part : 0);
	}
	SetBit(image, PALETTE_MODIFIER_COLOUR);
	AddSortableSpriteToDraw(image, palette, *ti, bounds, IsTransparencySet(TransparencyOption::Structures));
}

/**
 * Passengers a city service sends from one of its tiles, from the tile loop:
 * visitors, students and shoppers, like a house with that many people; a
 * stadium sends crowds on match days (one day a week) and few on the others.
 * @param tile The tile.
 */
void TileLoopCityService(TileIndex tile)
{
	const Object *o = Object::GetByTile(tile);
	const CityService service = GetCityServiceOfObjectType(o->type);
	uint people = _city_service_info[to_underlying(service)].passengers;
	if (people == 0 || o->town == nullptr) return;
	uint rounds = 1;
	if (service == CityService::Stadium) {
		const bool match_day = (TimerGameEconomy::date.base() + o->town->index.base()) % 7 == 0;
		if (match_day) {
			people = 255;
			rounds = 4;
		}
	}
	for (uint i = 0; i < rounds; i++) GenerateServicePassengers(o->town, tile, people);
}

/**
 * Cargo a city service's tile takes: passengers, for the bigger services.
 * @param tile The tile.
 * @param acceptance Cargo acceptance to add to.
 * @param always_accepted Cargo types always accepted.
 */
void AddCityServiceAcceptance(TileIndex tile, CargoArray &acceptance, CargoTypes &always_accepted)
{
	const CityService service = GetCityServiceOfObjectType(GetObjectType(tile));
	if (_city_service_info[to_underlying(service)].passengers == 0) return;
	const CargoType pass = GetCargoTypeByLabel(CT_PASSENGERS);
	if (!IsValidCargoType(pass)) return;
	acceptance[pass] += 8;
	always_accepted.Set(pass);
}

/**
 * Show a city service's coverage in the main view.
 * @param service The service, or CityService::End to show none.
 */
void ShowCityServiceOverlay(CityService service)
{
	if (_service_overlay == service) return;
	_service_overlay = service;
	MarkWholeScreenDirty();
}

/**
 * Which city service's coverage does the main view show?
 * @return The service, or CityService::End for none.
 */
CityService GetCityServiceOverlay()
{
	return _service_overlay;
}

/** Forget the coverage; used when starting or loading a game. */
void InitializeCityServices()
{
	_service_cover.clear();
	_service_cover.shrink_to_fit();
	_service_cover_dirty = true;
	_service_overlay = CityService::End;
}
