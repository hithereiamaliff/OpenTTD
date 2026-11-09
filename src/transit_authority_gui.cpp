/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file transit_authority_gui.cpp The window of a town's transit authority. */

#include "stdafx.h"
#include "transit_authority.h"
#include "road_pricing.h"
#include "road_pricing_cmd.h"
#include "company_base.h"
#include "transit_authority_cmd.h"
#include "command_func.h"
#include "company_func.h"
#include "dropdown_func.h"
#include "dropdown_type.h"
#include "federal.h"
#include "line.h"
#include "order_base.h"
#include "station_base.h"
#include "strings_func.h"
#include "town.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"

#include "widgets/transit_authority_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** Why tenders and contracts ended, in words. */
static constexpr StringID TRANSIT_END_REASONS[] = {
	STR_TRANSIT_END_EXPIRED, STR_TRANSIT_END_LOST, STR_TRANSIT_END_NO_BID, STR_TRANSIT_END_EXPIRED, STR_TRANSIT_END_REVIEW,
	STR_TRANSIT_END_BY_COMPANY, STR_TRANSIT_END_NO_SERVICE, STR_TRANSIT_END_LINE_GONE, STR_TRANSIT_END_STATION_GONE, STR_TRANSIT_END_NO_TAKER,
};
static_assert(std::size(TRANSIT_END_REASONS) == to_underlying(TransitContractEnd::End));

/**
 * A duration in days, with tenths.
 * @param ticks The duration.
 * @return The text.
 */
static std::string FormatTransitDays(uint32_t ticks)
{
	return GetString(STR_LINE_DURATION_DAYS, static_cast<int64_t>(ticks) * 10 / Ticks::DAY_TICKS, 1);
}

/** The window of a town's transit authority: its budget, tenders and contracts. */
struct TransitAuthorityWindow : Window {
	Scrollbar *vscroll = nullptr; ///< Scrollbar of the list.
	uint32_t selected = 0; ///< Number of the selected tender or contract, 0 for none.

	TransitAuthorityWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_TRA_SCROLLBAR);
		this->FinishInitNested(window_number);
		this->OnInvalidateData();
	}

	/**
	 * The town's tenders and contracts: running ones first, then tenders, then ended ones.
	 * @return The list.
	 */
	std::vector<const TransitContract *> GetContracts() const
	{
		std::vector<const TransitContract *> list;
		for (const TransitContract &c : GetTransitContracts()) {
			if (c.town == this->window_number) list.push_back(&c);
		}
		auto order = [](const TransitContract *c) { return c->state == TransitContractState::Active ? 0 : (c->state == TransitContractState::Tender ? 1 : 2); };
		std::stable_sort(list.begin(), list.end(), [&order](const TransitContract *a, const TransitContract *b) { return order(a) < order(b); });
		return list;
	}

	int RowHeight() const
	{
		return GetCharacterHeight(FontSize::Normal) + GetCharacterHeight(FontSize::Small) + WidgetDimensions::scaled.vsep_wide;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_TRA_BUDGET:
				size.height = WidgetDimensions::scaled.framerect.Vertical() + this->GetBudgetLineCount() * GetCharacterHeight(FontSize::Normal);
				/* Wide enough for the longest line; the window widens when a line outgrows it (see OnInvalidateData). */
				size.width = std::max({size.width, static_cast<uint>(ScaleGUITrad(560)), this->GetBudgetWidth()});
				break;

			case WID_TRA_LIST:
				resize.height = this->RowHeight();
				size.height = WidgetDimensions::scaled.framerect.Vertical() + 5 * this->RowHeight();
				fill.height = resize.height;
				break;
		}
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_TRA_CAPTION) {
			const Town *t = Town::GetIfValid(this->window_number);
			return GetString(t != nullptr && HasFederalTransitAgency(t) ? STR_TRANSIT_CAPTION_AGENCY : STR_TRANSIT_CAPTION, this->window_number);
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	/**
	 * The first line of a tender or contract.
	 * @param c It.
	 * @return The text.
	 */
	std::string GetRowTitle(const TransitContract &c) const
	{
		/* A feeder line the town asks for, before anyone runs it. */
		if (c.type == TransitContractType::Feeder && c.line == OrderListID::Invalid()) {
			if (c.state == TransitContractState::Tender) return GetString(STR_TRANSIT_ROW_FEEDER_WANTED, c.station, DistanceManhattan(c.target, Station::IsValidID(c.station) ? Station::Get(c.station)->xy : c.target), c.end);
			return GetString(STR_TRANSIT_ROW_FEEDER_ENDED, c.station, c.end, TRANSIT_END_REASONS[to_underlying(c.ended_why)]);
		}
		std::string line = GetTransitLineDescription(OrderList::GetIfValid(c.line));
		StringID type;
		switch (c.type) {
			case TransitContractType::FreeLine: type = STR_TRANSIT_TYPE_FREE; break;
			case TransitContractType::Feeder: type = STR_TRANSIT_TYPE_FEEDER; break;
			default: type = STR_TRANSIT_TYPE_GROSS_COST; break;
		}
		switch (c.state) {
			case TransitContractState::Tender: return GetString(STR_TRANSIT_ROW_TENDER, line, c.company, type, c.end);
			case TransitContractState::Active: return GetString(STR_TRANSIT_ROW_ACTIVE, line, c.company, type, c.fee, c.end);
			default: return GetString(STR_TRANSIT_ROW_ENDED, line, c.company, c.end, TRANSIT_END_REASONS[to_underlying(c.ended_why)]);
		}
	}

	/**
	 * The second line of a tender or contract.
	 * @param c It.
	 * @return The text.
	 */
	std::string GetRowDetail(const TransitContract &c) const
	{
		if (IsFeederOffer(c)) return GetString(STR_TRANSIT_DETAIL_FEEDER_WANTED, c.cost_basis);
		switch (c.state) {
			case TransitContractState::Tender:
				if (c.bid == UINT8_MAX) return GetString(STR_TRANSIT_DETAIL_TENDER_NO_BID, c.cost_basis);
				return GetString(STR_TRANSIT_DETAIL_TENDER, c.cost_basis, c.bid, c.cost_basis * (100 + c.bid) / 100);

			case TransitContractState::Active: {
				if (c.months == 0) return GetString(STR_TRANSIT_DETAIL_STARTED, c.required_departures, FormatTransitDays(c.baseline_wait));
				std::string on_time = c.last_on_time == UINT8_MAX ? GetString(STR_TRANSIT_NO_TIMETABLE) : GetString(STR_TRANSIT_ON_TIME, c.last_on_time);
				return GetString(STR_TRANSIT_DETAIL_ACTIVE, c.last_departures, c.required_departures, FormatTransitDays(c.last_excess_wait), on_time, c.last_payment, fmt::format("{:+}", static_cast<int>(c.last_kpi)), c.months_met, c.months);
			}

			default:
				if (c.ended_why == TransitContractEnd::LostTender) return GetString(STR_TRANSIT_DETAIL_LOST, c.rival_bid, c.rival_quality, c.bid, c.quality);
				return {};
		}
	}

	/**
	 * The lines above the list: the town's transit budget, fares, congestion charge and fare schemes.
	 * @param t The town.
	 * @return The lines.
	 */
	std::vector<std::string> GetBudgetLines(const Town *t) const
	{
		std::vector<std::string> lines;
		const Money budget = GetTownTransitBudget(t);
		if (budget == 0) {
			lines.push_back(GetString(STR_TRANSIT_BUDGET_NONE));
		} else {
			lines.push_back(GetString(STR_TRANSIT_BUDGET, budget, GetTownTransitCommitted(t), std::max<Money>(0, GetTownTransitAvailable(t))));
		}
		/* Federal money: the agency of a federal territory, the national transit fund. */
		if (HasFederalTransitAgency(t)) {
			lines.push_back(GetString(STR_TRANSIT_FEDERAL_AGENCY, GetFederalAgencyBudgetPercent(t), FEDERAL_AGENCY_TENDER_FACTOR));
		} else if (GetFederalTransitGrantPercent(t) > 0) {
			lines.push_back(GetString(STR_TRANSIT_FEDERAL_FUND, GetFederalTransitGrantPercent(t)));
		}
		lines.push_back(GetString(STR_TRANSIT_FARES, t->transit.fares_last_year, t->transit.fares_this_year));
		if (t->transit.congestion_charge) {
			lines.push_back(GetString(STR_TRANSIT_CONGESTION_CHARGE, t->transit.charge_since, t->transit.charged_last_month, t->transit.charge_income_last_month, t->transit.charge_last_year));
		} else {
			lines.push_back(GetString(STR_TRANSIT_NO_CONGESTION_CHARGE, GetCongestionChargeZoneLevel(t), 1));
		}
		if (!t->transit.policy_set) {
			lines.push_back(GetString(STR_TRANSIT_SCHEMES_UNDECIDED));
			return lines;
		}
		if (t->transit.resident_pass) {
			lines.push_back(GetString(STR_TRANSIT_RESIDENT_PASS, RESIDENT_PASS_SHARE, RESIDENT_PASS_FARES, t->transit.last_pass_holders, t->transit.last_pass_paid));
		} else {
			lines.push_back(GetString(STR_TRANSIT_NO_RESIDENT_PASS));
		}
		if (t->transit.pays_concessions) {
			lines.push_back(GetString(STR_TRANSIT_CONCESSIONS, t->transit.last_concessions_paid));
		} else {
			lines.push_back(GetString(STR_TRANSIT_NO_CONCESSIONS));
		}
		return lines;
	}

	/**
	 * How many lines the panel above the list has room for: five, more with federal money.
	 * @return The lines.
	 */
	uint GetBudgetLineCount() const
	{
		const Town *t = Town::GetIfValid(this->window_number);
		return std::max<uint>(5, t == nullptr ? 0 : static_cast<uint>(this->GetBudgetLines(t).size()));
	}

	/**
	 * Width the lines above the list need for the longest of them now.
	 * @return The width, frame included; 0 without a town.
	 */
	uint GetBudgetWidth() const
	{
		const Town *t = Town::GetIfValid(this->window_number);
		if (t == nullptr) return 0;
		uint width = 0;
		for (const std::string &line : this->GetBudgetLines(t)) width = std::max(width, GetStringBoundingBox(line).width);
		return width + WidgetDimensions::scaled.framerect.Horizontal();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		const Town *t = Town::GetIfValid(this->window_number);
		if (t == nullptr) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);

		switch (widget) {
			case WID_TRA_BUDGET:
				for (const std::string &line : this->GetBudgetLines(t)) {
					DrawString(ir, line);
					ir.top += GetCharacterHeight(FontSize::Normal);
				}
				break;

			case WID_TRA_LIST: {
				std::vector<const TransitContract *> contracts = this->GetContracts();
				if (contracts.empty()) {
					DrawStringMultiLine(ir, STR_TRANSIT_EMPTY);
					break;
				}
				int row = this->RowHeight();
				auto [first, last] = this->vscroll->GetVisibleRangeIterators(contracts);
				for (auto it = first; it != last; ++it) {
					const TransitContract &c = **it;
					Rect line = ir.WithHeight(row);
					if (c.id == this->selected) GfxFillRect(line.Shrink(-WidgetDimensions::scaled.framerect.left, 0), PC_DARK_GREY);
					DrawString(line, this->GetRowTitle(c));
					std::string detail = this->GetRowDetail(c);
					if (!detail.empty()) DrawString(line.left, line.right, line.top + GetCharacterHeight(FontSize::Normal), detail, TextColour::FromString, AlignmentH::Start, false, FontSize::Small);
					ir.top += row;
				}
				break;
			}
		}
	}

	/**
	 * The selected tender or contract.
	 * @return It, or nullptr.
	 */
	const TransitContract *GetSelected() const
	{
		if (this->selected == 0) return nullptr;
		const TransitContract *c = GetTransitContract(this->selected);
		return (c != nullptr && c->town == this->window_number) ? c : nullptr;
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_TRA_LIST: {
				std::vector<const TransitContract *> contracts = this->GetContracts();
				auto it = this->vscroll->GetScrolledItemFromWidget(contracts, pt.y, this, WID_TRA_LIST, WidgetDimensions::scaled.framerect.top);
				this->selected = (it == contracts.end()) ? 0 : (*it)->id;
				this->OnInvalidateData();
				break;
			}

			case WID_TRA_BID: {
				const TransitContract *c = this->GetSelected();
				if (c == nullptr) break;
				DropDownList list;
				for (uint margin = 0; margin <= TRANSIT_BID_MAX; margin += 5) {
					list.push_back(MakeDropDownListStringItem(GetString(STR_TRANSIT_BID_MARGIN, margin, c->cost_basis * (100 + margin) / 100), margin));
				}
				if (c->bid != UINT8_MAX) list.push_back(MakeDropDownListStringItem(STR_TRANSIT_BID_WITHDRAW, UINT8_MAX));
				ShowDropDownList(this, std::move(list), c->bid, WID_TRA_BID);
				break;
			}

			case WID_TRA_END:
				if (const TransitContract *c = this->GetSelected(); c != nullptr) Command<Commands::TransitEndContract>::Post(STR_ERROR_CAN_T_END_CONTRACT, c->id);
				break;

			case WID_TRA_CHARGE:
				Command<Commands::ProposeCongestionCharge>::Post(STR_ERROR_CAN_T_PROPOSE_CONGESTION_CHARGE, static_cast<TownID>(this->window_number));
				break;

			case WID_TRA_LINE:
				if (const TransitContract *c = this->GetSelected(); c != nullptr) {
					/* A wanted feeder line: show the houses it should reach. */
					if (IsFeederOffer(*c)) {
						ScrollMainWindowToTile(c->target);
						break;
					}
					const OrderList *list = OrderList::GetIfValid(c->line);
					if (list != nullptr && list->GetFirstSharedVehicle() != nullptr) ShowLineWindow(list->GetFirstSharedVehicle());
				}
				break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget != WID_TRA_BID) return;
		if (const TransitContract *c = this->GetSelected(); c != nullptr) Command<Commands::TransitBid>::Post(STR_ERROR_CAN_T_BID, c->id, static_cast<uint8_t>(index));
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_TRA_LIST, WidgetDimensions::scaled.framerect.Vertical());
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		if (!Town::IsValidID(this->window_number)) {
			this->Close();
			return;
		}
		this->vscroll->SetCount(this->GetContracts().size());
		const TransitContract *c = this->GetSelected();
		if (c == nullptr) this->selected = 0;
		bool mine = c != nullptr && c->company == _local_company;
		this->SetWidgetDisabledState(WID_TRA_BID, !mine || c->state != TransitContractState::Tender);
		this->SetWidgetDisabledState(WID_TRA_END, !mine || c->state != TransitContractState::Active);
		this->SetWidgetDisabledState(WID_TRA_LINE, c == nullptr || (OrderList::GetIfValid(c->line) == nullptr && !IsFeederOffer(*c)));
		const Town *t = Town::Get(this->window_number);
		this->SetWidgetDisabledState(WID_TRA_CHARGE, !Company::IsValidID(_local_company) || t->transit.congestion_charge);
		/* A line outgrew the panel, the congestion charge's say: widen the window. */
		const NWidgetBase *budget = this->GetWidget<NWidgetBase>(WID_TRA_BUDGET);
		const uint height = WidgetDimensions::scaled.framerect.Vertical() + this->GetBudgetLineCount() * GetCharacterHeight(FontSize::Normal);
		if (this->GetBudgetWidth() > budget->current_x || height != budget->smallest_y) this->ReInit();
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_transit_authority_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown, WID_TRA_CAPTION), SetToolTip(STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_DEFSIZEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_TRA_BUDGET), SetFill(1, 0), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Brown, WID_TRA_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_TRA_SCROLLBAR), SetToolTip(STR_TRANSIT_LIST_TOOLTIP), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Brown, WID_TRA_SCROLLBAR),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			NWidget(WWT_DROPDOWN, Colours::Brown, WID_TRA_BID), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_TRANSIT_BID_BUTTON, STR_TRANSIT_BID_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_TRA_END), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_TRANSIT_END_BUTTON, STR_TRANSIT_END_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_TRA_LINE), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_TRANSIT_LINE_BUTTON, STR_TRANSIT_LINE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_TRA_CHARGE), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_TRANSIT_CHARGE_BUTTON, STR_TRANSIT_CHARGE_TOOLTIP),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, Colours::Brown),
	EndContainer(),
};

static WindowDesc _transit_authority_desc(
	WindowPosition::Automatic, "view_transit_authority", 0, 0,
	WindowClass::TransitAuthority, WindowClass::None,
	{},
	_nested_transit_authority_widgets
);

/**
 * Open the transit authority window of a town.
 * @param town The town.
 */
void ShowTransitAuthorityWindow(TownID town)
{
	if (!Town::IsValidID(town)) return;
	AllocateWindowDescFront<TransitAuthorityWindow>(_transit_authority_desc, town.base());
}
