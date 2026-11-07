/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file city_clock_sl.cpp Code handling saving and loading of the vehicles that wait in depots for the next rush hour. */

#include "../stdafx.h"

#include "saveload.h"

#include "../city_clock.h"
#include "../vehicle_base.h"

#include "../safeguards.h"

/** A vehicle waiting for the next rush hour, for saving. */
struct OffPeakParkedSave {
	VehicleID vehicle = VehicleID::Invalid(); ///< The vehicle.
};

static const SaveLoad _offpeak_parked_desc[] = {
	SaveLoad::Variable<VarFileType::U32>("vehicle", SLE_OBJECT_ADDRESS(OffPeakParkedSave, vehicle)),
};

/** Vehicles that lines parked between the rush hours of the city clock (see city_clock.h). */
struct PEAKChunkHandler : ChunkHandler {
	PEAKChunkHandler() : ChunkHandler("PEAK", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_offpeak_parked_desc);

		int index = 0;
		for (VehicleID id : GetOffPeakParkedVehicles()) {
			OffPeakParkedSave save{id};
			SlSetArrayIndex(index++);
			SlObject(&save, _offpeak_parked_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_offpeak_parked_desc);

		GetOffPeakParkedVehicles().clear();
		while (SlIterateArray() != -1) {
			OffPeakParkedSave save;
			SlObject(&save, slt);
			const Vehicle *v = Vehicle::GetIfValid(save.vehicle);
			if (v != nullptr && v->IsPrimaryVehicle()) GetOffPeakParkedVehicles().push_back(save.vehicle);
		}
	}
};

static const PEAKChunkHandler PEAK;
static const ChunkHandlerRef city_clock_chunk_handlers[] = {
	PEAK,
};

extern const ChunkHandlerTable _city_clock_chunk_handlers(city_clock_chunk_handlers);
