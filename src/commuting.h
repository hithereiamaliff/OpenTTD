/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file commuting.h Commuting: where a town's people work, how they get there, and the roads they take. */

#ifndef COMMUTING_H
#define COMMUTING_H

#include "commuting_type.h"
#include "gfx_type.h"
#include "house_type.h"
#include "tile_type.h"
#include "town_type.h"

struct HouseSpec;
struct Town;

bool IsCommutingOn();
bool IsTownCommutingKnown(const Town *t);
uint GetCommuteCars(TileIndex tile);
uint GetTownCommuteTransitShare(const Town *t);
uint GetCommuteMinutes(uint ticks);
uint GetHouseCommuteShare(const Town *t, TileIndex tile, const HouseSpec &hs, bool appeal = false);
uint GetCommuterPassengerPercent(const Town *t, TileIndex tile, const HouseSpec &hs);
PaletteID GetCommuteOverlayPalette(TileIndex tile);

void UpdateTownCommuting(Town *t);
void OnTownCommutingRemoved(Town *t);
void RebuildCommuteFlows();
void OnCommutingSettingChanged();
void InitializeCommuting();

#endif /* COMMUTING_H */
