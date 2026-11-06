/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file traffic_light_sl.cpp Code handling saving and loading of traffic lights.
 *
 * Traffic lights live in their own table chunk. Savegames without the chunk
 * (every savegame made by a normal OpenTTD) simply have no traffic lights, so
 * they load without any conversion. Being a table chunk, fields can be added
 * later without breaking savegames made with this version.
 */

#include "../stdafx.h"

#include "saveload.h"

#include "../traffic_light.h"

#include "../safeguards.h"

static const SaveLoad _traffic_light_desc[] = {
	SaveLoad::Variable<VarFileType::U32>("tile", SLE_OBJECT_ADDRESS(TrafficLight, tile)),
	SaveLoad::Variable<VarFileType::U8>("owner", SLE_OBJECT_ADDRESS(TrafficLight, owner)),
	SaveLoad::Variable<VarFileType::U8>("mode", SLE_OBJECT_ADDRESS(TrafficLight, mode)),
	SaveLoad::Variable<VarFileType::U16>("green_x", SLE_OBJECT_ADDRESS(TrafficLight, green_x)),
	SaveLoad::Variable<VarFileType::U16>("green_y", SLE_OBJECT_ADDRESS(TrafficLight, green_y)),
	SaveLoad::Variable<VarFileType::U16>("min_green", SLE_OBJECT_ADDRESS(TrafficLight, min_green)),
	SaveLoad::Variable<VarFileType::U16>("offset", SLE_OBJECT_ADDRESS(TrafficLight, offset)),
	SaveLoad::Variable<VarFileType::U8>("green_axis", SLE_OBJECT_ADDRESS(TrafficLight, green_axis)),
	SaveLoad::Variable<VarFileType::Bool>("clearing", SLE_OBJECT_ADDRESS(TrafficLight, clearing)),
	SaveLoad::Variable<VarFileType::U16>("phase_ticks", SLE_OBJECT_ADDRESS(TrafficLight, phase_ticks)),
	SaveLoad::Variable<VarFileType::U16>("gap_ticks", SLE_OBJECT_ADDRESS(TrafficLight, gap_ticks)),
	SaveLoad::Variable<VarFileType::Bool>("bus_priority", SLE_OBJECT_ADDRESS(TrafficLight, bus_priority)),
};

/** Traffic lights at road junctions. */
struct TRFLChunkHandler : ChunkHandler {
	TRFLChunkHandler() : ChunkHandler("TRFL", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_traffic_light_desc);

		int index = 0;
		for (auto &[tile, tl] : _traffic_lights) {
			SlSetArrayIndex(index++);
			SlObject(&tl, _traffic_light_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_traffic_light_desc);

		_traffic_lights.clear();
		while (SlIterateArray() != -1) {
			TrafficLight tl;
			SlObject(&tl, slt);
			_traffic_lights[tl.tile] = tl;
		}
	}
};

static const TRFLChunkHandler TRFL;
static const ChunkHandlerRef traffic_light_chunk_handlers[] = {
	TRFL,
};

extern const ChunkHandlerTable _traffic_light_chunk_handlers(traffic_light_chunk_handlers);
