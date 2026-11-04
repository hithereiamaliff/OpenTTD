/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file town_zoning_gui.cpp Toolbar to zone land for homes, shops and offices, industry or parks. */

#include "stdafx.h"
#include "town_zoning.h"
#include "town_zoning_cmd.h"
#include "command_func.h"
#include "gui.h"
#include "hotkeys.h"
#include "sound_func.h"
#include "tilehighlight_func.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"

#include "widgets/town_zoning_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/**
 * The zone a tool paints.
 * @param widget The tool's button.
 * @return The zone; TownZone::None removes zoning.
 */
static TownZone GetZoneOfTool(WidgetID widget)
{
	switch (widget) {
		case WID_TZ_RESIDENTIAL: return TownZone::Residential;
		case WID_TZ_COMMERCIAL: return TownZone::Commercial;
		case WID_TZ_INDUSTRIAL: return TownZone::Industrial;
		case WID_TZ_PARK: return TownZone::Park;
		default: return TownZone::None;
	}
}

/**
 * The icon of a tool, also used as its mouse cursor.
 * @param widget The tool's button.
 * @return The sprite.
 */
static SpriteID GetIconOfTool(WidgetID widget)
{
	switch (widget) {
		case WID_TZ_RESIDENTIAL: return SPR_IMG_ZONE_RESIDENTIAL;
		case WID_TZ_COMMERCIAL: return SPR_IMG_ZONE_COMMERCIAL;
		case WID_TZ_INDUSTRIAL: return SPR_IMG_ZONE_INDUSTRIAL;
		case WID_TZ_PARK: return SPR_IMG_ZONE_PARK;
		default: return SPR_IMG_ZONE_CLEAR;
	}
}

/** Toolbar to zone land. Zones show in the main view while it is open. */
struct TownZoningWindow : Window {
	WidgetID last_tool = INVALID_WIDGET; ///< The tool in use.

	TownZoningWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		ShowTownZoneOverlay(true);
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		ShowTownZoneOverlay(false);
		this->Window::Close();
	}

	Point OnInitialPosition(int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		return AlignInitialConstructionToolbar(sm_width);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_TZ_RESIDENTIAL:
			case WID_TZ_COMMERCIAL:
			case WID_TZ_INDUSTRIAL:
			case WID_TZ_PARK:
			case WID_TZ_CLEAR:
				if (HandlePlacePushButton(this, widget, GetIconOfTool(widget), HT_RECT)) this->last_tool = widget;
				break;

			default:
				break;
		}
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_SET_TOWN_ZONE);
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, [[maybe_unused]] ViewportDragDropSelectionProcess select_proc, Point pt) override
	{
		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	void OnPlaceMouseUp([[maybe_unused]] ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x == -1 || select_proc != DDSP_SET_TOWN_ZONE) return;
		Command<Commands::SetTownZone>::Post(STR_ERROR_CAN_T_ZONE_HERE, end_tile, start_tile, GetZoneOfTool(this->last_tool));
	}

	void OnPlaceObjectAbort() override
	{
		this->RaiseButtons();
	}

	static inline HotkeyList hotkeys{"zoning", {
		Hotkey('1', "residential", WID_TZ_RESIDENTIAL),
		Hotkey('2', "commercial", WID_TZ_COMMERCIAL),
		Hotkey('3', "industrial", WID_TZ_INDUSTRIAL),
		Hotkey('4', "park", WID_TZ_PARK),
		Hotkey('5', "clear", WID_TZ_CLEAR),
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_town_zoning_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_TZ_CAPTION), SetStringTip(STR_TOWN_ZONING_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_TZ_RESIDENTIAL), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ZONE_RESIDENTIAL, STR_TOWN_ZONING_RESIDENTIAL_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_TZ_COMMERCIAL), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ZONE_COMMERCIAL, STR_TOWN_ZONING_COMMERCIAL_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_TZ_INDUSTRIAL), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ZONE_INDUSTRIAL, STR_TOWN_ZONING_INDUSTRIAL_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_TZ_PARK), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ZONE_PARK, STR_TOWN_ZONING_PARK_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_TZ_CLEAR), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_ZONE_CLEAR, STR_TOWN_ZONING_CLEAR_TOOLTIP),
	EndContainer(),
};

static WindowDesc _town_zoning_desc(
	WindowPosition::Manual, "toolbar_zoning", 0, 0,
	WindowClass::TownZoning, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_town_zoning_widgets,
	&TownZoningWindow::hotkeys
);

/** Open the zoning toolbar. */
void ShowTownZoningToolbar()
{
	AllocateWindowDescFront<TownZoningWindow>(_town_zoning_desc, 0);
}
