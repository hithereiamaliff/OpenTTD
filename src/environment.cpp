/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file environment.cpp Air pollution and land value.
 *
 * Once a month the map is surveyed in cells of 4 by 4 tiles. Congested roads,
 * diesel and steam vehicles, aircraft at airports and industries (processing
 * plants and power stations the most) give off pollution; trees and parks
 * take some up; the wind spreads it over a few cells. The result is an air
 * pollutant index on Malaysia's and Singapore's scale: good up to 50,
 * moderate up to 100, unhealthy up to 200, very unhealthy up to 300, then
 * hazardous. It follows the sources over a few months.
 *
 * Land value (0 to 100) comes from how central the land is in its town and
 * how big the town is, the city services and parks that reach it, good
 * passenger stations (railway stations the most), and water nearby; pollution
 * and industry take it down.
 *
 * Effects: polluted towns are less happy and grow slower; towns build
 * buildings that fit the land value (blocks of flats and offices where land is
 * dear, small houses where it is cheap, as in OpenTTD in between); towns
 * think less of companies whose
 * diesel vehicles pollute their air and better of those that run clean ones.
 */

#include "stdafx.h"
#include "environment.h"
#include "commuting.h"
#include "city_bus.h"
#include "city_services.h"
#include "company_base.h"
#include "company_func.h"
#include "core/backup_type.hpp"
#include "core/bitmath_func.hpp"
#include "core/math_func.hpp"
#include "engine_base.h"
#include "house.h"
#include "industry.h"
#include "interchange.h"
#include "map_func.h"
#include "news_func.h"
#include "road.h"
#include "road_map.h"
#include "roadveh.h"
#include "settings_type.h"
#include "station_base.h"
#include "station_map.h"
#include "strings_func.h"
#include "tile_map.h"
#include "town.h"
#include "town_map.h"
#include "town_zoning.h"
#include "town_budget.h"
#include "train.h"
#include "tree_map.h"
#include "vehicle_base.h"
#include "viewport_func.h"
#include "window_func.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/** Tiles per side of a cell of the pollution and land value maps. */
static constexpr uint ENV_CELL = 4;

/* What gives off pollution, in units per cell and month. */
static constexpr uint ROAD_EMISSION = 3; ///< A road tile, times the square of its congestion level.
static constexpr uint BUS_EMISSION = 80; ///< A diesel bus.
static constexpr uint LORRY_EMISSION = 120; ///< A lorry or van.
static constexpr uint DIESEL_TRAIN_EMISSION = 120; ///< A diesel train.
static constexpr uint STEAM_TRAIN_EMISSION = 200; ///< A steam train.
static constexpr uint SHIP_EMISSION = 60; ///< A ship.
static constexpr uint AIRCRAFT_EMISSION = 150; ///< An aircraft at or over an airport.
static constexpr uint HEAVY_INDUSTRY_EMISSION = 150; ///< A tile of a processing plant or power station.
static constexpr uint LIGHT_INDUSTRY_EMISSION = 60; ///< A tile of a mine or oil well.
/* What takes it up. */
static constexpr uint TREE_ABSORPTION = 3; ///< A tree.
static constexpr uint PARK_ABSORPTION = 40; ///< A cell within reach of a park.
/** Air pollutant index per unit of pollution, as a fraction. */
static constexpr uint API_NUMERATOR = 15, API_DENOMINATOR = 32;
/** Highest air pollutant index. */
static constexpr uint API_MAX = 500;
/** How far from a town its vehicles count for it, in tiles. */
static constexpr uint ENV_TOWN_REACH = 20;
/** The air is fine up to this index: no unhappiness, no slower growth, no complaints. */
static constexpr uint API_FINE = 50;

std::vector<uint16_t> _env_pollution; ///< Air pollutant index of each cell; empty while not known.
std::vector<uint8_t> _env_land_value; ///< Land value of each cell, 0 to 100.
static EnvironmentOverlay _environment_overlay = EnvironmentOverlay::None; ///< What the main view shows over the land.

/**
 * Cells across the map.
 * @return The number.
 */
static uint CellsX()
{
	return std::max(1U, Map::SizeX() / ENV_CELL);
}

/**
 * Cells down the map.
 * @return The number.
 */
static uint CellsY()
{
	return std::max(1U, Map::SizeY() / ENV_CELL);
}

/**
 * The cell of a tile.
 * @param tile The tile.
 * @return Its index in the maps.
 */
static uint CellOf(TileIndex tile)
{
	return std::min(TileY(tile) / ENV_CELL, CellsY() - 1) * CellsX() + std::min(TileX(tile) / ENV_CELL, CellsX() - 1);
}

/**
 * The tile in the middle of a cell.
 * @param cell The cell.
 * @return The tile.
 */
static TileIndex CellCentre(uint cell)
{
	return TileXY((cell % CellsX()) * ENV_CELL + ENV_CELL / 2, (cell / CellsX()) * ENV_CELL + ENV_CELL / 2);
}

/**
 * Are pollution and land value switched on?
 * @return True if they are.
 */
bool IsEnvironmentOn()
{
	return _settings_game.economy.environment != to_underlying(EnvironmentSetting::Off);
}

/**
 * Are pollution and land value known: switched on and worked out at least once?
 * @return True if they are.
 */
bool IsEnvironmentKnown()
{
	return IsEnvironmentOn() && !_env_pollution.empty() && _env_pollution.size() == static_cast<size_t>(CellsX()) * CellsY();
}

/**
 * The air pollutant index at a tile.
 * @param tile The tile.
 * @return The index, 0 if not known.
 */
uint GetTilePollution(TileIndex tile)
{
	return IsEnvironmentKnown() ? _env_pollution[CellOf(tile)] : 0;
}

/**
 * The land value at a tile.
 * @param tile The tile.
 * @return The value, 0 to 100; 0 if not known.
 */
uint GetTileLandValue(TileIndex tile)
{
	return IsEnvironmentKnown() ? _env_land_value[CellOf(tile)] : 0;
}

/**
 * How healthy air with this index is.
 * @param api The air pollutant index.
 * @return The band in words.
 */
StringID GetAirQualityName(uint api)
{
	if (api <= 50) return STR_AIR_QUALITY_GOOD;
	if (api <= 100) return STR_AIR_QUALITY_MODERATE;
	if (api <= 200) return STR_AIR_QUALITY_UNHEALTHY;
	if (api <= 300) return STR_AIR_QUALITY_VERY_UNHEALTHY;
	return STR_AIR_QUALITY_HAZARDOUS;
}

/**
 * Land value in words.
 * @param value The land value.
 * @return The band in words.
 */
StringID GetLandValueName(uint value)
{
	if (value < 20) return STR_LAND_VALUE_VERY_LOW;
	if (value < 40) return STR_LAND_VALUE_LOW;
	if (value < 60) return STR_LAND_VALUE_MEDIUM;
	if (value < 80) return STR_LAND_VALUE_HIGH;
	return STR_LAND_VALUE_VERY_HIGH;
}

/**
 * Colour of an air pollutant index on the map, in the colours the index is published in.
 * @param api The index.
 * @return The colour.
 */
PixelColour GetPollutionColour(uint api)
{
	if (api <= 50) return PC_LIGHT_BLUE;
	if (api <= 100) return PC_GREEN;
	if (api <= 200) return PC_YELLOW;
	if (api <= 300) return PC_ORANGE;
	return PC_RED;
}

/**
 * Colour of a land value on the map.
 * @param value The land value.
 * @return The colour.
 */
PixelColour GetLandValueColour(uint value)
{
	if (value < 20) return PC_DARK_GREY;
	if (value < 40) return PC_FIELDS;
	if (value < 60) return PC_YELLOW;
	if (value < 80) return PC_GREEN;
	return PC_WHITE;
}

/**
 * Recolour palette to tint a tile with in the main view, for what it shows.
 * @param tile The tile.
 * @return The palette, or PAL_NONE for no tint.
 */
PaletteID GetEnvironmentOverlayPalette(TileIndex tile)
{
	if (_environment_overlay == EnvironmentOverlay::Commuters) return GetCommuteOverlayPalette(tile);
	if (_environment_overlay == EnvironmentOverlay::None || !IsEnvironmentKnown()) return PAL_NONE;
	if (_environment_overlay == EnvironmentOverlay::Pollution) {
		const uint api = GetTilePollution(tile);
		if (api <= 50) return PALETTE_TO_BLUE;
		if (api <= 100) return PALETTE_TO_GREEN;
		if (api <= 200) return PALETTE_TO_YELLOW;
		if (api <= 300) return PALETTE_TO_ORANGE;
		return PALETTE_TO_RED;
	}
	const uint value = GetTileLandValue(tile);
	if (value < 20) return PALETTE_TO_GREY;
	if (value < 40) return PALETTE_TO_BROWN;
	if (value < 60) return PALETTE_TO_YELLOW;
	if (value < 80) return PALETTE_TO_GREEN;
	return PALETTE_TO_WHITE;
}

/**
 * Show air pollution or land value over the land in the main view; again to hide it.
 * @param overlay What to show.
 */
void ShowEnvironmentOverlay(EnvironmentOverlay overlay)
{
	_environment_overlay = (_environment_overlay == overlay) ? EnvironmentOverlay::None : overlay;
	MarkWholeScreenDirty();
}

/**
 * What the main view shows over the land.
 * @return The overlay.
 */
EnvironmentOverlay GetEnvironmentOverlay()
{
	return _environment_overlay;
}

/**
 * How much a town's polluted air takes off its happiness: a point per 10 above 50, at most 15.
 * @param t The town.
 * @return The points.
 */
uint GetTownPollutionUnhappiness(const Town *t)
{
	if (!IsEnvironmentKnown() || t->environment.api <= API_FINE) return 0;
	return std::min<uint>(15, (t->environment.api - API_FINE) / 10);
}

/**
 * How much slower a town with polluted air grows: a percent per 6 above 50, at most 25%.
 * @param t The town.
 * @return The percentage to add to the time between new houses.
 */
uint GetTownPollutionGrowthPenalty(const Town *t)
{
	if (!IsEnvironmentKnown() || t->environment.api <= API_FINE) return 0;
	return std::min<uint>(25, (t->environment.api - API_FINE) / 6);
}

/**
 * Towns build what fits the land. On land worth 30 to 59 (most of an
 * ordinary town) they build as in OpenTTD. Dearer land makes blocks of flats
 * and offices likelier, the more people a tile they hold and the dearer the
 * land, up to 3.5 times; cheap land (dirty air, industry, far from towns)
 * does the same for small houses. Public buildings are left alone.
 * @param probability The building's own chance.
 * @param hs The building.
 * @param tile Where it would go.
 * @return The chance to use.
 */
uint ScaleHouseProbabilityByLandValue(uint probability, const HouseSpec &hs, TileIndex tile)
{
	if (probability == 0 || !IsEnvironmentKnown() || GetHouseKind(hs) == HouseKind::Public) return probability;
	uint tiles = 1;
	if (hs.building_flags.Test(BuildingFlag::Size2x2)) {
		tiles = 4;
	} else if (hs.building_flags.Any({BuildingFlag::Size2x1, BuildingFlag::Size1x2})) {
		tiles = 2;
	}
	const int density = hs.population / tiles;
	const int value = static_cast<int>(GetTileLandValue(tile));
	int boost = 0; // In 1/1000 of the extra chance, 0 to 1000.
	if (value >= 60) {
		/* Dear land: the more people a tile, from 60 to 180, the likelier. */
		boost = Clamp(density - 60, 0, 120) * 1000 / 120 * (value - 60) / 40;
	} else if (value < 30) {
		/* Cheap land: the fewer people a tile, below 60, the likelier. */
		boost = Clamp(60 - density, 0, 60) * 1000 / 60 * (30 - value) / 30;
	}
	return std::max(1U, probability * static_cast<uint>(1000 + boost * 5 / 2) / 1000);
}

/**
 * How much a vehicle pollutes where it is: diesel and steam engines,
 * aircraft at airports; electric buses, trams and trolleybuses under wires
 * and electric trains don't.
 * @param v The vehicle (front).
 * @param tile Where it is.
 * @return The units.
 */
static uint GetVehicleEmission(const Vehicle *v, TileIndex tile)
{
	switch (v->type) {
		case VehicleType::Road: {
			const RoadVehicle *rv = RoadVehicle::From(v);
			if (IsElectricBus(rv) || GetRoadTypeInfo(rv->roadtype)->flags.Test(RoadTypeFlag::Catenary)) return 0;
			return rv->IsBus() ? BUS_EMISSION : LORRY_EMISSION;
		}

		case VehicleType::Train: {
			uint emission = 0;
			for (const Train *u = Train::From(v); u != nullptr; u = u->Next()) {
				const RailVehicleInfo &rvi = u->GetEngine()->VehInfo<RailVehicleInfo>();
				if (rvi.railveh_type == RailVehicleType::Wagon) continue;
				if (rvi.engclass == EngineClass::Steam) emission = std::max(emission, STEAM_TRAIN_EMISSION);
				if (rvi.engclass == EngineClass::Diesel) emission = std::max(emission, DIESEL_TRAIN_EMISSION);
			}
			return emission;
		}

		case VehicleType::Ship:
			return SHIP_EMISSION;

		case VehicleType::Aircraft:
			return IsAirportTile(tile) ? AIRCRAFT_EMISSION : 0;

		default:
			return 0;
	}
}

/**
 * How much a tile of an industry pollutes: processing plants and power
 * stations the most, mines and wells less; farms, forests and the industries
 * of town centres (banks, water towers, toy shops) not at all.
 * @param spec The industry.
 * @return The units per tile at normal production.
 */
static uint GetIndustryEmission(const IndustrySpec *spec)
{
	if (spec->behaviour.Any({IndustryBehaviour::OnlyInTown, IndustryBehaviour::OnlyNearTown})) return 0;
	if (spec->life_type.Test(IndustryLifeType::Organic)) return 0;
	if (spec->life_type.Test(IndustryLifeType::Processing) || spec->life_type == INDUSTRYLIFE_BLACK_HOLE) return HEAVY_INDUSTRY_EMISSION;
	if (spec->life_type.Test(IndustryLifeType::Extractive)) return LIGHT_INDUSTRY_EMISSION;
	return 0;
}

/**
 * Spread values over the neighbouring cells, twice: the wind carries
 * pollution a few cells away.
 * @param grid The values by cell.
 */
static void SpreadOverCells(std::vector<int32_t> &grid)
{
	const uint cx = CellsX(), cy = CellsY();
	std::vector<int32_t> across(grid.size());
	for (uint pass = 0; pass < 2; pass++) {
		for (uint y = 0; y < cy; y++) {
			for (uint x = 0; x < cx; x++) {
				const uint i = y * cx + x;
				across[i] = grid[x > 0 ? i - 1 : i] + grid[i] + grid[x + 1 < cx ? i + 1 : i];
			}
		}
		for (uint y = 0; y < cy; y++) {
			for (uint x = 0; x < cx; x++) {
				const uint i = y * cx + x;
				grid[i] = (across[y > 0 ? i - cx : i] + across[i] + across[y + 1 < cy ? i + cx : i]) / 9;
			}
		}
	}
}

/** A house, for the towns' figures. */
struct EnvironmentHouse {
	uint cell; ///< Its cell.
	TownID town; ///< Its town.
	uint16_t population; ///< Its people.
};

/** Forget pollution and land value, as when they are switched off. */
static void ClearEnvironment()
{
	_env_pollution.clear();
	_env_pollution.shrink_to_fit();
	_env_land_value.clear();
	_env_land_value.shrink_to_fit();
	for (Town *t : Town::Iterate()) t->environment = {};
}

/** The month ended: survey the map for pollution and land value, then the towns' figures, opinions and news. */
static void UpdateEnvironmentMonthly()
{
	if (!IsEnvironmentOn()) {
		if (!_env_pollution.empty()) ClearEnvironment();
		return;
	}

	static constexpr uint SETTING_FACTOR[] = {0, 60, 100, 150};
	const uint factor = SETTING_FACTOR[std::min<uint>(_settings_game.economy.environment, std::size(SETTING_FACTOR) - 1)];
	const uint n = CellsX() * CellsY();
	const bool first = !IsEnvironmentKnown();
	if (first) {
		_env_pollution.assign(n, 0);
		_env_land_value.assign(n, 0);
	}

	std::vector<int32_t> emission(n, 0), green(n, 0);
	std::vector<uint8_t> water(n, 0), industry(n, 0);
	std::vector<EnvironmentHouse> houses;

	/* Cells in a town's zones: low-emission zones (centre and inner suburbs) and car-free centres. */
	std::vector<uint8_t> low_emission(n, 0), car_free(n, 0);
	auto mark_town_cells = [](const Town *t, HouseZone zone, std::vector<uint8_t> &cells) {
		const uint radius_sq = t->cache.squared_town_zone_radius[to_underlying(zone)];
		if (radius_sq == 0) return;
		const int reach = static_cast<int>(IntSqrt(radius_sq) / ENV_CELL) + 1;
		const int tx = TileX(t->xy) / ENV_CELL, ty = TileY(t->xy) / ENV_CELL;
		for (int y = std::max(0, ty - reach); y <= std::min<int>(CellsY() - 1, ty + reach); y++) {
			for (int x = std::max(0, tx - reach); x <= std::min<int>(CellsX() - 1, tx + reach); x++) {
				const uint cell = y * CellsX() + x;
				if (DistanceSquare(CellCentre(cell), t->xy) < radius_sq) cells[cell] = 1;
			}
		}
	};
	for (const Town *t : Town::Iterate()) {
		if (HasOrdinance(t, Ordinance::LowEmissionZone)) mark_town_cells(t, HouseZone::TownInnerSuburb, low_emission);
		if (HasOrdinance(t, Ordinance::CarFreeCentre)) mark_town_cells(t, HouseZone::TownCentre, car_free);
	}

	/* Roads, trees, water, industry and houses, row by row. */
	const uint size_x = Map::SizeX(), size_y = Map::SizeY(), cells_x = CellsX(), cells_y = CellsY();
	for (uint y = 0; y < size_y; y++) {
		const uint row = std::min(y / ENV_CELL, cells_y - 1) * cells_x;
		for (uint x = 0; x < size_x; x++) {
			const TileIndex tile = TileXY(x, y);
			const TileType type = GetTileType(tile);
			if (type == TileType::Clear || type == TileType::Railway || type == TileType::Void) continue;
			const uint cell = row + std::min(x / ENV_CELL, cells_x - 1);
			switch (type) {
				case TileType::Road:
					if (!IsRoadDepot(tile)) {
						const uint level = GetRoadCongestion(tile);
						/* A low-emission zone keeps the oldest, dirtiest cars out. */
						if (level != 0) emission[cell] += ROAD_EMISSION * level * level * factor / 100 * (low_emission[cell] != 0 ? 70 : 100) / 100;
					}
					break;

				case TileType::Trees:
					green[cell] += TREE_ABSORPTION * GetTreeCount(tile);
					break;

				case TileType::Water:
					water[cell] = 1;
					break;

				case TileType::Industry:
					industry[cell] = 1;
					break;

				case TileType::House: {
					const uint16_t population = HouseSpec::Get(GetHouseType(tile))->population;
					if (population > 0) houses.push_back({cell, GetTownIndex(tile), population});
					break;
				}

				default:
					break;
			}
		}
	}

	for (const Industry *ind : Industry::Iterate()) {
		uint rate = GetIndustryEmission(GetIndustrySpec(ind->type));
		if (rate == 0) continue;
		rate = rate * (ind->prod_level + PRODLEVEL_DEFAULT) / (2 * PRODLEVEL_DEFAULT) * factor / 100;
		for (TileIndex tile : ind->location) {
			if (IsTileType(tile, TileType::Industry) && GetIndustryIndex(tile) == ind->index) emission[CellOf(tile)] += rate;
		}
	}

	/* Vehicles where they are now, and how much each company's road vehicles and trains pollute each town. */
	std::vector<std::array<uint32_t, MAX_COMPANIES>> town_emission(Town::GetPoolSize());
	std::vector<std::array<uint16_t, MAX_COMPANIES>> town_clean(Town::GetPoolSize());
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!v->IsPrimaryVehicle() || v->vehstatus.Any({VehState::Stopped, VehState::Crashed}) || v->IsInDepot()) continue;
		const TileIndex tile = v->type == VehicleType::Aircraft ? TileVirtXY(v->x_pos, v->y_pos) : v->tile;
		if (!IsValidTile(tile)) continue;
		const uint units = GetVehicleEmission(v, tile) * factor / 100;
		emission[CellOf(tile)] += units;

		if (!Company::IsValidID(v->owner) || (v->type != VehicleType::Road && v->type != VehicleType::Train)) continue;
		const Town *t = ClosestTownFromTile(tile, ENV_TOWN_REACH);
		if (t == nullptr) continue;
		if (units > 0) {
			town_emission[t->index.base()][v->owner.base()] += units;
		} else {
			town_clean[t->index.base()][v->owner.base()]++;
		}
	}

	/* Parks take up pollution around them. */
	for (uint cell = 0; cell < n; cell++) {
		if (HasBit(GetCityServicesCoveringTile(CellCentre(cell)), to_underlying(CityService::Park))) green[cell] += PARK_ABSORPTION;
	}

	/* The wind spreads it; the index follows over a few months. */
	SpreadOverCells(emission);
	SpreadOverCells(green);
	for (uint cell = 0; cell < n; cell++) {
		const int32_t net = emission[cell] - green[cell];
		const uint target = net <= 0 ? 0 : std::min<uint>(API_MAX, static_cast<uint>(net) * API_NUMERATOR / API_DENOMINATOR);
		_env_pollution[cell] = static_cast<uint16_t>(first ? target : (_env_pollution[cell] * 3 + target + 2) / 4);
	}

	/* Land value: towns, their centres and size. */
	std::vector<int16_t> base(n, 10);
	for (const Town *t : Town::Iterate()) {
		const uint edge_sq = t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)];
		if (edge_sq == 0) continue;
		const int reach = static_cast<int>(IntSqrt(edge_sq) / ENV_CELL) + 1;
		const int size_bonus = std::min(15, 3 * static_cast<int>(FindLastBit(t->cache.population / 1000 + 1)));
		const int tx = TileX(t->xy) / ENV_CELL, ty = TileY(t->xy) / ENV_CELL;
		for (int y = std::max(0, ty - reach); y <= std::min<int>(CellsY() - 1, ty + reach); y++) {
			for (int x = std::max(0, tx - reach); x <= std::min<int>(CellsX() - 1, tx + reach); x++) {
				const uint cell = y * CellsX() + x;
				const TileIndex centre = CellCentre(cell);
				if (DistanceSquare(centre, t->xy) >= edge_sq) continue;
				int value;
				switch (GetTownRadiusGroup(t, centre)) {
					case HouseZone::TownCentre: value = 56; break;
					case HouseZone::TownInnerSuburb: value = 48; break;
					case HouseZone::TownOuterSuburb: value = 40; break;
					case HouseZone::TownOutskirt: value = 34; break;
					default: value = 30; break;
				}
				base[cell] = static_cast<int16_t>(std::max<int>(base[cell], value + size_bonus));
			}
		}
	}

	/* Good passenger stations: railway stations the most, transport hubs and BRT stations a bit more. */
	std::vector<uint8_t> transit(n, 0);
	for (const Station *st : Station::Iterate()) {
		uint rating = 0;
		for (const CargoSpec *cs : CargoSpec::Iterate()) {
			if (!cs->IsValid() || !IsCargoInClass(cs->Index(), CargoClass::Passengers)) continue;
			const GoodsEntry &ge = st->goods[cs->Index()];
			if (ge.HasRating()) rating = std::max<uint>(rating, ge.rating);
		}
		if (rating == 0) continue;
		uint bonus = (st->facilities.Test(StationFacility::Train) ? 12 : 5) * rating / 255;
		if (IsActiveTransportHub(st) || st->brt_grade > 0) bonus += 4;
		/* Transit-oriented development: the town lets people build close to its railway stations. */
		if (st->facilities.Test(StationFacility::Train) && st->town != nullptr && HasOrdinance(st->town, Ordinance::TransitOrientedDevelopment)) bonus += 10;
		if (bonus == 0) continue;
		for (TileIndex tile : st->catchment_tiles) {
			uint8_t &best = transit[CellOf(tile)];
			best = std::max<uint8_t>(best, static_cast<uint8_t>(bonus));
		}
	}

	for (uint cell = 0; cell < n; cell++) {
		int value = base[cell];
		const uint16_t services = GetCityServicesCoveringTile(CellCentre(cell));
		for (CityService service : {CityService::Police, CityService::Fire, CityService::Health, CityService::Education}) {
			if (HasBit(services, to_underlying(service))) value += 4;
		}
		/* A university nearby makes land dearer still; so does a hawker centre, a little. */
		if (HasBit(services, to_underlying(CityService::University))) value += 4;
		if (HasBit(services, to_underlying(CityService::Market))) value += 2;
		if (HasBit(services, to_underlying(CityService::Park))) value += 8;
		value += transit[cell];
		if (water[cell] != 0) value += 5;
		if (industry[cell] != 0) value -= 5;
		/* Streets without through traffic. */
		if (car_free[cell] != 0) value += 5;
		value -= std::min<int>(50, _env_pollution[cell] / 5);
		const uint target = static_cast<uint>(Clamp(value, 0, static_cast<int>(LAND_VALUE_MAX)));
		_env_land_value[cell] = static_cast<uint8_t>(first ? target : (_env_land_value[cell] * 3 + target + 2) / 4);
	}

	/* Each town's figures, where its people live. */
	std::vector<std::array<uint64_t, 3>> town_sum(Town::GetPoolSize()); // people, index x people, value x people
	for (const EnvironmentHouse &house : houses) {
		auto &sum = town_sum[house.town.base()];
		sum[0] += house.population;
		sum[1] += static_cast<uint64_t>(_env_pollution[house.cell]) * house.population;
		sum[2] += static_cast<uint64_t>(_env_land_value[house.cell]) * house.population;
	}
	for (Town *t : Town::Iterate()) {
		const auto &sum = town_sum[t->index.base()];
		t->environment.api = static_cast<uint16_t>(sum[0] == 0 ? 0 : sum[1] / sum[0]);
		t->environment.land_value = static_cast<uint8_t>(sum[0] == 0 ? 0 : sum[2] / sum[0]);

		/* News when the air turns unhealthy; it has to clear up before the next. */
		if (t->environment.api > API_UNHEALTHY && !t->environment.unhealthy) {
			t->environment.unhealthy = true;
			AddNewsItem(GetEncodedString(STR_NEWS_TOWN_AIR_UNHEALTHY, t->index, t->environment.api), NewsType::General, NewsStyle::Normal, {}, t->index);
		} else if (t->environment.api < API_UNHEALTHY * 9 / 10) {
			t->environment.unhealthy = false;
		}
	}

	/* Towns think less of companies whose diesel vehicles pollute their air, and better of those running only clean ones. */
	Backup<CompanyID> cur_company(_current_company);
	for (Town *t : Town::Iterate()) {
		for (const Company *c : Company::Iterate()) {
			const uint32_t units = town_emission[t->index.base()][c->index.base()];
			const uint clean = town_clean[t->index.base()][c->index.base()];
			int change = 0;
			int limit = 0;
			if (units > 0 && t->environment.api > API_FINE) {
				change = -static_cast<int>(std::min<uint32_t>(30, units / 40));
				limit = RATING_POOR;
			} else if (units == 0 && clean >= 3) {
				change = 10;
				limit = RATING_GOOD;
			}
			if (change == 0) continue;
			cur_company.Change(c->index);
			ChangeTownRating(t, change, limit, DoCommandFlag::Execute);
		}
	}
	cur_company.Restore();

	SetWindowClassesDirty(WindowClass::TownView);
	SetWindowClassesDirty(WindowClass::SmallMap);
	if (_environment_overlay != EnvironmentOverlay::None) MarkWholeScreenDirty();
}

/** Survey the map once a month. */
static const IntervalTimer<TimerGameEconomy> _environment_monthly({TimerGameEconomy::Trigger::Month, TimerGameEconomy::Priority::None}, [](auto)
{
	UpdateEnvironmentMonthly();
});

/** Pollution and land value were switched on or off, or made stronger or weaker. */
void OnEnvironmentSettingChanged()
{
	if (!IsEnvironmentOn()) ClearEnvironment();
	SetWindowClassesDirty(WindowClass::TownView);
	SetWindowClassesDirty(WindowClass::SmallMap);
	MarkWholeScreenDirty();
}

/** Forget pollution and land value; used when starting or loading a game. */
void InitializeEnvironment()
{
	_env_pollution.clear();
	_env_pollution.shrink_to_fit();
	_env_land_value.clear();
	_env_land_value.shrink_to_fit();
	_environment_overlay = EnvironmentOverlay::None;
}
