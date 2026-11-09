/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file state_type.h Types for states and federal territories: their governments, elections and money. */

#ifndef STATE_TYPE_H
#define STATE_TYPE_H

#include "company_type.h"
#include "economy_type.h"
#include "town_type.h"

/** Index of a state (or federal territory) in the list of states. */
using StateID = uint8_t;
/** No state: the town or the land belongs to none. */
static constexpr StateID INVALID_STATE = UINT8_MAX;
/** Most states and federal territories a map can have. */
static constexpr uint MAX_STATES = 32;
/** Longest name a player may give a state, in characters. */
static constexpr uint MAX_LENGTH_STATE_NAME_CHARS = 32;

/** What a state is. */
enum class StateKind : uint8_t {
	State, ///< A state (negeri) with its own assembly and government.
	FederalTerritory, ///< A federal territory (wilayah persekutuan): one city run by its City Hall under the federal government.
	End, ///< End marker.
};

/** The platforms voters choose between: what a government does with its money. No real parties. */
enum class Platform : uint8_t {
	None, ///< No government elected yet: a caretaker, who spends nothing on a platform.
	TransitFirst, ///< Transit First: grants to the towns' transit budgets; towns readier to tame cars.
	RoadsAndGrowth, ///< Roads and Growth: towns grow faster, cheaper land, money to build in the towns that lag.
	HomesAndServices, ///< Homes and Services: grants to the councils' services and estates; more estates of flats.
	End, ///< End marker.
};

/** Number of platforms that stand in elections (all but None). */
static constexpr uint PLATFORM_COUNT = static_cast<uint>(Platform::End) - 1;

/** Seats won, per platform that stands. */
using PlatformSeats = std::array<uint16_t, PLATFORM_COUNT>;

/** A state or federal territory (saved in chunk STAT). */
struct State {
	TownID capital = TownID::Invalid(); ///< Its capital, whose name it takes; for a federal territory, its city.
	std::string name{}; ///< Name the players gave it; empty for its usual name.
	StateKind kind = StateKind::State; ///< State or federal territory.
	bool region = false; ///< On another land, across the sea or behind a land border: run like Sabah and Sarawak (own land laws and immigration, no reclamation in its waters).
	uint8_t colour = 0; ///< Its colour on the map (a #Colours).
	Platform government = Platform::None; ///< Who governs it; for a federal territory, unused (the federal government does).
	PlatformSeats seats{}; ///< Seats each platform won at its last election.
	int32_t elected = 0; ///< Year of its last election; 0 if none yet.
	int32_t next_election = 0; ///< Year of its next election.
	uint8_t service_score = 0; ///< How its voters rated the service they get at its last election, 0-100; the government is judged on the change.
	int32_t formed = 0; ///< Year it was formed.
	Money treasury = 0; ///< Money in its treasury.
	Money premiums_this_year = 0; ///< Land premiums companies paid this year.
	Money premiums_last_year = 0; ///< The same last year.
	Money levies_this_year = 0; ///< Taken from rich councils' reserves this year.
	Money levies_last_year = 0; ///< The same last year.
	Money grants_this_year = 0; ///< The federal grant this year.
	Money grants_last_year = 0; ///< The same last year.
	Money spent_this_year = 0; ///< Spent on its government's platform this year.
	Money spent_last_year = 0; ///< The same last year.

	/* Not saved: worked out from the towns. */
	uint16_t towns = 0; ///< Towns in it.
	uint32_t population = 0; ///< People in them.
};

/** The federation: whether the states were formed, and the federal government (saved in chunk SFED). */
struct Federation {
	bool formed = false; ///< The states were formed.
	int32_t formed_year = 0; ///< Year they were formed.
	Platform government = Platform::None; ///< The federal government, which also runs the federal territories.
	PlatformSeats seats{}; ///< Seats in parliament each platform won at the last general election.
	int32_t elected = 0; ///< Year of the last general election; 0 if none yet.
	int32_t next_election = 0; ///< Year of the next general election.
	uint8_t service_score = 0; ///< How voters rated the service they get, at the last general election.
	Money grants_this_year = 0; ///< Federal grants paid this year: to the states and the city halls.
	Money grants_last_year = 0; ///< The same last year.
	uint8_t plan = 0; ///< Number of the federal government's five-year plan; 0 before the first.
	int32_t plan_year = 0; ///< Year the plan began.
	Money programmes_this_year = 0; ///< Spent on the national programmes this year: transit grants, the pass and fuel subsidies, hospitals and universities.
	Money programmes_last_year = 0; ///< The same last year.
	Money projects_this_year = 0; ///< Paid to companies for the plan's projects this year.
	Money projects_last_year = 0; ///< The same last year.
};

/** The seats a town gave each platform in an election. */
struct ElectionTown {
	TownID town = TownID::Invalid(); ///< The town.
	PlatformSeats seats{}; ///< Seats each platform won there.
};

/** A state's assembly, elected with parliament at a general election. */
struct ElectionAssembly {
	StateID state = INVALID_STATE; ///< The state.
	Platform before = Platform::None; ///< Who governed it before.
	Platform after = Platform::None; ///< Who governs it now.
	PlatformSeats seats{}; ///< Seats each platform won.
};

/**
 * An election: a general election (parliament, with the assemblies of the states on the main land) or a region's.
 * The last general election and the last election of each region are kept (saved in chunk SELN).
 */
struct ElectionResult {
	StateID state = INVALID_STATE; ///< The region that voted, or #INVALID_STATE for a general election.
	int32_t year = 0; ///< Year of polling day.
	uint8_t month = 0; ///< Month of polling day, the first of the month.
	Platform before = Platform::None; ///< Who governed before.
	Platform after = Platform::None; ///< Who governs now.
	PlatformSeats seats{}; ///< Seats each platform won, in parliament or in the region's assembly.
	bool declared = false; ///< The results were announced, two days after polling day.
	std::vector<ElectionTown> towns{}; ///< The seats of each town, in the order the count declares them: the smallest town first.
	std::vector<ElectionAssembly> assemblies{}; ///< For a general election: the states' assemblies elected with parliament.
};

/** Kinds of project in the federal government's five-year plan. */
enum class FederalProjectType : uint8_t {
	MetroLine, ///< A railway line with four stations or more in a city, running often.
	BrtLine, ///< A bus line graded BRT with three stops or more in a town, running often.
	Expressway, ///< A company's expressway that roads from two towns join.
	Hospital, ///< A hospital built by a company in a town that has none.
	University, ///< A university built by a company in a town that has none.
	End, ///< End marker.
};

/** Stages of a project of the five-year plan. */
enum class FederalProjectState : uint8_t {
	Open, ///< Offered: the first company to deliver it is paid.
	Delivered, ///< A company delivered it and was paid.
	Lapsed, ///< Nobody delivered it before the plan ended, or it isn't needed any more.
	End, ///< End marker.
};

/** A project of the federal government's five-year plan (saved in chunk SFPR). */
struct FederalProject {
	FederalProjectType type = FederalProjectType::MetroLine; ///< What it is.
	FederalProjectState state = FederalProjectState::Open; ///< Its stage.
	uint8_t plan = 0; ///< Number of the plan it belongs to.
	TownID town = TownID::Invalid(); ///< The town it is for.
	TownID town2 = TownID::Invalid(); ///< An expressway's other town.
	CompanyID company = CompanyID::Invalid(); ///< The company that delivered it.
	Money payment = 0; ///< What the federal government pays the company that delivers it.
	int32_t offered = 0; ///< Year it was offered.
	int32_t done = 0; ///< Year it was delivered or lapsed.
};

/** What a town's voters make of things, each 0-100: the higher, the more of it. */
struct VoterConcerns {
	uint8_t traffic = 0; ///< How much of its roads crawl.
	uint8_t transit = 0; ///< How much of its travel goes by transit.
	uint8_t satisfaction = 50; ///< How satisfied passengers are with its stations (50 when it has none).
	uint8_t air = 0; ///< How dirty its air is.
	uint8_t services = 0; ///< How much its people lack city services.
	uint8_t housing = 0; ///< How poor its housing is: run-down estates.
	uint8_t taxes = 0; ///< How high its taxes are.
	uint8_t growth = 0; ///< How much it wants to grow: small towns the most.
};

#endif /* STATE_TYPE_H */
