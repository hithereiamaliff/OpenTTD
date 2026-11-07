/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file line_gui.cpp The line window and line badges. */

#include "stdafx.h"
#include "line.h"
#include "brt.h"
#include "city_clock.h"
#include "satisfaction.h"
#include "line_cmd.h"
#include "metro.h"
#include "command_func.h"
#include "company_func.h"
#include "core/string_consumer.hpp"
#include "dropdown_common_type.h"
#include "dropdown_func.h"
#include "fare.h"
#include "fare_gui.h"
#include "transit_authority.h"
#include "gfx_func.h"
#include "gui.h"
#include "order_base.h"
#include "palette_func.h"
#include "settings_type.h"
#include "station_base.h"
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
std::string FormatLineDuration(TimerGameTick::Ticks ticks)
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

		const Vehicle *first = list->GetFirstSharedVehicle();
		FarePolicy fare = GetVehicleFare(first);
		bool own = line.fare_scheme != FareScheme::Inherit || line.fare_price != 0;
		StringID whose = own ? STR_LINE_FARE_OWN : STR_LINE_FARE_COMPANYS;
		if (const TransitContract *contract = GetLineContract(list); contract != nullptr) {
			lines.push_back(GetString(STR_LINE_CONTRACT, contract->town, contract->type == TransitContractType::FreeLine ? STR_LINE_CONTRACT_FREE : STR_LINE_CONTRACT_GROSS, contract->fee, contract->end));
		} else if (fare.scheme == FareScheme::Free) {
			lines.push_back(GetString(STR_LINE_FARE_FREE, whose));
		} else {
			lines.push_back(GetString(STR_LINE_FARE, GetFareSchemeName(fare.scheme), fare.price, GetFareExample(fare, 20, false), whose));
		}

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

		/* How satisfied its passengers are. */
		if (line.satisfaction != SATISFACTION_UNKNOWN) {
			const auto &p = line.satisfaction_parts;
			lines.push_back(GetString(STR_LINE_SATISFACTION, line.satisfaction));
			lines.push_back(GetString(STR_LINE_SATISFACTION_PARTS, p[to_underlying(SatisfactionPart::Waiting)], p[to_underlying(SatisfactionPart::Crowding)],
					p[to_underlying(SatisfactionPart::Punctuality)], p[to_underlying(SatisfactionPart::Fare)], p[to_underlying(SatisfactionPart::Condition)], p[to_underlying(SatisfactionPart::Comfort)]));
		} else {
			lines.push_back(GetString(IsPassengerLine(list) ? STR_LINE_SATISFACTION_UNKNOWN : STR_LINE_SATISFACTION_NO_PASSENGERS));
			lines.emplace_back();
		}

		/* The BRT score of bus lines. */
		if (IsBusLine(list)) {
			const BrtScore brt = GetLineBrtScore(list);
			if (brt.known) {
				static constexpr StringID GRADES[] = {STR_LINE_BRT_GRADE_NONE, STR_LINE_BRT_GRADE_BASIC, STR_LINE_BRT_GRADE_BRONZE, STR_LINE_BRT_GRADE_SILVER, STR_LINE_BRT_GRADE_GOLD};
				const StringID grade = GRADES[to_underlying(brt.grade)];
				if (brt.penalty > 0) {
					lines.push_back(GetString(STR_LINE_BRT_SCORE_PENALTY, brt.score, grade, brt.penalty));
				} else {
					lines.push_back(GetString(STR_LINE_BRT_SCORE, brt.score, grade));
				}
				if (brt.crossings == 0) {
					lines.push_back(GetString(STR_LINE_BRT_PARTS_NO_JUNCTIONS, brt.busway, brt.lanes, brt.shared, brt.gated));
				} else {
					lines.push_back(GetString(STR_LINE_BRT_PARTS, brt.busway, brt.lanes, brt.shared, brt.gated, brt.signals * 100 / brt.crossings));
				}
			} else {
				lines.push_back(GetString(STR_LINE_BRT_UNKNOWN));
				lines.emplace_back();
			}
		} else {
			lines.emplace_back();
			lines.emplace_back();
		}

		/* When it runs: all day, or fewer vehicles between the rush hours; and whether on demand. */
		std::string service;
		if (line.offpeak_vehicles == UINT8_MAX) {
			service = GetString(STR_LINE_SERVICE_ALL_DAY);
		} else if (!IsCityClockOn()) {
			service = GetString(STR_LINE_SERVICE_CLOCK_OFF);
		} else if (!CanLineParkBetweenPeaks(list)) {
			service = GetString(STR_LINE_SERVICE_TOO_SHORT);
		} else if (line.offpeak_vehicles == 0) {
			service = GetString(STR_LINE_SERVICE_PEAK_ONLY, CountOffPeakParked(list));
		} else {
			service = GetString(STR_LINE_SERVICE_OFFPEAK, line.offpeak_vehicles, CountOffPeakParked(list));
		}
		lines.push_back(GetString(line.on_demand ? STR_LINE_SERVICE_ON_DEMAND : STR_LINE_SERVICE, service));

		/* Trains: with or without drivers, and what running without them needs. */
		if (IsTrainLine(list)) {
			if (line.driverless) {
				lines.push_back(GetString(STR_LINE_DRIVERLESS_ON, 100 - DRIVERLESS_RUNNING_COST_PERCENT, DRIVERLESS_BOARDING_PERCENT - 100));
			} else {
				const StringID problem = GetDriverlessProblem(list);
				uint stops = 0;
				const uint doors = GetLineStopsWithScreenDoors(list, &stops);
				if (problem == INVALID_STRING_ID) {
					lines.push_back(GetString(STR_LINE_DRIVERLESS_READY, GetDriverlessFittingCost(list)));
				} else if (problem == STR_ERROR_DRIVERLESS_NOT_ELECTRIC) {
					lines.push_back(GetString(STR_LINE_DRIVERLESS_NEEDS, GetString(STR_LINE_DRIVERLESS_NEED_ELECTRIC)));
				} else if (problem == STR_ERROR_DRIVERLESS_NO_SCREEN_DOORS) {
					lines.push_back(GetString(STR_LINE_DRIVERLESS_NEEDS, GetString(STR_LINE_DRIVERLESS_NEED_DOORS, doors, stops)));
				} else {
					lines.push_back(GetString(STR_LINE_DRIVERLESS_NEEDS, GetString(STR_LINE_DRIVERLESS_NEED_NO_CROSSINGS, line.this_month.crossings + line.last_month.crossings)));
				}
			}
		} else {
			lines.emplace_back();
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
	static constexpr uint INFO_LINES = 15;

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_LN_INFO) return;
		size.height = WidgetDimensions::scaled.framerect.Vertical() + GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_wide + INFO_LINES * GetCharacterHeight(FontSize::Normal);
		size.width = std::max(size.width, static_cast<uint>(ScaleGUITrad(440)));
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_LN_OFFPEAK) {
			const OrderList *list = this->GetList();
			const uint8_t keep = list == nullptr ? UINT8_MAX : list->line.offpeak_vehicles;
			if (keep == UINT8_MAX) return GetString(STR_LINE_OFFPEAK_BUTTON_ALL);
			if (keep == 0) return GetString(STR_LINE_OFFPEAK_BUTTON_PEAK_ONLY);
			return GetString(STR_LINE_OFFPEAK_BUTTON_KEEP, keep);
		}
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

			case WID_LN_FARE:
				ShowDropDownList(this, MakeFareSchemeDropDown(true), to_underlying(list->line.fare_scheme), WID_LN_FARE);
				break;

			case WID_LN_FARE_PRICE:
				ShowDropDownList(this, MakeFarePriceDropDown(true), list->line.fare_price, WID_LN_FARE_PRICE);
				break;

			case WID_LN_OFFPEAK: {
				/* All vehicles, one fewer than the line has down to one, or rush hours only. */
				const uint8_t keep = list->line.offpeak_vehicles;
				const uint most = std::max<uint>(std::min<uint>(list->GetNumVehicles(), 100), keep == UINT8_MAX ? 0 : keep + 1);
				DropDownList dlist;
				dlist.push_back(MakeDropDownListStringItem(STR_LINE_OFFPEAK_ALL, UINT8_MAX));
				for (uint n = most - (most > 0 ? 1 : 0); n >= 1; n--) {
					dlist.push_back(MakeDropDownListStringItem(GetString(STR_LINE_OFFPEAK_KEEP, n), n));
				}
				dlist.push_back(MakeDropDownListStringItem(STR_LINE_OFFPEAK_PEAK_ONLY, 0));
				ShowDropDownList(this, std::move(dlist), keep, WID_LN_OFFPEAK);
				break;
			}

			case WID_LN_ON_DEMAND:
				Command<Commands::SetLineProperty>::Post(STR_ERROR_CAN_T_CHANGE_LINE, v->index, LineProperty::OnDemand, list->line.on_demand ? 0 : 1, {});
				break;

			case WID_LN_DRIVERLESS:
				Command<Commands::SetLineProperty>::Post(list->line.driverless ? STR_ERROR_CAN_T_CHANGE_LINE : STR_ERROR_CAN_T_RUN_DRIVERLESS, v->index, LineProperty::Driverless, list->line.driverless ? 0 : 1, {});
				break;

			case WID_LN_MAP:
				ShowLineMapWindow(v);
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
		const OrderList *list = this->GetList();
		if (list == nullptr) return;
		LineProperty property;
		switch (widget) {
			case WID_LN_COLOUR: property = LineProperty::Colour; break;
			case WID_LN_FARE: property = LineProperty::FareScheme; break;
			case WID_LN_FARE_PRICE: property = LineProperty::FarePrice; break;
			case WID_LN_OFFPEAK: property = LineProperty::OffPeakVehicles; break;
			default: return;
		}
		Command<Commands::SetLineProperty>::Post(STR_ERROR_CAN_T_CHANGE_LINE, list->GetFirstSharedVehicle()->index, property, static_cast<uint32_t>(index), {});
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
		this->SetWidgetsDisabledState(!mine, WID_LN_NUMBER, WID_LN_NAME, WID_LN_COLOUR, WID_LN_TARGET, WID_LN_OFFPEAK, WID_LN_ON_DEMAND);
		this->SetWidgetLoweredState(WID_LN_ON_DEMAND, list->line.on_demand);
		this->SetWidgetDisabledState(WID_LN_DRIVERLESS, !mine || !IsTrainLine(list));
		this->SetWidgetLoweredState(WID_LN_DRIVERLESS, list->line.driverless);
		this->SetWidgetsDisabledState(!mine || GetLineContract(list) != nullptr, WID_LN_FARE, WID_LN_FARE_PRICE);
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
		NWidget(WWT_DROPDOWN, Colours::Grey, WID_LN_FARE), SetFill(1, 0), SetStringTip(STR_LINE_FARE_BUTTON, STR_LINE_FARE_TOOLTIP),
		NWidget(WWT_DROPDOWN, Colours::Grey, WID_LN_FARE_PRICE), SetFill(1, 0), SetStringTip(STR_LINE_FARE_PRICE_BUTTON, STR_LINE_FARE_PRICE_TOOLTIP),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_DROPDOWN, Colours::Grey, WID_LN_OFFPEAK), SetFill(1, 0), SetStringTip(STR_LINE_OFFPEAK_BUTTON_ALL, STR_LINE_OFFPEAK_TOOLTIP),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_LN_ON_DEMAND), SetFill(1, 0), SetStringTip(STR_LINE_ON_DEMAND_BUTTON, STR_LINE_ON_DEMAND_TOOLTIP),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_LN_DRIVERLESS), SetFill(1, 0), SetStringTip(STR_LINE_DRIVERLESS_BUTTON, STR_LINE_DRIVERLESS_TOOLTIP),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_LN_VEHICLES), SetFill(1, 0), SetStringTip(STR_LINE_VEHICLES_BUTTON, STR_LINE_VEHICLES_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_LN_ORDERS), SetFill(1, 0), SetStringTip(STR_LINE_ORDERS_BUTTON, STR_LINE_ORDERS_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_LN_TIMETABLE), SetFill(1, 0), SetStringTip(STR_LINE_TIMETABLE_BUTTON, STR_LINE_TIMETABLE_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_LN_MAP), SetFill(1, 0), SetStringTip(STR_LINE_MAP_BUTTON, STR_LINE_MAP_TOOLTIP),
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

/** One expected departure from a station. */
struct Departure {
	TimerGameTick::Ticks when; ///< Ticks from now until it leaves.
	VehicleID vehicle; ///< The vehicle.
	VehicleOrderID order; ///< The order it leaves from.
	std::array<StationID, 2> next{StationID::Invalid(), StationID::Invalid()}; ///< Its next two stops.
};

/**
 * Does a vehicle stop at a station for this order?
 * @param order The order.
 * @param station The station.
 * @return True if it is a station order for this station that isn't "go via".
 */
static bool StopsAt(const Order *order, StationID station)
{
	return order->IsType(OT_GOTO_STATION) && order->GetDestination() == station && !order->GetNonStopType().Test(OrderNonStopFlag::GoVia);
}

/**
 * The next station stops of an order list after an order.
 * @param list The order list.
 * @param from The order after which to look.
 * @param here The station to skip.
 * @return Up to two stations.
 */
static std::array<StationID, 2> GetNextStops(const OrderList *list, VehicleOrderID from, StationID here)
{
	std::array<StationID, 2> next{StationID::Invalid(), StationID::Invalid()};
	uint found = 0;
	VehicleOrderID idx = from;
	for (uint steps = 0; steps < list->GetNumOrders() && found < next.size(); steps++) {
		idx = list->GetNext(idx);
		const Order *order = list->GetOrderAt(idx);
		if (order == nullptr || !order->IsType(OT_GOTO_STATION) || order->GetNonStopType().Test(OrderNonStopFlag::GoVia)) continue;
		StationID st = order->GetDestination().ToStationID();
		if (st == here || (found > 0 && st == next[found - 1])) continue;
		next[found++] = st;
	}
	return next;
}

/**
 * When will a vehicle next leave a station? Worked out from where it is on
 * its orders and the timetabled or estimated times of those orders.
 * @param v The vehicle.
 * @param station The station.
 * @return The departure, if the vehicle stops there.
 */
static std::optional<Departure> GetNextDeparture(const Vehicle *v, StationID station)
{
	const OrderList *list = v->orders;
	VehicleOrderID num = list->GetNumOrders();
	if (num == 0 || v->cur_real_order_index >= num) return std::nullopt;
	if (std::ranges::none_of(list->GetOrders(), [station](const Order &order) { return StopsAt(&order, station); })) return std::nullopt;

	VehicleOrderID idx = v->cur_real_order_index;
	const Order *order = list->GetOrderAt(idx);
	TimerGameTick::Ticks time;
	if (v->current_order.IsType(OT_LOADING) && v->current_order.GetNonStopType().Any()) {
		/* Loading at the stop of its current order (a stop that isn't in its
		 * orders doesn't count): it leaves when its usual stay there is over. */
		time = std::max<TimerGameTick::Ticks>(0, order->GetWaitTime() - v->current_order_time);
		if (StopsAt(order, station)) return Departure{time, v->index, idx, GetNextStops(list, idx, station)};
		idx = list->GetNext(idx);
		order = list->GetOrderAt(idx);
		time += order->GetTravelTime();
	} else {
		time = std::max<TimerGameTick::Ticks>(0, order->GetTravelTime() - v->current_order_time);
	}

	for (uint steps = 0; steps < num; steps++) {
		if (StopsAt(order, station)) return Departure{time + order->GetWaitTime(), v->index, idx, GetNextStops(list, idx, station)};
		time += order->GetWaitTime();
		idx = list->GetNext(idx);
		order = list->GetOrderAt(idx);
		time += order->GetTravelTime();
	}
	return std::nullopt;
}

/**
 * Work out when vehicles will next leave a station.
 * @param station The station.
 * @return The departures, soonest first.
 */
static std::vector<Departure> GetDepartures(StationID station)
{
	std::vector<Departure> departures;
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!v->IsPrimaryVehicle() || v->orders == nullptr || v->vehstatus.Any({VehState::Stopped, VehState::Crashed})) continue;
		if (auto departure = GetNextDeparture(v, station); departure.has_value()) departures.push_back(*departure);
	}
	auto soonest = [](const Departure &a, const Departure &b) { return a.when != b.when ? a.when < b.when : a.vehicle < b.vehicle; };
	std::ranges::sort(departures, soonest);

	/* Lines that hold vehicles at their first stop for even spacing let one go
	 * only every target gap. */
	std::vector<const OrderList *> held;
	for (const Departure &d : departures) {
		const OrderList *list = Vehicle::Get(d.vehicle)->orders;
		if (list->line.hold_for_spacing && list->line.target_gap > 0 && d.order == GetLineFirstStop(list) && std::ranges::find(held, list) == held.end()) held.push_back(list);
	}
	if (held.empty()) return departures;
	for (const OrderList *list : held) {
		TimerGameTick::Ticks earliest = list->line.last_departure == 0 ? 0 :
				static_cast<TimerGameTick::Ticks>(list->line.last_departure + list->line.target_gap - TimerGameTick::counter);
		for (Departure &d : departures) {
			if (Vehicle::Get(d.vehicle)->orders != list || d.order != GetLineFirstStop(list)) continue;
			d.when = std::max(d.when, earliest);
			earliest = d.when + list->line.target_gap;
		}
	}
	std::ranges::sort(departures, soonest);
	return departures;
}

/** Departure board of a station: the next vehicles to leave, line by line. */
struct DeparturesWindow : Window {
	static constexpr uint ROWS = 14; ///< How many departures to show.
	std::vector<Departure> departures; ///< What is shown.

	DeparturesWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->departures = GetDepartures(StationID(window_number));
		this->InitNested(window_number);
	}

	int RowHeight() const
	{
		return std::max(GetCharacterHeight(FontSize::Normal), GetCharacterHeight(FontSize::Small) + ScaleGUITrad(1)) + WidgetDimensions::scaled.vsep_normal;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_DEP_LIST) return;
		size.height = WidgetDimensions::scaled.framerect.Vertical() + ROWS * this->RowHeight();
		size.width = std::max(size.width, static_cast<uint>(ScaleGUITrad(420)));
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_DEP_CAPTION) return GetString(STR_DEPARTURES_CAPTION, StationID(this->window_number));
		return this->Window::GetWidgetString(widget, stringid);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_DEP_LIST) return;
		bool rtl = _current_text_dir == TD_RTL;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		if (this->departures.empty()) {
			DrawString(ir, STR_DEPARTURES_NONE);
			return;
		}

		/* Size the badge and vehicle columns to what is shown. */
		int badge_width = 0;
		int vehicle_width = 0;
		uint counted = 0;
		for (const Departure &d : this->departures) {
			if (counted++ == ROWS) break;
			const Vehicle *v = Vehicle::GetIfValid(d.vehicle);
			if (v == nullptr) continue;
			badge_width = std::max<int>(badge_width, GetLineBadgeWidth(v->orders));
			vehicle_width = std::max<int>(vehicle_width, GetStringBoundingBox(GetString(STR_DEPARTURES_VEHICLE, v->index)).width);
		}
		int time_width = GetStringBoundingBox(GetString(STR_DEPARTURES_IN, FormatLineDuration(99 * TicksPerDisplayUnit() + TicksPerDisplayUnit() * 9 / 10))).width;
		vehicle_width = std::min(vehicle_width, static_cast<int>(ir.Width()) / 3);
		int row = this->RowHeight();
		uint shown = 0;
		for (const Departure &d : this->departures) {
			if (shown++ == ROWS) break;
			Rect line = ir.WithHeight(row);
			ir.top += row;
			const Vehicle *v = Vehicle::GetIfValid(d.vehicle);
			if (v == nullptr) continue;

			int text_top = line.top + (row - GetCharacterHeight(FontSize::Normal)) / 2;
			DrawLineBadge(line.WithWidth(badge_width, rtl).WithY(line.top + (row - GetCharacterHeight(FontSize::Small)) / 2, line.bottom), v->orders, rtl);

			Rect rest = line.Indent(badge_width + WidgetDimensions::scaled.hsep_wide, rtl);
			Rect time = rest.WithWidth(time_width, !rtl);
			Rect vehicle = rest.Indent(time_width + WidgetDimensions::scaled.hsep_wide, !rtl).WithWidth(vehicle_width, !rtl);
			Rect to = rest.Indent(time_width + vehicle_width + 2 * WidgetDimensions::scaled.hsep_wide, !rtl);

			std::string where;
			if (d.next[0] == StationID::Invalid()) {
				where = GetString(STR_DEPARTURES_TERMINATES);
			} else if (d.next[1] == StationID::Invalid()) {
				where = GetString(STR_DEPARTURES_TO, d.next[0]);
			} else {
				where = GetString(STR_DEPARTURES_TO_THEN, d.next[0], d.next[1]);
			}
			if (to.left < to.right) DrawString(to.left, to.right, text_top, where);
			DrawString(vehicle.left, vehicle.right, text_top, GetString(STR_DEPARTURES_VEHICLE, v->index));
			DrawString(time.left, time.right, text_top, d.when < Ticks::DAY_TICKS / 2 ? GetString(STR_DEPARTURES_NOW) : GetString(STR_DEPARTURES_IN, FormatLineDuration(d.when)), TextColour::FromString, AlignmentH::End);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_DEP_LIST) return;
		int index = (pt.y - this->GetWidget<NWidgetBase>(WID_DEP_LIST)->pos_y - WidgetDimensions::scaled.framerect.top) / this->RowHeight();
		if (index < 0 || static_cast<size_t>(index) >= std::min<size_t>(this->departures.size(), ROWS)) return;
		const Vehicle *v = Vehicle::GetIfValid(this->departures[index].vehicle);
		if (v == nullptr) return;
		if (_ctrl_pressed) {
			ShowLineWindow(v);
		} else {
			ShowVehicleViewWindow(v);
		}
	}

	/** Refresh the board every second. */
	const IntervalTimer<TimerWindow> refresh_interval = {std::chrono::seconds(1), [this](auto) {
		if (!Station::IsValidID(this->window_number)) {
			this->Close();
			return;
		}
		this->departures = GetDepartures(StationID(this->window_number));
		this->SetWidgetDirty(WID_DEP_LIST);
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_departures_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_DEP_CAPTION), SetToolTip(STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_DEP_LIST), SetFill(1, 0), SetToolTip(STR_DEPARTURES_LIST_TOOLTIP), EndContainer(),
};

static WindowDesc _departures_desc(
	WindowPosition::Automatic, "view_departures", 0, 0,
	WindowClass::Departures, WindowClass::None,
	{},
	_nested_departures_widgets
);

/**
 * Open the departure board of a station.
 * @param station The station.
 */
void ShowDeparturesWindow(StationID station)
{
	AllocateWindowDescFront<DeparturesWindow>(_departures_desc, station.base());
}
