/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file commuting_sl.cpp Code handling saving and loading of towns' commutes.
 *
 * Each town's commuters, the shares taking transit by cell and the cars on
 * each road are kept in chunk TCOM. Savegames without it start with towns
 * that work out their commutes within a month; until then their traffic is
 * worked out as before.
 */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../commuting.h"
#include "../map_func.h"
#include "../town.h"

#include "../safeguards.h"

static const SaveLoad _town_commuting_desc[] = {
	SaveLoad::Variable<VarFileType::Bool>("valid", SLE_OBJECT_ADDRESS(Town, commuting.valid)),
	SaveLoad::Variable<VarFileType::U8>("cell_size", SLE_OBJECT_ADDRESS(Town, commuting.cell_size)),
	SaveLoad::Variable<VarFileType::U32>("commuters", SLE_OBJECT_ADDRESS(Town, commuting.commuters)),
	SaveLoad::Variable<VarFileType::U32>("by_transit", SLE_OBJECT_ADDRESS(Town, commuting.by_transit)),
	SaveLoad::Variable<VarFileType::U32>("car_time", SLE_OBJECT_ADDRESS(Town, commuting.car_time)),
	SaveLoad::Variable<VarFileType::U32>("transit_time", SLE_OBJECT_ADDRESS(Town, commuting.transit_time)),
	SaveLoad::Variable<VarFileType::U16>("grid_x", SLE_OBJECT_ADDRESS(Town, commuting.grid_x)),
	SaveLoad::Variable<VarFileType::U16>("grid_y", SLE_OBJECT_ADDRESS(Town, commuting.grid_y)),
	SaveLoad::Variable<VarFileType::U16>("grid_w", SLE_OBJECT_ADDRESS(Town, commuting.grid_w)),
	SaveLoad::Variable<VarFileType::U16>("grid_h", SLE_OBJECT_ADDRESS(Town, commuting.grid_h)),
	SaveLoad::Vector<VarFileType::U8>("home_share", SLE_OBJECT_ADDRESS(Town, commuting.home_share)),
	SaveLoad::Vector<VarFileType::U8>("job_share", SLE_OBJECT_ADDRESS(Town, commuting.job_share)),
	SaveLoad::Vector<VarFileType::U8>("home_appeal", SLE_OBJECT_ADDRESS(Town, commuting.home_appeal)),
	SaveLoad::Vector<VarFileType::U8>("job_appeal", SLE_OBJECT_ADDRESS(Town, commuting.job_appeal)),
	SaveLoad::Vector<VarFileType::U16>("link_towns", SLE_OBJECT_ADDRESS(Town, commuting.link_towns)),
	SaveLoad::Vector<VarFileType::U32>("link_commuters", SLE_OBJECT_ADDRESS(Town, commuting.link_commuters)),
	SaveLoad::Vector<VarFileType::U32>("link_transit", SLE_OBJECT_ADDRESS(Town, commuting.link_transit)),
	SaveLoad::Vector<VarFileType::U32>("flow_tiles", SLE_OBJECT_ADDRESS(Town, commuting.flow_tiles)),
	SaveLoad::Vector<VarFileType::U16>("flow_cars", SLE_OBJECT_ADDRESS(Town, commuting.flow_cars)),
};

/** Each town's commutes, by town. */
struct TCOMChunkHandler : ChunkHandler {
	TCOMChunkHandler() : ChunkHandler("TCOM", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_town_commuting_desc);

		for (Town *t : Town::Iterate()) {
			SlSetArrayIndex(t->index);
			SlObject(t, _town_commuting_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_town_commuting_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			if (!Town::IsValidID(index)) SlErrorCorrupt("Commutes of a town that doesn't exist");
			Town *t = Town::Get(index);
			SlObject(t, slt);
			TownCommuting &c = t->commuting;
			/* Figures that don't fit together, or roads off the map, are of no use: the town works them out again. */
			const size_t cells = static_cast<size_t>(c.grid_w) * c.grid_h;
			bool sane = (c.cell_size == 4 || c.cell_size == 8) && c.home_share.size() == cells && c.job_share.size() == cells &&
					c.home_appeal.size() == cells && c.job_appeal.size() == cells && c.flow_tiles.size() == c.flow_cars.size() &&
					c.link_towns.size() == c.link_commuters.size() && c.link_towns.size() == c.link_transit.size();
			for (size_t i = 0; sane && i < c.flow_tiles.size(); i++) {
				if (c.flow_tiles[i] >= Map::Size() || (i > 0 && c.flow_tiles[i] <= c.flow_tiles[i - 1])) sane = false;
			}
			if (!sane) c = {};
		}
		RebuildCommuteFlows();
	}
};

static const TCOMChunkHandler TCOM;
static const ChunkHandlerRef commuting_chunk_handlers[] = {
	TCOM,
};

extern const ChunkHandlerTable _commuting_chunk_handlers(commuting_chunk_handlers);
