/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fare.cpp Passenger fares. */

#include "stdafx.h"
#include "fare.h"
#include "fare_cmd.h"
#include "cargotype.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "economy_func.h"
#include "order_base.h"
#include "road_func.h"
#include "roadveh.h"
#include "station_base.h"
#include "transit_authority.h"
#include "vehicle_base.h"
#include "window_func.h"

#include "table/strings.h"

#include "safeguards.h"

/** A flat fare costs what a standard fare for this many tiles does. */
static constexpr uint FLAT_FARE_TILES = 20;
/** A zone costs what a standard fare for this many tiles does. */
static constexpr uint ZONE_FARE_TILES = 16;
/** Every this many tiles a journey goes into one more zone. */
static constexpr uint ZONE_TILES = 40;
/** Distance fares start with what this many tiles cost. */
static constexpr uint BOARDING_TILES = 3;
/** Distance fares charge this share (in percent) of the standard fare per tile. */
static constexpr uint DISTANCE_RATE = 90;
/** Share of riders (in percent) that are students, seniors or disabled. */
static constexpr uint CONCESSION_SHARE = 20;
/** Trips a pass holder makes in a month. */
static constexpr uint TRIPS_PER_PASS = 40;

/**
 * The transport type of a vehicle, for its fares.
 * @param v The vehicle.
 * @return The transport type.
 */
FareMode GetFareMode(const Vehicle *v)
{
	switch (v->type) {
		case VehicleType::Train: return FareMode::Rail;
		case VehicleType::Road: return RoadTypeIsTram(RoadVehicle::From(v)->roadtype) ? FareMode::Tram : FareMode::Bus;
		case VehicleType::Ship: return FareMode::Water;
		case VehicleType::Aircraft: return FareMode::Air;
		default: return FareMode::Bus;
	}
}

/**
 * The fare a vehicle charges: its company's for its transport type, unless its line has its own.
 * @param front The vehicle (front).
 * @return The fare.
 */
FarePolicy GetVehicleFare(const Vehicle *front)
{
	const Company *c = Company::GetIfValid(front->owner);
	FarePolicy fare = (c == nullptr) ? FarePolicy{} : c->fares[to_underlying(GetFareMode(front))];
	if (front->orders != nullptr) {
		/* Lines under contract charge the town's fare: a distance fare, or none on free lines. */
		if (const TransitContract *contract = GetLineContract(front->orders); contract != nullptr) {
			return contract->type == TransitContractType::FreeLine ? FarePolicy{FareScheme::Free, 100, FareConcession::None, 0} : FarePolicy{FareScheme::Distance, 100, FareConcession::None, 0};
		}
		const LineInfo &line = front->orders->line;
		if (line.fare_scheme != FareScheme::Inherit) fare.scheme = line.fare_scheme;
		if (line.fare_price != 0) fare.price = line.fare_price;
	}
	return fare;
}

/**
 * How many riders buy a monthly pass: the cheaper the pass compared with
 * paying for each of a month's trips, the more.
 * @param pass Pass price in single fares, 0 if there is no pass.
 * @return Share of riders, in percent.
 */
uint GetPassUptake(uint8_t pass)
{
	if (pass == 0 || pass >= FARE_PASS_MAX) return 0;
	return (FARE_PASS_MAX - pass) * 3 / 2;
}

/**
 * Share of a fare that riders pay on average: pass holders pay nothing per
 * trip, concession riders half or nothing.
 * @param fare The fare.
 * @return The share, in percent.
 */
uint GetFarePayingShare(const FarePolicy &fare)
{
	uint discount = (fare.concession == FareConcession::Half) ? 50 : (fare.concession == FareConcession::Free ? 100 : 0);
	return (100 - GetPassUptake(fare.pass)) * (100 - CONCESSION_SHARE * discount / 100) / 100;
}

/**
 * The standard fare for a distance: OpenTTD's payment for a trip that takes no time.
 * @param count Number of passengers.
 * @param tiles Distance in tiles.
 * @param cargo The passenger cargo.
 * @return The fare.
 */
static Money GetStandardFare(uint count, uint tiles, CargoType cargo)
{
	return GetTransportedGoodsIncome(count, tiles, 0, cargo);
}

/**
 * The full fare (before passes and concessions) for a journey.
 * @param fare The fare.
 * @param count Number of passengers.
 * @param distance Distance travelled, in tiles.
 * @param periods Time travelled, in cargo aging periods.
 * @param cargo The passenger cargo.
 * @param other_town Whether the journey went to another town.
 * @return The fare.
 */
Money GetFare(const FarePolicy &fare, uint count, uint distance, uint16_t periods, CargoType cargo, bool other_town)
{
	Money money;
	switch (fare.scheme) {
		case FareScheme::Distance:
			money = GetStandardFare(count, distance + BOARDING_TILES, cargo) * DISTANCE_RATE / 100;
			break;

		case FareScheme::Flat:
			money = GetStandardFare(count, FLAT_FARE_TILES, cargo);
			break;

		case FareScheme::Zones: {
			uint zones = (other_town ? 2 : 1) + distance / ZONE_TILES;
			money = GetStandardFare(count, ZONE_FARE_TILES, cargo) * zones;
			break;
		}

		case FareScheme::Free:
			return 0;

		default:
			money = GetTransportedGoodsIncome(count, distance, periods, cargo);
			break;
	}
	return money * fare.price / 100;
}

/**
 * The passenger cargo fares are shown for.
 * @return The cargo, or INVALID_CARGO if the climate has none.
 */
static CargoType GetFarePassengerCargo()
{
	const auto &cargoes = CargoSpec::town_production_cargoes[TownProductionEffect::Passengers];
	return cargoes.empty() ? INVALID_CARGO : cargoes.front()->Index();
}

/**
 * What ten passengers pay for a journey, for showing fares.
 * @param fare The fare.
 * @param distance Distance in tiles.
 * @param other_town Whether the journey goes to another town.
 * @return The fare for ten passengers.
 */
Money GetFareExample(const FarePolicy &fare, uint distance, bool other_town)
{
	CargoType cargo = GetFarePassengerCargo();
	if (!IsValidCargoType(cargo)) return 0;
	return GetFare(fare, 10, distance, 0, cargo, other_town);
}

/**
 * What passengers pay when they get off a vehicle (or what a transfer is
 * worth): the fare of the vehicle for the whole journey from where they first
 * boarded, less what pass holders and concession riders don't pay.
 * @param front The vehicle (front).
 * @param count Number of passengers.
 * @param distance Distance from where they first boarded, in tiles.
 * @param periods Time since they first boarded, in cargo aging periods.
 * @param cargo The passenger cargo.
 * @param first_station Where they first boarded.
 * @param at Where they get off.
 * @return The income.
 */
Money GetPassengerIncome(const Vehicle *front, uint count, uint distance, uint16_t periods, CargoType cargo, StationID first_station, const Station *at)
{
	FarePolicy fare = GetVehicleFare(front);
	const Station *first = Station::GetIfValid(first_station);
	bool other_town = first != nullptr && at != nullptr && first->town != at->town;
	return GetFare(fare, count, distance, periods, cargo, other_town) * GetFarePayingShare(fare) / 100;
}

/**
 * How much what riders paid raises or lowers a station's passenger rating.
 * Riders at the classic fare change nothing; cheaper fares raise the rating
 * by up to 60 points (of 255), dearer ones lower it by 4 points for every 3%
 * above it, down to -150. With a usual rating that makes about 120% of the
 * classic fare the most profitable price.
 * @param level What riders paid, in percent of the classic fare.
 * @return Rating points.
 */
int GetFareRatingBonus(uint level)
{
	return Clamp((100 - static_cast<int>(std::min<uint>(level, 1000))) * 4 / 3, -150, 60);
}

/**
 * How much the fares riders paid for journeys from a station last month raise or lower its passenger rating.
 * @param st The station.
 * @return Rating points.
 */
int GetStationFareRatingBonus(const Station *st)
{
	return GetFareRatingBonus(st->fare_level);
}

/**
 * Passengers that boarded at a station paid for their journey: remember it
 * against what the classic fare would have been, for the station's rating.
 * @param first_station Where they boarded.
 * @param paid What they paid.
 * @param standard What OpenTTD's classic fare would have been.
 */
void OnFarePaid(StationID first_station, Money paid, Money standard)
{
	Station *st = Station::GetIfValid(first_station);
	if (st == nullptr) return;
	st->fare_paid += paid;
	st->fare_standard += standard;
}

/**
 * Which income a transport type's fares, passes and contract fees go to.
 * @param mode The transport type.
 * @return The expenses type.
 */
ExpensesType GetFareRevenueType(FareMode mode)
{
	switch (mode) {
		case FareMode::Rail: return ExpensesType::TrainRevenue;
		case FareMode::Water: return ExpensesType::ShipRevenue;
		case FareMode::Air: return ExpensesType::AircraftRevenue;
		default: return ExpensesType::RoadVehRevenue;
	}
}

/**
 * End of a month: sell monthly passes to the riders of the month, and work
 * out what riders paid at each station against the classic fare. Called
 * before the line statistics move on to the next month.
 */
void FaresMonthlyLoop()
{
	/* Riders per company and transport type, on lines that charge fares. */
	for (Company *c : Company::Iterate()) c->pass_stats = {};
	for (const OrderList *list : OrderList::Iterate()) {
		const Vehicle *v = list->GetFirstSharedVehicle();
		if (v == nullptr) continue;
		Company *c = Company::GetIfValid(v->owner);
		if (c == nullptr || GetVehicleFare(v).scheme == FareScheme::Free) continue;
		c->pass_stats[to_underlying(GetFareMode(v))].riders += list->line.this_month.riders;
	}

	CargoType cargo = GetFarePassengerCargo();
	for (Company *c : Company::Iterate()) {
		for (uint mode = 0; mode < to_underlying(FareMode::End); mode++) {
			const FarePolicy &fare = c->fares[mode];
			FarePassStats &stats = c->pass_stats[mode];
			uint uptake = GetPassUptake(fare.pass);
			if (uptake == 0 || !IsValidCargoType(cargo)) continue;
			stats.holders = static_cast<uint32_t>((static_cast<uint64_t>(stats.riders) * uptake + 50 * TRIPS_PER_PASS) / (100 * TRIPS_PER_PASS));
			stats.income = GetFare(FarePolicy{FareScheme::Flat, fare.price, FareConcession::None, 0}, stats.holders, 0, 0, cargo, false) * fare.pass;
			if (stats.income > 0) SubtractMoneyFromCompany(c->index, CommandCost(GetFareRevenueType(static_cast<FareMode>(mode)), -stats.income));
		}
	}

	for (Station *st : Station::Iterate()) {
		if (st->fare_standard > 0) st->fare_level = static_cast<uint16_t>(std::min<int64_t>(st->fare_paid * 100 / st->fare_standard, 1000));
		st->fare_paid = 0;
		st->fare_standard = 0;
	}

	InvalidateWindowClassesData(WindowClass::Fares);
}

/**
 * Change one of the company's fares.
 * @param flags Operation to perform.
 * @param mode The transport type.
 * @param setting What to change.
 * @param value The new scheme, price, concession or pass price.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetCompanyFare(DoCommandFlags flags, FareMode mode, FareSetting setting, uint16_t value)
{
	Company *c = Company::GetIfValid(_current_company);
	if (c == nullptr || mode >= FareMode::End) return CMD_ERROR;

	switch (setting) {
		case FareSetting::Scheme:
			if (value >= to_underlying(FareScheme::End)) return CMD_ERROR;
			break;
		case FareSetting::Price:
			if (value < FARE_PRICE_MIN || value > FARE_PRICE_MAX) return CMD_ERROR;
			break;
		case FareSetting::Concession:
			if (value >= to_underlying(FareConcession::End)) return CMD_ERROR;
			break;
		case FareSetting::Pass:
			if (value != 0 && (value < FARE_PASS_MIN || value > FARE_PASS_MAX)) return CMD_ERROR;
			break;
		default:
			return CMD_ERROR;
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		FarePolicy &fare = c->fares[to_underlying(mode)];
		switch (setting) {
			case FareSetting::Scheme: fare.scheme = static_cast<FareScheme>(value); break;
			case FareSetting::Price: fare.price = value; break;
			case FareSetting::Concession: fare.concession = static_cast<FareConcession>(value); break;
			case FareSetting::Pass: fare.pass = static_cast<uint8_t>(value); break;
			default: NOT_REACHED();
		}
		InvalidateWindowClassesData(WindowClass::Fares);
		InvalidateWindowClassesData(WindowClass::Line);
	}
	return CommandCost();
}
