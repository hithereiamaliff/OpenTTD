/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file state_gui.cpp The states window: the states and federal territories, their governments, voters, money and laws. */

#include "stdafx.h"
#include "state.h"
#include "state_cmd.h"
#include "command_func.h"
#include "gfx_func.h"
#include "network/network.h"
#include "palette_func.h"
#include "smallmap_gui.h"
#include "strings_func.h"
#include "textbuf_gui.h"
#include "town.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "widgets/state_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** Lines of the details of the selected state. */
static constexpr uint STATE_DETAIL_LINES = 15;

/** The states window. */
struct StatesWindow : Window {
	Scrollbar *vscroll = nullptr; ///< Scrollbar of the list.
	int selected = -1; ///< The selected state, -1 for none.
	PlatformSeats federal_poll{}; ///< Parliament if elected today.
	PlatformSeats poll{}; ///< The selected state's assembly if elected today.
	VoterConcerns concerns{}; ///< What the selected state's voters make of things.

	StatesWindow(WindowDesc &desc, WindowNumber window_number, StateID select) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_ST_SCROLLBAR);
		this->FinishInitNested(window_number);
		if (select < GetStates().size()) this->selected = select;
		this->OnInvalidateData();
	}

	void SelectState(StateID select)
	{
		if (select >= GetStates().size()) return;
		this->selected = select;
		this->vscroll->ScrollTowards(select);
		this->OnInvalidateData();
	}

	/**
	 * The seats of each platform, in words: "Transit First 7, Roads and Growth 3, Homes and Services 2".
	 * @param seats The seats.
	 * @return The words.
	 */
	static std::string SeatsText(const PlatformSeats &seats)
	{
		std::string text;
		for (uint p = 0; p < PLATFORM_COUNT; p++) {
			if (!text.empty()) text += ", ";
			text += GetString(STR_STATES_SEATS_ITEM, GetPlatformName(static_cast<Platform>(p + 1)), seats[p]);
		}
		return text;
	}

	static uint TotalSeats(const PlatformSeats &seats)
	{
		uint total = 0;
		for (uint16_t n : seats) total += n;
		return total;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_ST_FEDERATION:
				size.height = WidgetDimensions::scaled.framerect.Vertical() + 3 * GetCharacterHeight(FontSize::Normal);
				size.width = std::max<uint>(size.width, ScaleGUITrad(640));
				break;

			case WID_ST_LIST:
				resize.height = GetCharacterHeight(FontSize::Normal);
				size.height = WidgetDimensions::scaled.framerect.Vertical() + 8 * resize.height;
				fill.height = resize.height;
				break;

			case WID_ST_DETAILS:
				size.height = WidgetDimensions::scaled.framerect.Vertical() + STATE_DETAIL_LINES * GetCharacterHeight(FontSize::Normal);
				break;
		}
	}

	void DrawFederation(const Rect &r) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		if (!AreStatesOn()) {
			DrawStringMultiLine(ir, STR_STATES_OFF);
			return;
		}
		if (!AreStatesFormed()) {
			DrawStringMultiLine(ir, STR_STATES_NOT_FORMED);
			return;
		}
		const Federation &f = GetFederation();
		std::string text;
		if (f.government == Platform::None) {
			text = GetString(STR_STATES_FEDERATION_NONE, f.next_election);
		} else {
			text = GetString(STR_STATES_FEDERATION, GetPlatformName(f.government), f.seats[to_underlying(f.government) - 1], TotalSeats(f.seats), f.elected, f.next_election);
		}
		ir.top = DrawStringMultiLine(ir, text);
		DrawStringMultiLine(ir, GetString(STR_STATES_FEDERATION_POLL, SeatsText(this->federal_poll), f.grants_last_year));
	}

	void DrawList(const Rect &r) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		const int line = GetCharacterHeight(FontSize::Normal);
		const std::vector<State> &states = GetStates();
		const bool rtl = _current_text_dir == TD_RTL;
		const int swatch = line - 2;
		for (size_t i = this->vscroll->GetPosition(); i < states.size() && ir.top + line <= ir.bottom + 1; i++) {
			const State &s = states[i];
			Rect row = ir.WithHeight(line);
			if (static_cast<int>(i) == this->selected) GfxFillRect(row.Shrink(-WidgetDimensions::scaled.framerect.left, 0), PC_DARK_GREY);
			const Rect box = row.WithWidth(swatch, rtl).Shrink(0, 1, 0, 1);
			GfxFillRect(box, PC_BLACK);
			GfxFillRect(box.Shrink(1), GetColourGradient(static_cast<Colours>(s.colour), s.kind == StateKind::FederalTerritory ? Shade::Lightest : Shade::Normal));
			const Rect text = row.Indent(swatch + WidgetDimensions::scaled.hsep_wide, rtl);
			const StateID id = static_cast<StateID>(i);
			std::string name = GetStateName(id);
			if (s.region) name = GetString(STR_STATES_NAME_REGION, name);
			std::string str;
			if (s.kind == StateKind::FederalTerritory) {
				str = GetString(STR_STATES_ROW_TERRITORY, name, s.population, GetPlatformName(GetFederation().government));
			} else if (s.government == Platform::None) {
				str = GetString(STR_STATES_ROW_CARETAKER, name, s.towns, s.population, s.next_election);
			} else {
				str = GetString(STR_STATES_ROW_STATE, name, s.towns, s.population, GetPlatformName(s.government),
						s.seats[to_underlying(s.government) - 1], TotalSeats(s.seats), s.next_election);
			}
			DrawString(text, str);
			ir.top += line;
		}
	}

	void DrawDetails(const Rect &r) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		const State *s = this->selected >= 0 ? GetState(static_cast<StateID>(this->selected)) : nullptr;
		if (s == nullptr || !AreStatesFormed()) {
			DrawStringMultiLine(ir, STR_STATES_SELECT);
			return;
		}
		const StateID id = static_cast<StateID>(this->selected);
		if (!Town::IsValidID(s->capital)) {
			DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_DISSOLVED, GetStateName(id)));
			return;
		}
		const Federation &f = GetFederation();
		const Platform government = s->kind == StateKind::FederalTerritory ? f.government : s->government;
		const StringID month = s->region ? STR_MONTH_SEP : STR_MONTH_MAY;

		/* What it is. */
		if (s->kind == StateKind::FederalTerritory) {
			ir.top = DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_TERRITORY, GetStateName(id), s->population, GetTownCouncilName(Town::Get(s->capital)), s->capital, s->formed));
		} else {
			ir.top = DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_STATE, GetStateName(id), s->towns, s->population, s->capital, s->formed));
		}

		/* Who governs it, and what that does. */
		if (s->kind == StateKind::FederalTerritory) {
			ir.top = DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_FEDERAL, GetPlatformName(government)));
		} else if (government == Platform::None) {
			ir.top = DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_CARETAKER, month, s->next_election));
		} else {
			ir.top = DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_GOVERNMENT, GetPlatformName(government), s->seats[to_underlying(government) - 1], TotalSeats(s->seats),
					SeatsText(s->seats), s->elected, month, s->next_election));
		}
		static constexpr StringID EFFECTS[] = {STR_STATES_EFFECT_NONE, STR_STATES_EFFECT_TRANSIT_FIRST, STR_STATES_EFFECT_ROADS_AND_GROWTH, STR_STATES_EFFECT_HOMES_AND_SERVICES};
		static constexpr uint EFFECT_PERCENT[] = {0, 10, 15, 10};
		static_assert(std::size(EFFECTS) == to_underlying(Platform::End) && std::size(EFFECT_PERCENT) == to_underlying(Platform::End));
		const uint strength = GetPlatformStrength(id);
		const std::string paused = (government != Platform::None && strength == 0) ? GetString(STR_STATES_POLICY_PAUSED) : std::string{};
		ir.top = DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_POLICY, EFFECTS[to_underlying(government)], EFFECT_PERCENT[to_underlying(government)] * strength / 100, paused));

		/* Its voters. */
		const VoterConcerns &c = this->concerns;
		ir.top = DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_VOTERS, c.traffic, c.transit, c.satisfaction, c.air, c.services, c.housing, c.taxes));
		if (s->kind == StateKind::State) ir.top = DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_POLL, SeatsText(this->poll)));

		/* Its money. */
		if (s->kind == StateKind::FederalTerritory) {
			ir.top = DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_MONEY_TERRITORY, s->premiums_last_year, s->spent_last_year));
		} else {
			ir.top = DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_MONEY, s->treasury, strength, s->premiums_last_year, s->levies_last_year, s->grants_last_year, s->spent_last_year));
		}

		/* Its laws. */
		ir.top = DrawStringMultiLine(ir, GetString(STR_STATES_DETAIL_PREMIUM, GetStateLandPremium(id, 50)));
		if (s->region) DrawStringMultiLine(ir, STR_STATES_DETAIL_REGION);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_ST_FEDERATION: this->DrawFederation(r); break;
			case WID_ST_LIST: this->DrawList(r); break;
			case WID_ST_DETAILS: this->DrawDetails(r); break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_ST_LIST: {
				const int row = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_ST_LIST, WidgetDimensions::scaled.framerect.top);
				if (row == INT32_MAX || row < 0 || static_cast<size_t>(row) >= GetStates().size()) break;
				this->selected = row;
				this->OnInvalidateData();
				if (click_count > 1) {
					const State *s = GetState(static_cast<StateID>(row));
					if (s != nullptr && Town::IsValidID(s->capital)) ScrollMainWindowToTile(Town::Get(s->capital)->xy);
				}
				break;
			}

			case WID_ST_RENAME:
				if (this->selected < 0) break;
				ShowQueryString(GetStateName(static_cast<StateID>(this->selected)), STR_STATES_RENAME_QUERY, MAX_LENGTH_STATE_NAME_CHARS, this, CS_ALPHANUMERAL, {QueryStringFlag::EnableDefault, QueryStringFlag::LengthIsInChars});
				break;

			case WID_ST_CAPITAL: {
				const State *s = this->selected >= 0 ? GetState(static_cast<StateID>(this->selected)) : nullptr;
				if (s != nullptr && Town::IsValidID(s->capital)) ScrollMainWindowToTile(Town::Get(s->capital)->xy);
				break;
			}

			case WID_ST_MAP:
				ShowSmallMapOfStates();
				break;
		}
	}

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!str.has_value() || this->selected < 0) return;
		Command<Commands::RenameState>::Post(STR_ERROR_CAN_T_RENAME_STATE, static_cast<StateID>(this->selected), *str);
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_ST_LIST, WidgetDimensions::scaled.framerect.Vertical());
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		UpdateStateCaches();
		const std::vector<State> &states = GetStates();
		this->vscroll->SetCount(states.size());
		if (this->selected >= static_cast<int>(states.size())) this->selected = -1;
		if (this->selected < 0 && !states.empty()) this->selected = 0;
		if (AreStatesFormed()) {
			this->federal_poll = GetFederalPoll();
			if (this->selected >= 0) {
				const StateID id = static_cast<StateID>(this->selected);
				this->poll = GetStatePoll(id);
				this->concerns = GetStateConcerns(id);
			}
		}
		const bool none = this->selected < 0 || !AreStatesFormed();
		this->SetWidgetDisabledState(WID_ST_RENAME, none || (_networking && !_network_server));
		this->SetWidgetDisabledState(WID_ST_CAPITAL, none);
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_states_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown, WID_ST_CAPTION), SetStringTip(STR_STATES_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_DEFSIZEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_ST_FEDERATION), SetFill(1, 0), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Brown, WID_ST_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_ST_SCROLLBAR), SetToolTip(STR_STATES_LIST_TOOLTIP), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Brown, WID_ST_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_ST_DETAILS), SetFill(1, 0), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_ST_RENAME), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_STATES_RENAME_BUTTON, STR_STATES_RENAME_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_ST_CAPITAL), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_STATES_CAPITAL_BUTTON, STR_STATES_CAPITAL_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_ST_MAP), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_STATES_MAP_BUTTON, STR_STATES_MAP_TOOLTIP),
		NWidget(WWT_RESIZEBOX, Colours::Brown),
	EndContainer(),
};

static WindowDesc _states_desc(
	WindowPosition::Automatic, "states", 0, 0,
	WindowClass::States, WindowClass::None,
	{},
	_nested_states_widgets
);

/**
 * Open the states window.
 * @param select The state to select, or #INVALID_STATE.
 */
void ShowStatesWindow(StateID select)
{
	StatesWindow *w = AllocateWindowDescFront<StatesWindow, true>(_states_desc, 0, select);
	if (w != nullptr && select != INVALID_STATE) w->SelectState(select);
}
