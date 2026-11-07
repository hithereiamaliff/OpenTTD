/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file town_budget_sl.cpp Code handling saving and loading of towns' budgets and ordinances.
 *
 * Each town's budget and ordinances are kept in chunk TBUD. Savegames
 * without it start with towns that set up their budgets in the first month,
 * without ordinances.
 */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../town.h"
#include "../town_budget.h"

#include "../safeguards.h"

static const SaveLoad _town_budget_desc[] = {
	SaveLoad::Variable<VarFileType::Bool>("started", SLE_OBJECT_ADDRESS(Town, budget.started)),
	SaveLoad::Variable<VarFileType::U8>("tax_rate", SLE_OBJECT_ADDRESS(Town, budget.tax_rate)),
	SaveLoad::Variable<VarFileType::I64>("reserves", SLE_OBJECT_ADDRESS(Town, budget.reserves)),
	SaveLoad::Variable<VarFileType::I64>("taxes_this_year", SLE_OBJECT_ADDRESS(Town, budget.taxes_this_year)),
	SaveLoad::Variable<VarFileType::I64>("taxes_last_year", SLE_OBJECT_ADDRESS(Town, budget.taxes_last_year)),
	SaveLoad::Variable<VarFileType::I64>("fees_this_year", SLE_OBJECT_ADDRESS(Town, budget.fees_this_year)),
	SaveLoad::Variable<VarFileType::I64>("fees_last_year", SLE_OBJECT_ADDRESS(Town, budget.fees_last_year)),
	SaveLoad::Variable<VarFileType::I64>("spent_this_year", SLE_OBJECT_ADDRESS(Town, budget.spent_this_year)),
	SaveLoad::Variable<VarFileType::I64>("spent_last_year", SLE_OBJECT_ADDRESS(Town, budget.spent_last_year)),
	SaveLoad::Variable<VarFileType::U8>("ordinances", SLE_OBJECT_ADDRESS(Town, budget.ordinances)),
	SaveLoad::Array<VarFileType::I32, ORDINANCE_COUNT>("since", SLE_OBJECT_ADDRESS(Town, budget.since)),
	SaveLoad::Variable<VarFileType::U32>("trees_planted", SLE_OBJECT_ADDRESS(Town, budget.trees_planted)),
	SaveLoad::Variable<VarFileType::U32>("lez_charged_this_month", SLE_OBJECT_ADDRESS(Town, budget.lez_charged_this_month)),
	SaveLoad::Variable<VarFileType::U32>("lez_charged_last_month", SLE_OBJECT_ADDRESS(Town, budget.lez_charged_last_month)),
};

/** Each town's budget and ordinances, by town. */
struct TBUDChunkHandler : ChunkHandler {
	TBUDChunkHandler() : ChunkHandler("TBUD", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_town_budget_desc);

		for (Town *t : Town::Iterate()) {
			SlSetArrayIndex(t->index);
			SlObject(t, _town_budget_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_town_budget_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			if (!Town::IsValidID(index)) SlErrorCorrupt("Budget for a town that doesn't exist");
			Town *t = Town::Get(index);
			SlObject(t, slt);
			/* Keep known ordinances only, and a tax rate in range. */
			t->budget.ordinances &= static_cast<uint8_t>((1U << ORDINANCE_COUNT) - 1);
			t->budget.tax_rate = Clamp<uint8_t>(t->budget.tax_rate, 50, 200);
		}
		UpdateLowEmissionZoneTowns();
	}
};

static const TBUDChunkHandler TBUD;
static const ChunkHandlerRef town_budget_chunk_handlers[] = {
	TBUD,
};

extern const ChunkHandlerTable _town_budget_chunk_handlers(town_budget_chunk_handlers);
