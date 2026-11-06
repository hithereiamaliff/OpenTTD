/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file traffic_light.cpp Traffic lights at road junctions. */

#include "stdafx.h"
#include "traffic_light.h"
#include "town_traffic.h"
#include "traffic_light_cmd.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "debug.h"
#include "direction_func.h"
#include "economy_func.h"
#include "map_func.h"
#include "road_map.h"
#include "roadveh.h"
#include "track_func.h"
#include "vehicle_func.h"
#include "viewport_func.h"
#include "window_func.h"
#include "timer/timer_game_tick.h"
#include "core/bitmath_func.hpp"

#include "table/strings.h"

#include "safeguards.h"

/** All traffic lights in the game, by junction tile. Ordered, so iterating is deterministic. */
TrafficLightMap _traffic_lights;

/** The four directions vehicles can travel in, in a fixed order. */
static constexpr DiagDirection ALL_DIAGDIRS[] = {DiagDirection::NE, DiagDirection::SE, DiagDirection::SW, DiagDirection::NW};

/**
 * Does traffic travelling along the given axis currently have green?
 * @param axis The axis vehicles are travelling along.
 * @return true if vehicles on that axis may enter the junction.
 */
bool TrafficLight::IsGreenFor(Axis axis) const
{
	return !this->clearing && this->green_axis == to_underlying(axis);
}

/**
 * Does a bus have right of way along an axis? With bus signals buses get
 * green #TRAFFIC_LIGHT_BUS_HEAD_START ticks before the rest of the traffic.
 * @param axis The road through the junction.
 * @return True if a bus may enter.
 */
bool TrafficLight::IsGreenForBus(Axis axis) const
{
	if (this->IsGreenFor(axis)) return true;
	if (!this->bus_priority || !this->clearing || this->green_axis == to_underlying(axis)) return false;
	return this->phase_ticks + TRAFFIC_LIGHT_BUS_HEAD_START >= TRAFFIC_LIGHT_CLEARANCE;
}

/**
 * What does the light facing traffic along an axis show?
 * @param axis The axis vehicles are travelling along.
 * @return The lamp that is lit.
 */
TrafficLightLamp TrafficLight::GetLamp(Axis axis) const
{
	if (this->IsGreenFor(axis)) return TrafficLightLamp::Green;
	if (this->clearing && this->green_axis == to_underlying(axis) && this->phase_ticks < TRAFFIC_LIGHT_AMBER) return TrafficLightLamp::Amber;
	return TrafficLightLamp::Red;
}

/**
 * Can traffic lights be placed on this tile?
 * Only plain road tiles with a junction (three or more road pieces) qualify.
 * @param tile The tile to check.
 * @return true if the tile is a road junction.
 */
bool CanHaveTrafficLight(TileIndex tile)
{
	if (!IsValidTile(tile) || !IsNormalRoadTile(tile)) return false;
	return GetAllRoadBits(tile).Count() >= 3;
}

/**
 * Get the traffic lights of a junction.
 * @param tile The junction tile.
 * @return The lights, or nullptr if the junction has none.
 */
TrafficLight *GetTrafficLight(TileIndex tile)
{
	auto it = _traffic_lights.find(tile);
	return it == _traffic_lights.end() ? nullptr : &it->second;
}

/**
 * Get the trackdirs that vehicles may currently not enter because their light is red.
 * @param tile The junction tile.
 * @return The trackdirs behind a red light.
 */
TrackdirBits GetTrafficLightRedTrackdirs(TileIndex tile)
{
	const TrafficLight *tl = GetTrafficLight(tile);
	if (tl == nullptr) return {};

	TrackdirBits red{};
	for (DiagDirection dir : ALL_DIAGDIRS) {
		if (!tl->IsGreenFor(DiagDirToAxis(dir))) red |= DiagdirReachesTrackdirs(dir);
	}
	return red;
}

/**
 * Get the bus signal of traffic lights for traffic along an axis.
 * @param tile The junction tile.
 * @param axis The road through the junction.
 * @return -1 without bus signals, 1 when buses may go, 0 when they may not.
 */
int GetTrafficLightBusLamp(TileIndex tile, Axis axis)
{
	const TrafficLight *tl = GetTrafficLight(tile);
	if (tl == nullptr || !tl->bus_priority) return -1;
	return tl->IsGreenForBus(axis) ? 1 : 0;
}

/**
 * What do the lights facing traffic on this axis show?
 * @param tile The junction tile.
 * @param axis The axis of the traffic the light faces.
 * @return The lamp that is lit; red if the junction has no lights.
 */
TrafficLightLamp GetTrafficLightLamp(TileIndex tile, Axis axis)
{
	const TrafficLight *tl = GetTrafficLight(tile);
	return tl == nullptr ? TrafficLightLamp::Red : tl->GetLamp(axis);
}

/**
 * Should the lights facing traffic on this axis be drawn green?
 * @param tile The junction tile.
 * @param axis The axis of the traffic the light faces.
 * @return true for green, false for red.
 */
bool TrafficLightShowsGreen(TileIndex tile, Axis axis)
{
	const TrafficLight *tl = GetTrafficLight(tile);
	return tl != nullptr && tl->IsGreenFor(axis);
}

/**
 * Remove the traffic lights of a junction, if any.
 * @param tile The junction tile.
 */
static void DeleteTrafficLight(TileIndex tile)
{
	_traffic_lights.erase(tile);
	if (IsNormalRoadTile(tile)) SetTrafficLight(tile, false);
	MarkTileDirtyByTile(tile);
	CloseWindowById(WindowClass::TrafficLight, tile.base());
}

/**
 * Road pieces of a tile were removed: drop the lights if the tile is no longer a junction.
 * @param tile The tile that changed.
 */
void TrafficLightsOnRoadChanged(TileIndex tile)
{
	if (!_traffic_lights.contains(tile)) return;
	if (!CanHaveTrafficLight(tile)) DeleteTrafficLight(tile);
}

/**
 * A company went bankrupt or was taken over: hand its traffic lights to the new owner.
 * Lights of a bankrupt company keep working and become public.
 * @param old_owner The company that disappears.
 * @param new_owner The company taking over, or #INVALID_OWNER.
 */
void ChangeTrafficLightOwner(Owner old_owner, Owner new_owner)
{
	for (auto &[tile, tl] : _traffic_lights) {
		if (tl.owner == old_owner) tl.owner = (new_owner == INVALID_OWNER) ? OWNER_NONE : new_owner;
	}
}

/** Remove all traffic lights; used when starting or loading a game. */
void InitializeTrafficLights()
{
	_traffic_lights.clear();
}

/** After loading a game, rebuild the map bits from the list of lights and drop invalid entries. */
void AfterLoadTrafficLights()
{
	for (const auto tile : Map::Iterate()) {
		if (IsNormalRoadTile(tile)) SetTrafficLight(tile, false);
	}

	for (auto it = _traffic_lights.begin(); it != _traffic_lights.end(); /* nothing */) {
		TrafficLight &tl = it->second;
		if (it->first != tl.tile || !CanHaveTrafficLight(it->first)) {
			it = _traffic_lights.erase(it);
			continue;
		}
		if (tl.green_axis > to_underlying(Axis::Y)) tl.green_axis = to_underlying(Axis::X);
		if (tl.mode >= TrafficLightMode::End) tl.mode = TrafficLightMode::Smart;
		tl.green_x = Clamp(tl.green_x, TRAFFIC_LIGHT_MIN_TIME, TRAFFIC_LIGHT_MAX_TIME);
		tl.green_y = Clamp(tl.green_y, TRAFFIC_LIGHT_MIN_TIME, TRAFFIC_LIGHT_MAX_TIME);
		tl.min_green = Clamp(tl.min_green, TRAFFIC_LIGHT_MIN_TIME, std::min(tl.green_x, tl.green_y));
		tl.demand = 0;
		tl.bus_demand = 0;
		tl.entered = false;
		SetTrafficLight(it->first, true);
		++it;
	}
}

/**
 * Is a bus about to cross a junction? Looks for buses driving towards it on
 * the last #TRAFFIC_LIGHT_BUS_LOOKAHEAD tiles before it, from both sides.
 * Buses standing at a stop or in a depot there don't count.
 * @param junction The junction tile.
 * @param axis The road through the junction.
 * @return True if a bus is on its way in.
 */
static bool IsBusApproaching(TileIndex junction, Axis axis)
{
	for (DiagDirection side : {AxisToDiagDir(axis), ReverseDiagDir(AxisToDiagDir(axis))}) {
		const DiagDirection towards = ReverseDiagDir(side);
		TileIndex tile = junction;
		for (uint i = 0; i < TRAFFIC_LIGHT_BUS_LOOKAHEAD; i++) {
			tile = TileAddByDiagDir(tile, side);
			if (!IsValidTile(tile)) break;
			bool found = HasVehicleOnTile(tile, [towards](const Vehicle *v) {
				if (v->type != VehicleType::Road) return false;
				const RoadVehicle *front = RoadVehicle::From(v)->First();
				return front->IsBus() && !front->IsInDepot() && front->cur_speed > 0 &&
						!front->current_order.IsType(OT_LOADING) && DirToDiagDir(front->direction) == towards;
			});
			if (found) return true;
		}
	}
	return false;
}

/**
 * Run the controller of these lights for one tick.
 * Fixed lights follow the global tick counter, so all of them stay in step,
 * also after loading a game; smart lights react to the traffic.
 * @return True if the lights changed colour.
 */
bool TrafficLight::Update()
{
	const uint8_t old_axis = this->green_axis;
	const bool old_clearing = this->clearing;

	if (this->mode == TrafficLightMode::Fixed) {
		/* Cycle: green X, all red, green Y, all red. */
		uint32_t t = static_cast<uint32_t>((TimerGameTick::counter + this->offset) % this->GetCycleLength());
		uint32_t phase_start = 0;
		for (Axis axis : {Axis::X, Axis::Y}) {
			uint32_t green = this->GetGreenTime(axis);
			if (t < phase_start + green + TRAFFIC_LIGHT_CLEARANCE) {
				this->green_axis = to_underlying(axis);
				this->clearing = t >= phase_start + green;
				this->phase_ticks = static_cast<uint16_t>(this->clearing ? t - phase_start - green : t - phase_start);
				break;
			}
			phase_start += green + TRAFFIC_LIGHT_CLEARANCE;
		}
		return this->green_axis != old_axis || this->clearing != old_clearing;
	}

	Axis other = OtherAxis(static_cast<Axis>(this->green_axis));
	if (this->clearing) {
		if (++this->phase_ticks >= TRAFFIC_LIGHT_CLEARANCE) {
			this->green_axis = to_underlying(other);
			this->clearing = false;
			this->phase_ticks = 0;
			this->gap_ticks = 0;
		}
	} else {
		if (this->phase_ticks < UINT16_MAX) this->phase_ticks++;
		if (this->entered) {
			this->gap_ticks = 0;
		} else if (this->gap_ticks < UINT16_MAX) {
			this->gap_ticks++;
		}

		/* Switch when someone waits on the other road, the green road had its
		 * shortest green, and its traffic stopped or it used up its longest green. */
		const Axis green = static_cast<Axis>(this->green_axis);
		const uint16_t longest = this->GetGreenTime(green);
		bool other_waiting = HasBit(this->demand, to_underlying(other)) ||
				TownTrafficWaitsAt(this->tile, other, this->phase_ticks >= longest);
		/* Bus signals: a bus waiting on the other road cuts the green short, after half the shortest green... */
		bool bus_waiting = this->bus_priority && HasBit(this->bus_demand, to_underlying(other));
		uint16_t shortest = bus_waiting ? std::max<uint16_t>(TRAFFIC_LIGHT_MIN_TIME, this->min_green / 2) : this->min_green;
		if (other_waiting && this->phase_ticks >= shortest &&
				(bus_waiting || this->gap_ticks >= TRAFFIC_LIGHT_GAP || this->phase_ticks >= longest) &&
				/* ... and a bus about to cross on the green road keeps it, up to the longest green. */
				(bus_waiting || !this->bus_priority || this->phase_ticks >= longest || !IsBusApproaching(this->tile, green))) {
			this->clearing = true;
			this->phase_ticks = 0;
		}
	}
	return this->green_axis != old_axis || this->clearing != old_clearing;
}

/** Run the controllers of all traffic lights for one tick. Runs after vehicles have moved. */
void OnTick_TrafficLights()
{
	for (auto &[tile, tl] : _traffic_lights) {
		const TrafficLightLamp lamp_x = tl.GetLamp(Axis::X);
		const TrafficLightLamp lamp_y = tl.GetLamp(Axis::Y);
		bool changed = tl.Update();

		tl.demand = 0;
		tl.bus_demand = 0;
		tl.entered = false;
		if (changed) Debug(Facility::Misc, Severity::Debug2, "Traffic light {}x{}: {}", TileX(tile), TileY(tile), tl.clearing ? "all red" : (tl.green_axis == to_underlying(Axis::X) ? "green X" : "green Y"));
		/* Redraw for every change of colour, also from amber to red. */
		if (changed || tl.GetLamp(Axis::X) != lamp_x || tl.GetLamp(Axis::Y) != lamp_y) MarkTileDirtyByTile(tile);
	}
}

/**
 * Check whether the current company may change the lights of a junction.
 * Lights go on public (town) roads or on roads of the company itself.
 * @param tile The junction tile.
 * @return Success, or the reason why not.
 */
static CommandCost CheckTrafficLightRoadOwnership(TileIndex tile)
{
	for (RoadTramType rtt : ROADTRAMTYPES_ALL) {
		if (!HasTileRoadType(tile, rtt)) continue;
		Owner owner = GetRoadOwner(tile, rtt);
		if (owner == OWNER_TOWN || owner == OWNER_NONE || owner == OWNER_WATER) continue;
		CommandCost ret = CheckOwnership(owner, tile);
		if (ret.Failed()) return ret;
	}
	return CommandCost();
}

/**
 * Build traffic lights at a road junction.
 * @param flags Operation to perform.
 * @param tile The junction tile.
 * @return The cost of this operation or an error.
 */
CommandCost CmdBuildTrafficLight(DoCommandFlags flags, TileIndex tile)
{
	if (!IsValidTile(tile)) return CMD_ERROR;
	if (!CanHaveTrafficLight(tile)) return CommandCost(STR_ERROR_TRAFFIC_LIGHTS_NEED_JUNCTION);
	if (HasTrafficLight(tile)) return CommandCost(STR_ERROR_ALREADY_HAS_TRAFFIC_LIGHTS);
	if (HasRoadWorks(tile)) return CommandCost(STR_ERROR_ROAD_WORKS_IN_PROGRESS);

	CommandCost ret = CheckTrafficLightRoadOwnership(tile);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Start with green for the road that has the most branches; on a T-junction that is the main road. */
		RoadBits bits = GetAllRoadBits(tile);
		uint x_branches = bits.Test(RoadBit::NE) + bits.Test(RoadBit::SW);
		uint y_branches = bits.Test(RoadBit::NW) + bits.Test(RoadBit::SE);

		TrafficLight &tl = _traffic_lights[tile];
		tl = TrafficLight{};
		tl.tile = tile;
		/* Lights placed in the scenario editor or by a game script belong to nobody. */
		tl.owner = Company::IsValidID(_current_company) ? _current_company : OWNER_NONE;
		tl.green_axis = to_underlying(y_branches > x_branches ? Axis::Y : Axis::X);
		SetTrafficLight(tile, true);
		MarkTileDirtyByTile(tile);
	}

	/* About as much as a road depot: a controller plus four light posts. */
	return CommandCost(ExpensesType::Construction, _price[Price::BuildSignals] * 8);
}

/**
 * Check whether the current company may change or remove the lights of a junction.
 * Lights of a company are its own; public lights may be changed by anyone.
 * @param tl The lights.
 * @return Success, or the reason why not.
 */
static CommandCost CheckTrafficLightOwnership(const TrafficLight &tl)
{
	if (tl.owner == OWNER_NONE || tl.owner == OWNER_TOWN) return CommandCost();
	return CheckOwnership(tl.owner);
}

/**
 * Switch the bus signals of traffic lights on or off.
 * @param flags Operation to perform.
 * @param tile The junction tile.
 * @param on Whether buses get priority.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetTrafficLightBusPriority(DoCommandFlags flags, TileIndex tile, bool on)
{
	if (!IsValidTile(tile) || !IsNormalRoadTile(tile) || !HasTrafficLight(tile)) return CommandCost(STR_ERROR_NO_TRAFFIC_LIGHTS);
	TrafficLight *tl = GetTrafficLight(tile);
	if (tl == nullptr) return CommandCost(STR_ERROR_NO_TRAFFIC_LIGHTS);

	CommandCost ret = CheckTrafficLightOwnership(*tl);
	if (ret.Failed()) return ret;
	if (tl->bus_priority == on) return CommandCost(on ? STR_ERROR_ALREADY_BUILT : STR_ERROR_BUS_SIGNAL_NONE);

	if (flags.Test(DoCommandFlag::Execute)) {
		tl->bus_priority = on;
		MarkTileDirtyByTile(tile);
		InvalidateWindowData(WindowClass::TrafficLight, tile.base());
	}
	return CommandCost(ExpensesType::Construction, on ? GetPrice(Price::BuildObject, 25, nullptr) : Money{0});
}

/**
 * Change how the traffic lights of a junction switch.
 * @param flags Operation to perform.
 * @param tile The junction tile.
 * @param mode Smart or fixed time.
 * @param green_x (Longest) green for the road along the X axis, in ticks.
 * @param green_y (Longest) green for the road along the Y axis, in ticks.
 * @param min_green Shortest green of smart lights, in ticks.
 * @param offset Position in the cycle of fixed lights, in ticks.
 * @return The cost of this operation or an error.
 */
CommandCost CmdConfigureTrafficLight(DoCommandFlags flags, TileIndex tile, TrafficLightMode mode, uint16_t green_x, uint16_t green_y, uint16_t min_green, uint16_t offset)
{
	if (!IsValidTile(tile) || !IsNormalRoadTile(tile) || !HasTrafficLight(tile)) return CommandCost(STR_ERROR_NO_TRAFFIC_LIGHTS);
	TrafficLight *tl = GetTrafficLight(tile);
	if (tl == nullptr) return CommandCost(STR_ERROR_NO_TRAFFIC_LIGHTS);

	if (mode >= TrafficLightMode::End) return CMD_ERROR;
	if (!IsInsideMM(green_x, TRAFFIC_LIGHT_MIN_TIME, TRAFFIC_LIGHT_MAX_TIME + 1)) return CMD_ERROR;
	if (!IsInsideMM(green_y, TRAFFIC_LIGHT_MIN_TIME, TRAFFIC_LIGHT_MAX_TIME + 1)) return CMD_ERROR;
	if (!IsInsideMM(min_green, TRAFFIC_LIGHT_MIN_TIME, std::min(green_x, green_y) + 1)) return CMD_ERROR;
	if (offset >= green_x + green_y + 2 * TRAFFIC_LIGHT_CLEARANCE) return CMD_ERROR;

	CommandCost ret = CheckTrafficLightOwnership(*tl);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		tl->mode = mode;
		tl->green_x = green_x;
		tl->green_y = green_y;
		tl->min_green = min_green;
		tl->offset = offset;
		if (tl->Update()) MarkTileDirtyByTile(tile);
		InvalidateWindowData(WindowClass::TrafficLight, tile.base());
	}

	return CommandCost();
}

/**
 * Remove the traffic lights from a road junction.
 * @param flags Operation to perform.
 * @param tile The junction tile.
 * @return The cost of this operation or an error.
 */
CommandCost CmdRemoveTrafficLight(DoCommandFlags flags, TileIndex tile)
{
	if (!IsValidTile(tile) || !IsNormalRoadTile(tile) || !HasTrafficLight(tile)) return CommandCost(STR_ERROR_NO_TRAFFIC_LIGHTS);

	const TrafficLight *tl = GetTrafficLight(tile);
	if (tl != nullptr) {
		CommandCost ret = CheckTrafficLightOwnership(*tl);
		if (ret.Failed()) return ret;
	}

	if (flags.Test(DoCommandFlag::Execute)) DeleteTrafficLight(tile);

	return CommandCost(ExpensesType::Construction, _price[Price::ClearSignals] * 4);
}
