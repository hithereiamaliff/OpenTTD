/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file line_sl.cpp Code handling saving and loading of lines.
 *
 * Line data belongs to shared order lists (chunk ORDL) and is kept in its own
 * table chunk, indexed by order list. Savegames without it have lines without
 * numbers, names, colours or targets, and with fresh statistics.
 */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../order_base.h"

#include "../safeguards.h"

/** Statistics of one month, under the given prefix. */
#define LINE_MONTH_FIELDS(prefix, member) \
	SaveLoad::Variable<VarFileType::U32>(prefix ".riders", SLE_OBJECT_ADDRESS(LineInfo, member.riders)), \
	SaveLoad::Variable<VarFileType::U32>(prefix ".departures", SLE_OBJECT_ADDRESS(LineInfo, member.departures)), \
	SaveLoad::Variable<VarFileType::U32>(prefix ".load_sum", SLE_OBJECT_ADDRESS(LineInfo, member.load_sum)), \
	SaveLoad::Variable<VarFileType::U32>(prefix ".load_count", SLE_OBJECT_ADDRESS(LineInfo, member.load_count)), \
	SaveLoad::Variable<VarFileType::U8>(prefix ".max_load", SLE_OBJECT_ADDRESS(LineInfo, member.max_load)), \
	SaveLoad::Variable<VarFileType::U32>(prefix ".timed", SLE_OBJECT_ADDRESS(LineInfo, member.timed)), \
	SaveLoad::Variable<VarFileType::U32>(prefix ".on_time", SLE_OBJECT_ADDRESS(LineInfo, member.on_time))

static const SaveLoad _line_desc[] = {
	SaveLoad::String("number", SLE_OBJECT_ADDRESS(LineInfo, number), StringValidationSetting::AllowControlCode),
	SaveLoad::String("name", SLE_OBJECT_ADDRESS(LineInfo, name), StringValidationSetting::AllowControlCode),
	SaveLoad::Variable<VarFileType::U8>("colour", SLE_OBJECT_ADDRESS(LineInfo, colour)),
	SaveLoad::Variable<VarFileType::I32>("target_gap", SLE_OBJECT_ADDRESS(LineInfo, target_gap)),
	SaveLoad::Variable<VarFileType::Bool>("hold_for_spacing", SLE_OBJECT_ADDRESS(LineInfo, hold_for_spacing)),
	SaveLoad::Variable<VarFileType::U8>("fare_scheme", SLE_OBJECT_ADDRESS(LineInfo, fare_scheme)),
	SaveLoad::Variable<VarFileType::U16>("fare_price", SLE_OBJECT_ADDRESS(LineInfo, fare_price)),
	SaveLoad::Variable<VarFileType::U64>("last_departure", SLE_OBJECT_ADDRESS(LineInfo, last_departure)),
	SaveLoad::Array<VarFileType::U32, LINE_GAP_HISTORY>("gaps", SLE_OBJECT_ADDRESS(LineInfo, gaps)),
	SaveLoad::Variable<VarFileType::U8>("num_gaps", SLE_OBJECT_ADDRESS(LineInfo, num_gaps)),
	SaveLoad::Variable<VarFileType::U8>("next_gap", SLE_OBJECT_ADDRESS(LineInfo, next_gap)),
	LINE_MONTH_FIELDS("this_month", this_month),
	LINE_MONTH_FIELDS("last_month", last_month),
};

#undef LINE_MONTH_FIELDS

/** Lines, indexed by their order list. */
struct LINEChunkHandler : ChunkHandler {
	LINEChunkHandler() : ChunkHandler("LINE", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_line_desc);

		for (OrderList *list : OrderList::Iterate()) {
			SlSetArrayIndex(list->index);
			SlObject(&list->line, _line_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_line_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			LineInfo line;
			SlObject(&line, slt);
			if (!OrderList::IsValidID(index)) SlErrorCorrupt("Line for an order list that doesn't exist");
			if (line.num_gaps > LINE_GAP_HISTORY || line.next_gap >= LINE_GAP_HISTORY) SlErrorCorrupt("Invalid line gap history");
			if (to_underlying(line.colour) >= to_underlying(Colours::End) && line.colour != Colours::Invalid) line.colour = Colours::Invalid;
			if (line.fare_scheme >= FareScheme::End && line.fare_scheme != FareScheme::Inherit) line.fare_scheme = FareScheme::Inherit;
			if (line.fare_price != 0) line.fare_price = Clamp<uint16_t>(line.fare_price, FARE_PRICE_MIN, FARE_PRICE_MAX);
			OrderList::Get(index)->line = std::move(line);
		}
	}
};

static const LINEChunkHandler LINE;
static const ChunkHandlerRef line_chunk_handlers[] = {
	LINE,
};

extern const ChunkHandlerTable _line_chunk_handlers(line_chunk_handlers);
