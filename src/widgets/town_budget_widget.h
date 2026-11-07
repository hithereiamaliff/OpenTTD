/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file town_budget_widget.h Types related to the town budget window. */

#ifndef WIDGETS_TOWN_BUDGET_WIDGET_H
#define WIDGETS_TOWN_BUDGET_WIDGET_H

/** Widgets of the #TownBudgetWindow class. */
enum TownBudgetWidgets : WidgetID {
	WID_TB_CAPTION, ///< Caption of the window.
	WID_TB_SUMMARY, ///< Taxes, fees, spending and reserves.
	WID_TB_LIST, ///< The ordinances.
	WID_TB_SCROLLBAR, ///< Scrollbar of the list.
	WID_TB_PROPOSE, ///< Propose the selected ordinance.
};

#endif /* WIDGETS_TOWN_BUDGET_WIDGET_H */
