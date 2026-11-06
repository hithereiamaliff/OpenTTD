/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file bus_priority_widget.h Types related to the bus priority toolbar widgets. */

#ifndef WIDGETS_BUS_PRIORITY_WIDGET_H
#define WIDGETS_BUS_PRIORITY_WIDGET_H

/** Widgets of the #BusPriorityWindow class. */
enum BusPriorityWidgets : WidgetID {
	WID_BP_CAPTION, ///< Caption of the window.
	WID_BP_LANE, ///< Paint an all-day bus lane.
	WID_BP_LANE_PEAK, ///< Paint a peak-hours bus lane.
	WID_BP_GIVE_WAY, ///< Paint give-way markings at a bus stop.
	WID_BP_BUS_SIGNAL, ///< Switch on bus signals at traffic lights.
};

#endif /* WIDGETS_BUS_PRIORITY_WIDGET_H */
