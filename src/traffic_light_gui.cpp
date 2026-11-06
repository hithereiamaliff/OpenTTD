/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file traffic_light_gui.cpp Window to change how traffic lights switch. */

#include "stdafx.h"
#include "traffic_light.h"
#include "traffic_light_cmd.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "gui.h"
#include "road_map.h"
#include "strings_func.h"
#include "town.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "core/geometry_func.hpp"
#include "timer/timer.h"
#include "timer/timer_game_tick.h"
#include "timer/timer_window.h"

#include "widgets/traffic_light_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/** One click on an arrow changes a time by this many ticks; Ctrl+Click by ten times as much. */
static constexpr int TRAFFIC_LIGHT_STEP = Ticks::TICKS_PER_SECOND / 2;

/**
 * Convert ticks to tenths of a second, for display with one decimal.
 * @param ticks The time in ticks.
 * @return The time in tenths of a second, rounded.
 */
static int64_t TicksToDeciSeconds(uint ticks)
{
	return (static_cast<int64_t>(ticks) * 10 + Ticks::TICKS_PER_SECOND / 2) / Ticks::TICKS_PER_SECOND;
}

/** Window to change how the traffic lights of one junction switch. */
struct TrafficLightWindow : Window {
	TileIndex tile; ///< The junction with the lights.

	TrafficLightWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc), tile(TileIndex(window_number))
	{
		this->InitNested(window_number);
		this->OnInvalidateData();
	}

	/**
	 * The lights shown, if they still exist.
	 * @return The lights or nullptr.
	 */
	const TrafficLight *GetLights() const
	{
		if (!IsNormalRoadTile(this->tile) || !HasTrafficLight(this->tile)) return nullptr;
		return GetTrafficLight(this->tile);
	}

	/**
	 * May the local company change these lights?
	 * @param tl The lights.
	 * @return True if it may.
	 */
	static bool CanChange(const TrafficLight &tl)
	{
		if (tl.owner == OWNER_NONE || tl.owner == OWNER_TOWN) return Company::IsValidID(_local_company) || _game_mode == GameMode::Editor;
		return tl.owner == _local_company;
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;

		const TrafficLight *tl = this->GetLights();
		if (tl == nullptr) {
			this->Close();
			return;
		}

		bool fixed = tl->mode == TrafficLightMode::Fixed;
		bool can_change = CanChange(*tl);
		this->SetWidgetLoweredState(WID_TL_MODE_SMART, !fixed);
		this->SetWidgetLoweredState(WID_TL_MODE_FIXED, fixed);
		this->SetWidgetLoweredState(WID_TL_BUS_PRIORITY, tl->bus_priority);
		this->SetWidgetDisabledState(WID_TL_BUS_PRIORITY, !can_change);
		this->SetWidgetsDisabledState(!can_change, WID_TL_MODE_SMART, WID_TL_MODE_FIXED,
				WID_TL_GREEN_X_DECREASE, WID_TL_GREEN_X_INCREASE, WID_TL_GREEN_Y_DECREASE, WID_TL_GREEN_Y_INCREASE);
		this->SetWidgetsDisabledState(!can_change || fixed, WID_TL_MIN_GREEN_DECREASE, WID_TL_MIN_GREEN_INCREASE);
		this->SetWidgetsDisabledState(!can_change || !fixed, WID_TL_OFFSET_DECREASE, WID_TL_OFFSET_INCREASE);
		this->SetDirty();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		const TrafficLight *tl = this->GetLights();
		if (tl == nullptr) return this->Window::GetWidgetString(widget, stringid);
		bool fixed = tl->mode == TrafficLightMode::Fixed;

		switch (widget) {
			case WID_TL_CAPTION: {
				const Town *t = ClosestTownFromTile(this->tile, UINT_MAX);
				if (t == nullptr) return GetString(STR_TRAFFIC_LIGHT_CAPTION_NO_TOWN);
				return GetString(STR_TRAFFIC_LIGHT_CAPTION, t->index);
			}

			case WID_TL_GREEN_X:
				return GetString(fixed ? STR_TRAFFIC_LIGHT_GREEN_X : STR_TRAFFIC_LIGHT_LONGEST_GREEN_X, TicksToDeciSeconds(tl->green_x), 1);

			case WID_TL_GREEN_Y:
				return GetString(fixed ? STR_TRAFFIC_LIGHT_GREEN_Y : STR_TRAFFIC_LIGHT_LONGEST_GREEN_Y, TicksToDeciSeconds(tl->green_y), 1);

			case WID_TL_MIN_GREEN:
				if (fixed) return GetString(STR_TRAFFIC_LIGHT_SHORTEST_GREEN_UNUSED);
				return GetString(STR_TRAFFIC_LIGHT_SHORTEST_GREEN, TicksToDeciSeconds(tl->min_green), 1);

			case WID_TL_OFFSET:
				if (!fixed) return GetString(STR_TRAFFIC_LIGHT_OFFSET_UNUSED);
				return GetString(STR_TRAFFIC_LIGHT_OFFSET, TicksToDeciSeconds(tl->offset), 1, TicksToDeciSeconds(tl->GetCycleLength()), 1);

			case WID_TL_STATUS:
				if (tl->clearing && tl->phase_ticks < TRAFFIC_LIGHT_AMBER) return GetString(STR_TRAFFIC_LIGHT_STATUS_AMBER);
				if (tl->clearing) return GetString(STR_TRAFFIC_LIGHT_STATUS_CLEARING);
				return GetString(tl->green_axis == to_underlying(Axis::X) ? STR_TRAFFIC_LIGHT_STATUS_GREEN_X : STR_TRAFFIC_LIGHT_STATUS_GREEN_Y, TicksToDeciSeconds(tl->phase_ticks), 1);

			default:
				return this->Window::GetWidgetString(widget, stringid);
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		/* Make room for the longest texts with the largest numbers. */
		const int64_t max_time = TicksToDeciSeconds(TRAFFIC_LIGHT_MAX_TIME * 2 + 2 * TRAFFIC_LIGHT_CLEARANCE);
		auto fit = [&size](const std::string &text) { size = maxdim(size, GetStringBoundingBox(text)); };
		switch (widget) {
			case WID_TL_GREEN_X:
				fit(GetString(STR_TRAFFIC_LIGHT_LONGEST_GREEN_X, max_time, 1));
				fit(GetString(STR_TRAFFIC_LIGHT_GREEN_X, max_time, 1));
				break;

			case WID_TL_GREEN_Y:
				fit(GetString(STR_TRAFFIC_LIGHT_LONGEST_GREEN_Y, max_time, 1));
				fit(GetString(STR_TRAFFIC_LIGHT_GREEN_Y, max_time, 1));
				break;

			case WID_TL_MIN_GREEN:
				fit(GetString(STR_TRAFFIC_LIGHT_SHORTEST_GREEN, max_time, 1));
				fit(GetString(STR_TRAFFIC_LIGHT_SHORTEST_GREEN_UNUSED));
				break;

			case WID_TL_OFFSET:
				fit(GetString(STR_TRAFFIC_LIGHT_OFFSET, max_time, 1, max_time, 1));
				fit(GetString(STR_TRAFFIC_LIGHT_OFFSET_UNUSED));
				break;

			case WID_TL_STATUS:
				fit(GetString(STR_TRAFFIC_LIGHT_STATUS_GREEN_X, max_time, 1));
				fit(GetString(STR_TRAFFIC_LIGHT_STATUS_GREEN_Y, max_time, 1));
				fit(GetString(STR_TRAFFIC_LIGHT_STATUS_CLEARING));
				fit(GetString(STR_TRAFFIC_LIGHT_STATUS_AMBER));
				break;
		}
	}

	/**
	 * Ask the server to change the lights.
	 * @param tl The current lights.
	 * @param mode New mode.
	 * @param green_x New green of the north-east/south-west road.
	 * @param green_y New green of the north-west/south-east road.
	 * @param min_green New shortest green.
	 * @param offset New offset; made to fit the new cycle.
	 */
	void Change(const TrafficLight &tl, TrafficLightMode mode, int green_x, int green_y, int min_green, int offset)
	{
		green_x = Clamp(green_x, TRAFFIC_LIGHT_MIN_TIME, TRAFFIC_LIGHT_MAX_TIME);
		green_y = Clamp(green_y, TRAFFIC_LIGHT_MIN_TIME, TRAFFIC_LIGHT_MAX_TIME);
		min_green = Clamp(min_green, TRAFFIC_LIGHT_MIN_TIME, std::min(green_x, green_y));
		int cycle = green_x + green_y + 2 * TRAFFIC_LIGHT_CLEARANCE;
		offset = ((offset % cycle) + cycle) % cycle;
		if (mode == tl.mode && green_x == tl.green_x && green_y == tl.green_y && min_green == tl.min_green && offset == tl.offset) return;

		Command<Commands::ConfigureTrafficLight>::Post(STR_ERROR_CAN_T_CHANGE_TRAFFIC_LIGHTS, this->tile, mode,
				static_cast<uint16_t>(green_x), static_cast<uint16_t>(green_y), static_cast<uint16_t>(min_green), static_cast<uint16_t>(offset));
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget == WID_TL_LOCATION) {
			if (_ctrl_pressed) {
				ShowExtraViewportWindow(this->tile);
			} else {
				ScrollMainWindowToTile(this->tile);
			}
			return;
		}

		const TrafficLight *tl = this->GetLights();
		if (tl == nullptr || !CanChange(*tl)) return;

		if (widget == WID_TL_BUS_PRIORITY) {
			Command<Commands::SetTrafficLightBusPriority>::Post(STR_ERROR_CAN_T_CHANGE_BUS_SIGNAL, this->tile, !tl->bus_priority);
			return;
		}

		const int step = _ctrl_pressed ? TRAFFIC_LIGHT_STEP * 10 : TRAFFIC_LIGHT_STEP;
		TrafficLightMode mode = tl->mode;
		int green_x = tl->green_x;
		int green_y = tl->green_y;
		int min_green = tl->min_green;
		int offset = tl->offset;

		switch (widget) {
			case WID_TL_MODE_SMART: mode = TrafficLightMode::Smart; break;
			case WID_TL_MODE_FIXED: mode = TrafficLightMode::Fixed; break;
			case WID_TL_GREEN_X_DECREASE: green_x -= step; break;
			case WID_TL_GREEN_X_INCREASE: green_x += step; break;
			case WID_TL_GREEN_Y_DECREASE: green_y -= step; break;
			case WID_TL_GREEN_Y_INCREASE: green_y += step; break;
			case WID_TL_MIN_GREEN_DECREASE: min_green -= step; break;
			case WID_TL_MIN_GREEN_INCREASE: min_green += step; break;
			case WID_TL_OFFSET_DECREASE: offset -= step; break;
			case WID_TL_OFFSET_INCREASE: offset += step; break;
			default: return;
		}

		this->Change(*tl, mode, green_x, green_y, min_green, offset);
	}

	/** Keep the "now" line up to date. */
	const IntervalTimer<TimerWindow> redraw_interval = {std::chrono::milliseconds(250), [this](auto) {
		this->SetWidgetDirty(WID_TL_STATUS);
	}};
};

/** Widgets of the traffic light window. */
static constexpr std::initializer_list<NWidgetPart> _nested_traffic_light_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_TL_CAPTION),
		NWidget(WWT_PUSHIMGBTN, Colours::Grey, WID_TL_LOCATION), SetAspect(WidgetDimensions::ASPECT_LOCATION), SetSpriteTip(SPR_GOTO_LOCATION, STR_TRAFFIC_LIGHT_LOCATION_TOOLTIP),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.frametext), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_TEXTBTN, Colours::Grey, WID_TL_MODE_SMART), SetFill(1, 0), SetStringTip(STR_TRAFFIC_LIGHT_MODE_SMART, STR_TRAFFIC_LIGHT_MODE_SMART_TOOLTIP),
				NWidget(WWT_TEXTBTN, Colours::Grey, WID_TL_MODE_FIXED), SetFill(1, 0), SetStringTip(STR_TRAFFIC_LIGHT_MODE_FIXED, STR_TRAFFIC_LIGHT_MODE_FIXED_TOOLTIP),
			EndContainer(),
			NWidget(WWT_TEXTBTN, Colours::Grey, WID_TL_BUS_PRIORITY), SetFill(1, 0), SetStringTip(STR_TRAFFIC_LIGHT_BUS_PRIORITY, STR_TRAFFIC_LIGHT_BUS_PRIORITY_TOOLTIP),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_PUSHARROWBTN, Colours::Grey, WID_TL_GREEN_X_DECREASE), SetArrowWidgetTypeTip(ArrowWidgetType::Decrease, STR_TRAFFIC_LIGHT_DECREASE_TOOLTIP),
				NWidget(WWT_PUSHARROWBTN, Colours::Grey, WID_TL_GREEN_X_INCREASE), SetArrowWidgetTypeTip(ArrowWidgetType::Increase, STR_TRAFFIC_LIGHT_INCREASE_TOOLTIP),
				NWidget(WWT_TEXT, Colours::Invalid, WID_TL_GREEN_X), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_PUSHARROWBTN, Colours::Grey, WID_TL_GREEN_Y_DECREASE), SetArrowWidgetTypeTip(ArrowWidgetType::Decrease, STR_TRAFFIC_LIGHT_DECREASE_TOOLTIP),
				NWidget(WWT_PUSHARROWBTN, Colours::Grey, WID_TL_GREEN_Y_INCREASE), SetArrowWidgetTypeTip(ArrowWidgetType::Increase, STR_TRAFFIC_LIGHT_INCREASE_TOOLTIP),
				NWidget(WWT_TEXT, Colours::Invalid, WID_TL_GREEN_Y), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_PUSHARROWBTN, Colours::Grey, WID_TL_MIN_GREEN_DECREASE), SetArrowWidgetTypeTip(ArrowWidgetType::Decrease, STR_TRAFFIC_LIGHT_DECREASE_TOOLTIP),
				NWidget(WWT_PUSHARROWBTN, Colours::Grey, WID_TL_MIN_GREEN_INCREASE), SetArrowWidgetTypeTip(ArrowWidgetType::Increase, STR_TRAFFIC_LIGHT_INCREASE_TOOLTIP),
				NWidget(WWT_TEXT, Colours::Invalid, WID_TL_MIN_GREEN), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
				NWidget(WWT_PUSHARROWBTN, Colours::Grey, WID_TL_OFFSET_DECREASE), SetArrowWidgetTypeTip(ArrowWidgetType::Decrease, STR_TRAFFIC_LIGHT_OFFSET_DECREASE_TOOLTIP),
				NWidget(WWT_PUSHARROWBTN, Colours::Grey, WID_TL_OFFSET_INCREASE), SetArrowWidgetTypeTip(ArrowWidgetType::Increase, STR_TRAFFIC_LIGHT_OFFSET_INCREASE_TOOLTIP),
				NWidget(WWT_TEXT, Colours::Invalid, WID_TL_OFFSET), SetFill(1, 0),
			EndContainer(),
			NWidget(WWT_TEXT, Colours::Invalid, WID_TL_STATUS), SetFill(1, 0),
		EndContainer(),
	EndContainer(),
};

/** Window description of the traffic light window. */
static WindowDesc _traffic_light_desc(
	WindowPosition::Automatic, "traffic_light", 0, 0,
	WindowClass::TrafficLight, WindowClass::None,
	{},
	_nested_traffic_light_widgets
);

/**
 * Open the window to change how the traffic lights of a junction switch.
 * @param tile The junction tile.
 */
void ShowTrafficLightWindow(TileIndex tile)
{
	if (!IsNormalRoadTile(tile) || !HasTrafficLight(tile)) return;
	AllocateWindowDescFront<TrafficLightWindow>(_traffic_light_desc, tile.base());
}
