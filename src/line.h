/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file line.h Lines: the vehicles that share one order list, with a number, name and colour.
 *
 * A line is a shared order list (one vehicle on its own is a line too). It
 * can have a number like "101" or "CAT", a name and a colour, and a wanted
 * time between departures. Departures from its first stop, riders and how
 * full its vehicles leave are counted every month. Line data lives on the
 * order list and is saved in its own chunk (LINE).
 */

#ifndef LINE_H
#define LINE_H

#include "line_type.h"
#include "order_type.h"
#include "station_type.h"
#include "vehicle_type.h"

struct OrderList;
struct Rect;

VehicleOrderID GetLineFirstStop(const OrderList *list);
Colours GetLineColour(const OrderList *list);
uint32_t GetLineAverageGap(const LineInfo &line);
uint32_t GetLineAverageWait(const LineInfo &line);
uint GetLineVehiclesNeeded(const OrderList *list);

void OnLineVehicleDeparts(Vehicle *v, uint8_t occupancy);
bool MustHoldForLineSpacing(const Vehicle *v);
void OnLineRidersDelivered(const Vehicle *front, uint count);

int DrawLineBadge(const Rect &r, const OrderList *list, bool rtl = false);
std::string FormatLineDuration(TimerGameTick::Ticks ticks);
uint GetLineBadgeWidth(const OrderList *list);
void ShowLineWindow(const Vehicle *v);
void ShowLineMapWindow(const Vehicle *v);
void ShowDeparturesWindow(StationID station);

#endif /* LINE_H */
