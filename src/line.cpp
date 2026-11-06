/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file line.cpp Lines: the vehicles that share one order list, with a number, name and colour. */

#include "stdafx.h"
#include "line.h"
#include "line_cmd.h"
#include "command_func.h"
#include "company_func.h"
#include "fare.h"
#include "order_base.h"
#include "string_func.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "window_func.h"

#include "table/strings.h"

#include "safeguards.h"

/** A departure from the first stop up to this early counts as on time. */
static constexpr TimerGameTick::Ticks LINE_EARLY_TOLERANCE = Ticks::DAY_TICKS;
/** A departure from the first stop up to this late counts as on time. */
static constexpr TimerGameTick::Ticks LINE_LATE_TOLERANCE = 3 * Ticks::DAY_TICKS;
/** Longest target gap between departures that can be set. */
static constexpr TimerGameTick::Ticks LINE_MAX_TARGET_GAP = 365 * Ticks::DAY_TICKS;

/**
 * The order of a line that counts as its first stop: departures from there
 * set the line's rhythm.
 * @param list The order list.
 * @return The index of the first station order, or INVALID_VEH_ORDER_ID if there is none.
 */
VehicleOrderID GetLineFirstStop(const OrderList *list)
{
	if (list == nullptr) return INVALID_VEH_ORDER_ID;
	VehicleOrderID index = 0;
	for (const Order &order : list->GetOrders()) {
		if (order.IsType(OT_GOTO_STATION)) return index;
		index++;
	}
	return INVALID_VEH_ORDER_ID;
}

/**
 * The colour a line is drawn in.
 * @param list The order list.
 * @return The chosen colour, or one picked from the list's number when none is chosen.
 */
Colours GetLineColour(const OrderList *list)
{
	if (list->line.colour != Colours::Invalid) return list->line.colour;
	return static_cast<Colours>(list->index.base() % to_underlying(Colours::End));
}

/**
 * Average time between recent departures from the first stop.
 * @param line The line.
 * @return The average gap in ticks, or 0 if not known yet.
 */
uint32_t GetLineAverageGap(const LineInfo &line)
{
	if (line.num_gaps == 0) return 0;
	uint64_t sum = 0;
	for (uint i = 0; i < line.num_gaps; i++) sum += line.gaps[i];
	return static_cast<uint32_t>(sum / line.num_gaps);
}

/**
 * How long someone who turns up at the first stop at a random time waits on
 * average, over recent departures. With even gaps that is half the gap; the
 * more the vehicles bunch, the longer it gets.
 * @param line The line.
 * @return The average wait in ticks, or 0 if not known yet.
 */
uint32_t GetLineAverageWait(const LineInfo &line)
{
	uint64_t sum = 0, sum_squares = 0;
	for (uint i = 0; i < line.num_gaps; i++) {
		sum += line.gaps[i];
		sum_squares += static_cast<uint64_t>(line.gaps[i]) * line.gaps[i];
	}
	return sum == 0 ? 0 : static_cast<uint32_t>(sum_squares / (2 * sum));
}

/**
 * How many vehicles a line needs to run at its target gap, from the measured
 * (or timetabled) time of one round trip.
 * @param list The order list.
 * @return The number of vehicles, or 0 if there is no target or the round trip isn't known yet.
 */
uint GetLineVehiclesNeeded(const OrderList *list)
{
	TimerGameTick::Ticks round_trip = list->GetTotalDuration();
	if (list->line.target_gap <= 0 || round_trip <= 0) return 0;
	return CeilDiv(round_trip, list->line.target_gap);
}

/**
 * A vehicle of a line leaves a stop: count how full it leaves and, at the
 * first stop, the departure itself.
 * @param v The vehicle (front).
 * @param occupancy How full it leaves, in percent.
 */
void OnLineVehicleDeparts(Vehicle *v, uint8_t occupancy)
{
	OrderList *list = v->orders;
	if (list == nullptr) return;
	LineInfo &line = list->line;

	line.this_month.load_sum += occupancy;
	line.this_month.load_count++;
	line.this_month.max_load = std::max(line.this_month.max_load, occupancy);

	VehicleOrderID first = GetLineFirstStop(list);
	if (first == INVALID_VEH_ORDER_ID || v->cur_real_order_index != first) return;
	const Order *order = list->GetOrderAt(first);
	if (order == nullptr || order->GetDestination() != v->last_station_visited) return;

	line.this_month.departures++;
	if (line.last_departure != 0) line.AddGap(ClampTo<uint32_t>(TimerGameTick::counter - line.last_departure));
	line.last_departure = TimerGameTick::counter;

	if (v->vehicle_flags.Test(VehicleFlag::TimetableStarted) && list->IsCompleteTimetable()) {
		line.this_month.timed++;
		if (v->lateness_counter >= -LINE_EARLY_TOLERANCE && v->lateness_counter <= LINE_LATE_TOLERANCE) line.this_month.on_time++;
	}
	SetWindowDirty(WindowClass::Line, list->index.base());
}

/**
 * Must a vehicle wait at its line's first stop to keep the departures evenly
 * spaced? Only when the line holds for spacing and the target gap since the
 * last departure hasn't passed yet. The vehicle keeps loading meanwhile.
 * @param v The vehicle (front), loading at a stop.
 * @return True if it must wait.
 */
bool MustHoldForLineSpacing(const Vehicle *v)
{
	const OrderList *list = v->orders;
	if (list == nullptr || !list->line.hold_for_spacing || list->line.target_gap <= 0 || list->line.last_departure == 0) return false;

	VehicleOrderID first = GetLineFirstStop(list);
	if (first == INVALID_VEH_ORDER_ID || v->cur_real_order_index != first) return false;
	const Order *order = list->GetOrderAt(first);
	if (order == nullptr || order->GetDestination() != v->last_station_visited) return false;

	return TimerGameTick::counter < list->line.last_departure + list->line.target_gap;
}

/**
 * Passengers got off a vehicle of a line.
 * @param front The vehicle (front).
 * @param count How many.
 */
void OnLineRidersDelivered(const Vehicle *front, uint count)
{
	if (front->orders == nullptr) return;
	front->orders->line.this_month.riders += count;
}

/** Start each month's line statistics afresh, after selling monthly passes for the month's riders. */
static const IntervalTimer<TimerGameEconomy> _economy_lines_monthly({TimerGameEconomy::Trigger::Month, TimerGameEconomy::Priority::None}, [](auto)
{
	FaresMonthlyLoop();
	for (OrderList *list : OrderList::Iterate()) {
		list->line.last_month = list->line.this_month;
		list->line.this_month = {};
	}
	InvalidateWindowClassesData(WindowClass::Line);
});

/**
 * Change a line's number, name, colour, target gap or spacing.
 * @param flags Operation to perform.
 * @param veh_id Any vehicle of the line.
 * @param property What to change.
 * @param value The new colour, gap or switch.
 * @param text The new number or name.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetLineProperty(DoCommandFlags flags, VehicleID veh_id, LineProperty property, uint32_t value, const std::string &text)
{
	Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr || !v->IsPrimaryVehicle() || v->orders == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	switch (property) {
		case LineProperty::Number:
			if (Utf8StringLength(text) > MAX_LENGTH_LINE_NUMBER_CHARS) return CMD_ERROR;
			break;
		case LineProperty::Name:
			if (Utf8StringLength(text) > MAX_LENGTH_LINE_NAME_CHARS) return CMD_ERROR;
			break;
		case LineProperty::Colour:
			if (value >= to_underlying(Colours::End) && value != to_underlying(Colours::Invalid)) return CMD_ERROR;
			break;
		case LineProperty::TargetGap:
			if (value > static_cast<uint32_t>(LINE_MAX_TARGET_GAP)) return CMD_ERROR;
			break;
		case LineProperty::HoldForSpacing:
			if (value > 1) return CMD_ERROR;
			break;
		case LineProperty::FareScheme:
			if (value >= to_underlying(FareScheme::End) && value != to_underlying(FareScheme::Inherit)) return CMD_ERROR;
			break;
		case LineProperty::FarePrice:
			if (value != 0 && (value < FARE_PRICE_MIN || value > FARE_PRICE_MAX)) return CMD_ERROR;
			break;
		default:
			return CMD_ERROR;
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		LineInfo &line = v->orders->line;
		switch (property) {
			case LineProperty::Number: line.number = text; break;
			case LineProperty::Name: line.name = text; break;
			case LineProperty::Colour: line.colour = static_cast<Colours>(value); break;
			case LineProperty::TargetGap: line.target_gap = static_cast<TimerGameTick::Ticks>(value); break;
			case LineProperty::HoldForSpacing: line.hold_for_spacing = value != 0; break;
			case LineProperty::FareScheme: line.fare_scheme = static_cast<FareScheme>(value); break;
			case LineProperty::FarePrice: line.fare_price = static_cast<uint16_t>(value); break;
			default: NOT_REACHED();
		}
		SetWindowDirty(WindowClass::Line, v->orders->index.base());
		InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type), 0);
		InvalidateWindowClassesData(WindowClass::VehicleTimetable, 0);
	}

	return CommandCost();
}
