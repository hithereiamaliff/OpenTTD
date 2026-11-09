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
	RegionWaters = 2, ///< Sea within a region's waters: reclaiming land there needs its permit.
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
bool IsSabahOrSarawak(StateID id);
std::vector<uint16_t> GetTownLands();
StringID GetPlatformName(Platform platform);
StringID GetTownCouncilName(const Town *t);
StringID GetTownCouncilKind(const Town *t);

Money GetLandPremium(TileIndex tile);
Money GetStateLandPremium(StateID id, uint value);
CommandCost CheckLandForCompany(TileIndex tile, DoCommandFlags flags);
/** A region's permit for a company's new town or reclaimed sea tile (see GetRegionPermit). */
struct RegionPermit {
	StateID state = INVALID_STATE; ///< The region giving it; INVALID_STATE when none is needed.
	bool cofunded = false; ///< The state pays half; otherwise the company pays a permit fee to the state.
	Money amount = 0; ///< The state's half, or the fee.

	/**
	 * What the permit changes in the company's cost.
	 * @return Less by the state's half, or more by the fee.
	 */
	Money GetCompanyCostChange() const { return this->cofunded ? -this->amount : this->amount; }
};

CommandCost GetRegionPermit(TileIndex tile, Money cost, bool waters, RegionPermit &permit);
void ApplyRegionPermit(const RegionPermit &permit);
uint GetRegionWorkPassPercent(StateID id);
/** A region's immigration checkpoint adds this many minutes to every commute across its border. */
static constexpr uint REGION_CHECKPOINT_MINUTES = 15;
/** A region's permit fee for a new town or reclaimed land when it doesn't co-fund them, in percent of their cost. */
static constexpr uint REGION_PERMIT_FEE_PERCENT = 25;
std::pair<uint64_t, uint64_t> GetRegionJobsAndWorkers(StateID id);
bool DoesRegionWantGrowth(StateID id);

/** While one lives, the sea a command clears stays sea, under a dock or a ship depot: that isn't land reclamation. */
struct BuildingOnSeaScope {
	BuildingOnSeaScope();
	~BuildingOnSeaScope();
	BuildingOnSeaScope(const BuildingOnSeaScope &) = delete;
	BuildingOnSeaScope &operator=(const BuildingOnSeaScope &) = delete;
};

uint GetCommuteWorkPassPercent(const Town *home, const Town *work);
bool IsCommuteAcrossRegionBorder(const Town *home, const Town *work);
uint GetPlatformStrength(StateID id);
uint GetStateTransitGrantPercent(const Town *t);
uint GetStateGrowthPercent(const Town *t);
uint ScaleStateHouseProbability(const Town *t, const HouseSpec &hs, uint probability);
uint GetStateOrdinanceChance(const Town *t, uint ordinance, uint chance);

std::string GetSeatsText(const PlatformSeats &seats);
uint GetTotalSeats(const PlatformSeats &seats);
bool IsElectionCampaign(StateID id);
StringID GetElectionMonthName(StateID id);
std::vector<ElectionResult> &GetElections();
const ElectionResult *GetLastElection(StateID id);
void ShowElectionNightWindow(StateID id, bool count);

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
