/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fare_gui.cpp The window to set a company's passenger fares. */

#include "stdafx.h"
#include "fare.h"
#include "fare_cmd.h"
#include "fare_gui.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "dropdown_func.h"
#include "dropdown_type.h"
#include "strings_func.h"
#include "window_func.h"
#include "window_gui.h"

#include "widgets/fare_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** Names of the transport types. */
static constexpr StringID FARE_MODE_NAMES[] = {STR_FARES_MODE_BUS, STR_FARES_MODE_TRAM, STR_FARES_MODE_RAIL, STR_FARES_MODE_WATER, STR_FARES_MODE_AIR};
static_assert(std::size(FARE_MODE_NAMES) == to_underlying(FareMode::End));
/** Names and descriptions of the fare schemes. */
static constexpr StringID FARE_SCHEME_NAMES[] = {STR_FARE_SCHEME_CLASSIC, STR_FARE_SCHEME_DISTANCE, STR_FARE_SCHEME_FLAT, STR_FARE_SCHEME_ZONES, STR_FARE_SCHEME_FREE};
static constexpr StringID FARE_SCHEME_DESCRIPTIONS[] = {STR_FARE_SCHEME_CLASSIC_DESC, STR_FARE_SCHEME_DISTANCE_DESC, STR_FARE_SCHEME_FLAT_DESC, STR_FARE_SCHEME_ZONES_DESC, STR_FARE_SCHEME_FREE_DESC};
static_assert(std::size(FARE_SCHEME_NAMES) == to_underlying(FareScheme::End));
/** Names and descriptions of the concessions. */
static constexpr StringID FARE_CONCESSION_NAMES[] = {STR_FARE_CONCESSION_NONE, STR_FARE_CONCESSION_HALF, STR_FARE_CONCESSION_FREE};
static constexpr StringID FARE_CONCESSION_DESCRIPTIONS[] = {STR_FARE_CONCESSION_NONE_DESC, STR_FARE_CONCESSION_HALF_DESC, STR_FARE_CONCESSION_FREE_DESC};
static_assert(std::size(FARE_CONCESSION_NAMES) == to_underlying(FareConcession::End));
/** Price levels to choose from, in percent. */
static constexpr uint16_t FARE_PRICES[] = {25, 50, 75, 100, 125, 150, 175, 200, 250, 300};
/** Monthly pass prices to choose from, in single fares; 0 for no pass. */
static constexpr uint8_t FARE_PASSES[] = {0, 10, 15, 20, 25, 30};

/**
 * The name of a fare scheme.
 * @param scheme The scheme.
 * @return Its name.
 */
StringID GetFareSchemeName(FareScheme scheme)
{
	return FARE_SCHEME_NAMES[std::min(to_underlying(scheme), static_cast<uint8_t>(to_underlying(FareScheme::End) - 1))];
}

/**
 * List the fare schemes for a dropdown.
 * @param inherit Also offer the company's fare (for lines).
 * @return The list.
 */
DropDownList MakeFareSchemeDropDown(bool inherit)
{
	DropDownList list;
	if (inherit) list.push_back(MakeDropDownListStringItem(STR_LINE_FARE_COMPANY, to_underlying(FareScheme::Inherit)));
	for (uint i = 0; i < std::size(FARE_SCHEME_DESCRIPTIONS); i++) list.push_back(MakeDropDownListStringItem(FARE_SCHEME_DESCRIPTIONS[i], i));
	return list;
}

/**
 * List the price levels for a dropdown.
 * @param inherit Also offer the company's price (for lines).
 * @return The list.
 */
DropDownList MakeFarePriceDropDown(bool inherit)
{
	DropDownList list;
	if (inherit) list.push_back(MakeDropDownListStringItem(STR_LINE_FARE_PRICE_COMPANY, 0));
	for (uint16_t price : FARE_PRICES) list.push_back(MakeDropDownListStringItem(GetString(STR_FARE_PRICE, price), price));
	return list;
}

/** Window to set a company's passenger fares per transport type. */
struct FaresWindow : Window {
	FareMode dropdown_mode = FareMode::End; ///< Transport type of the open dropdown.
	FareSetting dropdown_setting = FareSetting::End; ///< What the open dropdown changes.

	FaresWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		this->owner = static_cast<Owner>(this->window_number);
	}

	/** Columns of the table. */
	enum Column : uint8_t { Mode, Scheme, Price, Concession, Pass, Columns };

	/**
	 * Width of each column.
	 * @return The widths.
	 */
	std::array<int, Columns> GetColumnWidths() const
	{
		std::array<int, Columns> widths{};
		widths[Mode] = GetStringBoundingBox(STR_FARES_HEADER_MODE).width;
		for (StringID s : FARE_MODE_NAMES) widths[Mode] = std::max<int>(widths[Mode], GetStringBoundingBox(s).width);
		widths[Scheme] = GetStringBoundingBox(STR_FARES_HEADER_SCHEME).width;
		for (StringID s : FARE_SCHEME_NAMES) widths[Scheme] = std::max<int>(widths[Scheme], GetStringBoundingBox(GetString(STR_FARES_VALUE, s)).width);
		widths[Price] = std::max(GetStringBoundingBox(STR_FARES_HEADER_PRICE).width, GetStringBoundingBox(GetString(STR_FARES_PRICE_VALUE, 300)).width);
		widths[Concession] = GetStringBoundingBox(STR_FARES_HEADER_CONCESSION).width;
		for (StringID s : FARE_CONCESSION_NAMES) widths[Concession] = std::max<int>(widths[Concession], GetStringBoundingBox(GetString(STR_FARES_VALUE, s)).width);
		widths[Pass] = std::max({GetStringBoundingBox(STR_FARES_HEADER_PASS).width, GetStringBoundingBox(GetString(STR_FARES_PASS_VALUE, 40)).width, GetStringBoundingBox(GetString(STR_FARES_VALUE, STR_FARE_PASS_NONE)).width});
		for (int &w : widths) w += WidgetDimensions::scaled.hsep_wide * 2;
		return widths;
	}

	/**
	 * The cells of one row of the table.
	 * @param r The row (without its second line).
	 * @return The rectangle of each column.
	 */
	std::array<Rect, Columns> GetCells(const Rect &r) const
	{
		bool rtl = _current_text_dir == TD_RTL;
		std::array<Rect, Columns> cells{};
		Rect rest = r;
		std::array<int, Columns> widths = this->GetColumnWidths();
		for (uint c = 0; c < Columns; c++) {
			cells[c] = rest.WithWidth(widths[c], rtl);
			rest = rest.Indent(widths[c], rtl);
		}
		return cells;
	}

	/** Height of the header and of each transport type's two lines. */
	int GetHeaderHeight() const { return GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_wide; }
	int GetRowHeight() const { return GetCharacterHeight(FontSize::Normal) + 2 * GetCharacterHeight(FontSize::Small) + WidgetDimensions::scaled.vsep_wide * 2; }

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_FA_TABLE) return;
		int width = 0;
		for (int w : this->GetColumnWidths()) width += w;
		Money big = 9999999;
		int indent = this->GetColumnWidths()[Mode] + WidgetDimensions::scaled.hsep_wide;
		width = std::max<int>(width, indent + GetStringBoundingBox(GetString(STR_FARES_EXAMPLE, big, big, big), FontSize::Small).width);
		width = std::max<int>(width, indent + GetStringBoundingBox(GetString(STR_FARES_PASSES, 999999, 99999, big), FontSize::Small).width);
		size.width = std::max<uint>(size.width, width + WidgetDimensions::scaled.framerect.Horizontal());
		size.height = WidgetDimensions::scaled.framerect.Vertical() + this->GetHeaderHeight() + to_underlying(FareMode::End) * this->GetRowHeight();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_FA_CAPTION) return GetString(STR_FARES_CAPTION, this->window_number);
		return this->Window::GetWidgetString(widget, stringid);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_FA_TABLE) return;
		const Company *c = Company::GetIfValid(this->window_number);
		if (c == nullptr) return;

		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		std::array<Rect, Columns> head = this->GetCells(ir.WithHeight(GetCharacterHeight(FontSize::Normal)));
		static constexpr StringID HEADERS[] = {STR_FARES_HEADER_MODE, STR_FARES_HEADER_SCHEME, STR_FARES_HEADER_PRICE, STR_FARES_HEADER_CONCESSION, STR_FARES_HEADER_PASS};
		for (uint col = 0; col < Columns; col++) DrawString(head[col].Shrink(WidgetDimensions::scaled.hsep_wide, 0), HEADERS[col]);
		ir.top += this->GetHeaderHeight();

		for (uint mode = 0; mode < to_underlying(FareMode::End); mode++) {
			const FarePolicy &fare = c->fares[mode];
			const FarePassStats &stats = c->pass_stats[mode];
			std::array<Rect, Columns> cells = this->GetCells(ir.WithHeight(GetCharacterHeight(FontSize::Normal)));
			auto cell = [&](Column col) { return cells[col].Shrink(WidgetDimensions::scaled.hsep_wide, 0); };
			DrawString(cell(Mode), FARE_MODE_NAMES[mode]);
			DrawString(cell(Scheme), GetString(STR_FARES_VALUE, GetFareSchemeName(fare.scheme)));
			DrawString(cell(Price), fare.scheme == FareScheme::Free ? GetString(STR_FARES_VALUE, STR_FARE_SCHEME_FREE) : GetString(STR_FARES_PRICE_VALUE, fare.price));
			DrawString(cell(Concession), GetString(STR_FARES_VALUE, FARE_CONCESSION_NAMES[to_underlying(fare.concession)]));
			DrawString(cell(Pass), fare.pass == 0 ? GetString(STR_FARES_VALUE, STR_FARE_PASS_NONE) : GetString(STR_FARES_PASS_VALUE, fare.pass));

			int y = ir.top + GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.vsep_normal;
			Rect detail = ir.Indent(cells[Mode].Width(), _current_text_dir == TD_RTL).Shrink(WidgetDimensions::scaled.hsep_wide, 0);
			DrawString(detail.left, detail.right, y, GetString(STR_FARES_EXAMPLE, GetFareExample(fare, 5, false), GetFareExample(fare, 20, false), GetFareExample(fare, 60, true)), TextColour::FromString, AlignmentH::Start, false, FontSize::Small);
			y += GetCharacterHeight(FontSize::Small);
			std::string last_month = (stats.holders > 0 || stats.income > 0) ? GetString(STR_FARES_PASSES, stats.riders, stats.holders, stats.income) : GetString(STR_FARES_NO_PASSES, stats.riders);
			DrawString(detail.left, detail.right, y, last_month, TextColour::FromString, AlignmentH::Start, false, FontSize::Small);
			ir.top += this->GetRowHeight();
		}
	}

	void OnClick(Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_FA_TABLE || this->window_number != _local_company.base()) return;
		const Company *c = Company::GetIfValid(this->window_number);
		if (c == nullptr) return;

		const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_FA_TABLE);
		Rect ir = nwid->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
		int y = pt.y - ir.top - this->GetHeaderHeight();
		if (y < 0) return;
		uint mode = y / this->GetRowHeight();
		if (mode >= to_underlying(FareMode::End)) return;

		Rect row = ir.WithY(ir.top + this->GetHeaderHeight() + mode * this->GetRowHeight(), ir.top + this->GetHeaderHeight() + mode * this->GetRowHeight() + GetCharacterHeight(FontSize::Normal) - 1);
		std::array<Rect, Columns> cells = this->GetCells(row);
		const FarePolicy &fare = c->fares[mode];
		for (uint col = Scheme; col < Columns; col++) {
			if (pt.x < cells[col].left || pt.x > cells[col].right) continue;
			DropDownList list;
			int selected = 0;
			switch (col) {
				case Scheme:
					list = MakeFareSchemeDropDown(false);
					selected = to_underlying(fare.scheme);
					this->dropdown_setting = FareSetting::Scheme;
					break;
				case Price:
					list = MakeFarePriceDropDown(false);
					selected = fare.price;
					this->dropdown_setting = FareSetting::Price;
					break;
				case Concession:
					for (uint i = 0; i < std::size(FARE_CONCESSION_DESCRIPTIONS); i++) list.push_back(MakeDropDownListStringItem(FARE_CONCESSION_DESCRIPTIONS[i], i));
					selected = to_underlying(fare.concession);
					this->dropdown_setting = FareSetting::Concession;
					break;
				default:
					for (uint8_t pass : FARE_PASSES) {
						list.push_back(pass == 0 ? MakeDropDownListStringItem(STR_FARE_PASS_NONE_DESC, 0) : MakeDropDownListStringItem(GetString(STR_FARE_PASS_DESC, pass, GetPassUptake(pass)), pass));
					}
					selected = fare.pass;
					this->dropdown_setting = FareSetting::Pass;
					break;
			}
			this->dropdown_mode = static_cast<FareMode>(mode);
			ShowDropDownListAt(this, std::move(list), selected, WID_FA_TABLE, cells[col], Colours::Grey);
			return;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget != WID_FA_TABLE || this->dropdown_mode >= FareMode::End || this->dropdown_setting >= FareSetting::End) return;
		Command<Commands::SetCompanyFare>::Post(STR_ERROR_CAN_T_CHANGE_FARES, this->dropdown_mode, this->dropdown_setting, static_cast<uint16_t>(index));
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		if (!Company::IsValidID(this->window_number)) {
			this->Close();
			return;
		}
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_fares_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_FA_CAPTION), SetToolTip(STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_FA_TABLE), SetFill(1, 0), SetToolTip(STR_FARES_TABLE_TOOLTIP), EndContainer(),
};

static WindowDesc _fares_desc(
	WindowPosition::Automatic, "company_fares", 0, 0,
	WindowClass::Fares, WindowClass::None,
	{},
	_nested_fares_widgets
);

/**
 * Open the fares window of a company.
 * @param company The company.
 */
void ShowFaresWindow(CompanyID company)
{
	if (!Company::IsValidID(company)) return;
	AllocateWindowDescFront<FaresWindow>(_fares_desc, company.base());
}
