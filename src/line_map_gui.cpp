/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file line_map_gui.cpp The line map: a line's stops from top to bottom, like
 * the map above the doors of a metro train, with its vehicles where they are,
 * the lines to change to at each stop, and how crowded the platforms are.
 */

#include "stdafx.h"
#include "line.h"
#include "metro.h"
#include "satisfaction.h"
#include "gfx_func.h"
#include "order_base.h"
#include "palette_func.h"
#include "station_base.h"
#include "station_func.h"
#include "strings_func.h"
#include "transit_authority.h"
#include "timer/timer.h"
#include "timer/timer_window.h"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"

#include "widgets/line_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** A stop of a line, as the map shows it. */
struct LineMapStop {
	StationID station; ///< The station.
	std::vector<const OrderList *> transfers; ///< Other passenger lines that stop there.
};

/** A vehicle of a line, where the map shows it. */
struct LineMapVehicle {
	VehicleID vehicle; ///< The vehicle.
	int from; ///< Row of the stop it left, or stands at.
	int to; ///< Row of the stop it heads for; the same as from at a stop.
	int progress; ///< How far between the two, 0 to 100.
	bool up; ///< It is on its way back up the map.
};

/** A line's stops and vehicles, laid out for the map. */
struct LineMapLayout {
	std::vector<LineMapStop> stops; ///< The rows, top to bottom.
	bool loop = false; ///< The vehicles run on from the last stop back to the first (drawn as one more row).
	std::vector<LineMapVehicle> vehicles; ///< The vehicles on their way.

	/**
	 * Rows the map needs.
	 * @return The number.
	 */
	int Rows() const
	{
		return static_cast<int>(this->stops.size()) + (this->loop ? 1 : 0);
	}
};

/**
 * Lay out a line for its map. A line that goes out and comes back the same
 * way (stops A B C D C B) shows each stop once, the vehicles on their way
 * back on the other side of the line; any other line is a loop.
 * @param list The line.
 * @return The layout.
 */
static LineMapLayout GetLineMapLayout(const OrderList *list)
{
	LineMapLayout layout;

	/* The stops in the order of the orders, without "go via". */
	std::vector<std::pair<VehicleOrderID, StationID>> seq;
	VehicleOrderID index = 0;
	for (const Order &order : list->GetOrders()) {
		if (order.IsType(OT_GOTO_STATION) && !order.GetNonStopType().Test(OrderNonStopFlag::GoVia)) seq.emplace_back(index, order.GetDestination().ToStationID());
		index++;
	}
	const int n = static_cast<int>(seq.size());
	if (n == 0) return layout;

	bool mirrored = n >= 2 && n % 2 == 0 && seq[0].second != seq[n / 2].second;
	for (int i = 1; mirrored && i < n / 2; i++) mirrored = seq[i].second == seq[n - i].second;
	const int rows = mirrored ? n / 2 + 1 : n;
	layout.loop = !mirrored && n >= 2;
	auto row_of = [mirrored, n](int p) { return (mirrored && p > n / 2) ? n - p : p; };

	for (int r = 0; r < rows; r++) layout.stops.push_back({seq[r].second, {}});

	/* Other passenger lines at the same stations, to change to. */
	for (const OrderList *other : OrderList::Iterate()) {
		if (other == list || other->GetFirstSharedVehicle() == nullptr || !IsPassengerLine(other)) continue;
		for (LineMapStop &stop : layout.stops) {
			const StationID station = stop.station;
			if (std::ranges::any_of(other->GetOrders(), [station](const Order &o) { return o.IsType(OT_GOTO_STATION) && o.GetDestination() == station && !o.GetNonStopType().Test(OrderNonStopFlag::GoVia); })) {
				stop.transfers.push_back(other);
			}
		}
	}
	for (LineMapStop &stop : layout.stops) {
		std::ranges::sort(stop.transfers, [](const OrderList *a, const OrderList *b) {
			if (a->line.number != b->line.number) return a->line.number < b->line.number;
			return a->index < b->index;
		});
	}

	/* The vehicles: at the stop they load at, or between the last and the next. */
	for (const Vehicle *v = list->GetFirstSharedVehicle(); v != nullptr; v = v->NextShared()) {
		if (v->vehstatus.Test(VehState::Crashed) || v->IsInDepot()) continue;
		int p = 0;
		for (int i = 0; i < n; i++) {
			if (seq[i].first >= v->cur_real_order_index) {
				p = i;
				break;
			}
		}
		const int prev = (p + n - 1) % n;
		LineMapVehicle mv{v->index, row_of(prev), row_of(p), 0, false};
		if (layout.loop && p == 0) mv.to = rows; // On its way round to the first stop again.
		if (mirrored) mv.up = (p == 0) || p > n / 2;

		if (v->current_order.IsType(OT_LOADING) && v->last_station_visited == seq[p].second) {
			mv.from = mv.to = row_of(p);
		} else {
			const Order *order = list->GetOrderAt(v->cur_real_order_index);
			const TimerGameTick::Ticks travel = order == nullptr ? 0 : order->GetTravelTime();
			mv.progress = travel > 0 ? std::min<int>(95, static_cast<int>(static_cast<int64_t>(v->current_order_time) * 100 / travel)) : 50;
		}
		layout.vehicles.push_back(mv);
	}
	return layout;
}

/** The map of a line: its stops, the lines to change to, crowding, and its vehicles on their way. */
struct LineMapWindow : Window {
	Scrollbar *vscroll = nullptr; ///< Scrollbar of the map.
	LineMapLayout layout; ///< What is shown.
	mutable std::vector<std::pair<Rect, VehicleID>> vehicle_marks; ///< Where the vehicles were drawn, for clicks.

	LineMapWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_LM_SCROLLBAR);
		this->FinishInitNested(window_number);
		const OrderList *list = this->GetList();
		if (list != nullptr) this->owner = list->GetFirstSharedVehicle()->owner;
		this->Refresh();
	}

	/**
	 * The order list of the line.
	 * @return The list, or nullptr if it is gone.
	 */
	const OrderList *GetList() const
	{
		const OrderList *list = OrderList::GetIfValid(this->window_number);
		if (list == nullptr || list->GetFirstSharedVehicle() == nullptr) return nullptr;
		return list;
	}

	/** Lay the map out again; close when the line is gone. */
	void Refresh()
	{
		const OrderList *list = this->GetList();
		if (list == nullptr) {
			this->Close();
			return;
		}
		this->layout = GetLineMapLayout(list);
		this->vscroll->SetCount(std::max(1, this->layout.Rows()));
		this->SetDirty();
	}

	/**
	 * Height of a row: the stop's name, and a small line about it.
	 * @return The height.
	 */
	int RowHeight() const
	{
		return GetCharacterHeight(FontSize::Normal) + GetCharacterHeight(FontSize::Small) + WidgetDimensions::scaled.vsep_wide * 2;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_LM_SUMMARY:
				size.height = WidgetDimensions::scaled.framerect.Vertical() + 2 * GetCharacterHeight(FontSize::Normal);
				size.width = std::max<uint>(size.width, ScaleGUITrad(380));
				break;

			case WID_LM_MAP:
				/* At least three stops; it opens with room for about eight (the default size, see _line_map_desc). */
				resize.height = this->RowHeight();
				size.height = WidgetDimensions::scaled.framerect.Vertical() + 3 * this->RowHeight();
				fill.height = resize.height;
				break;
		}
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_LM_CAPTION) {
			const OrderList *list = this->GetList();
			return list == nullptr ? std::string{} : GetString(STR_LINE_MAP_CAPTION, GetTransitLineDescription(list));
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	/**
	 * Draw the summary: the badge and name, and how the line runs.
	 * @param r Where.
	 * @param list The line.
	 */
	void DrawSummary(const Rect &r, const OrderList *list) const
	{
		const bool rtl = _current_text_dir == TD_RTL;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		const int badge = DrawLineBadge(ir.WithHeight(GetCharacterHeight(FontSize::Normal)), list, rtl);
		Rect name = ir.Indent(badge > 0 ? badge + WidgetDimensions::scaled.hsep_wide : 0, rtl);
		if (!list->line.name.empty()) DrawString(name, GetString(STR_LINE_NAME_LINE, list->line.name));
		ir.top += GetCharacterHeight(FontSize::Normal);

		const uint32_t gap = GetLineAverageGap(list->line);
		std::string how;
		if (list->line.driverless) {
			how = GetString(STR_LINE_MAP_DRIVERLESS);
		} else if (list->line.on_demand) {
			how = GetString(STR_LINE_MAP_ON_DEMAND);
		}
		if (gap > 0) {
			DrawString(ir, GetString(STR_LINE_MAP_SUMMARY, list->GetNumVehicles(), FormatLineDuration(gap), how));
		} else {
			DrawString(ir, GetString(STR_LINE_MAP_SUMMARY_NO_GAP, list->GetNumVehicles(), how));
		}
	}

	/**
	 * Draw a vehicle: a small arrow pointing the way it goes.
	 * @param x Middle.
	 * @param y Middle.
	 * @param up Pointing up.
	 * @param colour Fill colour.
	 * @return Where it was drawn.
	 */
	Rect DrawVehicleMark(int x, int y, bool up, PixelColour colour) const
	{
		const int w = ScaleGUITrad(4);
		const int h = ScaleGUITrad(5);
		std::array<Point, 3> shape;
		if (up) {
			shape = {Point{x - w, y + h}, Point{x + w, y + h}, Point{x, y - h}};
		} else {
			shape = {Point{x - w, y - h}, Point{x + w, y - h}, Point{x, y + h}};
		}
		GfxFillPolygon(shape, colour);
		return {x - w - 1, y - h - 1, x + w + 1, y + h + 1};
	}

	/**
	 * Draw the map.
	 * @param r Where.
	 * @param list The line.
	 */
	void DrawMap(const Rect &r, const OrderList *list) const
	{
		this->vehicle_marks.clear();
		const bool rtl = _current_text_dir == TD_RTL;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		if (this->layout.stops.empty()) {
			DrawString(ir, STR_LINE_MAP_NO_STOPS);
			return;
		}

		const int row = this->RowHeight();
		const int first = this->vscroll->GetPosition();
		const int lane = ScaleGUITrad(12); // Room for the vehicles on each side of the line.
		const int bar = ScaleGUITrad(6); // Width of the line.
		const int bar_left = rtl ? ir.right - lane - bar + 1 : ir.left + lane;
		const int bar_mid = bar_left + bar / 2;
		const int down_x = rtl ? bar_left + bar + lane / 2 : bar_left - lane / 2;
		const int up_x = rtl ? bar_left - lane / 2 : bar_left + bar + lane / 2;
		Rect text = ir.Indent(2 * lane + bar + WidgetDimensions::scaled.hsep_wide, rtl);

		/* Middle of the name line of a row, on the screen. */
		auto row_y = [&](int r) { return ir.top + (r - first) * row + WidgetDimensions::scaled.vsep_wide + GetCharacterHeight(FontSize::Normal) / 2; };

		const Colours colour = GetLineColour(list);
		const PixelColour line_colour = GetColourGradient(colour, Shade::Normal);
		const PixelColour line_dark = GetColourGradient(colour, Shade::Darkest);
		const int rows = this->layout.Rows();
		const int last_stop = static_cast<int>(this->layout.stops.size()) - 1;

		/* The line, from the first stop to the last (and round again). */
		const int top = std::max(row_y(0), ir.top - row);
		const int bottom = std::min(row_y(rows - 1), ir.bottom + row);
		if (top < bottom) {
			const int solid_bottom = this->layout.loop ? std::min(bottom, row_y(last_stop)) : bottom;
			if (top < solid_bottom) GfxFillRect(bar_left, top, bar_left + bar - 1, solid_bottom, line_colour);
			if (this->layout.loop && solid_bottom < bottom) {
				/* The way round to the first stop, dashed. */
				for (int y = solid_bottom; y < bottom; y += ScaleGUITrad(6)) GfxFillRect(bar_left, y, bar_left + bar - 1, std::min(bottom, y + ScaleGUITrad(3)), line_colour);
			}
		}

		/* The stops. */
		for (int r = first; r < rows && (r - first) * row < ir.Height(); r++) {
			const int y = row_y(r);
			const int name_top = y - GetCharacterHeight(FontSize::Normal) / 2;
			if (r > last_stop) {
				/* The loop back to the first stop. */
				DrawString(text.left, text.right, name_top, GetString(STR_LINE_MAP_BACK_TO, this->layout.stops[0].station), TextColour::Grey);
				continue;
			}
			const LineMapStop &stop = this->layout.stops[r];
			const Station *st = Station::GetIfValid(stop.station);
			if (st == nullptr) continue;

			/* A tick across the line, or a ring where other lines stop too. */
			const int half = ScaleGUITrad(stop.transfers.empty() ? 3 : 5);
			Rect mark{bar_mid - half - (stop.transfers.empty() ? ScaleGUITrad(2) : 0), y - half, bar_mid + half + (stop.transfers.empty() ? ScaleGUITrad(2) : 0), y + half};
			if (!stop.transfers.empty()) {
				GfxFillRect(mark, PC_BLACK);
				GfxFillRect(mark.Shrink(ScaleGUITrad(1)), PC_WHITE);
			} else {
				GfxFillRect(mark.left, y - ScaleGUITrad(1), mark.right, y + ScaleGUITrad(1), PC_WHITE);
			}

			/* The name, then the lines to change to. */
			/* Measure the name rather than use what DrawString returns: that is 0 when the name lies outside the part of the
			 * window being redrawn. */
			const std::string name = GetString(STR_LINE_MAP_STOP, stop.station);
			DrawString(text.left, text.right, name_top, name, TextColour::Black);
			const int name_width = std::min<int>(GetStringBoundingBox(name).width, text.Width());
			int name_end = rtl ? text.right - name_width + 1 : text.left + name_width - 1;
			int x = rtl ? name_end - WidgetDimensions::scaled.hsep_wide : name_end + WidgetDimensions::scaled.hsep_wide;
			uint shown = 0;
			for (const OrderList *other : stop.transfers) {
				if (shown == 8) break;
				const int width = GetLineBadgeWidth(other);
				const int box = width > 0 ? width : ScaleGUITrad(8);
				if (rtl ? x - box < text.left : x + box > text.right) break;
				Rect where{rtl ? x - box + 1 : x, y - GetCharacterHeight(FontSize::Small) / 2, rtl ? x : x + box - 1, y + GetCharacterHeight(FontSize::Small) / 2};
				if (width > 0) {
					DrawLineBadge(where, other, rtl);
				} else {
					GfxFillRect(where.left, where.top + ScaleGUITrad(1), where.right, where.bottom - ScaleGUITrad(1), GetColourGradient(GetLineColour(other), Shade::Normal));
				}
				x += rtl ? -(box + ScaleGUITrad(2)) : box + ScaleGUITrad(2);
				shown++;
			}

			/* Waiting passengers, and how full the railway platforms are. */
			const uint waiting = GetStationPassengersWaiting(st);
			const uint places = GetStationPlatformPlaces(st);
			std::string detail;
			if (places > 0) {
				const uint percent = waiting * 100 / places;
				StringID str = percent > PLATFORM_CROWDED ? STR_LINE_MAP_STOP_CROWDED : STR_LINE_MAP_STOP_PLATFORMS;
				detail = GetString(str, waiting, percent, st->screen_doors ? STR_LINE_MAP_STOP_DOORS : STR_EMPTY);
			} else {
				detail = GetString(STR_LINE_MAP_STOP_WAITING, waiting);
			}
			DrawString(text.left, text.right, name_top + GetCharacterHeight(FontSize::Normal), detail, TextColour::FromString, AlignmentH::Start, false, FontSize::Small);
		}

		/* The vehicles, beside the line on their way down, on the other side on their way back. */
		for (const LineMapVehicle &mv : this->layout.vehicles) {
			const int from_y = row_y(mv.from);
			const int to_y = row_y(mv.to);
			const int y = from_y + (to_y - from_y) * mv.progress / 100;
			if (y < ir.top || y > ir.bottom) continue;
			const bool up = mv.up;
			const int x = (up && !this->layout.loop) ? up_x : down_x;
			Rect where = this->DrawVehicleMark(x, y, up, line_dark);
			this->vehicle_marks.emplace_back(where, mv.vehicle);
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		const OrderList *list = this->GetList();
		if (list == nullptr) return;
		switch (widget) {
			case WID_LM_SUMMARY: this->DrawSummary(r, list); break;
			case WID_LM_MAP: this->DrawMap(r, list); break;
		}
	}

	void OnClick(Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_LM_MAP) return;

		/* A vehicle: open it, or its line with Ctrl. */
		for (const auto &[where, id] : this->vehicle_marks) {
			if (!where.Contains(pt)) continue;
			const Vehicle *v = Vehicle::GetIfValid(id);
			if (v == nullptr) return;
			if (_ctrl_pressed) {
				ShowLineWindow(v);
			} else {
				ShowVehicleViewWindow(v);
			}
			return;
		}

		/* A stop: show it, or open its window with Ctrl. */
		const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_LM_MAP);
		const int r = (pt.y - nwid->pos_y - WidgetDimensions::scaled.framerect.top) / this->RowHeight() + this->vscroll->GetPosition();
		if (r < 0 || r >= static_cast<int>(this->layout.stops.size())) return;
		const Station *st = Station::GetIfValid(this->layout.stops[r].station);
		if (st == nullptr) return;
		if (_ctrl_pressed) {
			ShowStationViewWindow(st->index);
		} else {
			ScrollMainWindowToTile(st->xy);
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_LM_MAP, WidgetDimensions::scaled.framerect.Vertical());
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->Refresh();
	}

	/** The vehicles move: refresh twice a second. */
	const IntervalTimer<TimerWindow> refresh_interval = {std::chrono::milliseconds(500), [this](auto) {
		this->Refresh();
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_line_map_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_LM_CAPTION), SetToolTip(STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_DEFSIZEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_LM_SUMMARY), SetFill(1, 0), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_LM_MAP), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_LM_SCROLLBAR), SetToolTip(STR_LINE_MAP_PANEL_TOOLTIP), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_LM_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, Colours::Grey),
		EndContainer(),
	EndContainer(),
};

/** The line map opens 294 pixels high at 1x, room for eight stops with the default fonts; it can be made as short as three. */
static WindowDesc _line_map_desc(
	WindowPosition::Automatic, "view_line_map", 0, 294,
	WindowClass::LineMap, WindowClass::None,
	{},
	_nested_line_map_widgets
);

/**
 * Open the map of a vehicle's line.
 * @param v The vehicle.
 */
void ShowLineMapWindow(const Vehicle *v)
{
	if (v == nullptr || v->orders == nullptr) return;
	AllocateWindowDescFront<LineMapWindow>(_line_map_desc, v->orders->index.base());
}
