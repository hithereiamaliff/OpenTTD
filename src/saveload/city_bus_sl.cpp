/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file city_bus_sl.cpp Code handling saving and loading of the batteries of the city fork's electric buses. */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../roadveh.h"
#include "../depot_base.h"
#include "../stop_charger.h"
#include "../station_map.h"
#include "../map_func.h"

#include "../safeguards.h"

static const SaveLoad _battery_desc[] = {
	SaveLoad::Variable<VarFileType::U32>("charge", SLE_OBJECT_ADDRESS(RoadVehicle, battery_charge)),
	SaveLoad::Variable<VarFileType::U16>("home_depot", SLE_OBJECT_ADDRESS(RoadVehicle, home_depot)),
};

/**
 * The city fork's data of road vehicles, by vehicle: battery charge of electric buses and the home depot
 * (see depot_yard.h). Saved apart from the vehicles to keep their chunk as it is; the name stayed from when
 * it held only batteries.
 */
struct BATTChunkHandler : ChunkHandler {
	BATTChunkHandler() : ChunkHandler("BATT", ChunkType::SparseTable) {}

	void Save() const override
	{
		SlTableHeader(_battery_desc);

		for (RoadVehicle *rv : RoadVehicle::Iterate()) {
			if (!rv->IsFrontEngine() || (rv->battery_charge == 0 && rv->home_depot == DepotID::Invalid())) continue;
			SlSetArrayIndex(rv->index);
			SlObject(rv, _battery_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_battery_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			RoadVehicle *rv = RoadVehicle::GetIfValid(index);
			if (rv == nullptr) SlErrorCorrupt("Battery for a vehicle that doesn't exist");
			SlObject(rv, slt);
			/* A home that isn't a depot any more (it can't happen, but better safe) is no home. */
			if (rv->home_depot != DepotID::Invalid() && !Depot::IsValidID(rv->home_depot)) rv->home_depot = DepotID::Invalid();
		}
	}
};

/** A charger at a bus stop. */
struct StopChargerSave {
	TileIndex tile = INVALID_TILE; ///< The stop.
};

static const SaveLoad _stop_charger_desc[] = {
	SaveLoad::Variable<VarFileType::U32>("tile", SLE_OBJECT_ADDRESS(StopChargerSave, tile)),
};

/** Chargers at bus stops (see stop_charger.h). */
struct STCHChunkHandler : ChunkHandler {
	STCHChunkHandler() : ChunkHandler("STCH", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_stop_charger_desc);

		int index = 0;
		for (TileIndex tile : GetStopChargers()) {
			StopChargerSave sc{tile};
			SlSetArrayIndex(index++);
			SlObject(&sc, _stop_charger_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_stop_charger_desc);

		GetStopChargers().clear();
		while (SlIterateArray() != -1) {
			StopChargerSave sc;
			SlObject(&sc, slt);
			if (!IsValidTile(sc.tile) || !IsAnyRoadStopTile(sc.tile)) continue; // Not a stop any more: no charger.
			GetStopChargers().insert(sc.tile);
		}
	}
};

static const BATTChunkHandler BATT;
static const STCHChunkHandler STCH;
static const ChunkHandlerRef city_bus_chunk_handlers[] = {
	BATT,
	STCH,
};

extern const ChunkHandlerTable _city_bus_chunk_handlers(city_bus_chunk_handlers);
