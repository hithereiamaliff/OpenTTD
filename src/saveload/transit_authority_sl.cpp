/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file transit_authority_sl.cpp Code handling saving and loading of town transit authorities.
 *
 * Each town's transit figures (chunk TTRA, by town) and all tenders and
 * contracts (chunk TCON) are kept in their own table chunks. Savegames
 * without them have no contracts.
 */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"

#include "../town.h"
#include "../transit_authority.h"

#include "../safeguards.h"

static const SaveLoad _town_transit_desc[] = {
	SaveLoad::Variable<VarFileType::I64>("fares_this_year", SLE_OBJECT_ADDRESS(Town, transit.fares_this_year)),
	SaveLoad::Variable<VarFileType::I64>("fares_last_year", SLE_OBJECT_ADDRESS(Town, transit.fares_last_year)),
	SaveLoad::Variable<VarFileType::I32>("last_tender", SLE_OBJECT_ADDRESS(Town, transit.last_tender)),
	SaveLoad::Variable<VarFileType::Bool>("policy_set", SLE_OBJECT_ADDRESS(Town, transit.policy_set)),
	SaveLoad::Variable<VarFileType::Bool>("resident_pass", SLE_OBJECT_ADDRESS(Town, transit.resident_pass)),
	SaveLoad::Variable<VarFileType::Bool>("pays_concessions", SLE_OBJECT_ADDRESS(Town, transit.pays_concessions)),
	SaveLoad::Variable<VarFileType::I64>("spent_this_year", SLE_OBJECT_ADDRESS(Town, transit.spent_this_year)),
	SaveLoad::Array<VarFileType::U32, MAX_COMPANIES>("pass_riders", SLE_OBJECT_ADDRESS(Town, transit.pass_riders)),
	SaveLoad::Array<VarFileType::I64, MAX_COMPANIES>("concessions_owed", SLE_OBJECT_ADDRESS(Town, transit.concessions_owed)),
	SaveLoad::Variable<VarFileType::U32>("last_pass_holders", SLE_OBJECT_ADDRESS(Town, transit.last_pass_holders)),
	SaveLoad::Variable<VarFileType::I64>("last_pass_paid", SLE_OBJECT_ADDRESS(Town, transit.last_pass_paid)),
	SaveLoad::Variable<VarFileType::I64>("last_concessions_paid", SLE_OBJECT_ADDRESS(Town, transit.last_concessions_paid)),
	SaveLoad::Variable<VarFileType::Bool>("told_budget_gone", SLE_OBJECT_ADDRESS(Town, transit.told_budget_gone)),
};

static const SaveLoad _transit_contract_desc[] = {
	SaveLoad::Variable<VarFileType::U32>("id", SLE_OBJECT_ADDRESS(TransitContract, id)),
	SaveLoad::Variable<VarFileType::U16>("town", SLE_OBJECT_ADDRESS(TransitContract, town)),
	SaveLoad::Variable<VarFileType::U8>("company", SLE_OBJECT_ADDRESS(TransitContract, company)),
	SaveLoad::Variable<VarFileType::U16>("line", SLE_OBJECT_ADDRESS(TransitContract, line)),
	SaveLoad::Variable<VarFileType::U8>("type", SLE_OBJECT_ADDRESS(TransitContract, type)),
	SaveLoad::Variable<VarFileType::U8>("state", SLE_OBJECT_ADDRESS(TransitContract, state)),
	SaveLoad::Variable<VarFileType::U8>("ended_why", SLE_OBJECT_ADDRESS(TransitContract, ended_why)),
	SaveLoad::Variable<VarFileType::I32>("start", SLE_OBJECT_ADDRESS(TransitContract, start)),
	SaveLoad::Variable<VarFileType::I32>("end", SLE_OBJECT_ADDRESS(TransitContract, end)),
	SaveLoad::Variable<VarFileType::I64>("cost_basis", SLE_OBJECT_ADDRESS(TransitContract, cost_basis)),
	SaveLoad::Variable<VarFileType::U8>("bid", SLE_OBJECT_ADDRESS(TransitContract, bid)),
	SaveLoad::Variable<VarFileType::U8>("rival_bid", SLE_OBJECT_ADDRESS(TransitContract, rival_bid)),
	SaveLoad::Variable<VarFileType::U8>("rival_quality", SLE_OBJECT_ADDRESS(TransitContract, rival_quality)),
	SaveLoad::Variable<VarFileType::U8>("quality", SLE_OBJECT_ADDRESS(TransitContract, quality)),
	SaveLoad::Variable<VarFileType::I64>("fee", SLE_OBJECT_ADDRESS(TransitContract, fee)),
	SaveLoad::Variable<VarFileType::U16>("required_departures", SLE_OBJECT_ADDRESS(TransitContract, required_departures)),
	SaveLoad::Variable<VarFileType::U32>("baseline_wait", SLE_OBJECT_ADDRESS(TransitContract, baseline_wait)),
	SaveLoad::Variable<VarFileType::U16>("months", SLE_OBJECT_ADDRESS(TransitContract, months)),
	SaveLoad::Variable<VarFileType::U16>("months_met", SLE_OBJECT_ADDRESS(TransitContract, months_met)),
	SaveLoad::Variable<VarFileType::U16>("idle_months", SLE_OBJECT_ADDRESS(TransitContract, idle_months)),
	SaveLoad::Variable<VarFileType::U16>("last_departures", SLE_OBJECT_ADDRESS(TransitContract, last_departures)),
	SaveLoad::Variable<VarFileType::U32>("last_excess_wait", SLE_OBJECT_ADDRESS(TransitContract, last_excess_wait)),
	SaveLoad::Variable<VarFileType::U8>("last_on_time", SLE_OBJECT_ADDRESS(TransitContract, last_on_time)),
	SaveLoad::Variable<VarFileType::I8>("last_kpi", SLE_OBJECT_ADDRESS(TransitContract, last_kpi)),
	SaveLoad::Variable<VarFileType::I64>("last_payment", SLE_OBJECT_ADDRESS(TransitContract, last_payment)),
	SaveLoad::Variable<VarFileType::I64>("paid", SLE_OBJECT_ADDRESS(TransitContract, paid)),
	SaveLoad::Variable<VarFileType::I64>("fares", SLE_OBJECT_ADDRESS(TransitContract, fares)),
	SaveLoad::Variable<VarFileType::I64>("last_fares", SLE_OBJECT_ADDRESS(TransitContract, last_fares)),
	SaveLoad::Variable<VarFileType::U32>("last_riders", SLE_OBJECT_ADDRESS(TransitContract, last_riders)),
};

/** Each town's transit figures, by town. */
struct TTRAChunkHandler : ChunkHandler {
	TTRAChunkHandler() : ChunkHandler("TTRA", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_town_transit_desc);

		for (Town *t : Town::Iterate()) {
			SlSetArrayIndex(t->index);
			SlObject(t, _town_transit_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_town_transit_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			if (!Town::IsValidID(index)) SlErrorCorrupt("Transit figures for a town that doesn't exist");
			SlObject(Town::Get(index), slt);
		}
	}
};

/** All tenders and contracts. */
struct TCONChunkHandler : ChunkHandler {
	TCONChunkHandler() : ChunkHandler("TCON", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_transit_contract_desc);

		int index = 0;
		for (TransitContract &c : GetTransitContracts()) {
			SlSetArrayIndex(index++);
			SlObject(&c, _transit_contract_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_transit_contract_desc);

		extern uint32_t &GetNextTransitContractNumber();
		std::vector<TransitContract> &contracts = GetTransitContracts();
		contracts.clear();
		while (SlIterateArray() != -1) {
			TransitContract c;
			SlObject(&c, slt);
			if (c.type >= TransitContractType::End || c.state >= TransitContractState::End || c.ended_why >= TransitContractEnd::End) SlErrorCorrupt("Invalid transit contract");
			if (c.bid != UINT8_MAX && c.bid > TRANSIT_BID_MAX) c.bid = TRANSIT_BID_MAX;
			GetNextTransitContractNumber() = std::max(GetNextTransitContractNumber(), c.id + 1);
			contracts.push_back(std::move(c));
		}
	}
};

static const TTRAChunkHandler TTRA;
static const TCONChunkHandler TCON;
static const ChunkHandlerRef transit_authority_chunk_handlers[] = {
	TTRA,
	TCON,
};

extern const ChunkHandlerTable _transit_authority_chunk_handlers(transit_authority_chunk_handlers);
