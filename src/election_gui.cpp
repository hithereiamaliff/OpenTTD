/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file election_gui.cpp The election night window: the count of an election, the towns declaring their seats one
 * by one (the smallest first, the big cities last), each platform's seats against a majority, and the result.
 * It opens by itself on polling day; the States window's Last election button opens it again.
 */

#include "stdafx.h"
#include "state.h"
#include "gfx_func.h"
#include "palette_func.h"
#include "strings_func.h"
#include "town.h"
#include "video/video_driver.hpp"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "timer/timer.h"
#include "timer/timer_window.h"
#include "widgets/state_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** The count takes about this long, however many towns vote, in milliseconds. */
static constexpr uint COUNT_MILLISECONDS = 7000;
/** A town declares at most this often, in milliseconds... */
static constexpr uint COUNT_MIN_STEP = 80;
/** ... and at least this often. */
static constexpr uint COUNT_MAX_STEP = 700;
/** The window lists this many of the towns declared last. */
static constexpr uint LATEST_TOWNS = 5;
/** Width of the window's text, in unscaled pixels. */
static constexpr uint ELECTION_TEXT_WIDTH = 560;

/**
 * Colour of a platform's bar.
 * @param p The platform's index among those that stand (0 for Transit First).
 * @return The colour.
 */
static Colours GetPlatformColour(uint p)
{
	static constexpr Colours COLOURS[] = {Colours::Green, Colours::Orange, Colours::Blue};
	static_assert(std::size(COLOURS) == PLATFORM_COUNT);
	return COLOURS[p];
}

/**
 * The seats a town gave, in words, leaving out platforms that won none: "Transit First 2, Roads and Growth 1".
 * @param seats The seats.
 * @return The words.
 */
static std::string GetTownSeatsText(const PlatformSeats &seats)
{
	std::string text;
	for (uint p = 0; p < PLATFORM_COUNT; p++) {
		if (seats[p] == 0) continue;
		if (!text.empty()) text += ", ";
		text += GetString(STR_STATES_SEATS_ITEM, GetPlatformName(static_cast<Platform>(p + 1)), seats[p]);
	}
	return text;
}

/** The election night window. */
struct ElectionNightWindow : Window {
	uint declared = 0; ///< How many towns declared their seats so far.

	ElectionNightWindow(WindowDesc &desc, WindowNumber window_number, bool count) : Window(desc)
	{
		const ElectionResult *r = GetLastElection(static_cast<StateID>(window_number));
		this->declared = (count || r == nullptr) ? 0 : static_cast<uint>(r->towns.size());
		this->InitNested(window_number);
		this->SetPace();
		this->UpdateButton();
	}

	/**
	 * The election shown.
	 * @return It, or nullptr if there is none (any more).
	 */
	const ElectionResult *GetElection() const
	{
		return GetLastElection(static_cast<StateID>(this->window_number));
	}

	/**
	 * Is the count going on?
	 * @return True while towns are left to declare.
	 */
	bool IsCounting() const
	{
		const ElectionResult *r = this->GetElection();
		return r != nullptr && this->declared < r->towns.size();
	}

	/** Declare a town every so often, so that the count takes about #COUNT_MILLISECONDS. */
	void SetPace()
	{
		const ElectionResult *r = this->GetElection();
		const uint towns = (r == nullptr) ? 1 : std::max<uint>(1, static_cast<uint>(r->towns.size()));
		this->count_interval.SetInterval(std::chrono::milliseconds(Clamp(COUNT_MILLISECONDS / towns, COUNT_MIN_STEP, COUNT_MAX_STEP)));
	}

	/** The button skips to the result while counting, and counts again after. */
	void UpdateButton()
	{
		const bool counting = this->IsCounting();
		this->GetWidget<NWidgetCore>(WID_EN_SKIP)->SetStringTip(counting ? STR_ELECTION_SKIP : STR_ELECTION_RECOUNT, counting ? STR_ELECTION_SKIP_TOOLTIP : STR_ELECTION_RECOUNT_TOOLTIP);
		this->SetWidgetDirty(WID_EN_SKIP);
	}

	/**
	 * The seats declared so far.
	 * @param r The election.
	 * @return Each platform's seats.
	 */
	PlatformSeats GetDeclaredSeats(const ElectionResult &r) const
	{
		PlatformSeats seats{};
		for (uint i = 0; i < this->declared && i < r.towns.size(); i++) {
			for (uint p = 0; p < PLATFORM_COUNT; p++) seats[p] += r.towns[i].seats[p];
		}
		return seats;
	}

	/**
	 * The result, once the count is over: who won and forms the government; for a general election, the states.
	 * @param r The election.
	 * @return The lines.
	 */
	static std::vector<std::string> GetResultLines(const ElectionResult &r)
	{
		std::vector<std::string> lines;
		if (r.after == Platform::None) return lines;
		const StringID verb = r.before == r.after ? STR_NEWS_STATE_ELECTION_KEEPS : STR_NEWS_STATE_ELECTION_FORMS;
		const uint won = r.seats[to_underlying(r.after) - 1];
		lines.push_back(GetString(r.state == INVALID_STATE ? STR_ELECTION_WINNER_GENERAL : STR_ELECTION_WINNER_STATE, GetPlatformName(r.after), won, GetTotalSeats(r.seats), verb));
		if (r.assemblies.empty()) return lines;
		lines.push_back(GetString(STR_ELECTION_ASSEMBLIES));
		for (const ElectionAssembly &a : r.assemblies) {
			if (a.after == Platform::None || GetState(a.state) == nullptr) continue;
			lines.push_back(GetString(STR_ELECTION_ASSEMBLY, GetStateName(a.state), GetPlatformName(a.after), a.before == a.after ? STR_NEWS_STATE_ELECTION_KEEPS : STR_NEWS_STATE_ELECTION_FORMS,
					a.seats[to_underlying(a.after) - 1], GetTotalSeats(a.seats)));
		}
		return lines;
	}

	/**
	 * Height of a row with a platform's bar.
	 * @return The height.
	 */
	static int GetBarRowHeight()
	{
		return GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_normal;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_EN_COUNT) return;
		size.width = std::max<uint>(size.width, ScaleGUITrad(ELECTION_TEXT_WIDTH));
		const int width = size.width - WidgetDimensions::scaled.framerect.Horizontal();
		const int line = GetCharacterHeight(FontSize::Normal);
		/* How far the count got, the bars, the majority, the latest towns; then the result, made room for from the start. */
		int height = line + WidgetDimensions::scaled.vsep_wide;
		height += PLATFORM_COUNT * GetBarRowHeight();
		height += line + WidgetDimensions::scaled.vsep_wide;
		height += (1 + LATEST_TOWNS) * line + WidgetDimensions::scaled.vsep_wide;
		if (const ElectionResult *r = this->GetElection(); r != nullptr) {
			for (const std::string &text : GetResultLines(*r)) height += GetStringHeight(text, width);
		}
		size.height = WidgetDimensions::scaled.framerect.Vertical() + height;
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget != WID_EN_CAPTION) return this->Window::GetWidgetString(widget, stringid);
		const ElectionResult *r = this->GetElection();
		if (r == nullptr) return GetString(STR_ELECTION_CAPTION);
		const StringID month = STR_MONTH_JAN + std::min<uint>(r->month, 11);
		if (r->state == INVALID_STATE) return GetString(STR_ELECTION_CAPTION_GENERAL, month, r->year);
		return GetString(STR_ELECTION_CAPTION_STATE, GetStateName(r->state), month, r->year);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_EN_COUNT) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		const ElectionResult *e = this->GetElection();
		if (e == nullptr) {
			DrawStringMultiLine(ir, STR_ELECTION_NONE);
			return;
		}
		const int line = GetCharacterHeight(FontSize::Normal);
		const bool rtl = _current_text_dir == TD_RTL;
		const PlatformSeats seats = this->GetDeclaredSeats(*e);
		const uint total = GetTotalSeats(e->seats);
		const uint towns = static_cast<uint>(e->towns.size());

		/* How far the count got. */
		if (this->IsCounting()) {
			DrawString(ir, GetString(STR_ELECTION_COUNTING, GetTotalSeats(seats), total, this->declared, towns));
		} else {
			DrawString(ir, GetString(STR_ELECTION_COUNTED, total, towns));
		}
		ir.top += line + WidgetDimensions::scaled.vsep_wide;

		/* A bar for each platform, as long as its share of all seats; the black line marks a majority. */
		int label_width = 0;
		for (uint p = 0; p < PLATFORM_COUNT; p++) label_width = std::max<int>(label_width, GetStringBoundingBox(GetPlatformName(static_cast<Platform>(p + 1))).width);
		const int number_width = GetStringBoundingBox(GetString(STR_JUST_INT, std::max<uint>(total, 100))).width;
		const uint majority = total / 2 + 1;
		for (uint p = 0; p < PLATFORM_COUNT; p++) {
			const Rect row = ir.WithHeight(line);
			DrawString(row.WithWidth(label_width, rtl), GetPlatformName(static_cast<Platform>(p + 1)), TextColour::Black);
			DrawString(row.WithWidth(number_width, !rtl), GetString(STR_JUST_INT, seats[p]), TextColour::Orange, AlignmentH::End);
			const Rect bar = row.Indent(label_width + WidgetDimensions::scaled.hsep_wide, rtl).Indent(number_width + WidgetDimensions::scaled.hsep_wide, !rtl).Shrink(0, 2, 0, 2);
			if (bar.Width() > 2 && total > 0) {
				GfxFillRect(bar, PC_DARK_GREY);
				const int filled = static_cast<int>(static_cast<int64_t>(bar.Width()) * seats[p] / total);
				if (filled > 0) GfxFillRect(bar.WithWidth(filled, rtl), GetColourGradient(GetPlatformColour(p), Shade::Lighter));
				const int mark = static_cast<int>(static_cast<int64_t>(bar.Width()) * std::min(majority, total) / total);
				const int x = rtl ? bar.right - mark : bar.left + mark;
				GfxFillRect(x, bar.top - 1, x, bar.bottom + 1, PC_BLACK);
			}
			ir.top += GetBarRowHeight();
		}
		DrawString(ir, GetString(STR_ELECTION_MAJORITY, majority));
		ir.top += line + WidgetDimensions::scaled.vsep_wide;

		/* The towns that declared last, the latest first. */
		DrawString(ir, STR_ELECTION_LATEST);
		ir.top += line;
		for (uint i = 0; i < LATEST_TOWNS; i++) {
			if (i < this->declared && this->declared - 1 - i < towns) {
				const ElectionTown &t = e->towns[this->declared - 1 - i];
				if (Town::IsValidID(t.town)) DrawString(ir, GetString(STR_ELECTION_TOWN, t.town, GetTotalSeats(t.seats), GetTownSeatsText(t.seats)));
			}
			ir.top += line;
		}
		ir.top += WidgetDimensions::scaled.vsep_wide;

		/* The result, once every seat is declared. */
		if (this->IsCounting()) return;
		for (const std::string &text : GetResultLines(*e)) ir.top = DrawStringMultiLine(ir, text);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_EN_SKIP) return;
		const ElectionResult *r = this->GetElection();
		if (r == nullptr) return;
		this->declared = this->IsCounting() ? static_cast<uint>(r->towns.size()) : 0;
		this->UpdateButton();
		this->SetWidgetDirty(WID_EN_COUNT);
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		const ElectionResult *r = this->GetElection();
		if (r == nullptr) {
			this->Close();
			return;
		}
		this->declared = std::min<uint>(this->declared, static_cast<uint>(r->towns.size()));
		this->UpdateButton();
		this->SetDirty();
	}

	/** Declare the next town. */
	IntervalTimer<TimerWindow> count_interval = {std::chrono::milliseconds(COUNT_MAX_STEP), [this](auto) {
		if (!this->IsCounting()) return;
		this->declared++;
		if (!this->IsCounting()) this->UpdateButton();
		this->SetWidgetDirty(WID_EN_COUNT);
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_election_night_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown, WID_EN_CAPTION), SetToolTip(STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_EN_COUNT), SetFill(1, 0), EndContainer(),
	NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_EN_SKIP), SetFill(1, 0), SetStringTip(STR_ELECTION_SKIP, STR_ELECTION_SKIP_TOOLTIP),
};

static WindowDesc _election_night_desc(
	WindowPosition::Automatic, "election_night", 0, 0,
	WindowClass::ElectionNight, WindowClass::None,
	{},
	_nested_election_night_widgets
);

/**
 * Open the election night window of an election.
 * @param id The region, or #INVALID_STATE for the last general election.
 * @param count True to count from the start (polling day), false to show the result at once.
 */
void ShowElectionNightWindow(StateID id, bool count)
{
	if (!VideoDriver::GetInstance()->HasGUI() || GetLastElection(id) == nullptr) return;
	CloseWindowById(WindowClass::ElectionNight, id);
	new ElectionNightWindow(_election_night_desc, id, count);
}
