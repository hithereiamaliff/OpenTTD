/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file line_gui.cpp The line window and line badges. */

#include "stdafx.h"
#include "line.h"
#include "line_cmd.h"
#include "command_func.h"
#include "company_func.h"
#include "core/string_consumer.hpp"
#include "dropdown_common_type.h"
#include "dropdown_func.h"
#include "gfx_func.h"
#include "gui.h"
#include "order_base.h"
#include "palette_func.h"
#include "settings_type.h"
#include "strings_func.h"
#include "textbuf_gui.h"
#include "timer/timer.h"
#include "timer/timer_window.h"
#include "timetable.h"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"

#include "widgets/line_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/**
 * Is a colour light enough to need dark text on it?
 * @param colour The colour.
 * @return True for light colours.
 */
static bool IsLightLineColour(Colours colour)
{
	switch (colour) {
		case Colours::PaleGreen:
		case Colours::Pink:
		case Colours::Yellow:
		case Colours::LightBlue:
		case Colours::Cream:
		case Colours::Grey:
		case Colours::White:
			return true;
		default:
			return false;
	}
}

/**
 * Width of a line's badge.
 * @param list The order list.
 * @return The width, 0 if the line has no number.
 */
uint GetLineBadgeWidth(const OrderList *list)
{
	if (list == nullptr || list->line.number.empty()) return 0;
	return GetStringBoundingBox(list->line.number, FontSize::Small).width + 2 * ScaleGUITrad(3);
}

/**
 * Draw a line's badge: its number on its colour.
 * @param r Where to draw; the badge goes at the start of it.
 * @param list The order list.
 * @param rtl Whether to start at the right.
 * @return The width drawn, 0 if the line has no number.
 */
int DrawLineBadge(const Rect &r, const OrderList *list, bool rtl)
{
	uint width = GetLineBadgeWidth(list);
	if (width == 0) return 0;

	int height = GetCharacterHeight(FontSize::Small) + ScaleGUITrad(1);
	Rect box = r.WithWidth(width, rtl).WithHeight(height);
	Colours colour = GetLineColour(list);
	GfxFillRect(box, GetColourGradient(colour, Shade::Dark));
	GfxFillRect(box.Shrink(WidgetDimensions::scaled.bevel), GetColourGradient(colour, Shade::Lighter));
	DrawString(box.left, box.right, box.top + (height - GetCharacterHeight(FontSize::Small)) / 2, list->line.number,
			IsLightLineColour(colour) ? TextColour::Black : TextColour::White, AlignmentH::Centre, false, FontSize::Small);
	return width;
}

/**
 * Ticks in one unit of the timetable display (day, second or tick).
 * @return The number of ticks.
 */
static TimerGameTick::Ticks TicksPerDisplayUnit()
{
	switch (_settings_client.gui.timetable_mode) {
		case TimetableMode::Days: return Ticks::DAY_TICKS;
		case TimetableMode::Seconds: return Ticks::TICKS_PER_SECOND;
		case TimetableMode::Ticks: return 1;
		default: NOT_REACHED();
	}
}

/**
 * A duration as the timetable shows it, with tenths of a day when it shows days.
 * @param ticks The duration.
 * @return The text.
 */
static std::string FormatLineDuration(TimerGameTick::Ticks ticks)
{
	switch (_settings_client.gui.timetable_mode) {
		case TimetableMode::Days: return GetString(STR_LINE_DURATION_DAYS, static_cast<int64_t>(ticks) * 10 / Ticks::DAY_TICKS, 1);
		case TimetableMode::Seconds: return GetString(STR_UNITS_SECONDS, ticks / Ticks::TICKS_PER_SECOND);
		default: return GetString(STR_UNITS_TICKS, ticks);
	}
}

/** Window showing a line: its number, name, colour, frequency and statistics. */
struct LineWindow : Window {
	WidgetID query_widget = INVALID_WIDGET; ///< Which button opened the text query.

	LineWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		const Vehicle *v = this->GetList()->GetFirstSharedVehicle();
		this->owner = v->owner;
		this->OnInvalidateData();
	}

	/**
	 * The order list of the line.
	 * @return The list, or nullptr if it is gone.
	 */
	OrderList *GetList() const
	{
		OrderList *list = OrderList::GetIfValid(this->window_number);
		if (list == nullptr || list->GetNumVehicles() == 0 || list->GetFirstSharedVehicle() == nullptr) return nullptr;
		return list;
	}

	/**
	 * The lines of text in the info panel.
	 * @param list The order list.
	 * @return The lines.
	 */
	std::vector<std::string> GetInfoLines(const OrderList *list) const
	{
		std::vector<std::string> lines;
		const LineInfo &line = list->line;

		TimerGameTick::Ticks round_trip = list->GetTotalDuration();
		if (round_trip > 0) {
			lines.push_back(GetString(STR_LINE_VEHICLES_ROUND_TRIP, list->GetNumVehicles(), FormatLineDuration(round_trip)));
		} else {
			lines.push_back(GetString(STR_LINE_VEHICLES_ROUND_TRIP_UNKNOWN, list->GetNumVehicles()));
		}

		uint32_t gap = GetLineAverageGap(line);
		if (gap > 0) {
			lines.push_back(GetString(STR_LINE_GAP, FormatLineDuration(gap)));
			uint32_t wait = GetLineAverageWait(line);
			uint32_t even_wait = (line.target_gap > 0 ? static_cast<uint32_t>(line.target_gap) : gap) / 2;
			lines.push_back(GetString(STR_LINE_WAIT, FormatLineDuration(wait), FormatLineDuration(wait > even_wait ? wait - even_wait : 0)));
		} else {
			lines.push_back(GetString(STR_LINE_GAP_UNKNOWN));
			lines.push_back(std::string{});
		}

		if (line.target_gap > 0) {
			uint needed = GetLineVehiclesNeeded(list);
			if (needed > 0) {
				lines.push_back(GetString(STR_LINE_TARGET, FormatLineDuration(line.target_gap), needed));
			} else {
				lines.push_back(GetString(STR_LINE_TARGET_NEED_UNKNOWN, FormatLineDuration(line.target_gap)));
			}
		} else {
			lines.push_back(GetString(STR_LINE_NO_TARGET));
		}

		lines.push_back(GetString(STR_LINE_RIDERS, line.last_month.riders, line.this_month.riders));

		if (line.last_month.load_count > 0) {
			lines.push_back(GetString(STR_LINE_LOAD, line.last_month.load_sum / line.last_month.load_count, line.last_month.max_load));
		} else {
			lines.push_back(GetString(STR_LINE_LOAD_UNKNOWN));
		}

		if (line.last_month.timed > 0) {
			lines.push_back(GetString(STR_LINE_ON_TIME, line.last_month.on_time * 100 / line.last_month.timed, line.last_month.timed));
		} else {
			lines.push_back(GetString(STR_LINE_ON_TIME_UNKNOWN));
		}

		Money profit_this_year = 0, profit_last_year = 0;
		for (const Vehicle *v = list->GetFirstSharedVehicle(); v != nullptr; v = v->NextShared()) {
			profit_this_year += v->GetDisplayProfitThisYear();
			profit_last_year += v->GetDisplayProfitLastYear();
		}
		lines.push_back(GetString(STR_LINE_PROFIT, profit_this_year, profit_last_year));
		return lines;
	}

	/** Number of lines in the info panel, below the badge row. */
	static constexpr uint INFO_LINES = 8;

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_LN_INFO) return;
		size.height = WidgetDimensions::scaled.framerect.Vertical() + GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_wide + INFO_LINES * GetCharacterHeight(FontSize::Normal);
		size.width = std::max(size.width, static_cast<uint>(ScaleGUITrad(380)));
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget != WID_LN_CAPTION) return this->Window::GetWidgetString(widget, stringid);

		const OrderList *list = this->GetList();
		if (list == nullptr) return {};
		const LineInfo &line = list->line;
		if (!line.number.empty() && !line.name.empty()) return GetString(STR_LINE_CAPTION_NUMBER_NAME, line.number, line.name);
		if (!line.number.empty()) return GetString(STR_LINE_CAPTION_NUMBER, line.number);
		if (!line.name.empty()) return GetString(STR_LINE_CAPTION_NAME, line.name);
		return GetString(STR_LINE_CAPTION_UNNAMED, list->GetFirstSharedVehicle()->index);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_LN_INFO) return;
		const OrderList *list = this->GetList();
		if (list == nullptr) return;

		bool rtl = _current_text_dir == TD_RTL;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);

		/* The badge and the name, or a hint to give the line a number. */
		int badge = DrawLineBadge(ir.WithHeight(GetCharacterHeight(FontSize::Normal)), list, rtl);
		Rect name = ir.Indent(badge > 0 ? badge + WidgetDimensions::scaled.hsep_wide : 0, rtl);
		if (!list->line.name.empty()) {
			DrawString(name, GetString(STR_LINE_NAME_LINE, list->line.name));
		} else if (list->line.number.empty()) {
			DrawString(name, STR_LINE_NO_NUMBER_HINT);
		}
		ir.top += GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_wide;

		for (const std::string &text : this->GetInfoLines(list)) {
			if (!text.empty()) DrawString(ir, text);
			ir.top += GetCharacterHeight(FontSize::Normal);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		OrderList *list = this->GetList();
		if (list == nullptr) return;
		const Vehicle *v = list->GetFirstSharedVehicle();

		switch (widget) {
			case WID_LN_NUMBER:
				this->query_widget = widget;
				ShowQueryString(list->line.number, STR_LINE_QUERY_NUMBER, MAX_LENGTH_LINE_NUMBER_CHARS + 1, this, CS_ALPHANUMERAL, {QueryStringFlag::EnableDefault, QueryStringFlag::LengthIsInChars});
				break;

			case WID_LN_NAME:
				this->query_widget = widget;
				ShowQueryString(list->line.name, STR_LINE_QUERY_NAME, MAX_LENGTH_LINE_NAME_CHARS + 1, this, CS_ALPHANUMERAL, {QueryStringFlag::EnableDefault, QueryStringFlag::LengthIsInChars});
				break;

			case WID_LN_COLOUR: {
				DropDownList dlist;
				dlist.push_back(MakeDropDownListStringItem(STR_LINE_COLOUR_AUTOMATIC, to_underlying(Colours::Invalid)));
				for (Colours colour = Colours::Begin; colour != Colours::End; ++colour) {
					dlist.push_back(MakeDropDownListIconItem(SPR_VEH_BUS_SIDE_VIEW, GetColourPalette(colour), STR_COLOUR_DARK_BLUE + to_underlying(colour), to_underlying(colour)));
				}
				ShowDropDownList(this, std::move(dlist), to_underlying(list->line.colour), WID_LN_COLOUR);
				break;
			}

			case WID_LN_TARGET: {
				this->query_widget = widget;
				std::string current = list->line.target_gap > 0 ? fmt::format("{}", list->line.target_gap / TicksPerDisplayUnit()) : std::string{};
				StringID caption;
				switch (_settings_client.gui.timetable_mode) {
					case TimetableMode::Days: caption = STR_LINE_QUERY_TARGET_DAYS; break;
					case TimetableMode::Seconds: caption = STR_LINE_QUERY_TARGET_SECONDS; break;
					default: caption = STR_LINE_QUERY_TARGET_TICKS; break;
				}
				ShowQueryString(current, caption, 7, this, CS_NUMERAL, {});
				break;
			}

			case WID_LN_HOLD:
				Command<Commands::SetLineProperty>::Post(STR_ERROR_CAN_T_CHANGE_LINE, v->index, LineProperty::HoldForSpacing, list->line.hold_for_spacing ? 0 : 1, {});
				break;

			case WID_LN_VEHICLES:
				ShowVehicleListWindow(v);
				break;

			case WID_LN_ORDERS:
				ShowOrdersWindow(v);
				break;

			case WID_LN_TIMETABLE:
				ShowTimetableWindow(v);
				break;

			default:
				break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget != WID_LN_COLOUR) return;
		const OrderList *list = this->GetList();
		if (list == nullptr) return;
		Command<Commands::SetLineProperty>::Post(STR_ERROR_CAN_T_CHANGE_LINE, list->GetFirstSharedVehicle()->index, LineProperty::Colour, static_cast<uint32_t>(index), {});
	}

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!str.has_value()) return;
		const OrderList *list = this->GetList();
		if (list == nullptr) return;
		VehicleID veh = list->GetFirstSharedVehicle()->index;

		switch (this->query_widget) {
			case WID_LN_NUMBER:
				Command<Commands::SetLineProperty>::Post(STR_ERROR_CAN_T_CHANGE_LINE, veh, LineProperty::Number, 0, *str);
				break;
			case WID_LN_NAME:
				Command<Commands::SetLineProperty>::Post(STR_ERROR_CAN_T_CHANGE_LINE, veh, LineProperty::Name, 0, *str);
				break;
			case WID_LN_TARGET: {
				uint64_t value = ParseInteger<uint64_t>(*str, 10, true).value_or(0) * TicksPerDisplayUnit();
				Command<Commands::SetLineProperty>::Post(STR_ERROR_CAN_T_CHANGE_LINE, veh, LineProperty::TargetGap, ClampTo<uint32_t>(value), {});
				break;
			}
			default:
				break;
		}
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		const OrderList *list = this->GetList();
		if (list == nullptr) {
			this->Close();
			return;
		}
		bool mine = this->owner == _local_company;
		this->SetWidgetsDisabledState(!mine, WID_LN_NUMBER, WID_LN_NAME, WID_LN_COLOUR, WID_LN_TARGET);
		this->SetWidgetDisabledState(WID_LN_HOLD, !mine || list->line.target_gap <= 0);
		this->SetWidgetLoweredState(WID_LN_HOLD, list->line.hold_for_spacing);
		this->SetDirty();
	}

	/** Refresh the statistics now and then. */
	const IntervalTimer<TimerWindow> refresh_interval = {std::chrono::seconds(1), [this](auto) {
		if (this->GetList() == nullptr) {
			this->Close();
			return;
		}
		this->SetWidgetDirty(WID_LN_INFO);
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_line_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_LN_CAPTION), SetToolTip(STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_LN_INFO), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_LN_NUMBER), SetFill(1, 0), SetStringTip(STR_LINE_NUMBER_BUTTON, STR_LINE_NUMBER_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_LN_NAME), SetFill(1, 0), SetStringTip(STR_LINE_NAME_BUTTON, STR_LINE_NAME_TOOLTIP),
		NWidget(WWT_DROPDOWN, Colours::Grey, WID_LN_COLOUR), SetFill(1, 0), SetStringTip(STR_LINE_COLOUR_BUTTON, STR_LINE_COLOUR_TOOLTIP),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_LN_TARGET), SetFill(1, 0), SetStringTip(STR_LINE_TARGET_BUTTON, STR_LINE_TARGET_TOOLTIP),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_LN_HOLD), SetFill(1, 0), SetStringTip(STR_LINE_HOLD_BUTTON, STR_LINE_HOLD_TOOLTIP),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_LN_VEHICLES), SetFill(1, 0), SetStringTip(STR_LINE_VEHICLES_BUTTON, STR_LINE_VEHICLES_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_LN_ORDERS), SetFill(1, 0), SetStringTip(STR_LINE_ORDERS_BUTTON, STR_LINE_ORDERS_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_LN_TIMETABLE), SetFill(1, 0), SetStringTip(STR_LINE_TIMETABLE_BUTTON, STR_LINE_TIMETABLE_TOOLTIP),
	EndContainer(),
};

static WindowDesc _line_desc(
	WindowPosition::Automatic, "view_line", 0, 0,
	WindowClass::Line, WindowClass::None,
	{},
	_nested_line_widgets
);

/**
 * Open the window of a vehicle's line.
 * @param v The vehicle.
 */
void ShowLineWindow(const Vehicle *v)
{
	if (v == nullptr || v->orders == nullptr) return;
	AllocateWindowDescFront<LineWindow>(_line_desc, v->orders->index.base());
}
