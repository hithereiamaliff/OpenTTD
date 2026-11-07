/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file road_pricing_widget.h Types related to the road pricing widgets. */

#ifndef WIDGETS_ROAD_PRICING_WIDGET_H
#define WIDGETS_ROAD_PRICING_WIDGET_H

/** Widgets of the #TollsToolbarWindow class. */
enum TollsToolbarWidgets : WidgetID {
	WID_TT_CAPTION, ///< Caption of the window.
	WID_TT_PLAZA, ///< Build a toll plaza.
	WID_TT_FREE_FLOW, ///< Replace a toll plaza's booths with a free-flow gantry.
	WID_TT_OVERVIEW, ///< Show the company's tolls.
};

/** Widgets of the #TollsWindow class. */
enum TollsWidgets : WidgetID {
	WID_TO_CAPTION, ///< Caption of the window.
	WID_TO_INFO, ///< The company's tolls and toll plazas.
	WID_TO_LEVEL, ///< Toll level dropdown.
	WID_TO_SCHEME, ///< Toll scheme dropdown.
};

#endif /* WIDGETS_ROAD_PRICING_WIDGET_H */
