/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file city_services_widget.h Types related to the city services widgets. */

#ifndef WIDGETS_CITY_SERVICES_WIDGET_H
#define WIDGETS_CITY_SERVICES_WIDGET_H

/** Widgets of the #CityServicesWindow class. The tools are in #CityService order. */
enum CityServicesWidgets : WidgetID {
	WID_CS_CAPTION,   ///< Caption of the window.
	WID_CS_POLICE,    ///< Build a police station.
	WID_CS_FIRE,      ///< Build a fire station.
	WID_CS_HEALTH,    ///< Build a clinic.
	WID_CS_EDUCATION, ///< Build a school.
	WID_CS_PARK,      ///< Build a park.
};

#endif /* WIDGETS_CITY_SERVICES_WIDGET_H */
