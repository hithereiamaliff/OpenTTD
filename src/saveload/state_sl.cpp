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
 * states in the first month. The last elections (for the election night
 * window and the results still to declare) are kept in SELN, the projects
 * of the federal government's five-year plan in SFPR.
 */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../federal.h"
#include "../map_func.h"
#include "../state.h"
#include "../town.h"

#include "../safeguards.h"

extern std::vector<State> _states;
extern Federation _federation;
extern std::vector<uint8_t> _state_cells;
extern std::vector<uint8_t> _state_cell_kinds;
extern std::vector<ElectionResult> _elections;
extern std::vector<FederalProject> _federal_projects;

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
	SaveLoad::Variable<VarFileType::U8>("plan", SLE_GLOBAL_ADDRESS(_federation.plan)),
	SaveLoad::Variable<VarFileType::I32>("plan_year", SLE_GLOBAL_ADDRESS(_federation.plan_year)),
	SaveLoad::Variable<VarFileType::I64>("programmes_this_year", SLE_GLOBAL_ADDRESS(_federation.programmes_this_year)),
	SaveLoad::Variable<VarFileType::I64>("programmes_last_year", SLE_GLOBAL_ADDRESS(_federation.programmes_last_year)),
	SaveLoad::Variable<VarFileType::I64>("projects_this_year", SLE_GLOBAL_ADDRESS(_federation.projects_this_year)),
	SaveLoad::Variable<VarFileType::I64>("projects_last_year", SLE_GLOBAL_ADDRESS(_federation.projects_last_year)),
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

/** The seats each town gave in an election. */
class SlElectionTowns : public VectorSaveLoadHandler<SlElectionTowns, ElectionResult, ElectionTown> {
public:
	static inline const SaveLoad description[] = {
		SaveLoad::Variable<VarFileType::U16>("town", SLE_OBJECT_ADDRESS(ElectionTown, town)),
		SaveLoad::Array<VarFileType::U16, PLATFORM_COUNT>("seats", SLE_OBJECT_ADDRESS(ElectionTown, seats)),
	};
	static inline const SaveLoadCompatTable compat_description = {};

	std::vector<ElectionTown> &GetVector(ElectionResult *r) const override { return r->towns; }
};

/** The states' assemblies elected with parliament. */
class SlElectionAssemblies : public VectorSaveLoadHandler<SlElectionAssemblies, ElectionResult, ElectionAssembly> {
public:
	static inline const SaveLoad description[] = {
		SaveLoad::Variable<VarFileType::U8>("state", SLE_OBJECT_ADDRESS(ElectionAssembly, state)),
		SaveLoad::Variable<VarFileType::U8>("before", SLE_OBJECT_ADDRESS(ElectionAssembly, before)),
		SaveLoad::Variable<VarFileType::U8>("after", SLE_OBJECT_ADDRESS(ElectionAssembly, after)),
		SaveLoad::Array<VarFileType::U16, PLATFORM_COUNT>("seats", SLE_OBJECT_ADDRESS(ElectionAssembly, seats)),
	};
	static inline const SaveLoadCompatTable compat_description = {};

	std::vector<ElectionAssembly> &GetVector(ElectionResult *r) const override { return r->assemblies; }
};

static const SaveLoad _election_desc[] = {
	SaveLoad::Variable<VarFileType::U8>("state", SLE_OBJECT_ADDRESS(ElectionResult, state)),
	SaveLoad::Variable<VarFileType::I32>("year", SLE_OBJECT_ADDRESS(ElectionResult, year)),
	SaveLoad::Variable<VarFileType::U8>("month", SLE_OBJECT_ADDRESS(ElectionResult, month)),
	SaveLoad::Variable<VarFileType::U8>("before", SLE_OBJECT_ADDRESS(ElectionResult, before)),
	SaveLoad::Variable<VarFileType::U8>("after", SLE_OBJECT_ADDRESS(ElectionResult, after)),
	SaveLoad::Array<VarFileType::U16, PLATFORM_COUNT>("seats", SLE_OBJECT_ADDRESS(ElectionResult, seats)),
	SaveLoad::Variable<VarFileType::Bool>("declared", SLE_OBJECT_ADDRESS(ElectionResult, declared)),
	SaveLoad::StructList<SlElectionTowns>("towns"),
	SaveLoad::StructList<SlElectionAssemblies>("assemblies"),
};

/** The last general election and the last election of each region. */
struct SELNChunkHandler : ChunkHandler {
	SELNChunkHandler() : ChunkHandler("SELN", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_election_desc);

		for (size_t i = 0; i < _elections.size(); i++) {
			SlSetArrayIndex(i);
			SlObject(&_elections[i], _election_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_election_desc);

		_elections.clear();
		while (SlIterateArray() != -1) {
			ElectionResult &r = _elections.emplace_back();
			SlObject(&r, slt);
			if (r.before >= Platform::End) r.before = Platform::None;
			if (r.after >= Platform::End) r.after = Platform::None;
			for (ElectionAssembly &a : r.assemblies) {
				if (a.before >= Platform::End) a.before = Platform::None;
				if (a.after >= Platform::End) a.after = Platform::None;
			}
		}
	}

	void FixPointers() const override
	{
		/* After the states are loaded: only elections of states that are there. */
		std::erase_if(_elections, [](const ElectionResult &r) { return r.state != INVALID_STATE && r.state >= _states.size(); });
		for (ElectionResult &r : _elections) {
			std::erase_if(r.assemblies, [](const ElectionAssembly &a) { return a.state >= _states.size(); });
		}
	}
};

static const SaveLoad _federal_project_desc[] = {
	SaveLoad::Variable<VarFileType::U8>("type", SLE_OBJECT_ADDRESS(FederalProject, type)),
	SaveLoad::Variable<VarFileType::U8>("state", SLE_OBJECT_ADDRESS(FederalProject, state)),
	SaveLoad::Variable<VarFileType::U8>("plan", SLE_OBJECT_ADDRESS(FederalProject, plan)),
	SaveLoad::Variable<VarFileType::U16>("town", SLE_OBJECT_ADDRESS(FederalProject, town)),
	SaveLoad::Variable<VarFileType::U16>("town2", SLE_OBJECT_ADDRESS(FederalProject, town2)),
	SaveLoad::Variable<VarFileType::U8>("company", SLE_OBJECT_ADDRESS(FederalProject, company)),
	SaveLoad::Variable<VarFileType::I64>("payment", SLE_OBJECT_ADDRESS(FederalProject, payment)),
	SaveLoad::Variable<VarFileType::I32>("offered", SLE_OBJECT_ADDRESS(FederalProject, offered)),
	SaveLoad::Variable<VarFileType::I32>("done", SLE_OBJECT_ADDRESS(FederalProject, done)),
};

/** The projects of the federal government's five-year plan, and of the plan before it. */
struct SFPRChunkHandler : ChunkHandler {
	SFPRChunkHandler() : ChunkHandler("SFPR", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_federal_project_desc);

		for (size_t i = 0; i < _federal_projects.size(); i++) {
			SlSetArrayIndex(i);
			SlObject(&_federal_projects[i], _federal_project_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_federal_project_desc);

		_federal_projects.clear();
		while (SlIterateArray() != -1) {
			FederalProject &p = _federal_projects.emplace_back();
			SlObject(&p, slt);
		}
		/* Keep what makes sense. */
		std::erase_if(_federal_projects, [](const FederalProject &p) { return p.type >= FederalProjectType::End || p.state >= FederalProjectState::End; });
	}
};

static const STATChunkHandler STAT;
static const SFEDChunkHandler SFED;
static const SMAPChunkHandler SMAP;
static const TSTAChunkHandler TSTA;
static const SELNChunkHandler SELN;
static const SFPRChunkHandler SFPR;
static const ChunkHandlerRef state_chunk_handlers[] = {
	STAT,
	SFED,
	SMAP,
	TSTA,
	SELN,
	SFPR,
};

extern const ChunkHandlerTable _state_chunk_handlers(state_chunk_handlers);
