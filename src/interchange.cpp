/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file interchange.cpp Bus interchanges and integrated transport hubs.
 *
 * A station with at least six bus stops can become a bus interchange (like
 * Singapore's bus interchanges and Kuala Lumpur's Pasar Seni hub): a roof
 * over its bus stops. Riders rate it better, and a crowd waiting there counts
 * half. An interchange that also has a railway station can become an
 * integrated transport hub, air-conditioned (like Singapore's Integrated
 * Transport Hubs next to the MRT): riders rate it better still, people come
 * from further away, and its town leaves the car at home more because
 * changing between bus and train is easy. Towns pay half of either upgrade
 * from their transit budget when they can, as the LTA builds them.
 */

#include "stdafx.h"
#include "interchange.h"
#include "interchange_cmd.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "economy_func.h"
#include "roadstop_base.h"
#include "station_base.h"
#include "station_map.h"
#include "town.h"
#include "transit_authority.h"
#include "transparency.h"
#include "tile_cmd.h"
#include "viewport_func.h"
#include "window_func.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/**
 * The bus stops of a station.
 * @param st The station.
 * @return How many bus stop tiles it has.
 */
uint CountBusStops(const Station *st)
{
	uint n = 0;
	for (const RoadStop *rs = st->bus_stops; rs != nullptr; rs = rs->next) n++;
	return n;
}

/**
 * Is a station a bus interchange now: upgraded, and with enough bus stops?
 * @param st The station.
 * @return True if it is.
 */
bool IsActiveInterchange(const Station *st)
{
	return st->interchange >= to_underlying(InterchangeLevel::Interchange) && CountBusStops(st) >= INTERCHANGE_MIN_BUS_STOPS;
}

/**
 * Is a station an integrated transport hub now: upgraded, an interchange, and with a railway station?
 * @param st The station.
 * @return True if it is.
 */
bool IsActiveTransportHub(const Station *st)
{
	return st->interchange >= to_underlying(InterchangeLevel::Hub) && !st->train_station.IsEmpty() && IsActiveInterchange(st);
}

/**
 * Rating bonus for passengers at a bus interchange (8%) or an integrated transport hub (16%).
 * @param st The station.
 * @return The bonus, on the rating's scale of 255.
 */
int GetStationInterchangeRatingBonus(const Station *st)
{
	if (st->interchange == 0) return 0;
	if (IsActiveTransportHub(st)) return 41;
	if (IsActiveInterchange(st)) return 20;
	return 0;
}

/**
 * Cost of making a station a bus interchange: a roof over each of its bus stops.
 * @param st The station.
 * @return The cost.
 */
Money GetInterchangeCost(const Station *st)
{
	return GetPrice(Price::BuildObject, 50, nullptr) * CountBusStops(st);
}

/**
 * Cost of making a bus interchange an integrated transport hub.
 * @return The cost.
 */
Money GetTransportHubCost()
{
	return GetPrice(Price::BuildObject, 750, nullptr);
}

/**
 * Yearly upkeep of a station's interchange and hub.
 * @param st The station.
 * @return The upkeep.
 */
Money GetInterchangeYearlyUpkeep(const Station *st)
{
	if (st->interchange == 0) return 0;
	Money upkeep = GetPrice(Price::BuildObject, 5, nullptr) * CountBusStops(st);
	if (st->interchange >= to_underlying(InterchangeLevel::Hub)) upkeep += GetPrice(Price::BuildObject, 100, nullptr);
	return upkeep;
}

/**
 * What a town pays of an upgrade of a station: half, if its transit budget has it to spare.
 * @param t The town.
 * @param cost The cost of the upgrade.
 * @return The town's part.
 */
Money GetTownShareOfUpgrade(const Town *t, Money cost)
{
	if (t == nullptr) return 0;
	const Money half = cost / 2;
	return GetTownTransitAvailable(t) - t->transit.spent_this_year >= half ? half : Money{0};
}

/**
 * Make a station a bus interchange, or a bus interchange an integrated transport hub.
 * @param flags Type of operation.
 * @param station_id The station.
 * @param level What to make it.
 * @return The cost of this operation or an error.
 */
CommandCost CmdUpgradeStation(DoCommandFlags flags, StationID station_id, InterchangeLevel level)
{
	Station *st = Station::GetIfValid(station_id);
	if (st == nullptr || level == InterchangeLevel::None || level >= InterchangeLevel::End) return CMD_ERROR;
	CommandCost ret = CheckOwnership(st->owner);
	if (ret.Failed()) return ret;

	if (st->interchange >= to_underlying(level)) return CommandCost(STR_ERROR_ALREADY_BUILT);
	if (CountBusStops(st) < INTERCHANGE_MIN_BUS_STOPS) return CommandCost(STR_ERROR_INTERCHANGE_TOO_FEW_STOPS);

	Money cost;
	if (level == InterchangeLevel::Interchange) {
		cost = GetInterchangeCost(st);
	} else {
		if (st->interchange < to_underlying(InterchangeLevel::Interchange)) return CommandCost(STR_ERROR_HUB_NEEDS_INTERCHANGE);
		if (st->train_station.IsEmpty()) return CommandCost(STR_ERROR_HUB_NEEDS_RAILWAY);
		cost = GetTransportHubCost();
	}
	const Money town_share = GetTownShareOfUpgrade(st->town, cost);

	if (flags.Test(DoCommandFlag::Execute)) {
		st->interchange = to_underlying(level);
		st->town->transit.spent_this_year += town_share;
		/* A hub draws people from further away. */
		st->RecomputeCatchment();
		for (const RoadStop *rs = st->bus_stops; rs != nullptr; rs = rs->next) MarkTileDirtyByTile(rs->xy);
		SetWindowDirty(WindowClass::StationView, st->index);
		SetWindowDirty(WindowClass::TransitAuthority, st->town->index);
	}
	return CommandCost(ExpensesType::Construction, cost - town_share);
}

/**
 * Draw the roof of a bus interchange over one of its bus stops; the roof of a hub has air-conditioning on it.
 * BRT stations keep their own platforms and roofs.
 * @param ti The bus stop tile.
 */
void DrawInterchangeRoof(const TileInfo *ti)
{
	const Station *st = Station::GetByTile(ti->tile);
	if (st->interchange == 0 || !IsActiveInterchange(st)) return;

	SpriteID image = IsActiveTransportHub(st) ? SPR_INTERCHANGE_ROOF_HUB : SPR_INTERCHANGE_ROOF;
	SetBit(image, PALETTE_MODIFIER_COLOUR);
	const PaletteID pal = GetCompanyPalette(st->owner);
	/* Above the buses, drawn from the tile's north corner. */
	AddSortableSpriteToDraw(image, pal, *ti, {{0, 0, 14}, {TILE_SIZE, TILE_SIZE, 2}, {0, 0, -14}}, IsTransparencySet(TransparencyOption::Buildings));
}

/** Each month: the upkeep of interchanges and hubs, and the hubs of each town. */
static const IntervalTimer<TimerGameEconomy> _interchange_monthly({TimerGameEconomy::Trigger::Month, TimerGameEconomy::Priority::None}, [](auto)
{
	std::array<Money, MAX_COMPANIES> upkeep{};
	for (Town *t : Town::Iterate()) t->transit.hubs = 0;
	for (const Station *st : Station::Iterate()) {
		if (st->interchange == 0) continue;
		if (Company::IsValidID(st->owner)) upkeep[st->owner.base()] += GetInterchangeYearlyUpkeep(st) / 12;
		if (IsActiveTransportHub(st) && st->town->transit.hubs < UINT8_MAX) st->town->transit.hubs++;
	}
	for (Company *c : Company::Iterate()) {
		if (upkeep[c->index.base()] != 0) SubtractMoneyFromCompany(c->index, CommandCost(ExpensesType::Property, upkeep[c->index.base()]));
	}
});
