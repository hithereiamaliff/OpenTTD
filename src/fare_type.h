/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fare_type.h Types for passenger fares. */

#ifndef FARE_TYPE_H
#define FARE_TYPE_H

#include "economy_type.h"

/** How a fare is worked out. */
enum class FareScheme : uint8_t {
	Classic, ///< OpenTTD's: by distance, less the longer the trip takes.
	Distance, ///< A boarding charge plus a rate per tile, however long the trip takes.
	Flat, ///< The same for every journey.
	Zones, ///< Per zone: one within a town, two to another town, one more per 40 tiles.
	Free, ///< No fare.
	End, ///< End marker.
	Inherit = 0xFF, ///< Lines: the company's fare for the transport type.
};

/** Transport types that each have their own fares. */
enum class FareMode : uint8_t {
	Bus, ///< Buses (road vehicles that aren't trams).
	Tram, ///< Trams.
	Rail, ///< Trains.
	Water, ///< Ships.
	Air, ///< Aircraft.
	End, ///< End marker.
};

/** What students, seniors and disabled riders (a fifth of all riders) pay. */
enum class FareConcession : uint8_t {
	None, ///< The full fare.
	Half, ///< Half the fare.
	Free, ///< Nothing.
	End, ///< End marker.
};

/** What of a company's fares to change, see CmdSetCompanyFare. */
enum class FareSetting : uint8_t {
	Scheme, ///< How fares are worked out.
	Price, ///< Price level.
	Concession, ///< Concession fares.
	Pass, ///< Monthly pass price.
	End, ///< End marker.
};

/** Fares of one transport type of a company. */
struct FarePolicy {
	FareScheme scheme = FareScheme::Classic; ///< How fares are worked out.
	uint16_t price = 100; ///< Price, in percent of the standard fare.
	FareConcession concession = FareConcession::None; ///< What concession riders pay.
	uint8_t pass = 0; ///< Price of a monthly pass in standard single fares, or 0 if there is none.
};

/** Monthly pass figures of one transport type for the last month. */
struct FarePassStats {
	uint32_t riders = 0; ///< Riders of lines that charge fares.
	uint32_t holders = 0; ///< Monthly passes sold.
	Money income = 0; ///< Income from passes.
};

static constexpr uint16_t FARE_PRICE_MIN = 25; ///< Lowest price level, in percent.
static constexpr uint16_t FARE_PRICE_MAX = 300; ///< Highest price level, in percent.
static constexpr uint8_t FARE_PASS_MIN = 10; ///< Cheapest monthly pass, in single fares.
static constexpr uint8_t FARE_PASS_MAX = 40; ///< Dearest monthly pass, in single fares.

#endif /* FARE_TYPE_H */
