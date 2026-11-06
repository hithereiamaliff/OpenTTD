/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file transit_authority_widget.h Types related to the town transit authority widgets. */

#ifndef WIDGETS_TRANSIT_AUTHORITY_WIDGET_H
#define WIDGETS_TRANSIT_AUTHORITY_WIDGET_H

/** Widgets of the #TransitAuthorityWindow class. */
enum TransitAuthorityWidgets : WidgetID {
	WID_TRA_CAPTION, ///< Caption of the window.
	WID_TRA_BUDGET, ///< The town's budget.
	WID_TRA_LIST, ///< Tenders and contracts.
	WID_TRA_SCROLLBAR, ///< Scrollbar of the list.
	WID_TRA_BID, ///< Bid for the selected tender.
	WID_TRA_END, ///< End the selected contract.
	WID_TRA_LINE, ///< Show the line of the selected tender or contract.
};

#endif /* WIDGETS_TRANSIT_AUTHORITY_WIDGET_H */
