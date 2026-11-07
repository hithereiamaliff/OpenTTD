/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file environment_sl.cpp Code handling saving and loading of air pollution and land value.
 *
 * The maps of air pollution and land value, by cell of 4 by 4 tiles, are kept
 * in chunk ENVR, and each town's figures in chunk TENV. Savegames without
 * them start without pollution and land value, which the first month works
 * out.
 */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../environment.h"
#include "../map_func.h"
#include "../town.h"

#include "../safeguards.h"

extern std::vector<uint16_t> _env_pollution;
extern std::vector<uint8_t> _env_land_value;

static const SaveLoad _environment_desc[] = {
	SaveLoad::Vector<VarFileType::U16>("pollution", SLE_GLOBAL_ADDRESS(_env_pollution)),
	SaveLoad::Vector<VarFileType::U8>("land_value", SLE_GLOBAL_ADDRESS(_env_land_value)),
};

/** The maps of air pollution and land value. */
struct ENVRChunkHandler : ChunkHandler {
	ENVRChunkHandler() : ChunkHandler("ENVR", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_environment_desc);

		SlSetArrayIndex(0);
		SlGlobList(_environment_desc);
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_environment_desc);

		if (SlIterateArray() == -1) return;
		SlGlobList(slt);
		if (SlIterateArray() != -1) SlErrorCorrupt("Too many ENVR entries");

		/* Maps of another size than this map's cells are of no use; the next month works them out again. */
		const size_t cells = static_cast<size_t>(std::max(1U, Map::SizeX() / 4)) * std::max(1U, Map::SizeY() / 4);
		if (_env_pollution.size() != cells || _env_land_value.size() != cells) {
			_env_pollution.clear();
			_env_land_value.clear();
		}
	}
};

static const SaveLoad _town_environment_desc[] = {
	SaveLoad::Variable<VarFileType::U16>("api", SLE_OBJECT_ADDRESS(Town, environment.api)),
	SaveLoad::Variable<VarFileType::U8>("land_value", SLE_OBJECT_ADDRESS(Town, environment.land_value)),
	SaveLoad::Variable<VarFileType::Bool>("unhealthy", SLE_OBJECT_ADDRESS(Town, environment.unhealthy)),
};

/** Each town's air and land value, by town. */
struct TENVChunkHandler : ChunkHandler {
	TENVChunkHandler() : ChunkHandler("TENV", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_town_environment_desc);

		for (Town *t : Town::Iterate()) {
			SlSetArrayIndex(t->index);
			SlObject(t, _town_environment_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_town_environment_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			if (!Town::IsValidID(index)) SlErrorCorrupt("Environment figures for a town that doesn't exist");
			SlObject(Town::Get(index), slt);
		}
	}
};

static const ENVRChunkHandler ENVR;
static const TENVChunkHandler TENV;
static const ChunkHandlerRef environment_chunk_handlers[] = {
	ENVR,
	TENV,
};

extern const ChunkHandlerTable _environment_chunk_handlers(environment_chunk_handlers);
