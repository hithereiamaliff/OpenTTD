/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file town_zoning_widget.h Types related to the zoning widgets. */

#ifndef WIDGETS_TOWN_ZONING_WIDGET_H
#define WIDGETS_TOWN_ZONING_WIDGET_H

/** Widgets of the #TownZoningWindow class. */
enum TownZoningWidgets : WidgetID {
	WID_TZ_CAPTION,     ///< Caption of the window.
	WID_TZ_RESIDENTIAL, ///< Zone land for homes.
	WID_TZ_COMMERCIAL,  ///< Zone land for shops and offices.
	WID_TZ_INDUSTRIAL,  ///< Zone land for industry.
	WID_TZ_PARK,        ///< Zone land for parks.
	WID_TZ_CLEAR,       ///< Remove zoning.
};

#endif /* WIDGETS_TOWN_ZONING_WIDGET_H */
