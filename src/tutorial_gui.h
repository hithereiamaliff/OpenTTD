/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file tutorial_gui.h City fork: the interactive tutorial, a window that walks the player through the game step by step. */

#ifndef TUTORIAL_GUI_H
#define TUTORIAL_GUI_H

void ShowTutorial();
void TutorialOnMainMenu();
void TutorialOnGameStart(bool openttd_save);

#endif /* TUTORIAL_GUI_H */
