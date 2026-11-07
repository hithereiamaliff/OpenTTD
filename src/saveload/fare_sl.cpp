/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file fare_sl.cpp Code handling saving and loading of passenger fares.
 *
 * Company fares (chunk FARE, by company) and what riders paid against the
 * classic fare at each station (chunk FSTA, by station) are kept in their own table
 * chunks. Savegames without them get OpenTTD's classic fares and no effect on
 * station ratings.
 */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../company_base.h"
#include "../station_base.h"

#include "../safeguards.h"

/** Fares and pass figures of one transport type, under the given name. */
#define FARE_MODE_FIELDS(name, mode) \
	SaveLoad::Variable<VarFileType::U8>(name ".scheme", SLE_OBJECT_ADDRESS(Company, fares[mode].scheme)), \
	SaveLoad::Variable<VarFileType::U16>(name ".price", SLE_OBJECT_ADDRESS(Company, fares[mode].price)), \
	SaveLoad::Variable<VarFileType::U8>(name ".concession", SLE_OBJECT_ADDRESS(Company, fares[mode].concession)), \
	SaveLoad::Variable<VarFileType::U8>(name ".pass", SLE_OBJECT_ADDRESS(Company, fares[mode].pass)), \
	SaveLoad::Variable<VarFileType::U32>(name ".pass_riders", SLE_OBJECT_ADDRESS(Company, pass_stats[mode].riders)), \
	SaveLoad::Variable<VarFileType::U32>(name ".pass_holders", SLE_OBJECT_ADDRESS(Company, pass_stats[mode].holders)), \
	SaveLoad::Variable<VarFileType::I64>(name ".pass_income", SLE_OBJECT_ADDRESS(Company, pass_stats[mode].income))

static const SaveLoad _company_fare_desc[] = {
	FARE_MODE_FIELDS("bus", 0),
	FARE_MODE_FIELDS("tram", 1),
	FARE_MODE_FIELDS("rail", 2),
	FARE_MODE_FIELDS("water", 3),
	FARE_MODE_FIELDS("air", 4),
};

#undef FARE_MODE_FIELDS

static const SaveLoad _station_fare_desc[] = {
	SaveLoad::Variable<VarFileType::I64>("fare_paid", SLE_OBJECT_ADDRESS(Station, fare_paid)),
	SaveLoad::Variable<VarFileType::I64>("fare_standard", SLE_OBJECT_ADDRESS(Station, fare_standard)),
	SaveLoad::Variable<VarFileType::U16>("fare_level", SLE_OBJECT_ADDRESS(Station, fare_level)),
	SaveLoad::Variable<VarFileType::U8>("brt_grade", SLE_OBJECT_ADDRESS(Station, brt_grade)),
	SaveLoad::Variable<VarFileType::U8>("interchange", SLE_OBJECT_ADDRESS(Station, interchange)),
	SaveLoad::Variable<VarFileType::U8>("satisfaction", SLE_OBJECT_ADDRESS(Station, satisfaction)),
	SaveLoad::Variable<VarFileType::Bool>("screen_doors", SLE_OBJECT_ADDRESS(Station, screen_doors)),
	SaveLoad::Variable<VarFileType::U16>("crowding", SLE_OBJECT_ADDRESS(Station, crowding)),
};

/**
 * Bring loaded fares back into range, in case of a broken savegame.
 * @param fare The fares.
 */
static void ValidateFare(FarePolicy &fare)
{
	if (fare.scheme >= FareScheme::End) fare.scheme = FareScheme::Classic;
	fare.price = Clamp(fare.price, FARE_PRICE_MIN, FARE_PRICE_MAX);
	if (fare.concession >= FareConcession::End) fare.concession = FareConcession::None;
	if (fare.pass != 0) fare.pass = Clamp(fare.pass, FARE_PASS_MIN, FARE_PASS_MAX);
}

/** Company fares, by company. */
struct FAREChunkHandler : ChunkHandler {
	FAREChunkHandler() : ChunkHandler("FARE", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_company_fare_desc);

		for (Company *c : Company::Iterate()) {
			SlSetArrayIndex(c->index);
			SlObject(c, _company_fare_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_company_fare_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			if (!Company::IsValidID(index)) SlErrorCorrupt("Fares for a company that doesn't exist");
			Company *c = Company::Get(index);
			SlObject(c, slt);
			for (FarePolicy &fare : c->fares) ValidateFare(fare);
		}
	}
};

/** How dear riders found the fares at each station, by station. */
struct FSTAChunkHandler : ChunkHandler {
	FSTAChunkHandler() : ChunkHandler("FSTA", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_station_fare_desc);

		for (Station *st : Station::Iterate()) {
			SlSetArrayIndex(st->index);
			SlObject(st, _station_fare_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_station_fare_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			if (!Station::IsValidID(index)) SlErrorCorrupt("Fare level for a station that doesn't exist");
			SlObject(Station::Get(index), slt);
		}
	}
};

static const FAREChunkHandler FARE;
static const FSTAChunkHandler FSTA;
static const ChunkHandlerRef fare_chunk_handlers[] = {
	FARE,
	FSTA,
};

extern const ChunkHandlerTable _fare_chunk_handlers(fare_chunk_handlers);
