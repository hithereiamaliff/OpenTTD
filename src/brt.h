/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file brt.h Bus rapid transit: stations raised onto viaducts, BRT stations with fare gates and level platforms, and the BRT score of lines. */

#ifndef BRT_H
#define BRT_H

#include "economy_type.h"
#include "tile_type.h"

struct TileInfo;
struct RoadVehicle;
struct Station;
struct LineMonthStats;
struct OrderList;

/** How a line measures up to the basics of bus rapid transit. */
enum class BrtGrade : uint8_t {
	None, ///< Not BRT.
	Basic, ///< Basic BRT.
	Bronze, ///< Bronze.
	Silver, ///< Silver.
	Gold, ///< Gold.
	End, ///< End marker.
};

/** The BRT score of a line for one month, with its parts. */
struct BrtScore {
	bool known = false; ///< Whether the line's buses drove and stopped enough to judge it.
	uint score = 0; ///< The score, 0 to 100.
	BrtGrade grade = BrtGrade::None; ///< The grade.
	uint busway = 0; ///< Percent of the distance on busways: bus-only roads, kerbed busways, bus-only viaducts and tunnels.
	uint lanes = 0; ///< Percent on all-day bus lanes.
	uint peak = 0; ///< Percent on peak bus lanes.
	uint shared = 0; ///< Percent in mixed traffic.
	uint gated = 0; ///< Percent of stops made at BRT stations.
	uint level = 0; ///< Percent of stops made at BRT stations by low-floor buses.
	uint crossings = 0; ///< Junctions and level crossings crossed.
	uint signals = 0; ///< Of those, with bus signals.
	uint lights = 0; ///< With traffic lights but without bus signals.
	uint dedicated_points = 0; ///< Points for dedicated lanes, of 25.
	uint alignment_points = 0; ///< Points for running apart from the traffic (busways), of 25.
	uint fare_points = 0; ///< Points for paying before boarding, of 15.
	uint level_points = 0; ///< Points for level boarding, of 15.
	uint junction_points = 0; ///< Points for priority at junctions, of 20.
	uint penalty = 0; ///< Points taken off for sections shared with the traffic.
};

bool IsElevatedRoadStop(TileIndex tile);
bool IsElevatedViaductEnd(TileIndex tile);
void DrawElevatedStopDeck(TileInfo *ti);

bool HasBrtStation(TileIndex tile);
Money GetBrtStationCost();
Money GetBrtStationYearlyUpkeep();
bool DrawBrtStation(const TileInfo *ti);
uint GetBrtBoardingPercent(const RoadVehicle *front);

void CountBrtTile(const RoadVehicle *front, TileIndex tile);
void CountBrtSpan(const RoadVehicle *front, TileIndex head);
void CountBrtStop(const RoadVehicle *front);

BrtScore CalcBrtScore(const LineMonthStats &stats);
BrtScore GetLineBrtScore(const OrderList *list);
bool IsBusLine(const OrderList *list);
int GetStationBrtRatingBonus(const Station *st);
uint GetBrtTenderQualityBonus(const OrderList *list);
void BrtMonthlyLoop();

void InitializeBrt();

/** Saveload access. */
std::set<TileIndex> &GetBrtStations();

#endif /* BRT_H */
