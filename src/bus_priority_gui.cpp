/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file bus_priority_gui.cpp The bus priority toolbar: bus lanes, give-way markings, bus signals, kerbed busways and BRT stations. Ctrl+Click removes. */

#include "stdafx.h"
#include "bus_priority.h"
#include "bus_priority_cmd.h"
#include "command_func.h"
#include "gui.h"
#include "hotkeys.h"
#include "road_map.h"
#include "sound_func.h"
#include "road_cmd.h"
#include "tilehighlight_func.h"
#include "traffic_light.h"
#include "traffic_light_cmd.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"

#include "widgets/bus_priority_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/** Toolbar with the bus priority tools. */
struct BusPriorityWindow : Window {
	WidgetID last_tool = WID_BP_LANE; ///< The tool being used.

	BusPriorityWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
	}

	Point OnInitialPosition(int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		return AlignInitialConstructionToolbar(sm_width);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget < WID_BP_LANE || widget > WID_BP_BRT_STATION) return;
		if (HandlePlacePushButton(this, widget, widget == WID_BP_BUS_SIGNAL ? ANIMCURSOR_BUILDSIGNALS : SPR_CURSOR_BUS_STATION, HT_RECT)) this->last_tool = widget;
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		switch (this->last_tool) {
			case WID_BP_LANE:
			case WID_BP_LANE_PEAK:
			case WID_BP_BUSWAY:
				VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_BUS_LANE);
				break;

			case WID_BP_BRT_STATION:
				Command<Commands::SetBrtStation>::Post(_ctrl_pressed ? STR_ERROR_CAN_T_REMOVE_BRT_STATION : STR_ERROR_CAN_T_BUILD_BRT_STATION, CcPlaySound_CONSTRUCTION_OTHER, tile, _ctrl_pressed);
				break;

			case WID_BP_GIVE_WAY:
				Command<Commands::SetGiveWayStop>::Post(_ctrl_pressed ? STR_ERROR_CAN_T_REMOVE_GIVE_WAY : STR_ERROR_CAN_T_BUILD_GIVE_WAY, CcPlaySound_CONSTRUCTION_OTHER, tile, _ctrl_pressed);
				break;

			case WID_BP_BUS_SIGNAL:
				Command<Commands::SetTrafficLightBusPriority>::Post(STR_ERROR_CAN_T_CHANGE_BUS_SIGNAL, CcPlaySound_CONSTRUCTION_OTHER, tile, !_ctrl_pressed);
				break;

			default: break;
		}
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, [[maybe_unused]] ViewportDragDropSelectionProcess select_proc, Point pt) override
	{
		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	void OnPlaceMouseUp([[maybe_unused]] ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x == -1 || select_proc != DDSP_BUS_LANE) return;
		if (this->last_tool == WID_BP_BUSWAY) {
			Command<Commands::BuildBusLane>::Post(_ctrl_pressed ? STR_ERROR_CAN_T_REMOVE_BUSWAY : STR_ERROR_CAN_T_BUILD_BUSWAY, CcPlaySound_CONSTRUCTION_OTHER, end_tile, start_tile, BusLane::Busway, _ctrl_pressed);
			return;
		}
		const BusLane lane = this->last_tool == WID_BP_LANE_PEAK ? BusLane::Peak : BusLane::AllDay;
		Command<Commands::BuildBusLane>::Post(_ctrl_pressed ? STR_ERROR_CAN_T_REMOVE_BUS_LANE : STR_ERROR_CAN_T_BUILD_BUS_LANE, CcPlaySound_CONSTRUCTION_OTHER, end_tile, start_tile, lane, _ctrl_pressed);
	}

	void OnPlaceObjectAbort() override
	{
		this->RaiseButtons();
	}

	static inline HotkeyList hotkeys{"bus_priority", {
		Hotkey('1', "bus_lane", WID_BP_LANE),
		Hotkey('2', "bus_lane_peak", WID_BP_LANE_PEAK),
		Hotkey('3', "give_way", WID_BP_GIVE_WAY),
		Hotkey('4', "bus_signal", WID_BP_BUS_SIGNAL),
		Hotkey('5', "busway", WID_BP_BUSWAY),
		Hotkey('6', "brt_station", WID_BP_BRT_STATION),
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_bus_priority_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_BP_CAPTION), SetStringTip(STR_BUS_PRIORITY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_BP_LANE), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_BUS_LANE, STR_BUS_PRIORITY_LANE_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_BP_LANE_PEAK), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_BUS_LANE_PEAK, STR_BUS_PRIORITY_LANE_PEAK_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_BP_GIVE_WAY), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_GIVE_WAY, STR_BUS_PRIORITY_GIVE_WAY_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_BP_BUS_SIGNAL), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_BUS_SIGNAL, STR_BUS_PRIORITY_BUS_SIGNAL_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_BP_BUSWAY), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_BUSWAY, STR_BUS_PRIORITY_BUSWAY_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_BP_BRT_STATION), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_BRT_STATION, STR_BUS_PRIORITY_BRT_STATION_TOOLTIP),
	EndContainer(),
};

static WindowDesc _bus_priority_desc(
	WindowPosition::Manual, "toolbar_bus_priority", 0, 0,
	WindowClass::BusPriority, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_bus_priority_widgets,
	&BusPriorityWindow::hotkeys
);

/** Show the bus priority toolbar. */
void ShowBusPriorityToolbar()
{
	AllocateWindowDescFront<BusPriorityWindow>(_bus_priority_desc, 0);
}
