/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file council_sl.cpp Code handling saving and loading of town councils.
 *
 * Each town's council (its standing, licences, charges, estates, report and
 * what the town had at the start of the month) is kept in chunk TCNL.
 * Savegames without it set up their councils in the first month.
 */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../council.h"
#include "../town.h"

#include "../safeguards.h"

static const SaveLoad _town_council_desc[] = {
	SaveLoad::Variable<VarFileType::Bool>("started", SLE_OBJECT_ADDRESS(Town, council.started)),
	SaveLoad::Variable<VarFileType::U8>("level", SLE_OBJECT_ADDRESS(Town, council.level)),
	SaveLoad::Variable<VarFileType::I32>("since", SLE_OBJECT_ADDRESS(Town, council.since)),
	SaveLoad::Variable<VarFileType::I64>("licences_this_year", SLE_OBJECT_ADDRESS(Town, council.licences_this_year)),
	SaveLoad::Variable<VarFileType::I64>("licences_last_year", SLE_OBJECT_ADDRESS(Town, council.licences_last_year)),
	SaveLoad::Variable<VarFileType::I64>("charges_this_year", SLE_OBJECT_ADDRESS(Town, council.charges_this_year)),
	SaveLoad::Variable<VarFileType::I64>("charges_last_year", SLE_OBJECT_ADDRESS(Town, council.charges_last_year)),
	SaveLoad::Variable<VarFileType::I64>("estates_this_year", SLE_OBJECT_ADDRESS(Town, council.estates_this_year)),
	SaveLoad::Variable<VarFileType::I64>("estates_last_year", SLE_OBJECT_ADDRESS(Town, council.estates_last_year)),
	SaveLoad::Variable<VarFileType::I64>("built_this_year", SLE_OBJECT_ADDRESS(Town, council.built_this_year)),
	SaveLoad::Variable<VarFileType::I64>("built_last_year", SLE_OBJECT_ADDRESS(Town, council.built_last_year)),
	SaveLoad::Variable<VarFileType::I64>("drawn_this_year", SLE_OBJECT_ADDRESS(Town, council.drawn_this_year)),
	SaveLoad::Variable<VarFileType::I64>("drawn_last_year", SLE_OBJECT_ADDRESS(Town, council.drawn_last_year)),
	SaveLoad::Variable<VarFileType::I64>("services_fund", SLE_OBJECT_ADDRESS(Town, council.services_fund)),
	SaveLoad::Variable<VarFileType::I64>("cofunded", SLE_OBJECT_ADDRESS(Town, council.cofunded)),
	SaveLoad::Variable<VarFileType::I64>("estates_fund", SLE_OBJECT_ADDRESS(Town, council.estates_fund)),
	SaveLoad::Variable<VarFileType::U8>("estate_condition", SLE_OBJECT_ADDRESS(Town, council.estate_condition)),
	SaveLoad::Variable<VarFileType::U8>("paint", SLE_OBJECT_ADDRESS(Town, council.paint)),
	SaveLoad::Variable<VarFileType::I32>("repainted", SLE_OBJECT_ADDRESS(Town, council.repainted)),
	SaveLoad::Variable<VarFileType::I32>("renewed", SLE_OBJECT_ADDRESS(Town, council.renewed)),
	SaveLoad::Variable<VarFileType::U16>("services_built", SLE_OBJECT_ADDRESS(Town, council.services_built)),
	SaveLoad::Array<VarFileType::U8, REPORT_ITEM_COUNT>("report", SLE_OBJECT_ADDRESS(Town, council.report)),
	SaveLoad::Variable<VarFileType::U32>("count_people", SLE_OBJECT_ADDRESS(Town, council.counts.people)),
	SaveLoad::Variable<VarFileType::U32>("count_business", SLE_OBJECT_ADDRESS(Town, council.counts.business)),
	SaveLoad::Variable<VarFileType::U32>("count_flats", SLE_OBJECT_ADDRESS(Town, council.counts.flats)),
	SaveLoad::Variable<VarFileType::U64>("count_flats_age", SLE_OBJECT_ADDRESS(Town, council.counts.flats_age)),
	SaveLoad::Variable<VarFileType::U16>("count_hospitals", SLE_OBJECT_ADDRESS(Town, council.counts.hospitals)),
	SaveLoad::Variable<VarFileType::U16>("count_universities", SLE_OBJECT_ADDRESS(Town, council.counts.universities)),
	SaveLoad::Variable<VarFileType::U16>("count_stadiums", SLE_OBJECT_ADDRESS(Town, council.counts.stadiums)),
};

/** Each town's council, by town. */
struct TCNLChunkHandler : ChunkHandler {
	TCNLChunkHandler() : ChunkHandler("TCNL", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_town_council_desc);

		for (Town *t : Town::Iterate()) {
			SlSetArrayIndex(t->index);
			SlObject(t, _town_council_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_town_council_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			if (!Town::IsValidID(index)) SlErrorCorrupt("Council of a town that doesn't exist");
			Town *t = Town::Get(index);
			SlObject(t, slt);
			/* Keep what makes sense. */
			TownCouncil &c = t->council;
			if (c.level >= CouncilLevel::End) c.level = CouncilLevel::District;
			c.estate_condition = std::min<uint8_t>(c.estate_condition, 100);
			for (ReportBand &band : c.report) {
				if (band > ReportBand::Red) band = ReportBand::None;
			}
		}
	}
};

static const TCNLChunkHandler TCNL;
static const ChunkHandlerRef council_chunk_handlers[] = {
	TCNL,
};

extern const ChunkHandlerTable _council_chunk_handlers(council_chunk_handlers);
