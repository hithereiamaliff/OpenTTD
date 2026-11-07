/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file metro.cpp Metro and rail: platform crowding, platform screen doors and driverless lines.
 *
 * Railway platforms hold so many people: 120 a tile, plus 30 for each bus or
 * tram stop of the same station. Above that the platforms are crowded:
 * passengers board and get off slower, rate the station lower and like their
 * lines less (Mini Metro's overcrowded stations, a metro at rush hour).
 *
 * Platform screen doors (Singapore's MRT, Kuala Lumpur's MRT lines) keep
 * boarding orderly on crowded platforms and make lines without drivers
 * possible. A line of electric trains whose stops all have screen doors and
 * that never crosses a road at a level crossing can run driverless (like
 * Singapore's North East Line or the Kelana Jaya Line): cheaper to run,
 * with automatic doors that board a little faster. A driverless train that
 * meets a level crossing gets its drivers back.
 */

#include "stdafx.h"
#include "metro.h"
#include "metro_cmd.h"
#include "cargotype.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "economy_func.h"
#include "engine_base.h"
#include "interchange.h"
#include "line.h"
#include "news_func.h"
#include "order_base.h"
#include "roadstop_base.h"
#include "satisfaction.h"
#include "station_base.h"
#include "station_map.h"
#include "strings_func.h"
#include "town.h"
#include "train.h"
#include "transit_authority.h"
#include "transparency.h"
#include "vehicle_func.h"
#include "viewport_func.h"
#include "window_func.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/**
 * The railway platform tiles of a station.
 * @param st The station.
 * @return How many there are.
 */
uint CountPlatformTiles(const Station *st)
{
	if (st->train_station.IsEmpty()) return 0;
	uint tiles = 0;
	for (TileIndex tile : st->train_station) {
		if (st->TileBelongsToRailStation(tile)) tiles++;
	}
	return tiles;
}

/**
 * How many passengers fit comfortably on a station's platforms: 120 a tile
 * of railway platform and 30 a bus or tram stop. Stations without railway
 * platforms have none: their passengers wait in the street.
 * @param st The station.
 * @return The places, 0 without railway platforms.
 */
uint GetStationPlatformPlaces(const Station *st)
{
	const uint platforms = CountPlatformTiles(st);
	if (platforms == 0) return 0;
	uint stops = 0;
	for (const RoadStop *rs = st->bus_stops; rs != nullptr; rs = rs->next) stops++;
	return platforms * PLATFORM_PLACES_PER_TILE + stops * PLATFORM_PLACES_PER_ROAD_STOP;
}

/**
 * Passengers at a station: waiting, and about to board.
 * @param st The station.
 * @return How many.
 */
uint GetStationPassengersWaiting(const Station *st)
{
	uint waiting = 0;
	for (const CargoSpec *cs : CargoSpec::Iterate()) {
		if (!cs->IsValid() || !IsCargoInClass(cs->Index(), CargoClass::Passengers)) continue;
		const GoodsEntry &ge = st->goods[cs->Index()];
		if (ge.HasData()) waiting += ge.GetData().cargo.TotalCount();
	}
	return waiting;
}

/**
 * How crowded a station's platforms are right now.
 * @param st The station.
 * @return Passengers in percent of the places, 0 without railway platforms.
 */
uint GetStationPlatformCrowding(const Station *st)
{
	const uint places = GetStationPlatformPlaces(st);
	if (places == 0) return 0;
	return GetStationPassengersWaiting(st) * 100 / places;
}

/**
 * Follow how crowded a station's platforms are, over the last ten days or
 * so; called with each update of its ratings.
 * @param st The station.
 */
void UpdateStationCrowding(Station *st)
{
	const uint now = std::min<uint>(GetStationPlatformCrowding(st), UINT16_MAX);
	st->crowding = static_cast<uint16_t>((st->crowding * 3 + now + 2) / 4);
}

/**
 * Passengers rate a station with crowded platforms lower: up to 16% at more
 * than twice the places, half that with screen doors.
 * @param st The station.
 * @return The penalty, on the rating's scale of 255.
 */
int GetPlatformCrowdingRatingPenalty(const Station *st)
{
	if (st->crowding <= PLATFORM_CROWDED) return 0;
	const int penalty = std::min(40, (st->crowding - static_cast<int>(PLATFORM_CROWDED)) / 3);
	return st->screen_doors ? penalty / 2 : penalty;
}

/**
 * How fast the passengers of a train get on and off: slower through crowds
 * on the platform (at most 30% slower) unless screen doors keep the queues
 * in order; faster with the automatic doors of a driverless train.
 * @param v The part of the train that loads.
 * @return The speed in percent.
 */
uint GetTrainBoardingPercent(const Vehicle *v)
{
	const Vehicle *front = v->First();
	uint percent = 100;
	if (IsCargoInClass(v->cargo_type, CargoClass::Passengers)) {
		const Station *st = Station::GetIfValid(front->last_station_visited);
		if (st != nullptr && !st->screen_doors && st->crowding > PLATFORM_CROWDED) percent -= std::min<uint>(30, (st->crowding - PLATFORM_CROWDED) / 4);
	}
	if (front->orders != nullptr && front->orders->line.driverless) percent = percent * DRIVERLESS_BOARDING_PERCENT / 100;
	return percent;
}

/**
 * Cost of platform screen doors along all platforms of a station.
 * @param st The station.
 * @return The cost.
 */
Money GetScreenDoorsCost(const Station *st)
{
	return GetPrice(Price::BuildObject, 60, nullptr) * CountPlatformTiles(st);
}

/**
 * Yearly upkeep of a station's platform screen doors.
 * @param st The station.
 * @return The upkeep.
 */
Money GetScreenDoorsYearlyUpkeep(const Station *st)
{
	if (!st->screen_doors) return 0;
	return GetPrice(Price::BuildObject, 4, nullptr) * CountPlatformTiles(st);
}

/**
 * Fit platform screen doors along all platforms of a station.
 * @param flags Type of operation.
 * @param station_id The station.
 * @return The cost of this operation or an error.
 */
CommandCost CmdBuildScreenDoors(DoCommandFlags flags, StationID station_id)
{
	Station *st = Station::GetIfValid(station_id);
	if (st == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(st->owner);
	if (ret.Failed()) return ret;

	if (st->screen_doors) return CommandCost(STR_ERROR_ALREADY_BUILT);
	if (CountPlatformTiles(st) == 0) return CommandCost(STR_ERROR_SCREEN_DOORS_NEED_PLATFORMS);

	const Money cost = GetScreenDoorsCost(st);
	const Money town_share = GetTownShareOfUpgrade(st->town, cost);
	if (flags.Test(DoCommandFlag::Execute)) {
		st->screen_doors = true;
		st->town->transit.spent_this_year += town_share;
		for (TileIndex tile : st->train_station) {
			if (st->TileBelongsToRailStation(tile)) MarkTileDirtyByTile(tile);
		}
		SetWindowDirty(WindowClass::StationView, st->index);
		SetWindowDirty(WindowClass::TransitAuthority, st->town->index);
		InvalidateWindowClassesData(WindowClass::Line);
	}
	return CommandCost(ExpensesType::Construction, cost - town_share);
}

/**
 * Draw the screen doors along both platforms of a railway station tile:
 * the far ones behind the train, the near ones in front of it.
 * @param ti The railway station tile.
 */
void DrawScreenDoors(const TileInfo *ti)
{
	const Station *st = Station::GetByTile(ti->tile);
	if (!st->screen_doors) return;

	const bool transparent = IsTransparencySet(TransparencyOption::Buildings);
	/* The sprites are drawn from the tile's north corner, the boxes along the platform edges. */
	if (GetRailStationAxis(ti->tile) == Axis::X) {
		AddSortableSpriteToDraw(SPR_SCREEN_DOORS_X_FAR, PAL_NONE, *ti, {{0, 4, 2}, {TILE_SIZE, 1, 7}, {0, -4, -2}}, transparent);
		AddSortableSpriteToDraw(SPR_SCREEN_DOORS_X_NEAR, PAL_NONE, *ti, {{0, 11, 2}, {TILE_SIZE, 1, 7}, {0, -11, -2}}, transparent);
	} else {
		AddSortableSpriteToDraw(SPR_SCREEN_DOORS_Y_FAR, PAL_NONE, *ti, {{4, 0, 2}, {1, TILE_SIZE, 7}, {-4, 0, -2}}, transparent);
		AddSortableSpriteToDraw(SPR_SCREEN_DOORS_Y_NEAR, PAL_NONE, *ti, {{11, 0, 2}, {1, TILE_SIZE, 7}, {-11, 0, -2}}, transparent);
	}
}

/**
 * Is a line run with trains?
 * @param list The line.
 * @return True if it is.
 */
bool IsTrainLine(const OrderList *list)
{
	const Vehicle *v = list->GetFirstSharedVehicle();
	return v != nullptr && v->type == VehicleType::Train;
}

/**
 * Are all the engines of a train electric (electric, monorail or maglev)?
 * @param front The train.
 * @return True if they are and it has any.
 */
static bool IsElectricTrain(const Train *front)
{
	bool engines = false;
	for (const Train *u = front; u != nullptr; u = u->Next()) {
		const RailVehicleInfo &rvi = u->GetEngine()->VehInfo<RailVehicleInfo>();
		if (rvi.railveh_type == RailVehicleType::Wagon) continue;
		engines = true;
		if (rvi.engclass < EngineClass::Electric) return false;
	}
	return engines;
}

/**
 * The stops of a line, and how many of them have platform screen doors.
 * @param list The line.
 * @param[out] stops How many different stations it stops at.
 * @return How many of those have screen doors.
 */
uint GetLineStopsWithScreenDoors(const OrderList *list, uint *stops)
{
	std::vector<StationID> seen;
	uint doors = 0;
	for (const Order &order : list->GetOrders()) {
		if (!order.IsType(OT_GOTO_STATION) || order.GetNonStopType().Test(OrderNonStopFlag::GoVia)) continue;
		const StationID id = order.GetDestination().ToStationID();
		if (std::ranges::find(seen, id) != seen.end()) continue;
		seen.push_back(id);
		const Station *st = Station::GetIfValid(id);
		if (st != nullptr && st->screen_doors) doors++;
	}
	*stops = static_cast<uint>(seen.size());
	return doors;
}

/**
 * Why a line can't run without drivers.
 * @param list The line.
 * @param check_crossings Also refuse when its trains crossed roads at level crossings this month or last month.
 * @return The reason, or INVALID_STRING_ID if it can.
 */
static StringID GetDriverlessProblem(const OrderList *list, bool check_crossings)
{
	if (!IsTrainLine(list)) return STR_ERROR_DRIVERLESS_ONLY_TRAINS;
	for (const Vehicle *v = list->GetFirstSharedVehicle(); v != nullptr; v = v->NextShared()) {
		if (!IsElectricTrain(Train::From(v))) return STR_ERROR_DRIVERLESS_NOT_ELECTRIC;
	}
	if (IsPassengerLine(list)) {
		uint stops = 0;
		if (GetLineStopsWithScreenDoors(list, &stops) < stops || stops == 0) return STR_ERROR_DRIVERLESS_NO_SCREEN_DOORS;
	}
	if (check_crossings && (list->line.this_month.crossings > 0 || list->line.last_month.crossings > 0)) return STR_ERROR_DRIVERLESS_LEVEL_CROSSINGS;
	return INVALID_STRING_ID;
}

/**
 * Why a line can't run without drivers.
 * @param list The line.
 * @return The reason, or INVALID_STRING_ID if it can.
 */
StringID GetDriverlessProblem(const OrderList *list)
{
	return GetDriverlessProblem(list, true);
}

/**
 * Cost of fitting the trains of a line for running without drivers: a fifth of the price of each engine.
 * @param list The line.
 * @return The cost.
 */
Money GetDriverlessFittingCost(const OrderList *list)
{
	Money cost = 0;
	for (const Vehicle *v = list->GetFirstSharedVehicle(); v != nullptr; v = v->NextShared()) {
		if (v->type != VehicleType::Train) continue;
		for (const Train *u = Train::From(v); u != nullptr; u = u->Next()) {
			if (u->GetEngine()->VehInfo<RailVehicleInfo>().railveh_type == RailVehicleType::Wagon) continue;
			cost += u->GetEngine()->GetCost() / 5;
		}
	}
	return cost;
}

/**
 * Running costs of a train, in percent: lower without a driver.
 * @param front The train.
 * @return The percentage.
 */
uint GetTrainRunningCostPercent(const Train *front)
{
	return (front->orders != nullptr && front->orders->line.driverless) ? DRIVERLESS_RUNNING_COST_PERCENT : 100;
}

/**
 * A line goes back to running with drivers.
 * @param list The line.
 * @param reason Why, for the news.
 */
static void EndDriverless(OrderList *list, StringID reason)
{
	list->line.driverless = false;
	const Vehicle *v = list->GetFirstSharedVehicle();
	if (v != nullptr && v->owner == _local_company) {
		AddVehicleAdviceNewsItem(AdviceType::DriverlessEnded, GetEncodedString(STR_NEWS_DRIVERLESS_ENDED, v->index, reason), v->index);
	}
	SetWindowDirty(WindowClass::Line, list->index.base());
	SetWindowClassesDirty(WindowClass::LineMap);
}

/**
 * The front of a train enters a level crossing: count it for its line; a
 * driverless line gets its drivers back.
 * @param front The train.
 */
void OnTrainEntersLevelCrossing(Train *front)
{
	OrderList *list = front->orders;
	if (list == nullptr) return;
	list->line.this_month.crossings++;
	if (list->line.driverless) EndDriverless(list, STR_DRIVERLESS_ENDED_CROSSING);
}

/**
 * The month ended: the upkeep of screen doors, and drivers back on driverless
 * lines that no longer qualify (trains bought without electric engines, stops
 * without screen doors). Called before the line statistics move on.
 */
void MetroMonthlyLoop()
{
	std::array<Money, MAX_COMPANIES> upkeep{};
	for (const Station *st : Station::Iterate()) {
		if (st->screen_doors && Company::IsValidID(st->owner)) upkeep[st->owner.base()] += GetScreenDoorsYearlyUpkeep(st) / 12;
	}
	for (Company *c : Company::Iterate()) {
		if (upkeep[c->index.base()] != 0) SubtractMoneyFromCompany(c->index, CommandCost(ExpensesType::Property, upkeep[c->index.base()]));
	}

	for (OrderList *list : OrderList::Iterate()) {
		if (!list->line.driverless) continue;
		const StringID problem = GetDriverlessProblem(list, false);
		if (problem == INVALID_STRING_ID) continue;
		EndDriverless(list, problem == STR_ERROR_DRIVERLESS_NO_SCREEN_DOORS ? STR_DRIVERLESS_ENDED_SCREEN_DOORS : STR_DRIVERLESS_ENDED_TRAINS);
	}
}
