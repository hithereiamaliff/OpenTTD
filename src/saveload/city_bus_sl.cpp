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

#include "../safeguards.h"

static const SaveLoad _battery_desc[] = {
	SaveLoad::Variable<VarFileType::U32>("charge", SLE_OBJECT_ADDRESS(RoadVehicle, battery_charge)),
};

/** Battery charge of electric buses, by vehicle; saved apart from the vehicles to keep their chunk as it is. */
struct BATTChunkHandler : ChunkHandler {
	BATTChunkHandler() : ChunkHandler("BATT", ChunkType::SparseTable) {}

	void Save() const override
	{
		SlTableHeader(_battery_desc);

		for (RoadVehicle *rv : RoadVehicle::Iterate()) {
			if (!rv->IsFrontEngine() || rv->battery_charge == 0) continue;
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
		}
	}
};

static const BATTChunkHandler BATT;
static const ChunkHandlerRef city_bus_chunk_handlers[] = {
	BATT,
};

extern const ChunkHandlerTable _city_bus_chunk_handlers(city_bus_chunk_handlers);
