/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file stop_charger.cpp Fast chargers at bus stops.
 *
 * A company can put a charger over one of its bus stops, typically at the end
 * of a line. Electric buses top up while they stand there, and one below 40%
 * waits under it until it gets there, so a line with chargers at its ends
 * hardly needs to send its buses to the depot to charge.
 */

#include "stdafx.h"
#include "stop_charger.h"
#include "stop_charger_cmd.h"
#include "city_bus.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "economy_func.h"
#include "news_func.h"
#include "roadveh.h"
#include "station_map.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"
#include "timer/timer_game_tick.h"
#include "transparency.h"
#include "viewport_func.h"
#include "window_func.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

static std::set<TileIndex> _stop_chargers; ///< Bus stop tiles with a charger.

/**
 * The bus stops with chargers, for saving and loading.
 * @return The stop tiles.
 */
std::set<TileIndex> &GetStopChargers()
{
	return _stop_chargers;
}

/** Forget the chargers; used when starting or loading a game. */
void InitializeStopChargers()
{
	_stop_chargers.clear();
}

/**
 * Does a bus stop have a charger?
 * @param tile The tile.
 * @return True if it has.
 */
bool HasStopCharger(TileIndex tile)
{
	return !_stop_chargers.empty() && _stop_chargers.contains(tile);
}

/**
 * Cost of building a charger at a stop.
 * @return The cost.
 */
Money GetStopChargerBuildCost()
{
	return GetPrice(Price::BuildObject, 250, nullptr);
}

/**
 * Yearly upkeep of a charger at a stop.
 * @return The upkeep.
 */
Money GetStopChargerYearlyUpkeep()
{
	return GetPrice(Price::BuildObject, 25, nullptr);
}

/**
 * A road stop was removed: so is its charger.
 * @param tile The stop's tile.
 */
void OnRoadStopRemoved(TileIndex tile)
{
	_stop_chargers.erase(tile);
}

/**
 * Build a charger over a bus stop.
 * @param flags Type of operation.
 * @param tile The bus stop.
 * @return The cost of this operation or an error.
 */
CommandCost CmdBuildStopCharger(DoCommandFlags flags, TileIndex tile)
{
	if (!IsValidTile(tile) || !IsAnyRoadStopTile(tile) || !IsBusStop(tile)) return CommandCost(STR_ERROR_STOP_CHARGER_BUS_STOP_ONLY);
	CommandCost ret = CheckTileOwnership(tile);
	if (ret.Failed()) return ret;
	if (HasStopCharger(tile)) return CommandCost(STR_ERROR_ALREADY_BUILT);

	if (flags.Test(DoCommandFlag::Execute)) {
		_stop_chargers.insert(tile);
		MarkTileDirtyByTile(tile);
	}
	return CommandCost(ExpensesType::Construction, GetStopChargerBuildCost());
}

/**
 * Remove the charger of a bus stop.
 * @param flags Type of operation.
 * @param tile The bus stop.
 * @return The cost of this operation or an error.
 */
CommandCost CmdRemoveStopCharger(DoCommandFlags flags, TileIndex tile)
{
	if (!IsValidTile(tile) || !HasStopCharger(tile)) return CommandCost(STR_ERROR_STOP_CHARGER_NONE);
	CommandCost ret = CheckTileOwnership(tile);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		_stop_chargers.erase(tile);
		MarkTileDirtyByTile(tile);
	}
	return CommandCost(ExpensesType::Construction, GetPrice(Price::ClearObject, 25, nullptr));
}

/**
 * Charge an electric bus standing at a stop with a charger; called every tick it is loading.
 * @param v The bus (front).
 */
void ChargeAtStop(RoadVehicle *v)
{
	if (!HasStopCharger(v->tile) || !IsElectricBus(v)) return;
	const uint32_t capacity = GetBatteryCapacityWh(v);
	if (v->battery_charge >= capacity) return;

	if (v->battery_charge == 0) DeleteVehicleNews(v->index, AdviceType::BatteryFlat);
	v->battery_charge = std::min<uint32_t>(capacity, v->battery_charge + STOP_CHARGER_KWH * 1000 / Ticks::DAY_TICKS);
	if (TimerGameTick::counter % 16 == 0) SetWindowDirty(WindowClass::VehicleDetails, v->index);
}

/**
 * Must a vehicle wait at its stop to charge? An electric bus below #STOP_CHARGE_HOLD_PERCENT does, under a charger.
 * @param v The vehicle (front), loading at a stop.
 * @return True if it must wait.
 */
bool MustWaitToCharge(const Vehicle *v)
{
	if (v->type != VehicleType::Road || _stop_chargers.empty() || !HasStopCharger(v->tile)) return false;
	const RoadVehicle *rv = RoadVehicle::From(v);
	return IsElectricBus(rv) && GetBatteryPercent(rv) < STOP_CHARGE_HOLD_PERCENT;
}

/**
 * Draw the charger over a bus stop: a gantry across the road with a pantograph over each lane.
 * @param ti The stop's tile.
 */
void DrawStopCharger(const TileInfo *ti)
{
	if (IsInvisibilitySet(TransparencyOption::Buildings)) return;
	Axis axis = IsDriveThroughStopTile(ti->tile) ? GetDriveThroughStopAxis(ti->tile) : DiagDirToAxis(GetBayRoadStopDir(ti->tile));
	SpriteID image = SPR_STOP_CHARGER_X + (axis == Axis::X ? 0 : 1);
	SetBit(image, PALETTE_MODIFIER_COLOUR);
	/* The gantry is above the buses under it. */
	AddSortableSpriteToDraw(image, GetCompanyPalette(GetTileOwner(ti->tile)), *ti, SpriteBounds({0, 0, 12}, {TILE_SIZE, TILE_SIZE, 3}, {0, 0, -12}), IsTransparencySet(TransparencyOption::Buildings));
}

/** Charge the upkeep of the chargers at stops, each month. */
static const IntervalTimer<TimerGameEconomy> _stop_chargers_monthly({TimerGameEconomy::Trigger::Month, TimerGameEconomy::Priority::None}, [](auto)
{
	std::array<Money, MAX_COMPANIES> upkeep{};
	for (TileIndex tile : _stop_chargers) {
		Owner owner = GetTileOwner(tile);
		if (Company::IsValidID(owner)) upkeep[owner.base()] += GetStopChargerYearlyUpkeep() / 12;
	}
	for (Company *c : Company::Iterate()) {
		if (upkeep[c->index.base()] != 0) SubtractMoneyFromCompany(c->index, CommandCost(ExpensesType::Property, upkeep[c->index.base()]));
	}
});
