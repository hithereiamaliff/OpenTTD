/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file town_budget_gui.cpp The window of a town's budget and ordinances. */

#include "stdafx.h"
#include "town_budget.h"
#include "town_budget_cmd.h"
#include "city_services.h"
#include "council.h"
#include "state.h"
#include "council_cmd.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "environment.h"
#include "road_pricing.h"
#include "road_pricing_cmd.h"
#include "strings_func.h"
#include "town.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"

#include "widgets/town_budget_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** Rows of the list: the ordinances, then the congestion charge. */
static constexpr uint TOWN_BUDGET_ROWS = ORDINANCE_COUNT + 1;

/** What each ordinance does, in words; the congestion charge last. */
static constexpr StringID ORDINANCE_EFFECTS[] = {
	STR_ORDINANCE_EFFECT_TRANSIT_FUND, STR_ORDINANCE_EFFECT_PARKING_LIMITS, STR_ORDINANCE_EFFECT_LOW_EMISSION_ZONE,
	STR_ORDINANCE_EFFECT_CAR_FREE_CENTRE, STR_ORDINANCE_EFFECT_TREE_PLANTING, STR_ORDINANCE_EFFECT_TRANSIT_ORIENTED_DEVELOPMENT,
	STR_ORDINANCE_EFFECT_CONGESTION_CHARGE,
};
static_assert(std::size(ORDINANCE_EFFECTS) == TOWN_BUDGET_ROWS);

/** What a town needs for each ordinance, in words; the congestion charge last. */
static constexpr StringID ORDINANCE_NEEDS[] = {
	STR_ORDINANCE_NEEDS_TRANSIT_FUND, STR_ORDINANCE_NEEDS_PARKING_LIMITS, STR_ORDINANCE_NEEDS_LOW_EMISSION_ZONE,
	STR_ORDINANCE_NEEDS_CAR_FREE_CENTRE, STR_ORDINANCE_NEEDS_TREE_PLANTING, STR_ORDINANCE_NEEDS_TRANSIT_ORIENTED_DEVELOPMENT,
	STR_ORDINANCE_NEEDS_CONGESTION_CHARGE,
};
static_assert(std::size(ORDINANCE_NEEDS) == TOWN_BUDGET_ROWS);

/** The window of a town's budget: taxes, spending, reserves, and the ordinances it has or could bring in. */
struct TownBudgetWindow : Window {
	Scrollbar *vscroll = nullptr; ///< Scrollbar of the list.
	int selected = -1; ///< Selected row, -1 for none.
	bool councils = false; ///< Whether the summary has room for the council's lines.

	TownBudgetWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->councils = AreTownCouncilsOn();
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_TB_SCROLLBAR);
		this->FinishInitNested(window_number);
		this->vscroll->SetCount(TOWN_BUDGET_ROWS);
		this->OnInvalidateData();
	}

	int RowHeight() const
	{
		return GetCharacterHeight(FontSize::Normal) + 2 * GetCharacterHeight(FontSize::Small) + WidgetDimensions::scaled.vsep_wide;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_TB_SUMMARY:
				size.height = WidgetDimensions::scaled.framerect.Vertical() + (this->councils ? 11 : 5) * GetCharacterHeight(FontSize::Normal);
				/* Wide enough for the longest line; the window widens when a line outgrows it (see OnInvalidateData). */
				size.width = std::max({size.width, static_cast<uint>(ScaleGUITrad(600)), this->GetSummaryWidth()});
				break;

			case WID_TB_LIST:
				resize.height = this->RowHeight();
				size.height = WidgetDimensions::scaled.framerect.Vertical() + TOWN_BUDGET_ROWS * this->RowHeight();
				fill.height = resize.height;
				break;
		}
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_TB_CAPTION) {
			const Town *t = Town::GetIfValid(this->window_number);
			if (t != nullptr && AreTownCouncilsOn()) return GetString(STR_TOWN_BUDGET_CAPTION_COUNCIL, GetTownCouncilName(t), this->window_number);
			return GetString(STR_TOWN_BUDGET_CAPTION, this->window_number);
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	/**
	 * Is the ordinance (or the congestion charge) of a row in force in the town?
	 * @param t The town.
	 * @param row The row.
	 * @return True if it is.
	 */
	static bool IsInForce(const Town *t, uint row)
	{
		if (row == ORDINANCE_COUNT) return t->transit.congestion_charge;
		return HasOrdinance(t, static_cast<Ordinance>(row));
	}

	/**
	 * Why the town would turn down the local company's proposal of a row.
	 * @param t The town.
	 * @param row The row.
	 * @return The reason, or INVALID_STRING_ID if it would agree.
	 */
	static StringID GetRefusal(const Town *t, uint row)
	{
		if (row == ORDINANCE_COUNT) return GetCongestionChargeRefusal(t, _local_company);
		return GetOrdinanceRefusal(t, static_cast<Ordinance>(row), _local_company);
	}

	/**
	 * The council's lines: its standing, licences and charges, new services, estates and report.
	 * @param lines Where the lines go.
	 * @param t The town.
	 */
	void AddCouncilLines(std::vector<std::string> &lines, const Town *t) const
	{
		const TownCouncil &c = t->council;
		const std::string since = c.since != 0 ? GetString(STR_TOWN_BUDGET_COUNCIL_SINCE, c.since) : std::string{};
		std::string next;
		switch (c.level) {
			case CouncilLevel::District: next = GetString(STR_TOWN_BUDGET_COUNCIL_NEXT_MUNICIPAL, COUNCIL_MUNICIPAL_POPULATION, GetCouncilIncomeNeeded(CouncilLevel::Municipal)); break;
			case CouncilLevel::Municipal: next = GetString(STR_TOWN_BUDGET_COUNCIL_NEXT_CITY, COUNCIL_CITY_POPULATION, GetCouncilIncomeNeeded(CouncilLevel::City)); break;
			default: next = GetString(STR_TOWN_BUDGET_COUNCIL_NEXT_NONE); break;
		}
		if (IsFederalTerritory(t)) next = GetString(STR_TOWN_BUDGET_COUNCIL_NEXT_CITY_HALL);
		/* In Malay the name doesn't say what kind of council it is: say it in English. */
		const std::string kind = AreCouncilNamesEnglish() ? std::string{} : GetString(STR_TOWN_BUDGET_COUNCIL_KIND, GetTownCouncilKind(t));
		lines.push_back(GetString(STR_TOWN_BUDGET_COUNCIL, GetTownCouncilName(t), t->index, kind, since));
		lines.push_back(std::move(next));
		lines.push_back(GetString(STR_TOWN_BUDGET_COUNCIL_INCOME, c.licences_last_year, c.licences_this_year, c.charges_last_year));
		const CityService plan = GetCouncilPlannedService(t);
		if (plan == CityService::End) {
			lines.push_back(GetString(STR_TOWN_BUDGET_COUNCIL_SERVICE_NONE, c.services_built, c.built_last_year));
		} else {
			lines.push_back(GetString(STR_TOWN_BUDGET_COUNCIL_SERVICE, GetCityServiceBuildingName(plan), GetCityServiceBuildCost(plan), GetCouncilSpendable(t), c.services_built));
		}
		const uint32_t flats = GetTownFlatsPopulation(t);
		if (flats == 0) {
			lines.push_back(GetString(STR_TOWN_BUDGET_COUNCIL_ESTATES_NONE));
		} else {
			const std::string renewed = c.renewed != 0 ? GetString(STR_TOWN_BUDGET_COUNCIL_RENEWED, c.renewed) : std::string{};
			lines.push_back(GetString(STR_TOWN_BUDGET_COUNCIL_ESTATES, flats, GetTownFlatsAge(t), c.estate_condition, c.repainted, renewed, c.estates_fund));
		}
		static constexpr StringID BANDS[] = {STR_COUNCIL_BAND_NONE, STR_COUNCIL_BAND_GREEN, STR_COUNCIL_BAND_AMBER, STR_COUNCIL_BAND_RED};
		const auto &report = c.report;
		if (report[to_underlying(ReportItem::Services)] == ReportBand::None) {
			lines.push_back(GetString(STR_TOWN_BUDGET_COUNCIL_REPORT_NONE));
		} else {
			lines.push_back(GetString(STR_TOWN_BUDGET_COUNCIL_REPORT, BANDS[to_underlying(report[0])], BANDS[to_underlying(report[1])], BANDS[to_underlying(report[2])]));
		}
	}

	/**
	 * The lines of the summary: the council's, then taxes, spending, fees, reserves and what they do.
	 * @param t The town.
	 * @return The lines.
	 */
	std::vector<std::string> GetSummaryLines(const Town *t) const
	{
		std::vector<std::string> lines;
		if (this->councils && AreTownCouncilsOn()) this->AddCouncilLines(lines, t);
		const TownBudget &b = t->budget;
		const Money base = GetTownTaxBase(t);
		const uint value = (IsEnvironmentKnown() && t->environment.land_value != 0) ? t->environment.land_value : 50;
		lines.push_back(GetString(STR_TOWN_BUDGET_TAXES, b.tax_rate, value, GetTownYearlyTaxes(t)));
		const Money ordinances = base * (GetTownOrdinanceCostPercent(t) - (HasOrdinance(t, Ordinance::TransitFund) ? GetOrdinanceCostPercent(Ordinance::TransitFund) : 0)) / 100;
		if (AreTownCouncilsOn()) {
			/* A tenth of the running costs goes into the council's services fund. */
			lines.push_back(GetString(STR_TOWN_BUDGET_SPENDING_COUNCIL, base * GetTownTransitSharePercent(t) / 100, GetTownTransitSharePercent(t),
					base * (TOWN_RUNNING_SHARE - COUNCIL_SERVICES_PERCENT) / 100, base * COUNCIL_SERVICES_PERCENT / 100, ordinances));
		} else {
			lines.push_back(GetString(STR_TOWN_BUDGET_SPENDING, base * GetTownTransitSharePercent(t) / 100, GetTownTransitSharePercent(t),
					base * TOWN_RUNNING_SHARE / 100, ordinances));
		}
		lines.push_back(GetString(STR_TOWN_BUDGET_FEES, b.fees_last_year, b.fees_this_year, b.lez_charged_last_month));
		lines.push_back(GetString(b.reserves < 0 ? STR_TOWN_BUDGET_RESERVES_NEGATIVE : STR_TOWN_BUDGET_RESERVES, b.reserves, b.taxes_last_year, b.spent_last_year));
		const int happiness = GetTownBudgetHappiness(t);
		lines.push_back(GetString(STR_TOWN_BUDGET_EFFECTS, fmt::format("{:+}", happiness), GetTownTaxGrowthPercent(t), b.trees_planted));
		return lines;
	}

	/**
	 * Width the summary needs for its longest line now.
	 * @return The width, frame included; 0 without a town or budgets.
	 */
	uint GetSummaryWidth() const
	{
		const Town *t = Town::GetIfValid(this->window_number);
		if (t == nullptr || !AreTownBudgetsOn()) return 0;
		uint width = 0;
		for (const std::string &line : this->GetSummaryLines(t)) width = std::max(width, GetStringBoundingBox(line).width);
		return width + WidgetDimensions::scaled.framerect.Horizontal();
	}

	void DrawSummary(const Rect &r, const Town *t) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		if (!AreTownBudgetsOn()) {
			DrawStringMultiLine(ir, STR_TOWN_BUDGET_OFF);
			return;
		}
		for (const std::string &line : this->GetSummaryLines(t)) {
			DrawString(ir, line);
			ir.top += GetCharacterHeight(FontSize::Normal);
		}
	}

	void DrawList(const Rect &r, const Town *t) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		const int row_height = this->RowHeight();
		const int small = GetCharacterHeight(FontSize::Small);
		for (uint row = this->vscroll->GetPosition(); row < TOWN_BUDGET_ROWS && ir.top < ir.bottom; row++) {
			Rect line = ir.WithHeight(row_height);
			if (static_cast<int>(row) == this->selected) GfxFillRect(line.Shrink(-WidgetDimensions::scaled.framerect.left, 0), PC_DARK_GREY);

			const StringID name = row == ORDINANCE_COUNT ? STR_ORDINANCE_CONGESTION_CHARGE : GetOrdinanceName(static_cast<Ordinance>(row));
			const uint cost = row == ORDINANCE_COUNT ? 0 : GetOrdinanceCostPercent(static_cast<Ordinance>(row));
			std::string title;
			if (IsInForce(t, row)) {
				const TimerGameEconomy::Date since = row == ORDINANCE_COUNT ? t->transit.charge_since : t->budget.since[row];
				title = GetString(STR_TOWN_BUDGET_ROW_IN_FORCE, name, since);
			} else {
				title = GetString(STR_TOWN_BUDGET_ROW_NOT_IN_FORCE, name);
			}
			DrawString(line, title);
			int y = line.top + GetCharacterHeight(FontSize::Normal);
			std::string effect = GetString(cost == 0 ? STR_TOWN_BUDGET_EFFECT_FREE : STR_TOWN_BUDGET_EFFECT_COST, ORDINANCE_EFFECTS[row], cost);
			DrawString(line.left, line.right, y, effect, TextColour::FromString, AlignmentH::Start, false, FontSize::Small);
			y += small;
			if (!IsInForce(t, row)) {
				const bool can = Company::IsValidID(_local_company) && GetRefusal(t, row) == INVALID_STRING_ID;
				DrawString(line.left, line.right, y, GetString(can ? STR_TOWN_BUDGET_NEEDS_CAN : STR_TOWN_BUDGET_NEEDS, ORDINANCE_NEEDS[row]), TextColour::FromString, AlignmentH::Start, false, FontSize::Small);
			}
			ir.top += row_height;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		const Town *t = Town::GetIfValid(this->window_number);
		if (t == nullptr) return;
		switch (widget) {
			case WID_TB_SUMMARY: this->DrawSummary(r, t); break;
			case WID_TB_LIST: this->DrawList(r, t); break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_TB_LIST: {
				const int row = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_TB_LIST, WidgetDimensions::scaled.framerect.top);
				this->selected = (row == INT32_MAX || row < 0 || row >= static_cast<int>(TOWN_BUDGET_ROWS)) ? -1 : row;
				this->OnInvalidateData();
				break;
			}

			case WID_TB_COFUND:
				Command<Commands::CofundCouncilService>::Post(STR_ERROR_CAN_T_COFUND_SERVICE, static_cast<TownID>(this->window_number));
				break;

			case WID_TB_PROPOSE:
				if (this->selected < 0) break;
				if (this->selected == static_cast<int>(ORDINANCE_COUNT)) {
					Command<Commands::ProposeCongestionCharge>::Post(STR_ERROR_CAN_T_PROPOSE_CONGESTION_CHARGE, static_cast<TownID>(this->window_number));
				} else {
					Command<Commands::ProposeOrdinance>::Post(STR_ERROR_CAN_T_PROPOSE_ORDINANCE, static_cast<TownID>(this->window_number), static_cast<Ordinance>(this->selected));
				}
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_TB_LIST, WidgetDimensions::scaled.framerect.Vertical());
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		const Town *t = Town::GetIfValid(this->window_number);
		if (t == nullptr) {
			this->Close();
			return;
		}
		if (this->councils != AreTownCouncilsOn()) {
			this->councils = AreTownCouncilsOn();
			this->ReInit();
		} else if (this->GetSummaryWidth() > this->GetWidget<NWidgetBase>(WID_TB_SUMMARY)->current_x) {
			/* A line outgrew the summary, a growing town's spending say: widen the window. */
			this->ReInit();
		}
		bool disabled = this->selected < 0 || !Company::IsValidID(_local_company) || IsInForce(t, this->selected);
		if (this->selected >= 0 && this->selected < static_cast<int>(ORDINANCE_COUNT) && !AreTownBudgetsOn()) disabled = true;
		this->SetWidgetDisabledState(WID_TB_PROPOSE, disabled);
		this->SetWidgetDisabledState(WID_TB_COFUND, !Company::IsValidID(_local_company) || GetCofundRefusal(t, _local_company) != INVALID_STRING_ID);
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_town_budget_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown, WID_TB_CAPTION), SetToolTip(STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_DEFSIZEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_TB_SUMMARY), SetFill(1, 0), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Brown, WID_TB_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_TB_SCROLLBAR), SetToolTip(STR_TOWN_BUDGET_LIST_TOOLTIP), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Brown, WID_TB_SCROLLBAR),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_TB_PROPOSE), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_TOWN_BUDGET_PROPOSE_BUTTON, STR_TOWN_BUDGET_PROPOSE_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_TB_COFUND), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_TOWN_BUDGET_COFUND_BUTTON, STR_TOWN_BUDGET_COFUND_TOOLTIP),
		NWidget(WWT_RESIZEBOX, Colours::Brown),
	EndContainer(),
};

static WindowDesc _town_budget_desc(
	WindowPosition::Automatic, "view_town_budget", 0, 0,
	WindowClass::TownBudget, WindowClass::None,
	{},
	_nested_town_budget_widgets
);

/**
 * Open the budget window of a town.
 * @param town The town.
 */
void ShowTownBudgetWindow(TownID town)
{
	if (!Town::IsValidID(town)) return;
	AllocateWindowDescFront<TownBudgetWindow>(_town_budget_desc, town.base());
}
