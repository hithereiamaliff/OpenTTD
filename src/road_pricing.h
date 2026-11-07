/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file road_pricing.h Road pricing: expressways, toll plazas run as concessions, and congestion charges of towns. */

#ifndef ROAD_PRICING_H
#define ROAD_PRICING_H

#include "road_pricing_type.h"
#include "company_type.h"
#include "strings_type.h"
#include "tile_type.h"
#include "town_type.h"

struct TileInfo;
struct RoadVehicle;
struct Town;

/** A toll plaza on a company's expressway. */
struct TollPlaza {
	TimerGameEconomy::Date concession_end{}; ///< When the concession runs out; it is then extended, with a toll cut.
	uint8_t cut = 0; ///< Toll cut in percent that extending the concession cost.
	bool free_flow = false; ///< A free-flow gantry instead of booths: nobody stops to pay.
	uint8_t towns = 0; ///< Towns its expressway linked last month.
	uint32_t trips = 0; ///< Trips of town traffic it took tolls from last month.
	uint32_t vehicles = 0; ///< Road vehicles of other companies that paid here last month.
	uint32_t vehicles_this_month = 0; ///< Road vehicles of other companies that paid here this month.
	Money income = 0; ///< What it took last month.
	Money income_this_month = 0; ///< What other companies' vehicles paid here this month.
};

static constexpr uint TOLL_CONCESSION_YEARS = 30; ///< Length of a toll concession.
static constexpr uint TOLL_EXTENSION_YEARS = 20; ///< A concession is extended this much when it runs out...
static constexpr uint8_t TOLL_EXTENSION_CUT = 18; ///< ...in exchange for a toll cut of this many percent (PLUS, 2011).
static constexpr uint8_t TOLL_MAX_CUT = 54; ///< Toll cuts from extensions stop here.
static constexpr uint TOLL_FREEZE_YEARS = 5; ///< How long a toll freeze lasts.

bool IsExpresswayRoad(TileIndex tile);
void DrawExpressway(const TileInfo *ti);

TollPlaza *GetTollPlaza(TileIndex tile);
/**
 * Is there a toll plaza on a tile?
 * @param tile The tile.
 * @return True if there is.
 */
inline bool HasTollPlaza(TileIndex tile) { return GetTollPlaza(tile) != nullptr; }
int GetTollPlazaSpeedLimit(TileIndex tile);
void DrawTollPlaza(const TileInfo *ti);
Money GetTollPlazaCost();
Money GetFreeFlowUpgradeCost();
Money GetTollPlazaYearlyUpkeep(bool free_flow);
uint8_t GetEffectiveTollLevel(CompanyID company);
Money GetVehicleToll(const RoadVehicle *front, TileIndex tile);
void OnRoadPricingTileCleared(TileIndex tile);
void OnRoadVehicleEntersTile(RoadVehicle *front, TileIndex from, TileIndex to);

uint GetTollClassPercent(const RoadVehicle *front);
bool IsInCongestionChargeZone(const Town *t, TileIndex tile);
uint GetCongestionChargeTrafficPercent(const Town *t, TileIndex tile);
uint GetCongestionChargeZoneLevel(const Town *t);
Money GetCongestionCharge(const RoadVehicle *front);
Money GetCongestionChargeProposalCost();
StringID GetCongestionChargeRefusal(const Town *t, CompanyID company);
void UpdateCongestionChargeTowns();

void InitializeRoadPricing();

/** Saveload access. */
std::map<TileIndex, TollPlaza> &GetTollPlazas();

void ShowTollsToolbar();
void ShowTollsWindow(CompanyID company);

#endif /* ROAD_PRICING_H */
