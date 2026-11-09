/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file federal_gui.cpp The federal government window: its national programmes, its five-year plan of projects for companies, and its money. */

#include "stdafx.h"
#include "federal.h"
#include "state.h"
#include "company_base.h"
#include "gfx_func.h"
#include "strings_func.h"
#include "town.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "timer/timer_game_economy.h"
#include "widgets/state_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** Width of the window's text, in unscaled pixels. */
static constexpr uint FEDERAL_TEXT_WIDTH = 600;

/** The federal government window. */
struct FederalPlanWindow : Window {
	Scrollbar *vscroll = nullptr; ///< Scrollbar of the list of projects.
	std::vector<size_t> rows{}; ///< The projects listed, as indices into GetFederalProjects(): the current plan's, then the plan's before.

	FederalPlanWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_FP_SCROLLBAR);
		this->FinishInitNested(window_number);
		this->OnInvalidateData();
	}

	/**
	 * What the window says above the list: the government, its programmes, its plan, the transit agencies and its money.
	 * @return The paragraphs.
	 */
	std::vector<std::string> GetSummary() const
	{
		std::vector<std::string> lines;
		if (!AreStatesFormed()) {
			lines.push_back(GetString(AreStatesOn() ? STR_STATES_NOT_FORMED : STR_STATES_OFF));
			return lines;
		}
		const Federation &f = GetFederation();
		if (f.government == Platform::None) {
			lines.push_back(GetString(STR_FEDERAL_GOVERNMENT_NONE, f.next_election));
		} else if (IsElectionCampaign(INVALID_STATE)) {
			lines.push_back(GetString(STR_FEDERAL_GOVERNMENT_CARETAKER, GetPlatformName(f.government), f.next_election));
		} else {
			lines.push_back(GetString(STR_FEDERAL_GOVERNMENT, GetPlatformName(f.government), f.seats[to_underlying(f.government) - 1], GetTotalSeats(f.seats), f.elected, f.next_election));
		}
		lines.push_back(GetString(STR_FEDERAL_PROGRAMMES, GetFederalProgrammeText(f.government)));
		if (f.plan == 0) {
			lines.push_back(GetString(STR_FEDERAL_PLAN_NONE));
		} else {
			const uint delivered = CountFederalProjects(FederalProjectState::Delivered);
			const uint open = CountFederalProjects(FederalProjectState::Open);
			const uint lapsed = CountFederalProjects(FederalProjectState::Lapsed);
			lines.push_back(GetString(STR_FEDERAL_PLAN, f.plan, f.plan_year, delivered + open + lapsed, delivered, open, FEDERAL_PROJECT_VOTES, FEDERAL_PROJECT_VOTES_MAX));
		}
		bool territories = false;
		for (const State &s : GetStates()) {
			if (s.kind == StateKind::FederalTerritory && Town::IsValidID(s.capital)) territories = true;
		}
		if (territories) lines.push_back(GetString(STR_FEDERAL_AGENCIES, FEDERAL_AGENCY_BUDGET_PERCENT, FEDERAL_AGENCY_TENDER_FACTOR));
		lines.push_back(GetString(STR_FEDERAL_MONEY, f.grants_last_year, f.programmes_last_year, f.projects_last_year, f.grants_this_year + f.programmes_this_year + f.projects_this_year));
		return lines;
	}

	/**
	 * Height of the paragraphs above the list, at the window's narrowest.
	 * @return The height, without the frame.
	 */
	int GetSummaryHeight() const
	{
		const int width = ScaleGUITrad(FEDERAL_TEXT_WIDTH) - WidgetDimensions::scaled.framerect.Horizontal();
		int height = 0;
		for (const std::string &line : this->GetSummary()) height += GetStringHeight(line, width) + WidgetDimensions::scaled.vsep_normal;
		return height;
	}

	/**
	 * Height of a project in the list: what it is and how it stands, and what a company must do.
	 * @return The height.
	 */
	static int GetRowHeight()
	{
		return 2 * GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_wide;
	}

	/**
	 * The second line of a project: what it needs, while it is open; which plan it was in otherwise.
	 * @param p The project.
	 * @return The line.
	 */
	static std::string GetNeedText(const FederalProject &p)
	{
		if (p.state != FederalProjectState::Open) return GetString(STR_FEDERAL_ROW_PLAN, p.plan, p.offered);
		switch (p.type) {
			case FederalProjectType::MetroLine: return GetString(GetFederalProjectNeed(p.type), FEDERAL_METRO_STATIONS, FEDERAL_LINE_DEPARTURES);
			case FederalProjectType::BrtLine: return GetString(GetFederalProjectNeed(p.type), FEDERAL_BRT_STOPS, FEDERAL_LINE_DEPARTURES);
			default: return GetString(GetFederalProjectNeed(p.type));
		}
	}

	/**
	 * The first line of a project: what it is, and how it stands.
	 * @param p The project.
	 * @return The line.
	 */
	static std::string GetStatusText(const FederalProject &p)
	{
		switch (p.state) {
			case FederalProjectState::Open: return GetString(STR_FEDERAL_ROW_OPEN, GetFederalProjectName(p), p.payment);
			case FederalProjectState::Delivered:
				if (Company::IsValidID(p.company)) return GetString(STR_FEDERAL_ROW_DELIVERED, GetFederalProjectName(p), p.company, p.done, p.payment);
				return GetString(STR_FEDERAL_ROW_DELIVERED_GONE, GetFederalProjectName(p), p.done, p.payment);
			default: return GetString(STR_FEDERAL_ROW_LAPSED, GetFederalProjectName(p), p.done);
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_FP_SUMMARY:
				size.width = std::max<uint>(size.width, ScaleGUITrad(FEDERAL_TEXT_WIDTH));
				size.height = WidgetDimensions::scaled.framerect.Vertical() + this->GetSummaryHeight();
				break;

			case WID_FP_LIST:
				resize.height = GetRowHeight();
				fill.height = resize.height;
				size.height = WidgetDimensions::scaled.framerect.Vertical() + 4 * resize.height;
				break;
		}
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_FP_CAPTION) return GetString(STR_FEDERAL_CAPTION);
		return this->Window::GetWidgetString(widget, stringid);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		switch (widget) {
			case WID_FP_SUMMARY:
				for (const std::string &line : this->GetSummary()) ir.top = DrawStringMultiLine(ir, line) + WidgetDimensions::scaled.vsep_normal;
				break;

			case WID_FP_LIST: {
				if (this->rows.empty()) {
					DrawStringMultiLine(ir, AreStatesFormed() && GetFederation().plan > 0 ? STR_FEDERAL_LIST_EMPTY : STR_FEDERAL_LIST_NO_PLAN);
					break;
				}
				const int line = GetCharacterHeight(FontSize::Normal);
				const std::vector<FederalProject> &projects = GetFederalProjects();
				auto [first, last] = this->vscroll->GetVisibleRangeIterators(this->rows);
				for (auto it = first; it != last; ++it) {
					if (*it >= projects.size()) break;
					const FederalProject &p = projects[*it];
					DrawString(ir.WithHeight(line), this->GetStatusText(p));
					DrawString(ir.Shrink(WidgetDimensions::scaled.hsep_indent, line, 0, 0).WithHeight(line), this->GetNeedText(p), TextColour::Black);
					ir.top += GetRowHeight();
				}
				break;
			}
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_FP_LIST) return;
		auto it = this->vscroll->GetScrolledItemFromWidget(this->rows, pt.y, this, WID_FP_LIST, WidgetDimensions::scaled.framerect.top);
		if (it == this->rows.end() || *it >= GetFederalProjects().size()) return;
		const Town *t = Town::GetIfValid(GetFederalProjects()[*it].town);
		if (t != nullptr) ScrollMainWindowToTile(t->xy);
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_FP_LIST, WidgetDimensions::scaled.framerect.Vertical());
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		/* The current plan's projects first, open before delivered before lapsed; then the plan's before. */
		this->rows.clear();
		const uint8_t plan = GetFederation().plan;
		const std::vector<FederalProject> &projects = GetFederalProjects();
		for (size_t i = 0; i < projects.size(); i++) this->rows.push_back(i);
		std::ranges::stable_sort(this->rows, [plan, &projects](size_t a, size_t b) {
			const bool ca = projects[a].plan == plan, cb = projects[b].plan == plan;
			if (ca != cb) return ca;
			return projects[a].state < projects[b].state;
		});
		this->vscroll->SetCount(this->rows.size());
		/* Fit the paragraphs above the list; a list made taller stays so. */
		const NWidgetBase *summary = this->GetWidget<NWidgetBase>(WID_FP_SUMMARY);
		const int wanted = WidgetDimensions::scaled.framerect.Vertical() + this->GetSummaryHeight();
		const int now = static_cast<int>(summary->smallest_y);
		if (wanted != now) {
			this->ReInit(0, wanted - now);
		} else {
			this->SetDirty();
		}
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_federal_plan_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown, WID_FP_CAPTION), SetToolTip(STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_DEFSIZEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_FP_SUMMARY), SetFill(1, 0), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Brown, WID_FP_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_FP_SCROLLBAR), SetToolTip(STR_FEDERAL_LIST_TOOLTIP), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, Colours::Brown, WID_FP_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, Colours::Brown),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _federal_plan_desc(
	WindowPosition::Automatic, "federal_plan", 0, 0,
	WindowClass::FederalPlan, WindowClass::None,
	{},
	_nested_federal_plan_widgets
);

/** Open the federal government window. */
void ShowFederalPlanWindow()
{
	AllocateWindowDescFront<FederalPlanWindow>(_federal_plan_desc, 0);
}
