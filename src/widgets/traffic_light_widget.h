/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file traffic_light_widget.h Types related to the traffic light widgets. */

#ifndef WIDGETS_TRAFFIC_LIGHT_WIDGET_H
#define WIDGETS_TRAFFIC_LIGHT_WIDGET_H

/** Widgets of the #TrafficLightWindow class. */
enum TrafficLightWidgets : WidgetID {
	WID_TL_CAPTION,            ///< Caption of the window.
	WID_TL_LOCATION,           ///< Scroll to the traffic lights.
	WID_TL_MODE_SMART,         ///< Switch to smart lights.
	WID_TL_MODE_FIXED,         ///< Switch to fixed time lights.
	WID_TL_GREEN_X_DECREASE,   ///< Shorter green for the north-east/south-west road.
	WID_TL_GREEN_X_INCREASE,   ///< Longer green for the north-east/south-west road.
	WID_TL_GREEN_X,            ///< Green time of the north-east/south-west road.
	WID_TL_GREEN_Y_DECREASE,   ///< Shorter green for the north-west/south-east road.
	WID_TL_GREEN_Y_INCREASE,   ///< Longer green for the north-west/south-east road.
	WID_TL_GREEN_Y,            ///< Green time of the north-west/south-east road.
	WID_TL_MIN_GREEN_DECREASE, ///< Shorter shortest green.
	WID_TL_MIN_GREEN_INCREASE, ///< Longer shortest green.
	WID_TL_MIN_GREEN,          ///< Shortest green of smart lights.
	WID_TL_OFFSET_DECREASE,    ///< Earlier in the cycle.
	WID_TL_OFFSET_INCREASE,    ///< Later in the cycle.
	WID_TL_OFFSET,             ///< Position in the cycle of fixed lights.
	WID_TL_STATUS,             ///< What the lights show right now.
	WID_TL_BUS_PRIORITY,       ///< Bus signals on or off.
};

#endif /* WIDGETS_TRAFFIC_LIGHT_WIDGET_H */
