/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file depot_yard_widget.h Types related to the depot yard widgets. */

#ifndef WIDGETS_DEPOT_YARD_WIDGET_H
#define WIDGETS_DEPOT_YARD_WIDGET_H

/** Widgets of the #DepotYardWindow class. The build buttons are in #DepotFacility order. */
enum DepotYardWidgets : WidgetID {
	WID_DY_CAPTION, ///< Caption of the window.
	WID_DY_LOCATION, ///< Scroll to the depot.
	WID_DY_INFO, ///< What the yard offers.
	WID_DY_BUILD_FIRST, ///< First build button: a bus park.
	WID_DY_BUILD_LAST = WID_DY_BUILD_FIRST + 8, ///< Last build button: a staff building.
};

#endif /* WIDGETS_DEPOT_YARD_WIDGET_H */
