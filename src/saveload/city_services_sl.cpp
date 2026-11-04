/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file city_services_sl.cpp Code handling saving and loading of city service coverage.
 *
 * The city service buildings themselves are objects (chunk OBJS). This chunk
 * keeps each town's coverage as of the last monthly update, because town
 * growth uses it until the next one. Savegames without it have no coverage.
 */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../town.h"

#include "../safeguards.h"

/** Coverage of one town, for saving and loading. */
struct TownServiceEntry {
	uint8_t police = 0; ///< Percentage covered by police.
	uint8_t fire = 0; ///< Percentage covered by fire stations.
	uint8_t health = 0; ///< Percentage covered by clinics.
	uint8_t education = 0; ///< Percentage covered by schools.
	uint8_t park = 0; ///< Percentage covered by parks.
};

static const SaveLoad _town_service_desc[] = {
	SaveLoad::Variable<VarFileType::U8>("police", SLE_OBJECT_ADDRESS(TownServiceEntry, police)),
	SaveLoad::Variable<VarFileType::U8>("fire", SLE_OBJECT_ADDRESS(TownServiceEntry, fire)),
	SaveLoad::Variable<VarFileType::U8>("health", SLE_OBJECT_ADDRESS(TownServiceEntry, health)),
	SaveLoad::Variable<VarFileType::U8>("education", SLE_OBJECT_ADDRESS(TownServiceEntry, education)),
	SaveLoad::Variable<VarFileType::U8>("park", SLE_OBJECT_ADDRESS(TownServiceEntry, park)),
};

static_assert(CITY_SERVICE_COUNT == 5, "Add new city services to TownServiceEntry");

/** City service coverage per town, indexed by town. */
struct CSVCChunkHandler : ChunkHandler {
	CSVCChunkHandler() : ChunkHandler("CSVC", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_town_service_desc);

		for (const Town *t : Town::Iterate()) {
			const auto &p = t->service_coverage.percent;
			TownServiceEntry entry{p[0], p[1], p[2], p[3], p[4]};
			SlSetArrayIndex(t->index);
			SlObject(&entry, _town_service_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_town_service_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			TownServiceEntry entry;
			SlObject(&entry, slt);
			if (!Town::IsValidID(index)) SlErrorCorrupt("City service coverage for a town that doesn't exist");
			Town::Get(index)->service_coverage.percent = {entry.police, entry.fire, entry.health, entry.education, entry.park};
		}
	}
};

static const CSVCChunkHandler CSVC;
static const ChunkHandlerRef city_service_chunk_handlers[] = {
	CSVC,
};

extern const ChunkHandlerTable _city_service_chunk_handlers(city_service_chunk_handlers);
