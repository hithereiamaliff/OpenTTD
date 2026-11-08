/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file state_sl.cpp Code handling saving and loading of states.
 *
 * The states and federal territories are kept in chunk STAT, the federation
 * (the federal government and its elections) in SFED, the map of states in
 * SMAP and each town's state in TSTA. Savegames without them form their
 * states in the first month.
 */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../map_func.h"
#include "../state.h"
#include "../town.h"

#include "../safeguards.h"

extern std::vector<State> _states;
extern Federation _federation;
extern std::vector<uint8_t> _state_cells;
extern std::vector<uint8_t> _state_cell_kinds;

static const SaveLoad _state_desc[] = {
	SaveLoad::Variable<VarFileType::U16>("capital", SLE_OBJECT_ADDRESS(State, capital)),
	SaveLoad::String("name", SLE_OBJECT_ADDRESS(State, name), StringValidationSetting::AllowControlCode),
	SaveLoad::Variable<VarFileType::U8>("kind", SLE_OBJECT_ADDRESS(State, kind)),
	SaveLoad::Variable<VarFileType::Bool>("region", SLE_OBJECT_ADDRESS(State, region)),
	SaveLoad::Variable<VarFileType::U8>("colour", SLE_OBJECT_ADDRESS(State, colour)),
	SaveLoad::Variable<VarFileType::U8>("government", SLE_OBJECT_ADDRESS(State, government)),
	SaveLoad::Array<VarFileType::U16, PLATFORM_COUNT>("seats", SLE_OBJECT_ADDRESS(State, seats)),
	SaveLoad::Variable<VarFileType::I32>("elected", SLE_OBJECT_ADDRESS(State, elected)),
	SaveLoad::Variable<VarFileType::I32>("next_election", SLE_OBJECT_ADDRESS(State, next_election)),
	SaveLoad::Variable<VarFileType::U8>("service_score", SLE_OBJECT_ADDRESS(State, service_score)),
	SaveLoad::Variable<VarFileType::I32>("formed", SLE_OBJECT_ADDRESS(State, formed)),
	SaveLoad::Variable<VarFileType::I64>("treasury", SLE_OBJECT_ADDRESS(State, treasury)),
	SaveLoad::Variable<VarFileType::I64>("premiums_this_year", SLE_OBJECT_ADDRESS(State, premiums_this_year)),
	SaveLoad::Variable<VarFileType::I64>("premiums_last_year", SLE_OBJECT_ADDRESS(State, premiums_last_year)),
	SaveLoad::Variable<VarFileType::I64>("levies_this_year", SLE_OBJECT_ADDRESS(State, levies_this_year)),
	SaveLoad::Variable<VarFileType::I64>("levies_last_year", SLE_OBJECT_ADDRESS(State, levies_last_year)),
	SaveLoad::Variable<VarFileType::I64>("grants_this_year", SLE_OBJECT_ADDRESS(State, grants_this_year)),
	SaveLoad::Variable<VarFileType::I64>("grants_last_year", SLE_OBJECT_ADDRESS(State, grants_last_year)),
	SaveLoad::Variable<VarFileType::I64>("spent_this_year", SLE_OBJECT_ADDRESS(State, spent_this_year)),
	SaveLoad::Variable<VarFileType::I64>("spent_last_year", SLE_OBJECT_ADDRESS(State, spent_last_year)),
};

/** The states and federal territories, by index. */
struct STATChunkHandler : ChunkHandler {
	STATChunkHandler() : ChunkHandler("STAT", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_state_desc);

		for (size_t i = 0; i < _states.size(); i++) {
			SlSetArrayIndex(i);
			SlObject(&_states[i], _state_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_state_desc);

		_states.clear();
		int index;
		while ((index = SlIterateArray()) != -1) {
			if (static_cast<size_t>(index) != _states.size() || _states.size() >= MAX_STATES) SlErrorCorrupt("Invalid state index");
			State &s = _states.emplace_back();
			SlObject(&s, slt);
			/* Keep what makes sense. */
			if (s.kind >= StateKind::End) s.kind = StateKind::State;
			if (s.government >= Platform::End) s.government = Platform::None;
			s.service_score = std::min<uint8_t>(s.service_score, 100);
		}
	}
};

static const SaveLoad _federation_desc[] = {
	SaveLoad::Variable<VarFileType::Bool>("formed", SLE_GLOBAL_ADDRESS(_federation.formed)),
	SaveLoad::Variable<VarFileType::I32>("formed_year", SLE_GLOBAL_ADDRESS(_federation.formed_year)),
	SaveLoad::Variable<VarFileType::U8>("government", SLE_GLOBAL_ADDRESS(_federation.government)),
	SaveLoad::Array<VarFileType::U16, PLATFORM_COUNT>("seats", SLE_GLOBAL_ADDRESS(_federation.seats)),
	SaveLoad::Variable<VarFileType::I32>("elected", SLE_GLOBAL_ADDRESS(_federation.elected)),
	SaveLoad::Variable<VarFileType::I32>("next_election", SLE_GLOBAL_ADDRESS(_federation.next_election)),
	SaveLoad::Variable<VarFileType::U8>("service_score", SLE_GLOBAL_ADDRESS(_federation.service_score)),
	SaveLoad::Variable<VarFileType::I64>("grants_this_year", SLE_GLOBAL_ADDRESS(_federation.grants_this_year)),
	SaveLoad::Variable<VarFileType::I64>("grants_last_year", SLE_GLOBAL_ADDRESS(_federation.grants_last_year)),
};

/** The federation. */
struct SFEDChunkHandler : ChunkHandler {
	SFEDChunkHandler() : ChunkHandler("SFED", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_federation_desc);

		SlSetArrayIndex(0);
		SlGlobList(_federation_desc);
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_federation_desc);

		if (SlIterateArray() == -1) return;
		SlGlobList(slt);
		if (SlIterateArray() != -1) SlErrorCorrupt("Too many SFED entries");
		if (_federation.government >= Platform::End) _federation.government = Platform::None;
		_federation.service_score = std::min<uint8_t>(_federation.service_score, 100);
	}
};

static const SaveLoad _state_map_desc[] = {
	SaveLoad::Vector<VarFileType::U8>("states", SLE_GLOBAL_ADDRESS(_state_cells)),
	SaveLoad::Vector<VarFileType::U8>("kinds", SLE_GLOBAL_ADDRESS(_state_cell_kinds)),
};

/** The map of states. */
struct SMAPChunkHandler : ChunkHandler {
	SMAPChunkHandler() : ChunkHandler("SMAP", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_state_map_desc);

		SlSetArrayIndex(0);
		SlGlobList(_state_map_desc);
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_state_map_desc);

		if (SlIterateArray() == -1) return;
		SlGlobList(slt);
		if (SlIterateArray() != -1) SlErrorCorrupt("Too many SMAP entries");

		/* A map of another size than this map's cells is of no use; the next year works it out again. */
		const size_t cells = static_cast<size_t>(std::max(1U, Map::SizeX() / STATE_CELL_SIZE)) * std::max(1U, Map::SizeY() / STATE_CELL_SIZE);
		if (_state_cells.size() != cells || _state_cell_kinds.size() != cells) {
			_state_cells.clear();
			_state_cell_kinds.clear();
		}
		for (uint8_t &kind : _state_cell_kinds) {
			if (kind > to_underlying(StateCell::RegionWaters)) kind = to_underlying(StateCell::Land);
		}
	}

	void FixPointers() const override
	{
		/* After the states are loaded: no cell may name one that isn't there. */
		for (uint8_t &state : _state_cells) {
			if (state >= _states.size()) state = INVALID_STATE;
		}
	}
};

static const SaveLoad _town_state_desc[] = {
	SaveLoad::Variable<VarFileType::U8>("state", SLE_OBJECT_ADDRESS(Town, state)),
};

/** Each town's state, by town. */
struct TSTAChunkHandler : ChunkHandler {
	TSTAChunkHandler() : ChunkHandler("TSTA", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_town_state_desc);

		for (Town *t : Town::Iterate()) {
			SlSetArrayIndex(t->index);
			SlObject(t, _town_state_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_town_state_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			if (!Town::IsValidID(index)) SlErrorCorrupt("State of a town that doesn't exist");
			SlObject(Town::Get(index), slt);
		}
	}

	void FixPointers() const override
	{
		/* After the states are loaded: a town may only be in one that is there; capitals must be towns. */
		for (Town *t : Town::Iterate()) {
			if (t->state >= _states.size()) t->state = INVALID_STATE;
		}
		for (State &s : _states) {
			if (!Town::IsValidID(s.capital)) s.capital = TownID::Invalid();
		}
		UpdateStateCaches();
	}
};

static const STATChunkHandler STAT;
static const SFEDChunkHandler SFED;
static const SMAPChunkHandler SMAP;
static const TSTAChunkHandler TSTA;
static const ChunkHandlerRef state_chunk_handlers[] = {
	STAT,
	SFED,
	SMAP,
	TSTA,
};

extern const ChunkHandlerTable _state_chunk_handlers(state_chunk_handlers);
