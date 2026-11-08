/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file state.h States and federal territories: governments elected on the service voters get, land laws, regions run like Sabah and Sarawak. */

#ifndef STATE_H
#define STATE_H

#include "state_type.h"
#include "command_type.h"
#include "company_type.h"
#include "house_type.h"
#include "strings_type.h"
#include "tile_type.h"
#include "town_type.h"

struct Town;
struct HouseSpec;

/** Size of the cells of the map of states, in tiles. */
static constexpr uint STATE_CELL_SIZE = 4;
/** A region's waters reach this many cells from its shores (12 tiles: a territorial sea). */
static constexpr uint REGION_WATERS_CELLS = 3;

/** What a cell of the map of states is. */
enum class StateCell : uint8_t {
	Land = 0, ///< Land.
	Sea = 1, ///< Sea.
	RegionWaters = 2, ///< Sea within a region's waters: nobody may reclaim land there.
};

bool AreStatesOn();
bool AreStatesFormed();
std::vector<State> &GetStates();
const Federation &GetFederation();
State *GetState(StateID id);
State *GetTownState(const Town *t);
bool IsFederalTerritory(const Town *t);
bool IsRegionTown(const Town *t);
Platform GetTownGovernment(const Town *t);
StateID GetTileState(TileIndex tile);
StateCell GetTileStateCell(TileIndex tile);

std::string GetStateName(StateID id);
StringID GetPlatformName(Platform platform);
StringID GetTownCouncilName(const Town *t);
StringID GetTownCouncilKind(const Town *t);

Money GetLandPremium(TileIndex tile);
Money GetStateLandPremium(StateID id, uint value);
CommandCost CheckLandForCompany(TileIndex tile, DoCommandFlags flags);
CommandCost CheckReclamation(TileIndex tile);
CommandCost CheckTownFoundingForStates(TileIndex tile);
bool CanCommuteBetween(const Town *a, const Town *b);
uint GetPlatformStrength(StateID id);
uint GetStateTransitGrantPercent(const Town *t);
uint GetStateGrowthPercent(const Town *t);
uint ScaleStateHouseProbability(const Town *t, const HouseSpec &hs, uint probability);
uint GetStateOrdinanceChance(const Town *t, uint ordinance, uint chance);

VoterConcerns GetVoterConcerns(const Town *t);
uint GetServiceScore(const VoterConcerns &c);
std::array<int, PLATFORM_COUNT> GetPlatformAppeal(const VoterConcerns &c);
uint GetTownSeats(const Town *t);
PlatformSeats GetStatePoll(StateID id);
PlatformSeats GetFederalPoll();
VoterConcerns GetStateConcerns(StateID id);

void UpdateStatesMonthly();
void OnTownCreatedForStates(Town *t);
void OnTownRemovedForStates(Town *t);
void UpdateStateCaches();
void InitializeStates();
void ShowStatesWindow(StateID select = INVALID_STATE);

#endif /* STATE_H */
