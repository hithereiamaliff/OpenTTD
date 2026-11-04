/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file street_life.h Street life: cars, vans, lorries and motorbikes on the roads, drawn as scenery.
 *
 * Town traffic itself is the congestion level of each road tile, which is
 * game state (see town_traffic.h). Street life only shows it: every player's
 * game puts its own vehicles on the roads in view, as many as the congestion
 * calls for, and lets them drive, turn, queue behind each other and behind
 * real vehicles, and stop at red lights and closed level crossings. They are
 * not saved, don't use the game's random numbers and never change the game,
 * so network games stay in sync; they only exist where someone looks.
 */

#ifndef STREET_LIFE_H
#define STREET_LIFE_H

#include "core/enum_type.hpp"

struct DrawPixelInfo;

/** What street life to show, see GUISettings::street_life. */
enum class StreetLifeSetting : uint8_t {
	Off, ///< None.
	Vehicles, ///< Cars, vans, lorries and motorbikes.
	All, ///< Vehicles and pedestrians.
};

void InitializeStreetLife();
void ViewportAddStreetLife(DrawPixelInfo *dpi);
void OnStreetLifeSettingChanged();
size_t GetStreetLifeVehicleCount();

#endif /* STREET_LIFE_H */
