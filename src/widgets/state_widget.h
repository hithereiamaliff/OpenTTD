/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file state_widget.h Types related to the states window. */

#ifndef WIDGETS_STATE_WIDGET_H
#define WIDGETS_STATE_WIDGET_H

/** Widgets of the #StatesWindow class. */
enum StatesWidgets : WidgetID {
	WID_ST_CAPTION, ///< Caption of the window.
	WID_ST_FEDERATION, ///< The federal government and the next general election.
	WID_ST_LIST, ///< The states and federal territories.
	WID_ST_SCROLLBAR, ///< Scrollbar of the list.
	WID_ST_DETAILS, ///< The selected state.
	WID_ST_RENAME, ///< Rename the selected state.
	WID_ST_CAPITAL, ///< Show the selected state's capital.
	WID_ST_MAP, ///< Show the map of states.
};

#endif /* WIDGETS_STATE_WIDGET_H */
