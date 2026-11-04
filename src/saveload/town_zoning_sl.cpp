/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file town_zoning_sl.cpp Code handling saving and loading of zoning.
 *
 * Zoned tiles live in their own table chunk. Savegames without it have no
 * zoning, so towns build as usual there.
 */

#include "../stdafx.h"

#include "saveload.h"

#include "../town_zoning.h"

#include "../safeguards.h"

/** One zoned tile, for saving and loading. */
struct TownZoneEntry {
	TileIndex tile = INVALID_TILE; ///< The tile.
	TownZone zone = TownZone::None; ///< Its zone.
};

static const SaveLoad _town_zone_desc[] = {
	SaveLoad::Variable<VarFileType::U32>("tile", SLE_OBJECT_ADDRESS(TownZoneEntry, tile)),
	SaveLoad::Variable<VarFileType::U8>("zone", SLE_OBJECT_ADDRESS(TownZoneEntry, zone)),
};

/** Zoned tiles. */
struct ZONEChunkHandler : ChunkHandler {
	ZONEChunkHandler() : ChunkHandler("ZONE", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_town_zone_desc);

		/* Save in tile order, so equal games make equal savegames. */
		std::vector<TownZoneEntry> entries;
		entries.reserve(_town_zones.size());
		for (const auto &[tile, zone] : _town_zones) entries.push_back({tile, zone});
		std::ranges::sort(entries, {}, &TownZoneEntry::tile);

		int index = 0;
		for (TownZoneEntry &entry : entries) {
			SlSetArrayIndex(index++);
			SlObject(&entry, _town_zone_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_town_zone_desc);

		_town_zones.clear();
		while (SlIterateArray() != -1) {
			TownZoneEntry entry;
			SlObject(&entry, slt);
			_town_zones[entry.tile] = entry.zone;
		}
	}
};

static const ZONEChunkHandler ZONE;
static const ChunkHandlerRef town_zone_chunk_handlers[] = {
	ZONE,
};

extern const ChunkHandlerTable _town_zone_chunk_handlers(town_zone_chunk_handlers);
