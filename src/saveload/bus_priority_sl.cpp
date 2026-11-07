/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file bus_priority_sl.cpp Code handling saving and loading of bus lanes and give-way markings at stops. */

#include "../stdafx.h"

#include "saveload.h"

#include "../bus_priority.h"
#include "../map_func.h"
#include "../brt.h"

#include "../safeguards.h"

/** A bus lane on a tile, for saving. */
struct BusLaneSave {
	TileIndex tile = INVALID_TILE; ///< The road tile.
	uint8_t lane = 0; ///< The #BusLane.
};

static const SaveLoad _bus_lane_desc[] = {
	SaveLoad::Variable<VarFileType::U32>("tile", SLE_OBJECT_ADDRESS(BusLaneSave, tile)),
	SaveLoad::Variable<VarFileType::U8>("lane", SLE_OBJECT_ADDRESS(BusLaneSave, lane)),
};

/** Bus lanes (see bus_priority.h). */
struct BLANChunkHandler : ChunkHandler {
	BLANChunkHandler() : ChunkHandler("BLAN", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_bus_lane_desc);

		int index = 0;
		for (const auto &[tile, lane] : GetBusLanes()) {
			BusLaneSave save{tile, static_cast<uint8_t>(lane)};
			SlSetArrayIndex(index++);
			SlObject(&save, _bus_lane_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_bus_lane_desc);

		GetBusLanes().clear();
		while (SlIterateArray() != -1) {
			BusLaneSave save;
			SlObject(&save, slt);
			if (!IsValidTile(save.tile) || save.lane == 0 || save.lane >= static_cast<uint8_t>(BusLane::End)) continue;
			GetBusLanes()[save.tile] = static_cast<BusLane>(save.lane);
		}
	}
};

/** A stop with give-way markings, for saving. */
struct GiveWaySave {
	TileIndex tile = INVALID_TILE; ///< The stop.
};

static const SaveLoad _give_way_desc[] = {
	SaveLoad::Variable<VarFileType::U32>("tile", SLE_OBJECT_ADDRESS(GiveWaySave, tile)),
};

/** Stops with give-way markings (see bus_priority.h). */
struct GWAYChunkHandler : ChunkHandler {
	GWAYChunkHandler() : ChunkHandler("GWAY", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_give_way_desc);

		int index = 0;
		for (TileIndex tile : GetGiveWayStops()) {
			GiveWaySave save{tile};
			SlSetArrayIndex(index++);
			SlObject(&save, _give_way_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_give_way_desc);

		GetGiveWayStops().clear();
		while (SlIterateArray() != -1) {
			GiveWaySave save;
			SlObject(&save, slt);
			if (IsValidTile(save.tile)) GetGiveWayStops().insert(save.tile);
		}
	}
};

/** A BRT station, for saving. */
struct BrtStationSave {
	TileIndex tile = INVALID_TILE; ///< The stop.
};

static const SaveLoad _brt_station_desc[] = {
	SaveLoad::Variable<VarFileType::U32>("tile", SLE_OBJECT_ADDRESS(BrtStationSave, tile)),
};

/** Bus stops that are BRT stations (see brt.h). */
struct BRTSChunkHandler : ChunkHandler {
	BRTSChunkHandler() : ChunkHandler("BRTS", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_brt_station_desc);

		int index = 0;
		for (TileIndex tile : GetBrtStations()) {
			BrtStationSave save{tile};
			SlSetArrayIndex(index++);
			SlObject(&save, _brt_station_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_brt_station_desc);

		GetBrtStations().clear();
		while (SlIterateArray() != -1) {
			BrtStationSave save;
			SlObject(&save, slt);
			if (IsValidTile(save.tile)) GetBrtStations().insert(save.tile);
		}
	}
};

static const BLANChunkHandler BLAN;
static const GWAYChunkHandler GWAY;
static const BRTSChunkHandler BRTS;
static const ChunkHandlerRef bus_priority_chunk_handlers[] = {
	BLAN,
	GWAY,
	BRTS,
};

extern const ChunkHandlerTable _bus_priority_chunk_handlers(bus_priority_chunk_handlers);
