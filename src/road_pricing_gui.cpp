/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file road_pricing_gui.cpp The tolls toolbar (toll plazas, free-flow gantries) and the window with a company's tolls. */

#include "stdafx.h"
#include "road_pricing.h"
#include "road_pricing_cmd.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "dropdown_func.h"
#include "dropdown_type.h"
#include "gui.h"
#include "hotkeys.h"
#include "road_cmd.h"
#include "road_gui.h"
#include "road_map.h"
#include "sound_func.h"
#include "strings_func.h"
#include "tilehighlight_func.h"
#include "town.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"

#include "widgets/road_pricing_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/** Toolbar with the toll tools. */
struct TollsToolbarWindow : Window {
	WidgetID last_tool = WID_TT_PLAZA; ///< The tool being used.

	TollsToolbarWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
	}

	Point OnInitialPosition(int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		/* Under the road toolbar it opens from, not over its last buttons. */
		return AlignInitialRoadSubToolbar(sm_width);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_TT_PLAZA:
			case WID_TT_FREE_FLOW:
				if (HandlePlacePushButton(this, widget, SPR_CURSOR_ROAD_NESW, HT_RECT)) this->last_tool = widget;
				break;

			case WID_TT_OVERVIEW:
				ShowTollsWindow(_local_company);
				SndClickBeep();
				break;

			default: break;
		}
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		if (this->last_tool == WID_TT_FREE_FLOW) {
			Command<Commands::BuildTollPlaza>::Post(STR_ERROR_CAN_T_BUILD_FREE_FLOW, CcPlaySound_CONSTRUCTION_OTHER, tile, TollPlazaAction::FreeFlow);
		} else if (_ctrl_pressed) {
			Command<Commands::BuildTollPlaza>::Post(STR_ERROR_CAN_T_REMOVE_TOLL_PLAZA, CcPlaySound_CONSTRUCTION_OTHER, tile, TollPlazaAction::Remove);
		} else {
			Command<Commands::BuildTollPlaza>::Post(STR_ERROR_CAN_T_BUILD_TOLL_PLAZA, CcPlaySound_CONSTRUCTION_OTHER, tile, TollPlazaAction::Build);
		}
	}

	void OnPlaceObjectAbort() override
	{
		this->RaiseButtons();
	}

	static inline HotkeyList hotkeys{"tolls", {
		Hotkey('1', "toll_plaza", WID_TT_PLAZA),
		Hotkey('2', "free_flow", WID_TT_FREE_FLOW),
		Hotkey('3', "toll_overview", WID_TT_OVERVIEW),
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_tolls_toolbar_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_TT_CAPTION), SetStringTip(STR_TOLLS_TOOLBAR_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_TT_PLAZA), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_TOLL_PLAZA, STR_TOLLS_TOOLBAR_PLAZA_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_TT_FREE_FLOW), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_FREE_FLOW, STR_TOLLS_TOOLBAR_FREE_FLOW_TOOLTIP),
		NWidget(WWT_PUSHIMGBTN, Colours::DarkGreen, WID_TT_OVERVIEW), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_TOLLS, STR_TOLLS_TOOLBAR_OVERVIEW_TOOLTIP),
	EndContainer(),
};

static WindowDesc _tolls_toolbar_desc(
	WindowPosition::Manual, "toolbar_tolls", 0, 0,
	WindowClass::TollsToolbar, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_tolls_toolbar_widgets,
	&TollsToolbarWindow::hotkeys
);

/** Show the tolls toolbar. */
void ShowTollsToolbar()
{
	AllocateWindowDescFront<TollsToolbarWindow>(_tolls_toolbar_desc, 0);
}

/** Toll plazas listed at most in the tolls window. */
static constexpr uint TOLLS_LIST_LINES = 10;
/** Lines above the list in the tolls window. */
static constexpr uint TOLLS_HEAD_LINES = 4;

/** A company's tolls: its toll level and scheme, a freeze, and its toll plazas. */
struct TollsWindow : Window {
	TollsWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		this->owner = static_cast<Owner>(window_number);
		this->OnInvalidateData();
	}

	/**
	 * The company's toll plazas.
	 * @return The tiles, in map order.
	 */
	std::vector<TileIndex> GetPlazas() const
	{
		std::vector<TileIndex> plazas;
		for (const auto &[tile, plaza] : GetTollPlazas()) {
			if (HasTollPlaza(tile) && GetRoadOwner(tile, RoadTramType::Road) == static_cast<Owner>(this->window_number)) plazas.push_back(tile);
		}
		return plazas;
	}

	/**
	 * The lines of the window: where tolls come from, a freeze, last month's income, then the toll plazas.
	 * @param c The company.
	 * @return The lines; the empty one leaves a gap above the plazas.
	 */
	std::vector<std::string> GetInfoLines(const Company *c) const
	{
		std::vector<std::string> lines;
		lines.push_back(GetString(STR_TOLLS_EXPLAIN));
		if (TimerGameEconomy::date < c->tolls.freeze_until) {
			lines.push_back(GetString(STR_TOLLS_FROZEN, c->tolls.freeze_until, GetEffectiveTollLevel(c->index)));
		} else {
			lines.push_back(GetString(STR_TOLLS_NOT_FROZEN));
		}
		lines.push_back(GetString(STR_TOLLS_INCOME, c->tolls.income_last_month, c->tolls.compensation_last_month));
		lines.emplace_back();

		std::vector<TileIndex> plazas = this->GetPlazas();
		if (plazas.empty()) {
			lines.push_back(GetString(STR_TOLLS_NO_PLAZAS));
			return lines;
		}
		uint shown = 0;
		for (TileIndex tile : plazas) {
			if (shown == TOLLS_LIST_LINES) {
				lines.push_back(GetString(STR_TOLLS_MORE, plazas.size() - shown));
				break;
			}
			const TollPlaza *p = GetTollPlaza(tile);
			const Town *t = ClosestTownFromTile(tile, UINT_MAX);
			std::string where = t == nullptr ? GetString(STR_TOLLS_PLAZA_AT, TileX(tile), TileY(tile)) : GetString(STR_TOLLS_PLAZA_NEAR, t->index, TileX(tile), TileY(tile));
			StringID kind = p->free_flow ? STR_TOLLS_PLAZA_FREE_FLOW : STR_TOLLS_PLAZA_BOOTHS;
			lines.push_back(GetString(STR_TOLLS_PLAZA, where, kind, p->towns, p->trips, p->vehicles, p->income, p->concession_end, p->cut));
			shown++;
		}
		return lines;
	}

	/**
	 * Size of the panel for its lines now: as many lines as it has, as wide as the longest.
	 * @return The size, frame included.
	 */
	Dimension GetInfoSize() const
	{
		const Company *c = Company::GetIfValid(this->window_number);
		const std::vector<std::string> lines = c == nullptr ? std::vector<std::string>{} : this->GetInfoLines(c);
		uint width = 0;
		for (const std::string &line : lines) width = std::max(width, GetStringBoundingBox(line).width);
		const uint count = std::max<uint>(static_cast<uint>(lines.size()), TOLLS_HEAD_LINES + 1);
		return {width + WidgetDimensions::scaled.framerect.Horizontal(), WidgetDimensions::scaled.framerect.Vertical() + count * GetCharacterHeight(FontSize::Normal)};
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_TO_INFO) return;
		/* Fit the lines; the window fits them again when plazas come or go, or a line outgrows it (see OnInvalidateData). */
		const Dimension d = this->GetInfoSize();
		size.height = d.height;
		size.width = std::max(size.width, d.width);
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		const Company *c = Company::GetIfValid(this->window_number);
		switch (widget) {
			case WID_TO_CAPTION: return GetString(STR_TOLLS_CAPTION, this->window_number);
			case WID_TO_LEVEL: return c == nullptr ? std::string{} : GetString(STR_TOLLS_LEVEL, c->tolls.level);
			case WID_TO_SCHEME: return c == nullptr ? std::string{} : GetString(c->tolls.scheme == TollScheme::PerKm ? STR_TOLLS_SCHEME_PER_KM : STR_TOLLS_SCHEME_FLAT);
			default: return this->Window::GetWidgetString(widget, stringid);
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_TO_INFO) return;
		const Company *c = Company::GetIfValid(this->window_number);
		if (c == nullptr) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		for (const std::string &line : this->GetInfoLines(c)) {
			DrawString(ir, line);
			ir.top += GetCharacterHeight(FontSize::Normal);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		const Company *c = Company::GetIfValid(this->window_number);
		if (c == nullptr || this->window_number != _local_company.base()) return;
		switch (widget) {
			case WID_TO_LEVEL: {
				DropDownList list;
				for (uint level = TOLL_LEVEL_MIN; level <= TOLL_LEVEL_MAX; level += TOLL_LEVEL_STEP) {
					list.push_back(MakeDropDownListStringItem(GetString(STR_TOLLS_LEVEL_ITEM, level), static_cast<int>(level)));
				}
				ShowDropDownList(this, std::move(list), c->tolls.level, WID_TO_LEVEL);
				break;
			}

			case WID_TO_SCHEME: {
				DropDownList list;
				list.push_back(MakeDropDownListStringItem(STR_TOLLS_SCHEME_PER_KM, to_underlying(TollScheme::PerKm)));
				list.push_back(MakeDropDownListStringItem(STR_TOLLS_SCHEME_FLAT, to_underlying(TollScheme::Flat)));
				ShowDropDownList(this, std::move(list), to_underlying(c->tolls.scheme), WID_TO_SCHEME);
				break;
			}

			default: break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		switch (widget) {
			case WID_TO_LEVEL: Command<Commands::SetTollPolicy>::Post(STR_ERROR_CAN_T_CHANGE_TOLLS, TollPolicyProperty::Level, static_cast<uint8_t>(index)); break;
			case WID_TO_SCHEME: Command<Commands::SetTollPolicy>::Post(STR_ERROR_CAN_T_CHANGE_TOLLS, TollPolicyProperty::Scheme, static_cast<uint8_t>(index)); break;
			default: break;
		}
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		if (!Company::IsValidID(this->window_number)) {
			this->Close();
			return;
		}
		const bool mine = this->window_number == _local_company.base();
		this->SetWidgetsDisabledState(!mine, WID_TO_LEVEL, WID_TO_SCHEME);
		/* Plazas came or went, or a line outgrew the window: fit the lines again. */
		const NWidgetBase *info = this->GetWidget<NWidgetBase>(WID_TO_INFO);
		const Dimension d = this->GetInfoSize();
		if (d.height != info->smallest_y || d.width > info->smallest_x) this->ReInit();
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_tolls_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_TO_CAPTION), SetToolTip(STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_TO_INFO), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_DROPDOWN, Colours::Grey, WID_TO_LEVEL), SetFill(1, 0), SetToolTip(STR_TOLLS_LEVEL_TOOLTIP),
		NWidget(WWT_DROPDOWN, Colours::Grey, WID_TO_SCHEME), SetFill(1, 0), SetToolTip(STR_TOLLS_SCHEME_TOOLTIP),
	EndContainer(),
};

static WindowDesc _tolls_desc(
	WindowPosition::Automatic, "company_tolls", 0, 0,
	WindowClass::Tolls, WindowClass::None,
	{},
	_nested_tolls_widgets
);

/**
 * Show the tolls of a company.
 * @param company The company.
 */
void ShowTollsWindow(CompanyID company)
{
	if (!Company::IsValidID(company)) return;
	AllocateWindowDescFront<TollsWindow>(_tolls_desc, company.base());
}
