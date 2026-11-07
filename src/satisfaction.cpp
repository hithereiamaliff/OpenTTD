/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file satisfaction.cpp Passenger satisfaction.
 *
 * Each month every passenger line gets a satisfaction score from 0 to 100, out
 * of six parts:
 *  - waiting (25%): how long people wait at its first stop, against how long
 *    the trip takes;
 *  - crowding (20%): how full its vehicles leave stops;
 *  - punctuality (20%): departures on time with a timetable, otherwise how
 *    evenly spaced they are;
 *  - fares (15%): what they pay against the standard fare;
 *  - vehicle condition (10%): how reliable the vehicles are;
 *  - comfort (10%): air-conditioned modern buses, quieter electric ones,
 *    newer vehicles.
 * A station gets the average of the lines that stop there: passengers rate it
 * up to 5% better or 8% worse for it. A town gets the average of its stations:
 * its happiness goes up or down by up to 10 points, and companies gain or lose
 * standing with it.
 */

#include "stdafx.h"
#include "satisfaction.h"
#include "cargotype.h"
#include "city_bus.h"
#include "company_base.h"
#include "company_func.h"
#include "core/backup_type.hpp"
#include "engine_base.h"
#include "fare.h"
#include "line.h"
#include "order_base.h"
#include "settings_type.h"
#include "station_base.h"
#include "town.h"
#include "vehicle_base.h"
#include "timer/timer_game_tick.h"

#include "safeguards.h"

/** Weights of the parts of satisfaction, in percent, in #SatisfactionPart order. */
static constexpr std::array<uint, to_underlying(SatisfactionPart::End)> SATISFACTION_WEIGHTS = {25, 20, 20, 15, 10, 10};

/**
 * Does a line carry passengers?
 * @param list The line's order list.
 * @return True if one of its vehicles has room for passengers.
 */
bool IsPassengerLine(const OrderList *list)
{
	const Vehicle *v = list->GetFirstSharedVehicle();
	if (v == nullptr) return false;
	for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
		if (u->cargo_cap > 0 && IsCargoInClass(u->cargo_type, CargoClass::Passengers)) return true;
	}
	return false;
}

/**
 * Clamp a score to 0 to 100.
 * @param score The score.
 * @return The clamped score.
 */
static uint8_t Score(int score)
{
	return static_cast<uint8_t>(std::clamp(score, 0, 100));
}

/**
 * How satisfied passengers are with the wait: fully up to a tenth of a round
 * trip (half the one-way trip, at least two days), a quarter less for each
 * such stretch beyond.
 * @param list The line's order list.
 * @return The score.
 */
static uint8_t GetWaitingScore(const OrderList *list)
{
	const uint32_t wait = GetLineAverageWait(list->line);
	if (wait == 0) return 70;
	const int64_t tolerance = std::max<int64_t>(2 * Ticks::DAY_TICKS, list->GetTotalDuration() / 10);
	return Score(100 - static_cast<int>((static_cast<int64_t>(wait) - tolerance) * 25 / tolerance));
}

/**
 * How satisfied passengers are with the crowding: fully up to 60% full on
 * leaving a stop on average, then two points less per percent; 10 points less
 * when a vehicle left full.
 * @param stats Last month's statistics of the line.
 * @return The score.
 */
static uint8_t GetCrowdingScore(const LineMonthStats &stats)
{
	if (stats.load_count == 0) return 70;
	const int load = static_cast<int>(stats.load_sum / stats.load_count);
	return Score(100 - std::max(0, load - 60) * 2 - (stats.max_load >= 100 ? 10 : 0));
}

/**
 * How riders like the platforms of a line's railway stations: fine up to 70%
 * of their places, a point less for each percent above (none at 170%).
 * @param list The line.
 * @return The score, or 100 if it doesn't stop at railway platforms.
 */
static uint8_t GetPlatformScore(const OrderList *list)
{
	std::vector<StationID> seen;
	uint sum = 0, stations = 0;
	for (const Order &order : list->GetOrders()) {
		if (!order.IsType(OT_GOTO_STATION)) continue;
		const StationID id = order.GetDestination().ToStationID();
		if (std::ranges::find(seen, id) != seen.end()) continue;
		seen.push_back(id);
		const Station *st = Station::GetIfValid(id);
		if (st == nullptr || st->train_station.IsEmpty()) continue;
		sum += st->crowding;
		stations++;
	}
	if (stations == 0) return 100;
	return Score(100 - std::max(0, static_cast<int>(sum / stations) - 70));
}

/**
 * How satisfied passengers are with punctuality: the share of departures on
 * time with a timetable; otherwise how evenly spaced departures are, 30 points
 * less for each extra wait as long as the wait with even spacing.
 * @param list The line's order list.
 * @return The score.
 */
static uint8_t GetPunctualityScore(const OrderList *list)
{
	const LineMonthStats &stats = list->line.last_month;
	if (stats.timed > 0) return Score(static_cast<int>(stats.on_time * 100 / stats.timed));
	const uint32_t wait = GetLineAverageWait(list->line), even = GetLineAverageGap(list->line) / 2;
	if (wait == 0 || even == 0) return 70;
	return Score(100 - static_cast<int>(static_cast<int64_t>(wait - std::min(wait, even)) * 30 / even));
}

/**
 * How satisfied passengers are with the fare: fully when free or at half the
 * standard fare, 75 at the standard fare, a point less for each 2% dearer.
 * @param front A vehicle of the line.
 * @return The score.
 */
static uint8_t GetFareScore(const Vehicle *front)
{
	const FarePolicy fare = GetVehicleFare(front);
	if (fare.scheme == FareScheme::Free) return 100;
	return Score(100 - (static_cast<int>(fare.price) - 50) / 2);
}

/**
 * How comfortable a vehicle is: the modern city buses have air-conditioning,
 * electric ones are quieter; older road vehicles have none (worse in the
 * tropics); other vehicles are average. Two points less for each year over five.
 * @param v The vehicle.
 * @return The score.
 */
static int GetVehicleComfort(const Vehicle *v)
{
	int comfort;
	if (v->type == VehicleType::Road) {
		const Engine *e = v->GetEngine();
		if (GetCityBusSpec(e) != nullptr) {
			comfort = GetCityBusSpec(e)->battery_kwh > 0 ? 95 : 85;
		} else {
			comfort = _settings_game.game_creation.landscape == LandscapeType::Tropic ? 35 : 55;
		}
	} else {
		comfort = 70;
	}
	const int years = v->age.base() / CalendarTime::DAYS_IN_YEAR;
	return comfort - std::min(30, std::max(0, years - 5) * 2);
}

/**
 * Work out how satisfied a line's passengers were last month.
 * @param list The line's order list.
 */
static void UpdateLineSatisfaction(OrderList *list)
{
	LineInfo &line = list->line;
	const Vehicle *first = list->GetFirstSharedVehicle();
	if (first == nullptr || !IsPassengerLine(list) || line.last_month.departures == 0) {
		line.satisfaction = SATISFACTION_UNKNOWN;
		return;
	}

	uint64_t reliability = 0, comfort = 0, vehicles = 0;
	for (const Vehicle *v = first; v != nullptr; v = v->NextShared()) {
		reliability += v->reliability;
		comfort += std::max(0, GetVehicleComfort(v));
		vehicles++;
	}

	auto &parts = line.satisfaction_parts;
	parts[to_underlying(SatisfactionPart::Waiting)] = GetWaitingScore(list);
	/* Crowded vehicles, or crowded railway platforms, whichever is worse. */
	parts[to_underlying(SatisfactionPart::Crowding)] = std::min(GetCrowdingScore(line.last_month), GetPlatformScore(list));
	parts[to_underlying(SatisfactionPart::Punctuality)] = GetPunctualityScore(list);
	parts[to_underlying(SatisfactionPart::Fare)] = GetFareScore(first);
	parts[to_underlying(SatisfactionPart::Condition)] = Score(static_cast<int>(reliability * 100 / (vehicles * 0xFFFF)));
	parts[to_underlying(SatisfactionPart::Comfort)] = Score(static_cast<int>(comfort / vehicles));

	uint sum = 0;
	for (uint i = 0; i < parts.size(); i++) sum += parts[i] * SATISFACTION_WEIGHTS[i];
	line.satisfaction = static_cast<uint8_t>((sum + 50) / 100);
}

/**
 * Rating bonus for passengers at a station from the satisfaction with the lines that stop there.
 * @param st The station.
 * @return The bonus, on the rating's scale of 255: up to +13 (5%), down to -20 (8%).
 */
int GetStationSatisfactionRatingBonus(const Station *st)
{
	if (st->satisfaction == UINT8_MAX) return 0;
	return (static_cast<int>(st->satisfaction) - 60) / 3;
}

/**
 * Happiness a town gains or loses from the satisfaction with its stations.
 * @param t The town.
 * @return The points of happiness, -10 to +10.
 */
int GetTownSatisfactionHappiness(const Town *t)
{
	if (t->transit.satisfaction == UINT8_MAX) return 0;
	return (static_cast<int>(t->transit.satisfaction) - 50) / 5;
}

/** Each month, after the lines' statistics moved on: the satisfaction of lines, stations and towns. */
void SatisfactionMonthlyLoop()
{
	for (OrderList *list : OrderList::Iterate()) UpdateLineSatisfaction(list);

	/* Stations: the average of the passenger lines that stop there. */
	std::vector<std::pair<uint32_t, uint32_t>> stations(Station::GetPoolSize()); // sum, lines
	for (const OrderList *list : OrderList::Iterate()) {
		if (list->line.satisfaction == SATISFACTION_UNKNOWN) continue;
		std::vector<StationID> seen;
		for (const Order &order : list->GetOrders()) {
			if (!order.IsType(OT_GOTO_STATION)) continue;
			StationID id = order.GetDestination().ToStationID();
			if (!Station::IsValidID(id) || std::ranges::find(seen, id) != seen.end()) continue;
			seen.push_back(id);
			stations[id.base()].first += list->line.satisfaction;
			stations[id.base()].second++;
		}
	}

	/* Towns: the average of their stations, overall and per company. */
	struct TownSum {
		uint32_t sum = 0, count = 0;
		std::array<uint32_t, MAX_COMPANIES> company_sum{}, company_count{};
	};
	std::vector<TownSum> towns(Town::GetPoolSize());
	for (Station *st : Station::Iterate()) {
		const auto &[sum, lines] = stations[st->index.base()];
		st->satisfaction = lines == 0 ? UINT8_MAX : static_cast<uint8_t>(sum / lines);
		if (st->satisfaction == UINT8_MAX || st->town == nullptr) continue;
		TownSum &ts = towns[st->town->index.base()];
		ts.sum += st->satisfaction;
		ts.count++;
		if (Company::IsValidID(st->owner)) {
			ts.company_sum[st->owner.base()] += st->satisfaction;
			ts.company_count[st->owner.base()]++;
		}
	}

	/* Satisfied riders speak well of the company to their town; dissatisfied ones don't. */
	Backup<CompanyID> cur_company(_current_company);
	for (Town *t : Town::Iterate()) {
		const TownSum &ts = towns[t->index.base()];
		t->transit.satisfaction = ts.count == 0 ? UINT8_MAX : static_cast<uint8_t>(ts.sum / ts.count);
		for (const Company *c : Company::Iterate()) {
			const uint32_t count = ts.company_count[c->index.base()];
			if (count == 0) continue;
			const int change = (static_cast<int>(ts.company_sum[c->index.base()] / count) - 50) / 5;
			if (change == 0) continue;
			cur_company.Change(c->index);
			ChangeTownRating(t, change, change > 0 ? RATING_VERYGOOD : RATING_POOR, DoCommandFlag::Execute);
		}
	}
	cur_company.Restore();
}
