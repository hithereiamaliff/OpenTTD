/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file city_clock.cpp The city clock, rush hours, off-peak parking and on-demand lines.
 *
 *  - The city clock (setting economy.rush_hours, off by default) runs a city
 *    day per game month or per game year. Its morning (06:00-09:00) and
 *    evening (16:00-19:00) peaks have more passengers and more traffic; the
 *    night (00:00-06:00) and evening (19:00-24:00) fewer. Peak bus lanes are
 *    bus lanes in the peaks only.
 *  - With the clock on, a line can keep fewer vehicles running off-peak:
 *    when a peak ends the others go to a depot and wait; when the next peak
 *    starts they set out again (peaks-only lines keep none running).
 *  - An on-demand line (Rapid On-Demand, DRT) doesn't run its stops in
 *    order: after each stop its vehicles head for the stop of the line where
 *    the most passengers wait or that waited longest, nearer ones first.
 */

#include "stdafx.h"
#include "city_clock.h"
#include "cargotype.h"
#include "cargopacket.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "core/backup_type.hpp"
#include "core/random_func.hpp"
#include "line_type.h"
#include "map_func.h"
#include "order_base.h"
#include "order_func.h"
#include "settings_type.h"
#include "station_base.h"
#include "town_traffic.h"
#include "vehicle_base.h"
#include "vehicle_cmd.h"
#include "vehicle_func.h"
#include "window_func.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"
#include "timer/timer_game_tick.h"

#include "safeguards.h"

static std::vector<VehicleID> _offpeak_parked; ///< Vehicles sent to a depot to wait for the next peak.

/**
 * The vehicles that wait for the next peak, for saving and loading.
 * @return The vehicles.
 */
std::vector<VehicleID> &GetOffPeakParkedVehicles()
{
	return _offpeak_parked;
}

/** Forget the parked vehicles; used when starting or loading a game. */
void InitializeCityClock()
{
	_offpeak_parked.clear();
}

/**
 * Does the city clock run?
 * @return True if it does.
 */
bool IsCityClockOn()
{
	return _settings_game.economy.rush_hours != to_underlying(CityClockSetting::Off) && _settings_game.economy.rush_hours < to_underlying(CityClockSetting::End);
}

/**
 * The city day a date falls in: its game month or game year.
 * @param date The economy date.
 * @return The first date of that city day and the first date of the next one.
 */
static std::pair<TimerGameEconomy::Date, TimerGameEconomy::Date> GetCityDayBounds(TimerGameEconomy::Date date)
{
	const TimerGameEconomy::YearMonthDay ymd = TimerGameEconomy::ConvertDateToYMD(date);
	if (_settings_game.economy.rush_hours == to_underlying(CityClockSetting::Month)) {
		return {TimerGameEconomy::ConvertYMDToDate(ymd.year, ymd.month, 1),
				ymd.month == 11 ? TimerGameEconomy::ConvertYMDToDate(ymd.year + 1, 0, 1) : TimerGameEconomy::ConvertYMDToDate(ymd.year, ymd.month + 1, 1)};
	}
	return {TimerGameEconomy::ConvertYMDToDate(ymd.year, 0, 1), TimerGameEconomy::ConvertYMDToDate(ymd.year + 1, 0, 1)};
}

/**
 * How long the city day of a date lasts.
 * @param date The economy date.
 * @return The length in ticks.
 */
static int64_t GetCityDayTicks(TimerGameEconomy::Date date)
{
	const auto [start, next] = GetCityDayBounds(date);
	return static_cast<int64_t>((next - start).base()) * Ticks::DAY_TICKS;
}

/**
 * The minute of the city day at a moment.
 * @param date The economy date.
 * @param fract Ticks into that day.
 * @return The minute, 0 to 1439.
 */
static int GetCityMinuteAt(TimerGameEconomy::Date date, uint16_t fract)
{
	const auto [start, next] = GetCityDayBounds(date);
	const int64_t length = static_cast<int64_t>((next - start).base()) * Ticks::DAY_TICKS;
	const int64_t progress = static_cast<int64_t>((date - start).base()) * Ticks::DAY_TICKS + fract;
	return length <= 0 ? 0 : static_cast<int>(std::clamp<int64_t>(progress * 1440 / length, 0, 1439));
}

/**
 * The minute of the city day now.
 * @return The minute, 0 to 1439, or -1 when the city clock doesn't run.
 */
int GetCityMinuteOfDay()
{
	if (!IsCityClockOn()) return -1;

	/* Road vehicles ask on every step; work it out once per tick. */
	static TimerGameEconomy::Date cached_date{-1};
	static uint16_t cached_fract = 0;
	static uint8_t cached_setting = 0;
	static int cached_minute = 0;
	if (TimerGameEconomy::date != cached_date || TimerGameEconomy::date_fract != cached_fract || _settings_game.economy.rush_hours != cached_setting) {
		cached_date = TimerGameEconomy::date;
		cached_fract = TimerGameEconomy::date_fract;
		cached_setting = _settings_game.economy.rush_hours;
		cached_minute = GetCityMinuteAt(cached_date, cached_fract);
	}
	return cached_minute;
}

/**
 * The part of the city day at a minute.
 * @param minute The minute, 0 to 1439.
 * @return The part.
 */
static CityPeriod GetCityPeriodOfMinute(int minute)
{
	const int hour = minute / 60;
	if (hour < 6) return CityPeriod::Night;
	if (hour < 9) return CityPeriod::MorningPeak;
	if (hour < 16) return CityPeriod::Day;
	if (hour < 19) return CityPeriod::EveningPeak;
	return CityPeriod::Evening;
}

/**
 * The part of the city day now.
 * @return The part; #CityPeriod::Day when the city clock doesn't run.
 */
CityPeriod GetCityPeriod()
{
	if (!IsCityClockOn()) return CityPeriod::Day;
	return GetCityPeriodOfMinute(GetCityMinuteOfDay());
}

/**
 * Is a part of the city day a rush hour?
 * @param period The part.
 * @return True for the morning and evening peaks.
 */
static bool IsPeak(CityPeriod period)
{
	return period == CityPeriod::MorningPeak || period == CityPeriod::EveningPeak;
}

/**
 * Is it rush hour now?
 * @return True in the morning and evening peaks of the city clock.
 */
bool IsRushHour()
{
	return IsCityClockOn() && IsPeak(GetCityPeriod());
}

/**
 * How many passengers houses send to stations now, in percent of the average day.
 * @return The percentage: night 30, morning peak 200, day 100, evening peak 180, evening 60.
 */
uint GetCityPassengerPercent()
{
	static constexpr std::array<uint, to_underlying(CityPeriod::End)> PERCENT = {30, 200, 100, 180, 60};
	if (!IsCityClockOn()) return 100;
	return PERCENT[to_underlying(GetCityPeriod())];
}

/**
 * Town traffic at this time of the city day: a level more in the peaks, one less in the evening and two less at night.
 * Roads without traffic stay without.
 * @param level The congestion level of the road over a whole day.
 * @return The level now.
 */
uint8_t AdjustCongestionForCityTime(uint8_t level)
{
	static constexpr std::array<int, to_underlying(CityPeriod::End)> CHANGE = {-2, 1, 0, 1, -1};
	if (level == 0 || !IsCityClockOn()) return level;
	return static_cast<uint8_t>(std::clamp<int>(level + CHANGE[to_underlying(GetCityPeriod())], 0, ROAD_CONGESTION_MAX));
}

/**
 * Scale the passengers a house sends to its stations by the time of the city day.
 * @param cargo The cargo it makes.
 * @param[in,out] amount The amount.
 */
void ScalePassengersForCityTime(CargoType cargo, uint &amount)
{
	if (amount == 0 || !IsCityClockOn() || !IsCargoInClass(cargo, CargoClass::Passengers)) return;
	const uint scaled = amount * GetCityPassengerPercent();
	amount = scaled / 100;
	if (RandomRange(100) < scaled % 100) amount++;
}

/* Off-peak parking. */

/**
 * Did the city clock send a vehicle to a depot to wait for the next peak?
 * @param v The vehicle.
 * @return True if it is on the list of parked vehicles.
 */
static bool IsParkedForPeak(const Vehicle *v)
{
	return !_offpeak_parked.empty() && std::ranges::find(_offpeak_parked, v->index) != _offpeak_parked.end();
}

/**
 * Is a vehicle on its way to a depot to stop there?
 * @param v The vehicle.
 * @return True if it is.
 */
static bool IsGoingToDepotToStop(const Vehicle *v)
{
	return v->current_order.IsType(OT_GOTO_DEPOT) && v->current_order.GetDepotActionType().Test(OrderDepotActionFlag::Halt);
}

/**
 * Does a vehicle wait for the next rush hour: parked by the city clock, and still in or on its way to the depot
 * (not started again by its owner)?
 * @param v The vehicle.
 * @return True if it does.
 */
bool IsWaitingForPeak(const Vehicle *v)
{
	return IsParkedForPeak(v) && (v->IsStoppedInDepot() || IsGoingToDepotToStop(v));
}

/**
 * Let a vehicle that waited for the peak set out again, or turn back if it is still on its way to the depot.
 * @param v The vehicle.
 */
static void UnparkVehicle(Vehicle *v)
{
	std::erase(_offpeak_parked, v->index);
	if (!Company::IsValidID(v->owner)) return;

	Backup<CompanyID> cur_company(_current_company, v->owner);
	if (v->IsStoppedInDepot()) {
		/* The time it waited doesn't count as travel time towards its next stop, or the line's round trip would seem far longer. */
		if (Command<Commands::StartStopVehicle>::Do(DoCommandFlag::Execute, v->index, true).Succeeded()) v->current_order_time = 0;
	} else if (IsGoingToDepotToStop(v)) {
		/* Sending it again calls the trip to the depot off. */
		Command<Commands::SendVehicleToDepot>::Do(DoCommandFlag::Execute, v->index, DepotCommandFlags{}, VehicleListIdentifier{});
	}
	cur_company.Restore();
}

/** A peak started: all vehicles that waited for it set out again. */
static void UnparkAllVehicles()
{
	const std::vector<VehicleID> parked = std::move(_offpeak_parked);
	_offpeak_parked.clear();
	for (VehicleID id : parked) {
		Vehicle *v = Vehicle::GetIfValid(id);
		if (v != nullptr && v->IsPrimaryVehicle()) UnparkVehicle(v);
	}
}

/** City minutes from the end of the evening rush hour to the start of the morning one: the longest stretch between them. */
static constexpr int LONGEST_OFFPEAK_MINUTES = (24 - 19 + 6) * 60;

/**
 * Ticks until the next rush hour starts.
 * @return The ticks.
 */
static int64_t GetTicksUntilNextPeak()
{
	const int minute = GetCityMinuteOfDay();
	const int next = minute < 6 * 60 ? 6 * 60 : (minute < 16 * 60 ? 16 * 60 : (24 + 6) * 60);
	return (next - minute) * GetCityDayTicks(TimerGameEconomy::date) / 1440;
}

/**
 * Is it worth parking vehicles of a line for a while? Only when they can wait at least about a round trip:
 * the stretch has to last at least two of the line's round trips, as getting to the depot and back takes about one.
 * @param list The line's order list.
 * @param ticks How long the vehicles would wait.
 * @return True if it is worth it, or the round trip isn't known yet.
 */
static bool IsParkingWorthwhile(const OrderList *list, int64_t ticks)
{
	const TimerGameTick::Ticks round_trip = list->GetTotalDuration();
	return round_trip <= 0 || ticks >= 2 * static_cast<int64_t>(round_trip);
}

/**
 * Can a line park vehicles between the rush hours at all: is the longest stretch between them long enough for its round trip?
 * @param list The line's order list.
 * @return True if it can (or the city clock is off).
 */
bool CanLineParkBetweenPeaks(const OrderList *list)
{
	if (!IsCityClockOn()) return true;
	return IsParkingWorthwhile(list, LONGEST_OFFPEAK_MINUTES * GetCityDayTicks(TimerGameEconomy::date) / 1440);
}

/**
 * Between the peaks of the city clock, bring a line to the vehicles it keeps running then: send the others,
 * emptiest first, to a depot to wait for the next peak, or let waiting ones set out again when it keeps more.
 * @param list The line's order list.
 */
void ApplyOffPeakVehicles(OrderList *list)
{
	if (!IsCityClockOn() || IsRushHour()) return;
	const uint8_t keep = list->line.offpeak_vehicles;

	std::vector<std::pair<uint, Vehicle *>> running; // cargo on board, vehicle
	std::vector<Vehicle *> parked;
	for (Vehicle *v = list->GetFirstSharedVehicle(); v != nullptr; v = v->NextShared()) {
		if (IsWaitingForPeak(v)) {
			parked.push_back(v);
			continue;
		}
		/* Its owner started it again: it runs the line like the others. */
		if (IsParkedForPeak(v)) std::erase(_offpeak_parked, v->index);
		/* Vehicles stopped or sent to a depot by their owner, or crashed, don't run the line. */
		if (v->vehstatus.Any({VehState::Stopped, VehState::Crashed}) || v->IsInDepot() || IsGoingToDepotToStop(v)) continue;
		uint load = 0;
		for (const Vehicle *u = v; u != nullptr; u = u->Next()) load += u->cargo.TotalCount();
		running.emplace_back(load, v);
	}

	if (running.size() < keep) {
		/* It keeps more running now: the ones that wait set out again. */
		for (size_t i = 0; i < parked.size() && running.size() + i < keep; i++) UnparkVehicle(parked[i]);
		return;
	}
	if (running.size() == keep) return;
	/* Not when the next rush hour comes before they could wait there long. */
	if (!IsParkingWorthwhile(list, GetTicksUntilNextPeak())) return;

	std::sort(running.begin(), running.end(), [](const auto &a, const auto &b) { return a.first != b.first ? a.first < b.first : a.second->index < b.second->index; });
	for (size_t i = 0; i < running.size() - keep; i++) {
		Vehicle *v = running[i].second;
		if (!Company::IsValidID(v->owner)) continue;
		Backup<CompanyID> cur_company(_current_company, v->owner);
		if (Command<Commands::SendVehicleToDepot>::Do(DoCommandFlag::Execute, v->index, DepotCommandFlags{}, VehicleListIdentifier{}).Succeeded()) {
			_offpeak_parked.push_back(v->index);
		}
		cur_company.Restore();
	}
}

/**
 * How many vehicles of a line wait for the next rush hour.
 * @param list The line's order list.
 * @return The number of vehicles.
 */
uint CountOffPeakParked(const OrderList *list)
{
	uint n = 0;
	for (VehicleID id : _offpeak_parked) {
		const Vehicle *v = Vehicle::GetIfValid(id);
		if (v != nullptr && v->orders == list && IsWaitingForPeak(v)) n++;
	}
	return n;
}

/**
 * A vehicle is gone: it no longer waits for the peak.
 * @param v The vehicle.
 */
void ForgetOffPeakVehicle(const Vehicle *v)
{
	if (!_offpeak_parked.empty()) std::erase(_offpeak_parked, v->index);
}

/**
 * A vehicle that waits for the next peak was renewed or replaced: the new one waits instead.
 * @param old_vehicle The vehicle that was replaced.
 * @param new_vehicle The vehicle that replaced it.
 */
void ReplaceOffPeakVehicle(const Vehicle *old_vehicle, const Vehicle *new_vehicle)
{
	for (VehicleID &id : _offpeak_parked) {
		if (id == old_vehicle->index) id = new_vehicle->index;
	}
}

/** Each day: when a peak starts, all vehicles set out; when it ends, lines park the vehicles they don't need. */
static const IntervalTimer<TimerGameEconomy> _city_clock_daily({TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [](auto)
{
	if (!IsCityClockOn()) {
		if (!_offpeak_parked.empty()) UnparkAllVehicles();
		return;
	}
	const bool peak = IsPeak(GetCityPeriodOfMinute(GetCityMinuteAt(TimerGameEconomy::date, 0)));
	const bool was_peak = IsPeak(GetCityPeriodOfMinute(GetCityMinuteAt(TimerGameEconomy::date - 1, 0)));
	if (peak == was_peak) return;
	if (peak) {
		UnparkAllVehicles();
	} else {
		for (OrderList *list : OrderList::Iterate()) {
			if (list->line.offpeak_vehicles != UINT8_MAX) ApplyOffPeakVehicles(list);
		}
	}
	InvalidateWindowClassesData(WindowClass::Line);
});

/* On-demand lines. */

/**
 * An on-demand vehicle that just left a stop picks the next one of its line: where the most passengers
 * it can take wait (less what the line's other vehicles on their way there will take), where the
 * passengers on board want to get off, or that waited longest for a pickup; nearer ones first.
 * @param v The vehicle (front), just leaving a stop.
 * @return True if it picked a stop; its order index points at it then.
 */
bool ChooseOnDemandStop(Vehicle *v)
{
	if (v->orders == nullptr || !v->orders->line.on_demand || !v->current_order.IsType(OT_LEAVESTATION)) return false;

	/* What it carries; at the next stop the passengers on board make room, so it has its whole capacity. */
	uint capacity = 0;
	CargoTypes cargoes{};
	for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
		if (u->cargo_cap == 0) continue;
		capacity += u->cargo_cap;
		cargoes.Set(u->cargo_type);
	}

	auto orders = v->Orders();
	VehicleOrderID best = INVALID_VEH_ORDER_ID;
	uint64_t best_score = 0;
	for (VehicleOrderID i = 0; i < orders.size(); i++) {
		const Order &order = orders[i];
		if (!order.IsType(OT_GOTO_STATION)) continue;
		const StationID id = order.GetDestination().ToStationID();
		if (id == v->last_station_visited) continue;
		const Station *st = Station::GetIfValid(id);
		if (st == nullptr) continue;

		/* Passengers waiting there, and how long since a pickup. */
		uint waiting = 0, since = 0;
		for (CargoType cargo : cargoes) {
			const GoodsEntry &ge = st->goods[cargo];
			if (ge.HasData()) waiting += ge.GetData().cargo.AvailableCount();
			since = std::max<uint>(since, ge.time_since_pickup);
		}

		/* Passengers on board who want to get off there (with cargo distribution). */
		uint alighting = 0;
		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			for (const CargoPacket *cp : *u->cargo.Packets()) {
				if (cp->GetNextHop() == id) alighting += cp->Count();
			}
		}

		/* Room on the line's other vehicles that are there or on their way. */
		uint incoming = 0;
		bool served = false;
		for (const Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
			if (u == v) continue;
			const bool going = u->current_order.IsType(OT_GOTO_STATION) && u->current_order.GetDestination() == id;
			const bool loading = u->current_order.IsType(OT_LOADING) && u->last_station_visited == id;
			if (!going && !loading) continue;
			served = true;
			for (const Vehicle *w = u; w != nullptr; w = w->Next()) incoming += w->cargo_cap;
		}

		const uint pickup = std::min(waiting > incoming ? waiting - incoming : 0, capacity);
		const uint64_t score = (static_cast<uint64_t>(pickup) + alighting + (served ? 0 : since * 4) + 1) * 1024 / (DistanceManhattan(v->tile, st->xy) + 4);
		if (best == INVALID_VEH_ORDER_ID || score > best_score) {
			best = i;
			best_score = score;
		}
	}
	if (best == INVALID_VEH_ORDER_ID) return false;

	v->cur_implicit_order_index = best;
	v->cur_real_order_index = best;
	/* Stops it makes on the way don't become implicit orders. */
	if (v->IsGroundVehicle()) v->GetGroundVehicleFlags().Set(GroundVehicleFlag::SuppressImplicitOrders);
	InvalidateVehicleOrder(v, 0);
	return true;
}

/**
 * Where an on-demand vehicle may go next, for loading: any other stop of its line.
 * @param v The vehicle.
 * @param[out] next_station The stations.
 */
void GetOnDemandNextStations(const Vehicle *v, std::vector<StationID> &next_station)
{
	for (const Order &order : v->Orders()) {
		if (!order.IsType(OT_GOTO_STATION)) continue;
		const StationID id = order.GetDestination().ToStationID();
		if (id == v->last_station_visited || std::ranges::find(next_station, id) != next_station.end()) continue;
		next_station.push_back(id);
	}
}
