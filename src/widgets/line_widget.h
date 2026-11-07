/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file line_widget.h Types related to the line widgets. */

#ifndef WIDGETS_LINE_WIDGET_H
#define WIDGETS_LINE_WIDGET_H

/** Widgets of the #LineWindow class. */
enum LineWidgets : WidgetID {
	WID_LN_CAPTION,   ///< Caption of the window.
	WID_LN_INFO,      ///< Panel with the line's badge, name and statistics.
	WID_LN_NUMBER,    ///< Set the line number.
	WID_LN_NAME,      ///< Set the line name.
	WID_LN_COLOUR,    ///< Choose the line colour.
	WID_LN_TARGET,    ///< Set the wanted time between departures.
	WID_LN_HOLD,      ///< Toggle holding vehicles at the first stop for even spacing.
	WID_LN_VEHICLES,  ///< Show the line's vehicles.
	WID_LN_ORDERS,    ///< Show the line's orders.
	WID_LN_TIMETABLE, ///< Show the line's timetable.
	WID_LN_FARE, ///< Choose the line's own fare scheme.
	WID_LN_FARE_PRICE, ///< Choose the line's own price level.
	WID_LN_OFFPEAK, ///< Choose how many vehicles run between the rush hours of the city clock.
	WID_LN_ON_DEMAND, ///< Toggle running the line on demand.
	WID_LN_DRIVERLESS, ///< Toggle running the line's trains without drivers.
	WID_LN_MAP, ///< Show the line map.
};

/** Widgets of the #LineMapWindow class. */
enum LineMapWidgets : WidgetID {
	WID_LM_CAPTION, ///< Caption of the window.
	WID_LM_SUMMARY, ///< The line's badge, name and how it runs.
	WID_LM_MAP, ///< The map of its stops and vehicles.
	WID_LM_SCROLLBAR, ///< Scrollbar of the map.
};

/** Widgets of the #DeparturesWindow class. */
enum DeparturesWidgets : WidgetID {
	WID_DEP_CAPTION, ///< Caption of the window.
	WID_DEP_LIST,    ///< The departures.
};

#endif /* WIDGETS_LINE_WIDGET_H */
