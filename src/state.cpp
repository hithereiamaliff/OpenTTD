/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file state.cpp States and federal territories, as in Malaysia.
 *
 * Once towns have councils, the towns are grouped into states: the biggest
 * towns, spread over the map, become state capitals, and every other town
 * joins the nearest capital on its land. The biggest city becomes a federal
 * territory run by its City Hall (on big maps one more city too). Lands
 * across the sea, or behind a land border (a neck of land a few tiles wide),
 * with two towns or more are regions run like Sabah and Sarawak: they keep
 * their own land laws and immigration, and nobody may reclaim land in their
 * waters.
 *
 * States hold the land: companies pay a premium for land they take, more
 * where land is dear. States also get a federal grant and a share of what
 * rich councils keep beyond two years' taxes. Each state elects an assembly
 * (a seat a town and one more for each 10,000 people) every five years; a
 * general election elects parliament and the federal government, which runs
 * the federal territories. Voters judge only what they get: traffic,
 * transit, the air, city services, housing and taxes. Three platforms stand
 * (no real parties): Transit First (grants to the towns' transit budgets),
 * Roads and Growth (faster growth, cheaper land, money for building in the
 * towns that lag) and Homes and Services (grants to the councils' services
 * and estates, more estates of flats). A government that made things better
 * gains votes, one that made them worse loses them.
 *
 * The map of states is kept in cells of 4 by 4 tiles and saved, so every
 * player in a network game has the same; it is worked out again each year
 * and when towns come or go.
 */

#include "stdafx.h"
#include "state.h"
#include "state_cmd.h"
#include "federal.h"
#include "city_services.h"
#include "clear_map.h"
#include "command_func.h"
#include "commuting.h"
#include "company_base.h"
#include "company_func.h"
#include "core/random_func.hpp"
#include "council.h"
#include "economy_func.h"
#include "environment.h"
#include "map_func.h"
#include "news_func.h"
#include "settings_type.h"
#include "string_func.h"
#include "strings_func.h"
#include "tile_map.h"
#include "town.h"
#include "town_zoning.h"
#include "townname_func.h"
#include "town_budget.h"
#include "town_traffic.h"
#include "transit_authority.h"
#include "water_map.h"
#include "window_func.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"

#include "table/strings.h"

#include "safeguards.h"

std::vector<State> _states; ///< The states and federal territories (saved in chunk STAT).
Federation _federation; ///< The federation (saved in chunk SFED).
std::vector<uint8_t> _state_cells; ///< The map of states: the state of each cell, or #INVALID_STATE (saved in chunk SMAP).
std::vector<uint8_t> _state_cell_kinds; ///< What each cell is: a #StateCell (saved in chunk SMAP).
std::vector<ElectionResult> _elections; ///< The last general election and the last election of each region (saved in chunk SELN).

/** A state's assembly and parliament are elected for this many years. */
static constexpr int TERM_YEARS = 5;
/** General elections are held in this month (May), the elections of the regions in this one (September). */
static constexpr uint8_t GENERAL_ELECTION_MONTH = 4;
static constexpr uint8_t REGION_ELECTION_MONTH = 8;
/** The results of an election are declared this many days after polling day. */
static constexpr int ELECTION_DECLARE_DAYS = 2;
/** A town has a seat, and one more for each this many people. */
static constexpr uint PEOPLE_PER_SEAT = 10000;
/** Towns per state the map aims for, and the most states (as Malaysia's 13) on the main land and in a region. */
static constexpr uint TOWNS_PER_STATE = 6;
static constexpr uint MAX_MAIN_STATES = 13;
static constexpr uint MAX_REGION_STATES = 3;
/** The federal grant to a state, a year, in percent of its towns' taxes at the normal rate: what its platform costs. */
static constexpr uint FEDERAL_GRANT_PERCENT = 10;
/** The federal grant to a City Hall, a year, in percent of its town's taxes, into its services fund. */
static constexpr uint CITY_HALL_GRANT_PERCENT = 10;
/** What a government spends on its platform a year, in percent of its towns' taxes, at full strength. */
static constexpr uint PLATFORM_SPENDING_PERCENT = 10;
/** A state keeps this many months of its federal grant in its treasury; what it has beyond makes its platform stronger. */
static constexpr int RESERVE_MONTHS = 6;
/** A council keeps this many years of taxes; the state takes a third of what it has beyond. */
static constexpr uint LEVY_YEARS = 2;
/** The land premium on land worth 50, in percent of the price of a piece of railway track. */
static constexpr uint PREMIUM_PERCENT = 40;
/** Under Roads and Growth towns grow this much faster, at full strength: the time between new houses, less this, in percent. */
static constexpr uint ROADS_GROWTH_PERCENT = 15;
/** Under Homes and Services towns build estates of flats this much more readily, in percent. */
static constexpr uint HOMES_FLATS_PERCENT = 150;
/** Under Transit First towns get this share of their taxes on top for their transit budgets, in percent. */
static constexpr uint TRANSIT_GRANT_PERCENT = 10;
/** A region gives work passes to at least this share of the people from elsewhere who would work there, in percent. */
static constexpr uint REGION_MIN_WORK_PASSES = 20;
/** Roads and Growth gives this many more work passes in a region, Homes and Services this many fewer, in percent. */
static constexpr int REGION_GOVERNMENT_WORK_PASSES = 20;
/** A region wants to grow (and co-funds new towns and reclamation) when its towns lack this many workers, -100 to 100. */
static constexpr int REGION_GROWTH_LABOUR_NEED = 20;

/** Colours of the states on the map; federal territories are white. */
static constexpr Colours STATE_COLOURS[] = {
	Colours::Red, Colours::Green, Colours::Yellow, Colours::Orange, Colours::Pink, Colours::Purple, Colours::Brown,
	Colours::Cream, Colours::Mauve, Colours::PaleGreen, Colours::DarkGreen, Colours::Blue, Colours::LightBlue, Colours::Grey,
};

/**
 * Are there states (setting economy.states)? They need town councils.
 * @return True if there are.
 */
bool AreStatesOn()
{
	return _settings_game.economy.states && AreTownCouncilsOn();
}

/**
 * Are there states, and were they formed?
 * @return True if so.
 */
bool AreStatesFormed()
{
	return AreStatesOn() && _federation.formed;
}

/**
 * The states and federal territories.
 * @return The list, by index.
 */
std::vector<State> &GetStates()
{
	return _states;
}

/**
 * The federation: the federal government and its elections.
 * @return The federation.
 */
const Federation &GetFederation()
{
	return _federation;
}

/**
 * A state by its index.
 * @param id The index.
 * @return The state, or nullptr if there is none.
 */
State *GetState(StateID id)
{
	return id < _states.size() ? &_states[id] : nullptr;
}

/**
 * The state a town belongs to.
 * @param t The town.
 * @return The state, or nullptr if it has none or there are no states.
 */
State *GetTownState(const Town *t)
{
	return AreStatesFormed() ? GetState(t->state) : nullptr;
}

/**
 * Is a town a federal territory?
 * @param t The town.
 * @return True if it is.
 */
bool IsFederalTerritory(const Town *t)
{
	const State *s = GetTownState(t);
	return s != nullptr && s->kind == StateKind::FederalTerritory;
}

/**
 * Is a town in a region run like Sabah and Sarawak?
 * @param t The town.
 * @return True if it is.
 */
bool IsRegionTown(const Town *t)
{
	const State *s = GetTownState(t);
	return s != nullptr && s->region;
}

/**
 * Who governs a state: its own government, or for a federal territory the federal one.
 * @param s The state.
 * @return The platform in power.
 */
static Platform GetGovernment(const State &s)
{
	return s.kind == StateKind::FederalTerritory ? _federation.government : s.government;
}

/**
 * Who governs a town: its state's government, or for a federal territory the federal one.
 * @param t The town.
 * @return The platform in power; None without states.
 */
Platform GetTownGovernment(const Town *t)
{
	const State *s = GetTownState(t);
	return s == nullptr ? Platform::None : GetGovernment(*s);
}

/**
 * Can a government spend on its platform? A state needs a treasury not in debt; the federal government always can.
 * @param s The state.
 * @return True if it can.
 */
static bool CanSpend(const State &s)
{
	return s.kind == StateKind::FederalTerritory || s.treasury >= 0;
}

/**
 * How strongly a government carries out its platform, in percent: 100 on its federal grant, up to 200 with money
 * beyond #RESERVE_MONTHS months of it in its treasury (a year's grant beyond doubles it), from land premiums and
 * councils' levies. The federal government runs the territories at 100.
 * @param id The state.
 * @return The strength in percent; 0 if it can't spend.
 */
uint GetPlatformStrength(StateID id)
{
	const State *s = GetState(id);
	if (s == nullptr || !CanSpend(*s)) return 0;
	if (s->kind == StateKind::FederalTerritory || s->grants_last_year <= 0) return 100;
	const Money month = std::max<Money>(1, s->grants_last_year / 12);
	const Money surplus = s->treasury - month * RESERVE_MONTHS;
	if (surplus <= 0) return 100;
	return 100 + static_cast<uint>(std::min<Money>(100, surplus * 100 / (month * 12)).base());
}

/** @return Columns of the map of states. */
static uint StateCellsX()
{
	return std::max(1U, Map::SizeX() / STATE_CELL_SIZE);
}

/** @return Rows of the map of states. */
static uint StateCellsY()
{
	return std::max(1U, Map::SizeY() / STATE_CELL_SIZE);
}

/**
 * The cell of the map of states a tile is in.
 * @param tile The tile.
 * @return The index of the cell.
 */
static size_t StateCellOf(TileIndex tile)
{
	const uint x = std::min(TileX(tile) / STATE_CELL_SIZE, StateCellsX() - 1);
	const uint y = std::min(TileY(tile) / STATE_CELL_SIZE, StateCellsY() - 1);
	return static_cast<size_t>(y) * StateCellsX() + x;
}

/** @return True if the map of states fits this map. */
static bool IsStateMapValid()
{
	const size_t cells = static_cast<size_t>(StateCellsX()) * StateCellsY();
	return _state_cells.size() == cells && _state_cell_kinds.size() == cells;
}

/**
 * The state a tile lies in, by the map of states.
 * @param tile The tile.
 * @return The state, or #INVALID_STATE.
 */
StateID GetTileState(TileIndex tile)
{
	if (!AreStatesFormed() || !IsStateMapValid()) return INVALID_STATE;
	return _state_cells[StateCellOf(tile)];
}

/**
 * What the map of states says a tile is: land, sea, or a region's waters.
 * @param tile The tile.
 * @return The kind of cell.
 */
StateCell GetTileStateCell(TileIndex tile)
{
	if (!AreStatesFormed() || !IsStateMapValid()) return StateCell::Land;
	return static_cast<StateCell>(_state_cell_kinds[StateCellOf(tile)]);
}

/**
 * A state's name: the one players gave it, or "Negeri Funnford" ("Funnford State" in English), "Wilayah
 * Persekutuan Funnford" ("Federal Territory of Funnford") after its capital.
 * @param id The state.
 * @return The name.
 */
std::string GetStateName(StateID id)
{
	const State *s = GetState(id);
	if (s == nullptr) return {};
	if (!s->name.empty()) return s->name;
	if (!Town::IsValidID(s->capital)) return GetString(STR_STATE_NAME_DISSOLVED);
	const bool english = AreCouncilNamesEnglish();
	if (s->kind == StateKind::FederalTerritory) return GetString(english ? STR_STATE_NAME_TERRITORY_ENGLISH : STR_STATE_NAME_TERRITORY, s->capital);
	return GetString(english ? STR_STATE_NAME_ENGLISH : STR_STATE_NAME, s->capital);
}

/**
 * The name of a platform.
 * @param platform The platform.
 * @return The name; "Caretaker" for none.
 */
StringID GetPlatformName(Platform platform)
{
	static constexpr StringID NAMES[] = {STR_PLATFORM_NONE, STR_PLATFORM_TRANSIT_FIRST, STR_PLATFORM_ROADS_AND_GROWTH, STR_PLATFORM_HOMES_AND_SERVICES};
	static_assert(std::size(NAMES) == to_underlying(Platform::End));
	return platform < Platform::End ? NAMES[to_underlying(platform)] : STR_PLATFORM_NONE;
}

/**
 * The name of a town's council, which takes the town as its parameter: a federal territory's is its City Hall
 * (Dewan Bandaraya Funnford, Funnford City Hall).
 * @param t The town.
 * @return The string.
 */
StringID GetTownCouncilName(const Town *t)
{
	if (IsFederalTerritory(t)) return AreCouncilNamesEnglish() ? STR_COUNCIL_NAME_CITY_HALL_ENGLISH : STR_COUNCIL_NAME_CITY_HALL;
	return GetCouncilName(GetCouncilLevel(t));
}

/**
 * What a town's council is called in English: "municipal council", "city hall" for a federal territory.
 * @param t The town.
 * @return The string.
 */
StringID GetTownCouncilKind(const Town *t)
{
	if (IsFederalTerritory(t)) return STR_COUNCIL_KIND_CITY_HALL;
	return GetCouncilKind(GetCouncilLevel(t));
}

/* ------------------------------------------------------------------------
 * Lands and the map of states.
 * ------------------------------------------------------------------------ */

/** The lands of the map, in cells: land joined by land, split by the sea or by a narrow neck of land. */
struct StateLands {
	uint w = 0; ///< Columns.
	uint h = 0; ///< Rows.
	std::vector<uint8_t> sea; ///< Per cell: 1 if mostly sea.
	std::vector<uint16_t> label; ///< Per cell: the land it is part of, from 1; 0 for sea.
	std::vector<uint32_t> area; ///< Per land: its cells; [0] unused.
};

/**
 * Is a tile sea, or something built on the sea (docks, buoys, oil rigs)?
 * @param tile The tile.
 * @return True if it is.
 */
static bool IsSeaForStates(TileIndex tile)
{
	if (IsTileType(tile, TileType::Water)) return IsSea(tile);
	if (IsTileType(tile, TileType::Station) || IsTileType(tile, TileType::Industry) || IsTileType(tile, TileType::Object)) {
		return GetWaterClass(tile) == WaterClass::Sea;
	}
	return false;
}

/**
 * Work out the lands of the map. Land cells far enough from the sea are the cores of lands; cores joined
 * only by a neck of land a few cells wide are different lands, with a land border where they meet in the
 * neck. Land too thin for a core is a land of its own, or part of the land it joins.
 * @return The lands.
 */
static StateLands FindLands()
{
	StateLands l;
	l.w = StateCellsX();
	l.h = StateCellsY();
	const size_t n = static_cast<size_t>(l.w) * l.h;
	l.sea.assign(n, 0);
	l.label.assign(n, 0);
	l.area.assign(1, 0);
	bool any_sea = false;
	for (uint cy = 0; cy < l.h; cy++) {
		for (uint cx = 0; cx < l.w; cx++) {
			uint sea = 0, total = 0;
			for (uint y = cy * STATE_CELL_SIZE; y < std::min((cy + 1) * STATE_CELL_SIZE, Map::SizeY()); y++) {
				for (uint x = cx * STATE_CELL_SIZE; x < std::min((cx + 1) * STATE_CELL_SIZE, Map::SizeX()); x++) {
					const TileIndex tile = TileXY(x, y);
					if (IsTileType(tile, TileType::Void)) continue;
					total++;
					if (IsSeaForStates(tile)) sea++;
				}
			}
			const bool is_sea = total == 0 || (sea > 0 && sea * 2 >= total);
			l.sea[static_cast<size_t>(cy) * l.w + cx] = is_sea ? 1 : 0;
			any_sea |= is_sea;
		}
	}

	auto neighbours4 = [&](size_t c, auto &&visit) {
		const uint x = static_cast<uint>(c % l.w), y = static_cast<uint>(c / l.w);
		if (x > 0) visit(c - 1);
		if (x + 1 < l.w) visit(c + 1);
		if (y > 0) visit(c - l.w);
		if (y + 1 < l.h) visit(c + l.w);
	};

	/* Distance of each land cell from the sea, in cells (Chebyshev). */
	std::vector<uint16_t> dist(n, UINT16_MAX);
	std::vector<size_t> queue;
	queue.reserve(n);
	for (size_t c = 0; c < n; c++) {
		if (l.sea[c]) {
			dist[c] = 0;
			queue.push_back(c);
		}
	}
	for (size_t head = 0; head < queue.size(); head++) {
		const size_t c = queue[head];
		const int x = static_cast<int>(c % l.w), y = static_cast<int>(c / l.w);
		for (int dy = -1; dy <= 1; dy++) {
			for (int dx = -1; dx <= 1; dx++) {
				const int nx = x + dx, ny = y + dy;
				if (nx < 0 || ny < 0 || nx >= static_cast<int>(l.w) || ny >= static_cast<int>(l.h)) continue;
				const size_t nc = static_cast<size_t>(ny) * l.w + nx;
				if (dist[nc] != UINT16_MAX) continue;
				dist[nc] = dist[c] + 1;
				queue.push_back(nc);
			}
		}
	}

	/* Necks of land up to 2k cells wide split lands: 8 tiles on small maps, 16 from 512 tiles, 24 from 2048. */
	const uint cells_min = std::min(l.w, l.h);
	const uint16_t k = any_sea ? (cells_min <= 64 ? 1 : (cells_min <= 256 ? 2 : 3)) : 0;

	/* The cores, and their lands. */
	uint16_t labels = 0;
	queue.clear();
	for (size_t c = 0; c < n; c++) {
		if (l.sea[c] || l.label[c] != 0 || (any_sea && dist[c] <= k)) continue;
		labels++;
		l.label[c] = labels;
		const size_t first = queue.size();
		queue.push_back(c);
		for (size_t head = first; head < queue.size(); head++) {
			neighbours4(queue[head], [&](size_t nc) {
				if (l.sea[nc] || l.label[nc] != 0 || (any_sea && dist[nc] <= k)) return;
				l.label[nc] = labels;
				queue.push_back(nc);
			});
		}
	}
	/* The cores grow over the rest of their land, meeting in the necks. */
	for (size_t head = 0; head < queue.size(); head++) {
		const size_t c = queue[head];
		neighbours4(c, [&](size_t nc) {
			if (l.sea[nc] || l.label[nc] != 0) return;
			l.label[nc] = l.label[c];
			queue.push_back(nc);
		});
	}
	/* Land too thin for a core anywhere is a land of its own. */
	for (size_t c = 0; c < n; c++) {
		if (l.sea[c] || l.label[c] != 0) continue;
		labels++;
		l.label[c] = labels;
		const size_t first = queue.size();
		queue.push_back(c);
		for (size_t head = first; head < queue.size(); head++) {
			neighbours4(queue[head], [&](size_t nc) {
				if (l.sea[nc] || l.label[nc] != 0) return;
				l.label[nc] = labels;
				queue.push_back(nc);
			});
		}
	}
	l.area.assign(static_cast<size_t>(labels) + 1, 0);
	for (size_t c = 0; c < n; c++) l.area[l.label[c]]++;
	l.area[0] = 0;
	return l;
}

/**
 * The land a town stands on: the land of its centre's cell, or of a land cell next to it if that is sea.
 * @param l The lands.
 * @param t The town.
 * @return The land, 0 if none.
 */
static uint16_t GetTownLand(const StateLands &l, const Town *t)
{
	const size_t c = StateCellOf(t->xy);
	if (l.label[c] != 0) return l.label[c];
	const int x = static_cast<int>(c % l.w), y = static_cast<int>(c / l.w);
	for (int r = 1; r <= 2; r++) {
		for (int dy = -r; dy <= r; dy++) {
			for (int dx = -r; dx <= r; dx++) {
				const int nx = x + dx, ny = y + dy;
				if (nx < 0 || ny < 0 || nx >= static_cast<int>(l.w) || ny >= static_cast<int>(l.h)) continue;
				const uint16_t label = l.label[static_cast<size_t>(ny) * l.w + nx];
				if (label != 0) return label;
			}
		}
	}
	return 0;
}

/**
 * Which land each town stands on, split by the sea and by narrow necks of land as when the states formed: towns
 * on one land have the same number. For the federal plan, whose expressways stay on one land.
 * @return The land of each town, by town index; 0 for none.
 */
std::vector<uint16_t> GetTownLands()
{
	std::vector<uint16_t> lands(Town::GetPoolSize(), 0);
	if (!AreStatesFormed()) return lands;
	const StateLands l = FindLands();
	for (const Town *t : Town::Iterate()) lands[t->index.base()] = GetTownLand(l, t);
	return lands;
}

/**
 * Work out the map of states: each land cell belongs to the state of the nearest town on its land (nearest
 * along the land); a federal territory covers only its city, and the state around it the land beyond; land
 * without towns goes to the nearest state. A region's waters reach #REGION_WATERS_CELLS cells from its shores.
 * @param l The lands.
 */
static void RebuildStateMap(const StateLands &l)
{
	const size_t n = static_cast<size_t>(l.w) * l.h;
	_state_cells.assign(n, INVALID_STATE);
	_state_cell_kinds.assign(n, to_underlying(StateCell::Land));
	for (size_t c = 0; c < n; c++) {
		if (l.sea[c]) _state_cell_kinds[c] = to_underlying(StateCell::Sea);
	}

	/* Land: out from each town along its land; a federal territory only over its city. */
	std::vector<uint16_t> land_of(n, 0);
	std::vector<uint16_t> steps(n, 0);
	std::vector<uint16_t> limit(n, UINT16_MAX);
	std::vector<size_t> queue;
	queue.reserve(n);
	for (const Town *t : Town::Iterate()) {
		if (t->state >= _states.size()) continue;
		const size_t c = StateCellOf(t->xy);
		if (_state_cells[c] != INVALID_STATE) continue;
		_state_cells[c] = t->state;
		land_of[c] = GetTownLand(l, t);
		if (_states[t->state].kind == StateKind::FederalTerritory) {
			const uint radius = IntSqrt(t->cache.squared_town_zone_radius[to_underlying(HouseZone::TownEdge)]);
			limit[c] = static_cast<uint16_t>(radius / STATE_CELL_SIZE + 1);
		}
		queue.push_back(c);
	}
	for (size_t head = 0; head < queue.size(); head++) {
		const size_t c = queue[head];
		const uint x = static_cast<uint>(c % l.w), y = static_cast<uint>(c / l.w);
		if (steps[c] >= limit[c]) continue;
		auto visit = [&](size_t nc) {
			if (l.sea[nc] || _state_cells[nc] != INVALID_STATE) return;
			if (land_of[c] != 0 && l.label[nc] != land_of[c]) return;
			_state_cells[nc] = _state_cells[c];
			land_of[nc] = land_of[c] != 0 ? land_of[c] : l.label[nc];
			steps[nc] = steps[c] + 1;
			limit[nc] = limit[c];
			queue.push_back(nc);
		};
		if (x > 0) visit(c - 1);
		if (x + 1 < l.w) visit(c + 1);
		if (y > 0) visit(c - l.w);
		if (y + 1 < l.h) visit(c + l.w);
	}

	/* Land a federal territory cut off from its state's towns: the state reaches it past the city. */
	std::vector<uint8_t> seen(n, 0);
	queue.clear();
	for (size_t c = 0; c < n; c++) {
		if (l.sea[c] || _state_cells[c] == INVALID_STATE || _states[_state_cells[c]].kind == StateKind::FederalTerritory) continue;
		seen[c] = 1;
		queue.push_back(c);
	}
	std::vector<uint8_t> from(n, INVALID_STATE);
	for (size_t c : queue) from[c] = _state_cells[c];
	for (size_t head = 0; head < queue.size(); head++) {
		const size_t c = queue[head];
		const uint x = static_cast<uint>(c % l.w), y = static_cast<uint>(c / l.w);
		auto visit = [&](size_t nc) {
			if (l.sea[nc] || seen[nc] != 0 || l.label[nc] != l.label[c]) return;
			seen[nc] = 1;
			from[nc] = from[c];
			if (_state_cells[nc] == INVALID_STATE) _state_cells[nc] = from[c];
			queue.push_back(nc);
		};
		if (x > 0) visit(c - 1);
		if (x + 1 < l.w) visit(c + 1);
		if (y > 0) visit(c - l.w);
		if (y + 1 < l.h) visit(c + l.w);
	}

	/* Land without towns (small islands, a cut-off shore) goes to the nearest state, over the sea if need be. */
	std::fill(seen.begin(), seen.end(), 0);
	queue.clear();
	for (size_t c = 0; c < n; c++) {
		if (l.sea[c] || _state_cells[c] == INVALID_STATE) continue;
		seen[c] = 1;
		from[c] = _state_cells[c];
		/* A federal territory reaches no further than its city. */
		if (_states[_state_cells[c]].kind != StateKind::FederalTerritory) {
			steps[c] = 0;
			limit[c] = UINT16_MAX;
		}
		queue.push_back(c);
	}
	for (size_t head = 0; head < queue.size(); head++) {
		const size_t c = queue[head];
		const uint x = static_cast<uint>(c % l.w), y = static_cast<uint>(c / l.w);
		if (steps[c] >= limit[c]) continue;
		auto visit = [&](size_t nc) {
			if (seen[nc] != 0) return;
			seen[nc] = 1;
			from[nc] = from[c];
			steps[nc] = steps[c] + 1;
			limit[nc] = limit[c];
			if (!l.sea[nc] && _state_cells[nc] == INVALID_STATE) _state_cells[nc] = from[c];
			queue.push_back(nc);
		};
		if (x > 0) visit(c - 1);
		if (x + 1 < l.w) visit(c + 1);
		if (y > 0) visit(c - l.w);
		if (y + 1 < l.h) visit(c + l.w);
	}

	/* A region's waters: out from its shores over the sea. */
	std::vector<uint8_t> dist(n, UINT8_MAX);
	queue.clear();
	for (size_t c = 0; c < n; c++) {
		if (l.sea[c] || _state_cells[c] == INVALID_STATE || !_states[_state_cells[c]].region) continue;
		dist[c] = 0;
		queue.push_back(c);
	}
	for (size_t head = 0; head < queue.size(); head++) {
		const size_t c = queue[head];
		if (dist[c] >= REGION_WATERS_CELLS) continue;
		const int x = static_cast<int>(c % l.w), y = static_cast<int>(c / l.w);
		for (int dy = -1; dy <= 1; dy++) {
			for (int dx = -1; dx <= 1; dx++) {
				const int nx = x + dx, ny = y + dy;
				if (nx < 0 || ny < 0 || nx >= static_cast<int>(l.w) || ny >= static_cast<int>(l.h)) continue;
				const size_t nc = static_cast<size_t>(ny) * l.w + nx;
				if (!l.sea[nc] || dist[nc] != UINT8_MAX) continue;
				dist[nc] = dist[c] + 1;
				_state_cells[nc] = _state_cells[c];
				_state_cell_kinds[nc] = to_underlying(StateCell::RegionWaters);
				queue.push_back(nc);
			}
		}
	}
}

/** Count each state's towns and people. */
void UpdateStateCaches()
{
	for (State &s : _states) {
		s.towns = 0;
		s.population = 0;
	}
	for (const Town *t : Town::Iterate()) {
		if (t->state >= _states.size()) continue;
		State &s = _states[t->state];
		s.towns++;
		s.population += t->cache.population;
	}
}

/**
 * Pick the capitals of a land: its biggest towns, spread apart.
 * @param towns The land's towns that may be capitals, biggest first.
 * @param area The land's area, in tiles.
 * @param target How many capitals it should have.
 * @return The capitals, biggest first.
 */
static std::vector<const Town *> PickCapitals(const std::vector<const Town *> &towns, uint64_t area, uint target)
{
	std::vector<const Town *> capitals;
	if (towns.empty()) return capitals;
	uint spacing = IntSqrt(static_cast<uint>(std::min<uint64_t>(area / std::max(1U, target), UINT32_MAX))) * 3 / 4;
	for (int attempt = 0; attempt < 6; attempt++) {
		capitals.clear();
		for (const Town *t : towns) {
			if (capitals.size() >= target) break;
			bool far = true;
			for (const Town *c : capitals) {
				if (DistanceManhattan(t->xy, c->xy) < spacing) {
					far = false;
					break;
				}
			}
			if (far) capitals.push_back(t);
		}
		if (capitals.size() >= target) break;
		spacing = spacing * 4 / 5;
	}
	return capitals;
}

/** State capitals of Malaysia, for the states' capitals as they form, biggest first (see NameStatesAfterMalaysia). */
static constexpr std::string_view MALAYSIAN_CAPITALS[] = {
	"Johor Bahru", "George Town", "Ipoh", "Shah Alam", "Seremban", "Melaka", "Kuantan", "Kuala Terengganu", "Kota Bharu", "Alor Setar", "Kangar",
};
/** The states of those capitals, in the same order. */
static constexpr std::string_view MALAYSIAN_STATES[] = {
	"Johor", "Pulau Pinang", "Perak", "Selangor", "Negeri Sembilan", "Melaka", "Pahang", "Terengganu", "Kelantan", "Kedah", "Perlis",
};
static_assert(std::size(MALAYSIAN_CAPITALS) == std::size(MALAYSIAN_STATES));
/** Capitals of the regions run like Sabah and Sarawak, biggest first. */
static constexpr std::string_view MALAYSIAN_REGION_CAPITALS[] = { "Kuching", "Kota Kinabalu" };
/** Their states. */
static constexpr std::string_view MALAYSIAN_REGION_STATES[] = { "Sarawak", "Sabah" };
static_assert(std::size(MALAYSIAN_REGION_CAPITALS) == std::size(MALAYSIAN_REGION_STATES));

/**
 * Is a state called Sabah or Sarawak, the states regions are run like? Its texts then don't compare it with them.
 * @param id The state.
 * @return True if it is.
 */
bool IsSabahOrSarawak(StateID id)
{
	const std::string name = GetStateName(id);
	return std::ranges::find(MALAYSIAN_REGION_STATES, name) != std::end(MALAYSIAN_REGION_STATES);
}

/** Cities of the federal territories, biggest first. */
static constexpr std::string_view MALAYSIAN_FEDERAL_CITIES[] = { "Kuala Lumpur", "Putrajaya", "Labuan" };

/**
 * With Malaysian and Singaporean town names (setting game_creation.malaysian_town_names), the states that just
 * formed take Malaysian names: the federal territories' cities become Kuala Lumpur and Putrajaya, the states'
 * capitals the state capitals of Malaysia (Johor Bahru, George Town, Ipoh ...) and the regions' Kuching and Kota
 * Kinabalu, biggest first, and the states the names of their states (Johor, Pulau Pinang ... Sarawak, Sabah).
 * Only towns with a Malaysian town name are renamed, never one a player named; names already taken are skipped.
 */
static void NameStatesAfterMalaysia()
{
	if (!_settings_game.game_creation.malaysian_town_names) return;
	auto is_taken = [](std::string_view name) {
		for (const Town *t : Town::Iterate()) {
			if (t->name.empty() ? GetTownName(t) == name : t->name == name) return true;
		}
		return false;
	};
	auto rename = [&](StateID id, std::string_view capital, std::string_view state) {
		Town *t = Town::GetIfValid(_states[id].capital);
		if (t == nullptr || !t->name.empty() || !t->townnamegrfid.Empty() || t->townnametype != SPECSTR_TOWNNAME_START + MALAYSIAN_TOWNNAME_GENERATOR) return;
		if (is_taken(capital)) return;
		t->name = capital;
		t->UpdateVirtCoord();
		if (!state.empty() && _states[id].name.empty()) _states[id].name = state;
	};
	/* Biggest capital first, in each kind of state. */
	std::vector<StateID> order;
	for (StateID id = 0; id < _states.size(); id++) order.push_back(id);
	std::ranges::sort(order, [](StateID a, StateID b) {
		const Town *ta = Town::GetIfValid(_states[a].capital), *tb = Town::GetIfValid(_states[b].capital);
		const uint pa = ta == nullptr ? 0 : ta->cache.population, pb = tb == nullptr ? 0 : tb->cache.population;
		return pa != pb ? pa > pb : a < b;
	});
	size_t federal = 0, states = 0, regions = 0;
	for (StateID id : order) {
		const State &s = _states[id];
		if (s.kind == StateKind::FederalTerritory) {
			if (federal < std::size(MALAYSIAN_FEDERAL_CITIES)) rename(id, MALAYSIAN_FEDERAL_CITIES[federal++], {});
		} else if (s.region) {
			if (regions < std::size(MALAYSIAN_REGION_CAPITALS)) {
				rename(id, MALAYSIAN_REGION_CAPITALS[regions], MALAYSIAN_REGION_STATES[regions]);
				regions++;
			}
		} else if (states < std::size(MALAYSIAN_CAPITALS)) {
			rename(id, MALAYSIAN_CAPITALS[states], MALAYSIAN_STATES[states]);
			states++;
		}
	}
}

/** Form the states: capitals, federal territories, regions; every town in one. */
static void FormStates()
{
	_states.clear();
	_federation = {};
	_federation.formed = true;
	_elections.clear();
	InitializeFederal();
	const int year = TimerGameEconomy::year.base();
	_federation.formed_year = year;
	_federation.next_election = year + 1;

	std::vector<const Town *> towns;
	for (const Town *t : Town::Iterate()) towns.push_back(t);
	std::ranges::sort(towns, [](const Town *a, const Town *b) {
		return a->cache.population != b->cache.population ? a->cache.population > b->cache.population : a->index < b->index;
	});
	for (Town *t : Town::Iterate()) t->state = INVALID_STATE;
	if (towns.empty()) return;

	const StateLands lands = FindLands();
	std::vector<uint16_t> town_land(Town::GetPoolSize(), 0);
	std::vector<uint64_t> land_people(lands.area.size(), 0);
	std::vector<uint32_t> land_towns(lands.area.size(), 0);
	for (const Town *t : towns) {
		const uint16_t land = GetTownLand(lands, t);
		town_land[t->index.base()] = land;
		land_people[land] += t->cache.population;
		land_towns[land]++;
	}
	uint16_t main_land = 0;
	for (uint16_t land = 1; land < lands.area.size(); land++) {
		if (main_land == 0 || land_people[land] > land_people[main_land]) main_land = land;
	}
	auto is_region_land = [&](uint16_t land) {
		if (land == 0 || land == main_land || land_towns[land] < 2) return false;
		return lands.area[land] >= std::max<uint32_t>(64, lands.area[main_land] / 16);
	};
	for (uint16_t land = 1; land < lands.area.size(); land++) {
		if (land_towns[land] == 0 && lands.area[land] < 16) continue;
		Debug(Facility::Misc, Severity::Trace1, "[state] land {}: {} cells, {} towns, {} people{}", land, lands.area[land], land_towns[land], land_people[land],
				land == main_land ? ", main land" : (is_region_land(land) ? ", region" : ""));
	}

	auto add_state = [&](const Town *capital, StateKind kind, bool region) {
		State s;
		s.capital = capital->index;
		s.kind = kind;
		s.region = region;
		s.formed = year;
		s.next_election = year + (region ? 2 : 1);
		const size_t index = _states.size();
		s.colour = kind == StateKind::FederalTerritory ? to_underlying(Colours::White) : to_underlying(STATE_COLOURS[index % std::size(STATE_COLOURS)]);
		_states.push_back(s);
		return static_cast<StateID>(index);
	};

	/* The federal territories: the biggest city, and on big maps one more, far enough from it. */
	std::vector<const Town *> territories;
	for (const Town *t : towns) {
		if (town_land[t->index.base()] == main_land || main_land == 0) {
			territories.push_back(t);
			break;
		}
	}
	if (territories.empty()) territories.push_back(towns.front());
	const uint main_towns = main_land == 0 ? static_cast<uint>(towns.size()) : land_towns[main_land];
	const bool big_map = static_cast<uint64_t>(Map::SizeX()) * Map::SizeY() >= 512ULL * 512ULL && main_towns >= 20;

	/* The capitals of each land: the main land, and each region. */
	std::vector<std::vector<const Town *>> capitals_of(lands.area.size());
	for (uint16_t land = 0; land < lands.area.size(); land++) {
		if (land != main_land && !is_region_land(land)) continue;
		std::vector<const Town *> candidates;
		for (const Town *t : towns) {
			if (town_land[t->index.base()] != land || t == territories.front()) continue;
			candidates.push_back(t);
		}
		if (land == main_land && big_map && candidates.size() > 1) {
			/* The second territory: the biggest city a quarter of the map away from the first. */
			const uint far = std::max(Map::SizeX(), Map::SizeY()) / 4;
			for (auto it = candidates.begin(); it != candidates.end(); ++it) {
				if (DistanceManhattan((*it)->xy, territories.front()->xy) < far) continue;
				territories.push_back(*it);
				candidates.erase(it);
				break;
			}
		}
		const uint max = land == main_land ? MAX_MAIN_STATES : MAX_REGION_STATES;
		const uint target = Clamp((static_cast<uint>(candidates.size()) + TOWNS_PER_STATE / 2) / TOWNS_PER_STATE, 1U, max);
		capitals_of[land] = PickCapitals(candidates, static_cast<uint64_t>(std::max<uint32_t>(lands.area[land], 1)) * STATE_CELL_SIZE * STATE_CELL_SIZE, target);
	}

	for (const Town *t : territories) {
		Town::Get(t->index)->state = add_state(t, StateKind::FederalTerritory, false);
	}
	std::vector<StateID> state_of_capital(Town::GetPoolSize(), INVALID_STATE);
	for (uint16_t land = 0; land < lands.area.size(); land++) {
		for (const Town *c : capitals_of[land]) {
			state_of_capital[c->index.base()] = add_state(c, StateKind::State, is_region_land(land));
		}
	}

	/* Every other town joins the nearest capital on its land; towns on small islands and lands without one the nearest anywhere. */
	for (const Town *t : towns) {
		Town *town = Town::Get(t->index);
		if (town->state != INVALID_STATE) continue;
		if (state_of_capital[t->index.base()] != INVALID_STATE) {
			town->state = state_of_capital[t->index.base()];
			continue;
		}
		const uint16_t land = town_land[t->index.base()];
		StateID best = INVALID_STATE;
		uint best_dist = UINT_MAX;
		for (int pass = 0; pass < 3 && best == INVALID_STATE; pass++) {
			for (StateID id = 0; id < _states.size(); id++) {
				const State &s = _states[id];
				if (pass < 2 && s.kind != StateKind::State) continue;
				const Town *capital = Town::Get(s.capital);
				if (pass == 0 && town_land[capital->index.base()] != land) continue;
				const uint d = DistanceManhattan(t->xy, capital->xy);
				if (d < best_dist) {
					best_dist = d;
					best = id;
				}
			}
		}
		town->state = best;
	}

	/* A federal territory's council is its City Hall, with a city's powers. */
	for (const Town *t : territories) {
		Town *town = Town::Get(t->index);
		if (town->council.level < CouncilLevel::City) {
			town->council.level = CouncilLevel::City;
			town->council.since = year;
		}
	}

	UpdateStateCaches();
	RebuildStateMap(lands);
	NameStatesAfterMalaysia();

	uint states = 0, regions = 0;
	for (StateID id = 0; id < _states.size(); id++) {
		const State &s = _states[id];
		if (s.kind == StateKind::State) states++;
		if (s.region) regions++;
		Debug(Facility::Misc, Severity::Trace1, "[state] formed {} '{}': kind {} region {} capital {} towns {} people {}", id, GetStateName(id),
				to_underlying(s.kind), s.region ? 1 : 0, s.capital, s.towns, s.population);
	}
	Debug(Facility::Misc, Severity::Trace1, "[state] formed {} states, {} federal territories, {} in regions; lands {}, main land {} ({} cells)",
			states, territories.size(), regions, lands.area.size() - 1, main_land, main_land == 0 ? 0 : lands.area[main_land]);

	if (territories.size() > 1) {
		AddNewsItem(GetEncodedString(STR_NEWS_STATES_FORMED_TWO, states, territories[0]->index, territories[1]->index, _federation.next_election),
				NewsType::General, NewsStyle::Normal, {}, territories[0]->index);
	} else {
		AddNewsItem(GetEncodedString(STR_NEWS_STATES_FORMED, states, territories[0]->index, _federation.next_election),
				NewsType::General, NewsStyle::Normal, {}, territories[0]->index);
	}
	/* Each state of a region keeps its own laws, and its own checkpoint also towards the region's other states. */
	for (uint16_t land = 0; land < lands.area.size(); land++) {
		if (!is_region_land(land)) continue;
		const StringID immigration = capitals_of[land].size() > 1 ? STR_NEWS_STATE_REGION_IMMIGRATION_NEIGHBOURS : STR_NEWS_STATE_REGION_IMMIGRATION;
		for (const Town *c : capitals_of[land]) {
			const StateID id = state_of_capital[c->index.base()];
			const StringID like = IsSabahOrSarawak(id) ? STR_NEWS_STATE_REGION_RIGHTS : STR_NEWS_STATE_REGION_LIKE;
			AddNewsItem(GetEncodedString(STR_NEWS_STATE_REGION, GetStateName(id), like, immigration), NewsType::General, NewsStyle::Small, {}, _states[id].capital);
		}
	}
	InvalidateWindowClassesData(WindowClass::States);
	SetWindowClassesDirty(WindowClass::TownView);
}

/**
 * A new town joins the state whose land it is on, or the nearest state; never a federal territory.
 * @param t The town.
 */
void OnTownCreatedForStates(Town *t)
{
	t->state = INVALID_STATE;
	if (!_federation.formed || _states.empty()) return;
	StateID id = IsStateMapValid() ? _state_cells[StateCellOf(t->xy)] : INVALID_STATE;
	if (id < _states.size() && _states[id].kind != StateKind::State) id = INVALID_STATE;
	if (id >= _states.size()) {
		uint best_dist = UINT_MAX;
		for (StateID s = 0; s < _states.size(); s++) {
			if (_states[s].kind != StateKind::State || !Town::IsValidID(_states[s].capital)) continue;
			const uint d = DistanceManhattan(t->xy, Town::Get(_states[s].capital)->xy);
			if (d < best_dist) {
				best_dist = d;
				id = s;
			}
		}
	}
	t->state = id;
	UpdateStateCaches();
	RebuildStateMap(FindLands());
	InvalidateWindowClassesData(WindowClass::States);
}

/**
 * A town goes: a capital passes to the biggest town left in its state; a state with no towns left is dissolved.
 * @param t The town, still in the pool.
 */
void OnTownRemovedForStates(Town *t)
{
	if (!_federation.formed || t->state >= _states.size()) return;
	State &s = _states[t->state];
	const StateID id = t->state;
	t->state = INVALID_STATE;
	if (s.capital == t->index) {
		s.capital = TownID::Invalid();
		uint32_t best = 0;
		for (const Town *other : Town::Iterate()) {
			if (other == t || other->state != id) continue;
			if (!Town::IsValidID(s.capital) || other->cache.population > best) {
				s.capital = other->index;
				best = other->cache.population;
			}
		}
	}
	UpdateStateCaches();
	RebuildStateMap(FindLands());
	InvalidateWindowClassesData(WindowClass::States);
}

/* ------------------------------------------------------------------------
 * Land laws, immigration, reclamation.
 * ------------------------------------------------------------------------ */

/**
 * The premium a company pays the state for a tile of land it takes: #PREMIUM_PERCENT of a piece of railway
 * track on land worth 50, in proportion to what the land is worth; twice that in a region, which sets its
 * own; half under Roads and Growth.
 * @param tile The tile.
 * @return The premium; 0 outside the states.
 */
Money GetLandPremium(TileIndex tile)
{
	const uint value = IsEnvironmentKnown() ? std::max<uint>(GetTileLandValue(tile), 5) : 50;
	return GetStateLandPremium(GetTileState(tile), value);
}

/**
 * The land premium in a state on land of some value.
 * @param id The state.
 * @param value What the land is worth, 0-100.
 * @return The premium; 0 if there is no such state.
 */
Money GetStateLandPremium(StateID id, uint value)
{
	const State *s = GetState(id);
	if (s == nullptr) return 0;
	Money premium = _price[Price::BuildRail] * PREMIUM_PERCENT / 100 * value / 50;
	if (s->region) premium *= 2;
	if (GetGovernment(*s) == Platform::RoadsAndGrowth && CanSpend(*s)) premium /= 2;
	return premium;
}

/**
 * A company takes a tile of land (grass, rough land, fields, trees, a house): it pays the state a land
 * premium. In a region, forest and farmland in a town's area are its people's native customary land: they
 * give it up only to a company their town rates at least Mediocre (above Poor, as the town window names ratings).
 * @param tile The tile.
 * @param flags The command's flags; the premium goes to the state when it is done.
 * @return The premium, or an error.
 */
CommandCost CheckLandForCompany(TileIndex tile, DoCommandFlags flags)
{
	CommandCost cost(ExpensesType::Construction);
	if (!AreStatesFormed() || !Company::IsValidID(_current_company)) return cost;
	if (!IsTileType(tile, TileType::Clear) && !IsTileType(tile, TileType::Trees) && !IsTileType(tile, TileType::House)) return cost;
	const StateID id = GetTileState(tile);
	State *s = GetState(id);
	if (s == nullptr) return cost;

	const bool native = IsTileType(tile, TileType::Trees) || (IsTileType(tile, TileType::Clear) && IsClearGround(tile, ClearGround::Fields));
	if (s->region && native) {
		const Town *t = ClosestTownFromTile(tile, _settings_game.economy.dist_local_authority);
		if (t != nullptr && t->state == id) {
			const int rating = t->have_ratings.Test(_current_company) ? t->ratings[_current_company] : RATING_INITIAL;
			if (rating <= RATING_POOR) {
				Debug(Facility::Misc, Severity::Trace1, "[state] native customary land at {},{}: town {} rates company {} at {}", TileX(tile), TileY(tile), t->index, _current_company, rating);
				return CommandCostWithParam(STR_ERROR_NATIVE_CUSTOMARY_LAND, t->index);
			}
		}
	}

	const Money premium = GetLandPremium(tile);
	if (premium > 0 && flags.Test(DoCommandFlag::Execute)) {
		s->treasury += premium;
		s->premiums_this_year += premium;
	}
	cost.AddCost(premium);
	return cost;
}

/** Commands building a dock or a ship depot on the sea now (see #BuildingOnSeaScope). */
static uint _building_on_sea = 0;

BuildingOnSeaScope::BuildingOnSeaScope()
{
	_building_on_sea++;
}

BuildingOnSeaScope::~BuildingOnSeaScope()
{
	_building_on_sea--;
}

/**
 * What a region's government makes of the work in its towns: one job for every two people living
 * there is in balance (as for zoning).
 * @param id The region.
 * @param[out] jobs People working in its shops and offices.
 * @param[out] workers People living in its homes who could work.
 * @return From -100 (far more workers than jobs) to 100 (far more jobs than workers).
 */
static int GetRegionLabourNeed(StateID id, uint64_t &jobs, uint64_t &workers)
{
	jobs = 0;
	workers = 0;
	for (const Town *t : Town::Iterate()) {
		if (t->state != id) continue;
		const TownZoneDemand demand = GetTownZoneDemand(t);
		jobs += demand.jobs;
		workers += demand.residents / 2;
	}
	const int64_t scale = static_cast<int64_t>(std::max(jobs, workers));
	if (scale == 0) return 0;
	return static_cast<int>(Clamp<int64_t>((static_cast<int64_t>(jobs) - static_cast<int64_t>(workers)) * 100 / scale, -100, 100));
}

/**
 * Share of the people from elsewhere who would work in a region's towns that it gives work passes: as Sabah and
 * Sarawak do, it lets in more when its towns lack workers and fewer when they lack jobs; Roads and Growth lets in
 * more, Homes and Services fewer.
 * @param id The region.
 * @return The share in percent, from #REGION_MIN_WORK_PASSES to 100.
 */
uint GetRegionWorkPassPercent(StateID id)
{
	const State *s = GetState(id);
	if (s == nullptr || !s->region) return 100;
	uint64_t jobs, workers;
	int percent = 50 + GetRegionLabourNeed(id, jobs, workers) / 2;
	switch (GetGovernment(*s)) {
		case Platform::RoadsAndGrowth: percent += REGION_GOVERNMENT_WORK_PASSES; break;
		case Platform::HomesAndServices: percent -= REGION_GOVERNMENT_WORK_PASSES; break;
		default: break;
	}
	return static_cast<uint>(Clamp(percent, static_cast<int>(REGION_MIN_WORK_PASSES), 100));
}

/**
 * The work a region has, for its window.
 * @param id The region.
 * @return Its jobs and the workers living there.
 */
std::pair<uint64_t, uint64_t> GetRegionJobsAndWorkers(StateID id)
{
	uint64_t jobs, workers;
	GetRegionLabourNeed(id, jobs, workers);
	return {jobs, workers};
}

/**
 * Does a region's government want to grow, and so co-fund the new towns and the land companies reclaim? When its
 * towns lack workers, or under Roads and Growth.
 * @param id The region.
 * @return True if it does.
 */
bool DoesRegionWantGrowth(StateID id)
{
	const State *s = GetState(id);
	if (s == nullptr || !s->region) return false;
	if (GetGovernment(*s) == Platform::RoadsAndGrowth) return true;
	uint64_t jobs, workers;
	return GetRegionLabourNeed(id, jobs, workers) >= REGION_GROWTH_LABOUR_NEED;
}

/**
 * The permit a region gives a company for a new town on its land, or for turning sea into land in its waters.
 * Its towns' councils have a say: the region's town nearest the place must rate the company Mediocre or better.
 * The region then co-funds it (the company pays half) when it wants to grow, or the company pays it all and a
 * permit fee to the state. Docks and ship depots keep the sea under them (see #BuildingOnSeaScope) and need none.
 * @param tile The town's centre, or the sea tile.
 * @param cost What it costs without the permit.
 * @param waters True for reclaiming a sea tile, false for founding a town.
 * @param[out] permit What the permit is: none when none is needed.
 * @return An error if the region refuses one.
 */
CommandCost GetRegionPermit(TileIndex tile, Money cost, bool waters, RegionPermit &permit)
{
	permit = {};
	if (!AreStatesFormed() || !Company::IsValidID(_current_company)) return CommandCost();
	if (waters && _building_on_sea > 0) return CommandCost();
	const StateID id = GetTileState(tile);
	const State *s = GetState(id);
	if (s == nullptr || !s->region) return CommandCost();
	/* A town on the region's land; reclamation in its waters, and along its shores in the cells of its coast. */
	const StateCell kind = GetTileStateCell(tile);
	if (waters ? kind == StateCell::Sea : kind != StateCell::Land) return CommandCost();

	const Town *council = nullptr;
	uint best = UINT_MAX;
	for (const Town *t : Town::Iterate()) {
		if (t->state != id) continue;
		const uint d = DistanceManhattan(tile, t->xy);
		if (d < best) {
			best = d;
			council = t;
		}
	}
	if (council != nullptr) {
		const int rating = council->have_ratings.Test(_current_company) ? council->ratings[_current_company] : RATING_INITIAL;
		if (rating <= RATING_POOR) {
			Debug(Facility::Misc, Severity::Trace1, "[state] no permit at {},{} from state {}: town {} rates company {} at {}", TileX(tile), TileY(tile), id, council->index, _current_company, rating);
			CommandCost error(STR_ERROR_REGION_PERMIT_REFUSED);
			if (IsLocalCompany()) error.SetEncodedMessage(GetEncodedString(STR_ERROR_REGION_PERMIT_REFUSED, GetStateName(id), council->index));
			return error;
		}
	}

	permit.state = id;
	permit.cofunded = DoesRegionWantGrowth(id);
	permit.amount = permit.cofunded ? cost / 2 : cost * REGION_PERMIT_FEE_PERCENT / 100;
	return CommandCost();
}

/**
 * A company built what a region's permit was for: the state pays its half (from its treasury as far as it can, the
 * federal government the rest), or takes the permit fee.
 * @param permit The permit.
 */
void ApplyRegionPermit(const RegionPermit &permit)
{
	State *s = GetState(permit.state);
	if (s == nullptr || permit.amount <= 0) return;
	if (permit.cofunded) {
		const Money from_state = std::min(permit.amount, std::max<Money>(0, s->treasury));
		s->treasury -= from_state;
		s->spent_this_year += from_state;
		_federation.grants_this_year += permit.amount - from_state;
	} else {
		s->treasury += permit.amount;
		s->premiums_this_year += permit.amount;
	}
}

/**
 * The share of the people living in one town who would work in the other that may: across into a region,
 * those the region gives work passes (see GetRegionWorkPassPercent); out of one, everybody.
 * @param home The town they live in.
 * @param work The town they would work in.
 * @return The share in percent.
 */
uint GetCommuteWorkPassPercent(const Town *home, const Town *work)
{
	if (!IsCommuteAcrossRegionBorder(home, work)) return 100;
	const State *s = GetState(work->state);
	return (s != nullptr && s->region) ? GetRegionWorkPassPercent(work->state) : 100;
}

/**
 * Do people living in one town and working in the other cross a region's border, and pass its checkpoint?
 * @param home The town they live in.
 * @param work The town they work in.
 * @return True if they do.
 */
bool IsCommuteAcrossRegionBorder(const Town *home, const Town *work)
{
	if (!AreStatesFormed() || home == work) return false;
	auto zone = [](const Town *t) -> uint {
		const State *s = GetState(t->state);
		return (s != nullptr && s->region) ? t->state : MAX_STATES;
	};
	return zone(home) != zone(work);
}

/* ------------------------------------------------------------------------
 * Platforms in power.
 * ------------------------------------------------------------------------ */

/**
 * The grant a town's transit budget gets under Transit First, while the state has the money.
 * @param t The town.
 * @return The grant, in percent of its taxes.
 */
uint GetStateTransitGrantPercent(const Town *t)
{
	const State *s = GetTownState(t);
	if (s == nullptr || GetGovernment(*s) != Platform::TransitFirst) return 0;
	return TRANSIT_GRANT_PERCENT * GetPlatformStrength(t->state) / 100;
}

/**
 * How a town's state changes the time between its new houses: shorter under Roads and Growth, and under a federal
 * government of Roads and Growth.
 * @param t The town.
 * @return The time in percent of normal.
 */
uint GetStateGrowthPercent(const Town *t)
{
	const State *s = GetTownState(t);
	if (s == nullptr) return 100;
	uint faster = GetFederalGrowthPercent(t);
	if (GetGovernment(*s) == Platform::RoadsAndGrowth) faster += ROADS_GROWTH_PERCENT * GetPlatformStrength(t->state) / 100;
	return 100 - faster;
}

/**
 * Under Homes and Services towns build estates of flats more readily.
 * @param t The town.
 * @param hs The house type.
 * @param probability Its probability so far.
 * @return The probability.
 */
uint ScaleStateHouseProbability(const Town *t, const HouseSpec &hs, uint probability)
{
	if (probability == 0 || GetTownGovernment(t) != Platform::HomesAndServices || !IsEstateFlat(hs)) return probability;
	return probability * HOMES_FLATS_PERCENT / 100;
}

/**
 * How readily a town brings in an ordinance under its state's government: Transit First makes it readier to
 * tame cars and fund transit, Roads and Growth slower to limit cars.
 * @param t The town.
 * @param ordinance The ordinance (an #Ordinance).
 * @param chance The chance one in this a year it would otherwise.
 * @return The chance, one in this.
 */
uint GetStateOrdinanceChance(const Town *t, uint ordinance, uint chance)
{
	const Ordinance o = static_cast<Ordinance>(ordinance);
	switch (GetTownGovernment(t)) {
		case Platform::TransitFirst:
			if (o == Ordinance::TransitFund || o == Ordinance::CarFreeCentre || o == Ordinance::TransitOrientedDevelopment ||
					o == Ordinance::ParkingLimits || o == Ordinance::LowEmissionZone) {
				return std::max(1U, chance / 2);
			}
			return chance;

		case Platform::RoadsAndGrowth:
			if (o == Ordinance::CarFreeCentre || o == Ordinance::ParkingLimits || o == Ordinance::LowEmissionZone) return chance * 2;
			return chance;

		default:
			return chance;
	}
}

/* ------------------------------------------------------------------------
 * Voters and elections.
 * ------------------------------------------------------------------------ */

/**
 * What a town's voters make of things.
 * @param t The town.
 * @return Their concerns.
 */
VoterConcerns GetVoterConcerns(const Town *t)
{
	VoterConcerns c;
	const TownTrafficSummary traffic = GetTownTrafficSummary(t);
	c.traffic = traffic.roads == 0 ? 0 : static_cast<uint8_t>(std::min<uint>(100, traffic.congested * 400 / traffic.roads));
	const uint share = IsTownCommutingKnown(t) ? GetTownCommuteTransitShare(t) : GetTownCarReduction(t);
	c.transit = static_cast<uint8_t>(std::min<uint>(100, share * 2));
	c.satisfaction = t->transit.satisfaction <= 100 ? t->transit.satisfaction : 50;
	c.air = IsEnvironmentKnown() ? static_cast<uint8_t>(std::min<uint>(100, t->environment.api * 100U / 150)) : 0;
	uint covered = 0;
	for (uint s = 0; s < BASIC_CITY_SERVICE_COUNT; s++) covered += t->service_coverage.percent[s];
	c.services = static_cast<uint8_t>(100 - std::min<uint>(100, covered / BASIC_CITY_SERVICE_COUNT));
	const CouncilCounts &n = t->council.counts;
	c.housing = (n.people > 0 && static_cast<uint64_t>(n.flats) * 10 >= n.people) ? static_cast<uint8_t>(100 - std::min<uint8_t>(100, t->council.estate_condition)) : 20;
	c.taxes = AreTownBudgetsOn() ? static_cast<uint8_t>(Clamp((static_cast<int>(t->budget.tax_rate) - 70) * 100 / 80, 0, 100)) : 37;
	c.growth = static_cast<uint8_t>(100 - std::min<uint>(100, t->cache.population / 100));
	return c;
}

/**
 * How voters rate the service they get, 0-100: transit and how good it is, roads that flow, clean air, city services.
 * @param c Their concerns.
 * @return The score.
 */
uint GetServiceScore(const VoterConcerns &c)
{
	return (c.transit + c.satisfaction + (100 - c.traffic) + (100 - c.air) + (100 - c.services)) / 5;
}

/**
 * How much each platform appeals to a town's voters, about 0-100. Transit First to people who ride and like
 * it, to drivers stuck in traffic and to people breathing dirty air; Roads and Growth to people who drive, to
 * drivers stuck in traffic, to small towns that want to grow and to people with high taxes; Homes and Services
 * to people who lack city services or live in run-down estates, and to people with high taxes.
 * @param c The voters' concerns.
 * @return The appeal of each platform that stands.
 */
std::array<int, PLATFORM_COUNT> GetPlatformAppeal(const VoterConcerns &c)
{
	return {
		(35 * c.transit + 20 * c.satisfaction + 25 * c.traffic + 20 * c.air) / 100,
		(35 * (100 - c.transit) + 30 * c.traffic + 15 * c.taxes + 20 * c.growth) / 100,
		(45 * c.services + 35 * c.housing + 20 * c.taxes) / 100,
	};
}

/**
 * Seats a town has, in its state's assembly and in parliament.
 * @param t The town.
 * @return The seats: one, and one more for each #PEOPLE_PER_SEAT people.
 */
uint GetTownSeats(const Town *t)
{
	return 1 + t->cache.population / PEOPLE_PER_SEAT;
}

/**
 * Votes in a town: each seat goes to the platform that appeals most, give or take.
 * @param t The town.
 * @param c Its voters' concerns.
 * @param incumbent The platform in power.
 * @param bonus What voters give (or take from) the platform in power for how things went.
 * @param noise Whether votes vary (an election) or not (a poll).
 * @param[in,out] seats Seats won, added to.
 */
static void VoteInTown(const Town *t, const VoterConcerns &c, Platform incumbent, int bonus, bool noise, PlatformSeats &seats)
{
	const std::array<int, PLATFORM_COUNT> appeal = GetPlatformAppeal(c);
	const uint town_seats = GetTownSeats(t);
	for (uint seat = 0; seat < town_seats; seat++) {
		uint best = 0;
		int best_votes = INT_MIN;
		for (uint p = 0; p < PLATFORM_COUNT; p++) {
			int votes = appeal[p];
			if (incumbent != Platform::None && static_cast<uint>(to_underlying(incumbent)) - 1 == p) votes += bonus;
			if (noise) votes += static_cast<int>(RandomRange(13)) - 6;
			if (votes > best_votes) {
				best_votes = votes;
				best = p;
			}
		}
		seats[best]++;
	}
}

/**
 * Who forms the government after an election: the platform with the most seats; on a tie the one in power keeps it.
 * @param seats Seats won.
 * @param incumbent The platform in power.
 * @return The government.
 */
static Platform FormGovernment(const PlatformSeats &seats, Platform incumbent)
{
	uint best = 0;
	for (uint p = 1; p < PLATFORM_COUNT; p++) {
		if (seats[p] > seats[best]) best = p;
	}
	if (incumbent != Platform::None && seats[to_underlying(incumbent) - 1] == seats[best]) return incumbent;
	return static_cast<Platform>(best + 1);
}

/** A town's voters, worked out once for an election or a poll. */
struct TownVoters {
	const Town *town; ///< The town.
	VoterConcerns concerns; ///< What they make of things.
};

/**
 * The voters of a state's towns, or of all towns.
 * @param id The state, or #INVALID_STATE for all.
 * @return The voters, by town.
 */
static std::vector<TownVoters> GetVoters(StateID id)
{
	std::vector<TownVoters> voters;
	for (const Town *t : Town::Iterate()) {
		if (id != INVALID_STATE && t->state != id) continue;
		voters.push_back({t, GetVoterConcerns(t)});
	}
	return voters;
}

/**
 * The service score of voters, weighted by people.
 * @param voters The voters.
 * @return The score, 0-100.
 */
static uint GetVotersScore(const std::vector<TownVoters> &voters)
{
	uint64_t sum = 0, people = 0;
	for (const TownVoters &v : voters) {
		const uint weight = std::max<uint>(1, v.town->cache.population);
		sum += static_cast<uint64_t>(GetServiceScore(v.concerns)) * weight;
		people += weight;
	}
	return people == 0 ? 50 : static_cast<uint>(sum / people);
}

/**
 * What voters give the platform in power: half the change in their score since they elected it, at most 10 either way.
 * @param score Their score now.
 * @param then Their score at the last election.
 * @param elected Year of the last election; 0 if none.
 * @return The votes added to the platform in power.
 */
static int GetIncumbentBonus(uint score, uint then, int32_t elected)
{
	if (elected == 0) return 0;
	return Clamp((static_cast<int>(score) - static_cast<int>(then)) / 2, -10, 10);
}

/**
 * How a state's assembly would look if voters went to the polls today.
 * @param id The state.
 * @return The seats each platform would win.
 */
PlatformSeats GetStatePoll(StateID id)
{
	PlatformSeats seats{};
	const State *s = GetState(id);
	if (s == nullptr || s->kind != StateKind::State) return seats;
	const std::vector<TownVoters> voters = GetVoters(id);
	const int bonus = GetIncumbentBonus(GetVotersScore(voters), s->service_score, s->elected);
	for (const TownVoters &v : voters) VoteInTown(v.town, v.concerns, s->government, bonus, false, seats);
	return seats;
}

/**
 * How parliament would look if voters went to the polls today.
 * @return The seats each platform would win.
 */
PlatformSeats GetFederalPoll()
{
	PlatformSeats seats{};
	const std::vector<TownVoters> voters = GetVoters(INVALID_STATE);
	const int bonus = GetIncumbentBonus(GetVotersScore(voters), _federation.service_score, _federation.elected);
	for (const TownVoters &v : voters) VoteInTown(v.town, v.concerns, _federation.government, bonus, false, seats);
	return seats;
}

/**
 * What a state's voters make of things, weighted by people.
 * @param id The state.
 * @return Their concerns.
 */
VoterConcerns GetStateConcerns(StateID id)
{
	const std::vector<TownVoters> voters = GetVoters(id);
	std::array<uint64_t, 8> sum{};
	uint64_t people = 0;
	for (const TownVoters &v : voters) {
		const uint w = std::max<uint>(1, v.town->cache.population);
		const VoterConcerns &c = v.concerns;
		sum[0] += c.traffic * w;
		sum[1] += c.transit * w;
		sum[2] += c.satisfaction * w;
		sum[3] += c.air * w;
		sum[4] += c.services * w;
		sum[5] += c.housing * w;
		sum[6] += c.taxes * w;
		sum[7] += c.growth * w;
		people += w;
	}
	VoterConcerns c;
	if (people == 0) return c;
	c.traffic = static_cast<uint8_t>(sum[0] / people);
	c.transit = static_cast<uint8_t>(sum[1] / people);
	c.satisfaction = static_cast<uint8_t>(sum[2] / people);
	c.air = static_cast<uint8_t>(sum[3] / people);
	c.services = static_cast<uint8_t>(sum[4] / people);
	c.housing = static_cast<uint8_t>(sum[5] / people);
	c.taxes = static_cast<uint8_t>(sum[6] / people);
	c.growth = static_cast<uint8_t>(sum[7] / people);
	return c;
}

/* ------------------------------------------------------------------------
 * The run-up to an election, polling day and the results.
 *
 * Two months before polling day the assembly (for a general election
 * parliament and the assemblies of the states on the main land) is
 * dissolved, and the government carries on as a caretaker: it keeps up what
 * it pays for but starts nothing new. Nomination day is a month before
 * polling day. On polling day (the first of the month) the votes are counted
 * town by town, which the election night window shows; the results are
 * declared two days later.
 * ------------------------------------------------------------------------ */

/**
 * The seats of each platform, in words: "Transit First 7, Roads and Growth 3, Homes and Services 2".
 * @param seats The seats.
 * @return The words.
 */
std::string GetSeatsText(const PlatformSeats &seats)
{
	std::string text;
	for (uint p = 0; p < PLATFORM_COUNT; p++) {
		if (!text.empty()) text += ", ";
		text += GetString(STR_STATES_SEATS_ITEM, GetPlatformName(static_cast<Platform>(p + 1)), seats[p]);
	}
	return text;
}

/**
 * All the seats.
 * @param seats The seats of each platform.
 * @return Their sum.
 */
uint GetTotalSeats(const PlatformSeats &seats)
{
	uint total = 0;
	for (uint16_t n : seats) total += n;
	return total;
}

/**
 * Is the run-up to an election on: the assembly dissolved and the government a caretaker until polling day?
 * @param id The state, or #INVALID_STATE for the general election. States on the main land and federal
 *           territories vote with parliament; regions on their own.
 * @return True from two months before polling day until polling day.
 */
bool IsElectionCampaign(StateID id)
{
	if (!AreStatesFormed()) return false;
	const int year = TimerGameEconomy::year.base();
	const uint month = TimerGameEconomy::month;
	const State *s = GetState(id);
	if (s == nullptr || s->kind != StateKind::State || !s->region) {
		return year >= _federation.next_election && month + 2 >= GENERAL_ELECTION_MONTH && month < GENERAL_ELECTION_MONTH;
	}
	return s->towns > 0 && year >= s->next_election && month + 2 >= REGION_ELECTION_MONTH && month < REGION_ELECTION_MONTH;
}

/**
 * The month of polling day of a state's next election.
 * @param id The state, or #INVALID_STATE for the general election.
 * @return The month's name.
 */
StringID GetElectionMonthName(StateID id)
{
	const State *s = GetState(id);
	return (s != nullptr && s->kind == StateKind::State && s->region) ? STR_MONTH_SEP : STR_MONTH_MAY;
}

/**
 * The elections kept: the last general election and the last election of each region.
 * @return The elections.
 */
std::vector<ElectionResult> &GetElections()
{
	return _elections;
}

/**
 * The last election of a region, or the last general election.
 * @param id The region, or #INVALID_STATE for the general election.
 * @return The election, or nullptr if there was none yet.
 */
const ElectionResult *GetLastElection(StateID id)
{
	for (const ElectionResult &r : _elections) {
		if (r.state == id) return &r;
	}
	return nullptr;
}

/**
 * Keep an election, in place of the last one of the same kind.
 * @param r The election.
 */
static void KeepElection(ElectionResult &&r)
{
	std::erase_if(_elections, [&r](const ElectionResult &e) { return e.state == r.state; });
	_elections.push_back(std::move(r));
}

/**
 * Order the towns of an election the way the count declares them: the smallest towns first, the big cities last.
 * @param towns The towns.
 */
static void SortForCount(std::vector<ElectionTown> &towns)
{
	std::ranges::sort(towns, [](const ElectionTown &a, const ElectionTown &b) {
		const Town *ta = Town::GetIfValid(a.town), *tb = Town::GetIfValid(b.town);
		const uint pa = ta == nullptr ? 0 : ta->cache.population, pb = tb == nullptr ? 0 : tb->cache.population;
		return pa != pb ? pa < pb : a.town < b.town;
	});
}

/**
 * A state elects its assembly, seat by seat; the platform with the most seats forms its government.
 * @param id The state.
 * @param[out] towns If not nullptr, the seats each town gave.
 * @return The seats each platform won; none if the state has no towns.
 */
static PlatformSeats ElectAssembly(StateID id, std::vector<ElectionTown> *towns)
{
	State &s = _states[id];
	PlatformSeats seats{};
	const std::vector<TownVoters> voters = GetVoters(id);
	if (voters.empty()) {
		s.next_election = TimerGameEconomy::year.base() + TERM_YEARS;
		return seats;
	}
	const uint score = GetVotersScore(voters);
	const int bonus = GetIncumbentBonus(score, s.service_score, s.elected);
	for (const TownVoters &v : voters) {
		const VoterConcerns &c = v.concerns;
		const std::array<int, PLATFORM_COUNT> appeal = GetPlatformAppeal(c);
		Debug(Facility::Misc, Severity::Trace1, "[state] votes town {} people {}: traffic {} transit {} satisfaction {} air {} services {} housing {} taxes {} growth {}; appeal {} {} {}",
				v.town->index, v.town->cache.population, c.traffic, c.transit, c.satisfaction, c.air, c.services, c.housing, c.taxes, c.growth, appeal[0], appeal[1], appeal[2]);
		PlatformSeats here{};
		VoteInTown(v.town, v.concerns, s.government, bonus, true, here);
		for (uint p = 0; p < PLATFORM_COUNT; p++) seats[p] += here[p];
		if (towns != nullptr) towns->push_back({v.town->index, here});
	}
	const Platform before = s.government;
	s.government = FormGovernment(seats, before);
	s.seats = seats;
	s.elected = TimerGameEconomy::year.base();
	s.next_election = s.elected + TERM_YEARS;
	s.service_score = static_cast<uint8_t>(score);
	Debug(Facility::Misc, Severity::Trace1, "[state] election {} '{}': seats {} {} {} of {}, government {} (was {}), score {}, bonus {}",
			id, GetStateName(id), seats[0], seats[1], seats[2], GetTotalSeats(seats), to_underlying(s.government), to_underlying(before), score, bonus);
	return seats;
}

/**
 * Announce news about an election in the state's capital, or the federal territory's city for a general election.
 * @param id The state, or #INVALID_STATE for the general election.
 * @param headline The news.
 * @param style Its style.
 */
static void AddElectionNews(StateID id, EncodedString &&headline, NewsStyle style)
{
	TownID where = TownID::Invalid();
	if (const State *s = GetState(id); s != nullptr) {
		where = s->capital;
	} else {
		for (const State &st : _states) {
			if (st.kind == StateKind::FederalTerritory && Town::IsValidID(st.capital)) {
				where = st.capital;
				break;
			}
		}
	}
	if (Town::IsValidID(where)) {
		AddNewsItem(std::move(headline), NewsType::General, style, {}, where);
	} else {
		AddNewsItem(std::move(headline), NewsType::General, style, {});
	}
}

/**
 * The states whose assemblies are elected with parliament: those on the main land with towns.
 * @return How many.
 */
static uint CountStatesVotingWithParliament()
{
	uint states = 0;
	for (const State &s : _states) {
		if (s.kind == StateKind::State && !s.region && s.towns > 0) states++;
	}
	return states;
}

/**
 * Two months before polling day: the assembly (for a general election parliament and the assemblies of the
 * states on the main land) is dissolved; the government carries on as a caretaker.
 * @param id The region, or #INVALID_STATE for the general election.
 */
static void DissolveForElection(StateID id)
{
	const int year = TimerGameEconomy::year.base();
	if (id == INVALID_STATE) {
		const uint states = CountStatesVotingWithParliament();
		if (_federation.government == Platform::None) {
			AddElectionNews(id, GetEncodedString(STR_NEWS_ELECTION_CALLED_GENERAL, year, states), NewsStyle::Normal);
		} else {
			AddElectionNews(id, GetEncodedString(STR_NEWS_ELECTION_DISSOLVED_GENERAL, GetPlatformName(_federation.government), year, states), NewsStyle::Normal);
		}
		return;
	}
	const State &s = _states[id];
	if (s.government == Platform::None) {
		AddElectionNews(id, GetEncodedString(STR_NEWS_ELECTION_CALLED_STATE, GetStateName(id), year), NewsStyle::Small);
	} else {
		AddElectionNews(id, GetEncodedString(STR_NEWS_ELECTION_DISSOLVED_STATE, GetStateName(id), GetPlatformName(s.government), year), NewsStyle::Small);
	}
}

/**
 * A month before polling day: nomination day. Every platform stands in every seat; the polls say how it looks.
 * @param id The region, or #INVALID_STATE for the general election.
 */
static void HoldNominationDay(StateID id)
{
	const int year = TimerGameEconomy::year.base();
	if (id == INVALID_STATE) {
		const PlatformSeats poll = GetFederalPoll();
		const uint seats = GetTotalSeats(poll);
		AddElectionNews(id, GetEncodedString(STR_NEWS_ELECTION_NOMINATION_GENERAL, seats * PLATFORM_COUNT, seats, GetSeatsText(poll), year), NewsStyle::Normal);
		return;
	}
	const PlatformSeats poll = GetStatePoll(id);
	const uint seats = GetTotalSeats(poll);
	AddElectionNews(id, GetEncodedString(STR_NEWS_ELECTION_NOMINATION_STATE, GetStateName(id), seats * PLATFORM_COUNT, seats, GetSeatsText(poll), year), NewsStyle::Small);
}

/** Polling day of a general election: parliament, and the assemblies of the states on the main land, which vote with it. */
static void HoldGeneralElection()
{
	const int year = TimerGameEconomy::year.base();
	ElectionResult r;
	r.state = INVALID_STATE;
	r.year = year;
	r.month = GENERAL_ELECTION_MONTH;

	for (StateID id = 0; id < _states.size(); id++) {
		const State &s = _states[id];
		if (s.kind != StateKind::State || s.region || s.towns == 0) continue;
		const Platform before = s.government;
		const PlatformSeats seats = ElectAssembly(id, nullptr);
		r.assemblies.push_back({id, before, _states[id].government, seats});
	}

	const std::vector<TownVoters> voters = GetVoters(INVALID_STATE);
	const uint score = GetVotersScore(voters);
	/* Voters judge the federal government on their service, and on the projects of its plan companies delivered. */
	int bonus = GetIncumbentBonus(score, _federation.service_score, _federation.elected);
	if (_federation.government != Platform::None) bonus += GetFederalProjectVotes();
	for (const TownVoters &v : voters) {
		PlatformSeats here{};
		VoteInTown(v.town, v.concerns, _federation.government, bonus, true, here);
		for (uint p = 0; p < PLATFORM_COUNT; p++) r.seats[p] += here[p];
		r.towns.push_back({v.town->index, here});
	}
	r.before = _federation.government;
	_federation.government = FormGovernment(r.seats, r.before);
	r.after = _federation.government;
	_federation.seats = r.seats;
	_federation.elected = year;
	_federation.next_election = year + TERM_YEARS;
	_federation.service_score = static_cast<uint8_t>(score);
	Debug(Facility::Misc, Severity::Trace1, "[state] general election: seats {} {} {} of {}, government {} (was {}), score {}, bonus {}",
			r.seats[0], r.seats[1], r.seats[2], GetTotalSeats(r.seats), to_underlying(r.after), to_underlying(r.before), score, bonus);

	SortForCount(r.towns);
	const uint towns = static_cast<uint>(r.towns.size());
	const uint assemblies = static_cast<uint>(r.assemblies.size());
	KeepElection(std::move(r));
	AddElectionNews(INVALID_STATE, GetEncodedString(STR_NEWS_ELECTION_POLLING_GENERAL, towns, assemblies), NewsStyle::Normal);
	ShowElectionNightWindow(INVALID_STATE, true);
}

/**
 * Polling day of a region, which votes on its own.
 * @param id The region.
 */
static void HoldRegionElection(StateID id)
{
	ElectionResult r;
	r.state = id;
	r.year = TimerGameEconomy::year.base();
	r.month = REGION_ELECTION_MONTH;
	r.before = _states[id].government;
	r.seats = ElectAssembly(id, &r.towns);
	r.after = _states[id].government;
	if (r.towns.empty()) return;

	SortForCount(r.towns);
	const uint towns = static_cast<uint>(r.towns.size());
	KeepElection(std::move(r));
	AddElectionNews(id, GetEncodedString(STR_NEWS_ELECTION_POLLING_STATE, GetStateName(id), towns), NewsStyle::Normal);
	ShowElectionNightWindow(id, true);
}

/**
 * Two days after polling day: the results are declared.
 * @param r The election.
 */
static void DeclareElection(ElectionResult &r)
{
	r.declared = true;
	if (r.after == Platform::None) return;
	const StringID verb = r.before == r.after ? STR_NEWS_STATE_ELECTION_KEEPS : STR_NEWS_STATE_ELECTION_FORMS;
	const uint won = r.seats[to_underlying(r.after) - 1];
	if (r.state == INVALID_STATE) {
		std::string states;
		for (const ElectionAssembly &a : r.assemblies) {
			if (a.after == Platform::None || a.state >= _states.size()) continue;
			if (!states.empty()) states += ", ";
			states += GetString(STR_NEWS_ELECTION_ASSEMBLY_ITEM, GetPlatformName(a.after), GetStateName(a.state), a.seats[to_underlying(a.after) - 1], GetTotalSeats(a.seats));
		}
		AddElectionNews(INVALID_STATE, GetEncodedString(STR_NEWS_ELECTION_RESULT_GENERAL, GetPlatformName(r.after), won, GetTotalSeats(r.seats), GetSeatsText(r.seats), verb, states), NewsStyle::Normal);
		/* The government elected makes its five-year plan. */
		StartFederalPlan();
	} else if (r.state < _states.size()) {
		AddElectionNews(r.state, GetEncodedString(STR_NEWS_ELECTION_RESULT_STATE, GetPlatformName(r.after), GetStateName(r.state), won, GetTotalSeats(r.seats), GetSeatsText(r.seats), verb), NewsStyle::Normal);
	}
	InvalidateWindowClassesData(WindowClass::States);
	InvalidateWindowClassesData(WindowClass::ElectionNight);
}

/** Each day: declare the results two days after polling day. */
static const IntervalTimer<TimerGameEconomy> _states_daily({TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [](auto)
{
	if (!AreStatesFormed() || _game_mode == GameMode::Editor) return;
	const TimerGameEconomy::Date today = TimerGameEconomy::date;
	for (ElectionResult &r : _elections) {
		if (r.declared) continue;
		if (today >= TimerGameEconomy::ConvertYMDToDate(TimerGameEconomy::Year{r.year}, r.month, 1) + ELECTION_DECLARE_DAYS) DeclareElection(r);
	}
});

/* ------------------------------------------------------------------------
 * Money, each month and each year.
 * ------------------------------------------------------------------------ */

/**
 * A government spends on its platform in a town, from its treasury (or federal money for a federal territory).
 * @param t The town.
 * @param government The platform in power.
 * @param month_base A month of the town's taxes at the normal rate.
 * @param strength How strongly it carries out its platform, in percent.
 * @return What it spent.
 */
static Money SpendOnPlatform(Town *t, Platform government, Money month_base, uint strength)
{
	switch (government) {
		case Platform::TransitFirst:
			/* The grant goes to the town's transit budget (see GetTownTransitBudget); towns that small have none. */
			return GetTownTransitBudget(t) > 0 ? month_base * TRANSIT_GRANT_PERCENT * strength / 10000 : Money{0};

		case Platform::HomesAndServices: {
			const Money grant = month_base * PLATFORM_SPENDING_PERCENT * strength / 10000 / 2;
			t->council.services_fund += grant;
			t->council.estates_fund += grant;
			return 2 * grant;
		}

		case Platform::RoadsAndGrowth:
			/* Faster growth (see GetStateGrowthPercent) and, each quarter, new buildings funded in one town in turn. */
			return month_base * PLATFORM_SPENDING_PERCENT * strength / 10000;

		default:
			return 0;
	}
}

/**
 * Under Roads and Growth, each quarter a state funds new buildings for three months in one of its towns, each in turn.
 * @param id The state.
 */
static void FundBuildingsInTurn(StateID id)
{
	std::vector<Town *> towns;
	for (Town *t : Town::Iterate()) {
		if (t->state == id && t->cache.population >= 100) towns.push_back(t);
	}
	if (towns.empty()) return;
	const uint turn = static_cast<uint>(TimerGameEconomy::year.base()) * 4 + TimerGameEconomy::month / 3;
	Town *t = towns[turn % towns.size()];
	if (t->fund_buildings_months != 0) return;
	FundTownBuildingsByState(t);
	Debug(Facility::Misc, Severity::Trace1, "[state] state {} funds new buildings in town {} ({} people)", id, t->index, t->cache.population);
}

/** At the year's end: last year's figures; rich councils give the state a share of their reserves. */
static void StatesNewYear()
{
	for (State &s : _states) {
		s.premiums_last_year = s.premiums_this_year;
		s.levies_last_year = s.levies_this_year;
		s.grants_last_year = s.grants_this_year;
		s.spent_last_year = s.spent_this_year;
		s.premiums_this_year = 0;
		s.levies_this_year = 0;
		s.grants_this_year = 0;
		s.spent_this_year = 0;
	}
	_federation.grants_last_year = _federation.grants_this_year;
	_federation.grants_this_year = 0;
	_federation.programmes_last_year = _federation.programmes_this_year;
	_federation.programmes_this_year = 0;
	_federation.projects_last_year = _federation.projects_this_year;
	_federation.projects_this_year = 0;
	UpdateFederalYearly();

	for (Town *t : Town::Iterate()) {
		State *s = GetState(t->state);
		if (s == nullptr || s->kind != StateKind::State) continue;
		const Money keep = GetTownTaxBase(t) * LEVY_YEARS;
		if (t->budget.reserves <= keep) continue;
		const Money levy = (t->budget.reserves - keep) / 3;
		t->budget.reserves -= levy;
		s->treasury += levy;
		s->levies_this_year += levy;
	}

	RebuildStateMap(FindLands());
	for (StateID id = 0; id < _states.size(); id++) {
		const State &s = _states[id];
		Debug(Facility::Misc, Severity::Trace1, "[state] year {} state {} '{}': towns {} people {} government {} treasury {} premiums {} levies {} grants {} spent {}",
				TimerGameEconomy::year.base(), id, GetStateName(id), s.towns, s.population, to_underlying(GetGovernment(s)), s.treasury,
				s.premiums_last_year, s.levies_last_year, s.grants_last_year, s.spent_last_year);
	}
}

/** Each month: form the states if not yet; federal grants; what governments spend; elections. */
void UpdateStatesMonthly()
{
	if (!AreStatesOn() || _game_mode == GameMode::Editor) return;
	if (!_federation.formed) {
		FormStates();
		return;
	}
	UpdateStateCaches();
	if (TimerGameEconomy::month == 0) StatesNewYear();

	for (Town *t : Town::Iterate()) {
		if (t->state >= _states.size()) continue;
		State &s = _states[t->state];
		const Money month_base = GetTownTaxBase(t) / 12;
		if (s.kind == StateKind::FederalTerritory) {
			/* The City Hall's federal grant, into its services fund. */
			const Money grant = month_base * CITY_HALL_GRANT_PERCENT / 100;
			t->council.services_fund += grant;
			_federation.grants_this_year += grant;
			const Money spent = SpendOnPlatform(t, _federation.government, month_base, 100);
			s.spent_this_year += spent;
			_federation.grants_this_year += spent;
			continue;
		}
		const Money grant = month_base * FEDERAL_GRANT_PERCENT / 100;
		s.treasury += grant;
		s.grants_this_year += grant;
		_federation.grants_this_year += grant;
	}
	/* Governments spend on their platforms, more with money to spare. */
	for (StateID id = 0; id < _states.size(); id++) {
		State &s = _states[id];
		if (s.kind != StateKind::State || s.government == Platform::None) continue;
		const uint strength = GetPlatformStrength(id);
		if (strength == 0) continue;
		Money spent = 0;
		for (Town *t : Town::Iterate()) {
			if (t->state != id) continue;
			spent += SpendOnPlatform(t, s.government, GetTownTaxBase(t) / 12, strength);
		}
		/* The grants a month are what the federal grant brings in, at full strength; never more than the treasury has. */
		spent = std::min(spent, std::max<Money>(s.treasury, 0));
		s.treasury -= spent;
		s.spent_this_year += spent;
	}
	if (TimerGameEconomy::month % 3 == 0) {
		for (StateID id = 0; id < _states.size(); id++) {
			const State &s = _states[id];
			/* A caretaker starts nothing new. */
			if (s.kind == StateKind::State && s.government == Platform::RoadsAndGrowth && CanSpend(s) && s.towns > 0 && !IsElectionCampaign(id)) FundBuildingsInTurn(id);
		}
	}

	/* Elections: dissolution two months before polling day, nomination day a month before, polling day. */
	const int year = TimerGameEconomy::year.base();
	const uint month = TimerGameEconomy::month;
	if (year >= _federation.next_election) {
		if (month + 2 == GENERAL_ELECTION_MONTH) DissolveForElection(INVALID_STATE);
		if (month + 1 == GENERAL_ELECTION_MONTH) HoldNominationDay(INVALID_STATE);
		if (month == GENERAL_ELECTION_MONTH) HoldGeneralElection();
	}
	for (StateID id = 0; id < _states.size(); id++) {
		const State &s = _states[id];
		if (s.kind != StateKind::State || !s.region || s.towns == 0 || year < s.next_election) continue;
		if (month + 2 == REGION_ELECTION_MONTH) DissolveForElection(id);
		if (month + 1 == REGION_ELECTION_MONTH) HoldNominationDay(id);
		if (month == REGION_ELECTION_MONTH) HoldRegionElection(id);
	}

	/* The federal government's programmes and plan. */
	UpdateFederalMonthly();
	InvalidateWindowClassesData(WindowClass::States);
}

/**
 * Rename a state or federal territory.
 * @param flags Type of operation.
 * @param state_id The state.
 * @param text Its new name; empty for its usual name.
 * @return The cost of this operation or an error.
 */
CommandCost CmdRenameState(DoCommandFlags flags, StateID state_id, const std::string &text)
{
	State *s = GetState(state_id);
	if (s == nullptr || !AreStatesFormed()) return CMD_ERROR;
	if (!text.empty() && Utf8StringLength(text) >= MAX_LENGTH_STATE_NAME_CHARS) return CMD_ERROR;
	if (flags.Test(DoCommandFlag::Execute)) {
		s->name = text;
		InvalidateWindowClassesData(WindowClass::States);
		SetWindowClassesDirty(WindowClass::TownView);
		MarkWholeScreenDirty();
	}
	return CommandCost();
}

/**
 * Set who governs a state; for a federal territory, the federal government. For game scripts.
 * @param flags Type of operation.
 * @param state_id The state.
 * @param platform The platform in power.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetStateGovernment(DoCommandFlags flags, StateID state_id, Platform platform)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	State *s = GetState(state_id);
	if (s == nullptr || !AreStatesFormed() || platform >= Platform::End) return CMD_ERROR;
	if (flags.Test(DoCommandFlag::Execute)) {
		if (s->kind == StateKind::FederalTerritory) {
			_federation.government = platform;
		} else {
			s->government = platform;
		}
		InvalidateWindowClassesData(WindowClass::States);
		SetWindowClassesDirty(WindowClass::TownView);
	}
	return CommandCost();
}

/** Forget the states; used when starting or loading a game. */
void InitializeStates()
{
	_states.clear();
	_federation = {};
	_state_cells.clear();
	_state_cell_kinds.clear();
	_elections.clear();
	InitializeFederal();
}
