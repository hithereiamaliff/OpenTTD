/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file satisfaction.h Passenger satisfaction with lines: from waiting, crowding, punctuality, fares, vehicle condition and comfort. */

#ifndef SATISFACTION_H
#define SATISFACTION_H

#include "line_type.h"

struct OrderList;
struct Station;
struct Town;

bool IsPassengerLine(const OrderList *list);
int GetStationSatisfactionRatingBonus(const Station *st);
int GetTownSatisfactionHappiness(const Town *t);
void SatisfactionMonthlyLoop();

#endif /* SATISFACTION_H */
