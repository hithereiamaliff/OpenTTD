/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file tutorial_widget.h Types related to the tutorial window. */

#ifndef WIDGETS_TUTORIAL_WIDGET_H
#define WIDGETS_TUTORIAL_WIDGET_H

/** Widgets of the #TutorialWindow class. */
enum TutorialWidgets : WidgetID {
	WID_TUT_CLOSE, ///< Close box: leaves the tutorial.
	WID_TUT_CAPTION, ///< Caption of the window.
	WID_TUT_TEXT, ///< The text of the page.
	WID_TUT_SEL_STEP, ///< Selection: the buttons of a step, or nothing.
	WID_TUT_SEL_OFFER, ///< Selection: the buttons of an offer, or nothing.
	WID_TUT_SEL_CONTENTS, ///< Selection: the buttons of the chapters page, or nothing.
	WID_TUT_CONTENTS, ///< Show the chapters.
	WID_TUT_BACK, ///< Go to the previous step.
	WID_TUT_NEXT, ///< Go to the next step, or finish.
	WID_TUT_EXIT, ///< Leave the tutorial.
	WID_TUT_START, ///< Offer: start the tutorial.
	WID_TUT_NOT_NOW, ///< Offer: not now.
	WID_TUT_CONTINUE, ///< Contents: go back to the step the player was at.
	WID_TUT_CHAPTER_START, ///< Contents: chapter "Start a game".
	WID_TUT_CHAPTER_BUS, ///< Contents: chapter "Your first bus line".
	WID_TUT_CHAPTER_NEW, ///< Contents: chapter "What's new for OpenTTD players".
	WID_TUT_CONTENTS_EXIT, ///< Contents: leave the tutorial.
};

#endif /* WIDGETS_TUTORIAL_WIDGET_H */
