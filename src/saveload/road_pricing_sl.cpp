/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file road_pricing_sl.cpp Code handling saving and loading of road pricing: toll plazas (chunk TOLL)
 * and the companies' tolls (chunk TOLC). Congestion charges are saved with the towns' transit (chunk TTRA).
 */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../company_base.h"
#include "../map_func.h"
#include "../road_pricing.h"

#include "../safeguards.h"

/** A toll plaza, for saving. */
struct TollPlazaSave {
	TileIndex tile = INVALID_TILE; ///< The plaza's tile.
	TollPlaza plaza{}; ///< The plaza.
};

static const SaveLoad _toll_plaza_desc[] = {
	SaveLoad::Variable<VarFileType::U32>("tile", SLE_OBJECT_ADDRESS(TollPlazaSave, tile)),
	SaveLoad::Variable<VarFileType::I32>("concession_end", SLE_OBJECT_ADDRESS(TollPlazaSave, plaza.concession_end)),
	SaveLoad::Variable<VarFileType::U8>("cut", SLE_OBJECT_ADDRESS(TollPlazaSave, plaza.cut)),
	SaveLoad::Variable<VarFileType::Bool>("free_flow", SLE_OBJECT_ADDRESS(TollPlazaSave, plaza.free_flow)),
	SaveLoad::Variable<VarFileType::U8>("towns", SLE_OBJECT_ADDRESS(TollPlazaSave, plaza.towns)),
	SaveLoad::Variable<VarFileType::U32>("trips", SLE_OBJECT_ADDRESS(TollPlazaSave, plaza.trips)),
	SaveLoad::Variable<VarFileType::U32>("vehicles", SLE_OBJECT_ADDRESS(TollPlazaSave, plaza.vehicles)),
	SaveLoad::Variable<VarFileType::U32>("vehicles_this_month", SLE_OBJECT_ADDRESS(TollPlazaSave, plaza.vehicles_this_month)),
	SaveLoad::Variable<VarFileType::I64>("income", SLE_OBJECT_ADDRESS(TollPlazaSave, plaza.income)),
	SaveLoad::Variable<VarFileType::I64>("income_this_month", SLE_OBJECT_ADDRESS(TollPlazaSave, plaza.income_this_month)),
};

/** Toll plazas (see road_pricing.h). */
struct TOLLChunkHandler : ChunkHandler {
	TOLLChunkHandler() : ChunkHandler("TOLL", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_toll_plaza_desc);

		int index = 0;
		for (const auto &[tile, plaza] : GetTollPlazas()) {
			TollPlazaSave save{tile, plaza};
			SlSetArrayIndex(index++);
			SlObject(&save, _toll_plaza_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_toll_plaza_desc);

		GetTollPlazas().clear();
		while (SlIterateArray() != -1) {
			TollPlazaSave save;
			SlObject(&save, slt);
			if (IsValidTile(save.tile)) GetTollPlazas()[save.tile] = save.plaza;
		}
	}
};

static const SaveLoad _company_toll_desc[] = {
	SaveLoad::Variable<VarFileType::U8>("level", SLE_OBJECT_ADDRESS(Company, tolls.level)),
	SaveLoad::Variable<VarFileType::U8>("scheme", SLE_OBJECT_ADDRESS(Company, tolls.scheme)),
	SaveLoad::Variable<VarFileType::I32>("freeze_until", SLE_OBJECT_ADDRESS(Company, tolls.freeze_until)),
	SaveLoad::Variable<VarFileType::I64>("income_last_month", SLE_OBJECT_ADDRESS(Company, tolls.income_last_month)),
	SaveLoad::Variable<VarFileType::I64>("compensation_last_month", SLE_OBJECT_ADDRESS(Company, tolls.compensation_last_month)),
};

/** The companies' tolls. */
struct TOLCChunkHandler : ChunkHandler {
	TOLCChunkHandler() : ChunkHandler("TOLC", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_company_toll_desc);

		for (Company *c : Company::Iterate()) {
			SlSetArrayIndex(c->index);
			SlObject(c, _company_toll_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_company_toll_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			if (!Company::IsValidID(index)) SlErrorCorrupt("Tolls of a company that doesn't exist");
			Company *c = Company::Get(index);
			SlObject(c, slt);
			/* Bring a broken savegame back into range. */
			if (c->tolls.level < TOLL_LEVEL_MIN || c->tolls.level > TOLL_LEVEL_MAX) c->tolls.level = 100;
			if (c->tolls.scheme >= TollScheme::End) c->tolls.scheme = TollScheme::PerKm;
		}
	}
};

static const TOLLChunkHandler TOLL;
static const TOLCChunkHandler TOLC;
static const ChunkHandlerRef road_pricing_chunk_handlers[] = {
	TOLL,
	TOLC,
};

extern const ChunkHandlerTable _road_pricing_chunk_handlers(road_pricing_chunk_handlers);
