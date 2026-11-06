/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file depot_yard_gui.cpp The depot yard window: what a depot's yard offers, and building its facilities. */

#include "stdafx.h"
#include "depot_yard.h"
#include "city_bus.h"
#include "command_func.h"
#include "company_func.h"
#include "depot_base.h"
#include "depot_map.h"
#include "gui.h"
#include "object_cmd.h"
#include "road_cmd.h"
#include "roadveh.h"
#include "strings_func.h"
#include "timer/timer.h"
#include "timer/timer_window.h"
#include "tilehighlight_func.h"
#include "vehicle_func.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"

#include "widgets/depot_yard_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

static_assert(WID_DY_BUILD_LAST - WID_DY_BUILD_FIRST + 1 == DEPOT_FACILITY_COUNT);

/** Window showing a depot's yard and building its facilities. */
struct DepotYardWindow : Window {
	DepotFacility placing = DepotFacility::End; ///< The facility being placed.

	DepotYardWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		const Depot *d = Depot::GetIfValid(this->GetDepot());
		if (d != nullptr) this->owner = GetTileOwner(d->xy);
		this->UpdateButtons();
	}

	DepotID GetDepot() const
	{
		return static_cast<DepotID>(this->window_number);
	}

	/** Only the depot's owner can build. */
	void UpdateButtons()
	{
		bool own = this->owner == _local_company;
		for (WidgetID w = WID_DY_BUILD_FIRST; w <= WID_DY_BUILD_LAST; w++) this->SetWidgetDisabledState(w, !own);
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_DY_CAPTION) return GetString(STR_DEPOT_YARD_CAPTION, VehicleType::Road, this->GetDepot());
		if (widget >= WID_DY_BUILD_FIRST && widget <= WID_DY_BUILD_LAST) return GetString(GetDepotFacilityName(static_cast<DepotFacility>(widget - WID_DY_BUILD_FIRST)));
		return this->Window::GetWidgetString(widget, stringid);
	}

	/** The lines of the info panel. */
	std::vector<std::string> GetInfoLines() const
	{
		std::vector<std::string> lines;
		const Depot *d = Depot::GetIfValid(this->GetDepot());
		if (d == nullptr) return lines;
		const DepotYardInfo &yard = GetDepotYard(this->GetDepot());

		lines.push_back(GetString(STR_DEPOT_YARD_PLACES, yard.homed, yard.places));
		if (yard.homed > yard.places) lines.push_back(GetString(STR_DEPOT_YARD_OVERFLOW, yard.homed - yard.places, GetYardOverflowPercent(yard)));

		uint total = 0;
		for (uint16_t kwh : yard.chargers) total += kwh;
		lines.push_back(GetString(STR_DEPOT_YARD_CHARGERS, yard.chargers.size(), total, yard.grid));

		uint charging = 0, waiting = 0;
		for (const Vehicle *v : VehiclesOnTile(d->xy)) {
			if (v->type != VehicleType::Road || !v->IsFrontEngine()) continue;
			const RoadVehicle *rv = RoadVehicle::From(v);
			if (!IsElectricBus(rv) || !rv->IsChainInDepot() || rv->battery_charge >= GetBatteryCapacityWh(rv)) continue;
			(IsAtDepotCharger(rv) ? charging : waiting)++;
		}
		if (charging + waiting > 0) lines.push_back(GetString(STR_DEPOT_YARD_CHARGING_NOW, charging, waiting));

		lines.push_back(GetString(yard.homed > yard.repair_capacity ? STR_DEPOT_YARD_REPAIRS_SHORT : STR_DEPOT_YARD_REPAIRS, yard.repair_capacity, GetYardWearPercent(yard)));
		if (yard.fuel_capacity > 0) lines.push_back(GetString(STR_DEPOT_YARD_FUEL, yard.fuel_capacity, GetYardFuelDiscountPercent(yard)));
		if (yard.solar_capacity > 0) lines.push_back(GetString(STR_DEPOT_YARD_SOLAR, yard.solar_capacity, GetYardSolarDiscountPercent(yard)));
		if (StaffBudget budget = GetStaffBudget(this->owner); budget != SB_OFF) {
			StringID pay = STR_CONFIG_SETTING_STAFF_BUDGET_OFF + budget;
			if (yard.homed > yard.staff_capacity) {
				lines.push_back(GetString(STR_DEPOT_YARD_STAFF_SHORT, yard.staff_capacity, pay, GetYardStaffWearPercent(yard)));
			} else {
				lines.push_back(GetString(STR_DEPOT_YARD_STAFF, yard.staff_capacity, pay));
			}
		}
		lines.push_back(GetString(STR_DEPOT_YARD_UPKEEP, GetDepotYardYearlyUpkeep(yard)));
		lines.push_back(GetString(STR_DEPOT_YARD_HINT));
		return lines;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_DY_INFO) return;
		size.width = std::max(size.width, static_cast<uint>(ScaleGUITrad(420)));
		size.height = std::max(size.height, static_cast<uint>(12 * GetCharacterHeight(FontSize::Normal)) + padding.height);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_DY_INFO) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		for (const std::string &line : this->GetInfoLines()) {
			ir.top = DrawStringMultiLine(ir, line);
			if (ir.top >= ir.bottom) break;
		}
	}

	bool OnTooltip([[maybe_unused]] Point pt, WidgetID widget, TooltipCloseCondition close_cond) override
	{
		if (widget < WID_DY_BUILD_FIRST || widget > WID_DY_BUILD_LAST) return false;
		DepotFacility facility = static_cast<DepotFacility>(widget - WID_DY_BUILD_FIRST);
		GuiShowTooltips(this, GetEncodedString(GetDepotFacilityTooltip(facility), GetDepotFacilityBuildCost(facility), GetDepotFacilityYearlyUpkeep(facility)), close_cond);
		return true;
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget == WID_DY_LOCATION) {
			const Depot *d = Depot::GetIfValid(this->GetDepot());
			if (d != nullptr) {
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(d->xy);
				} else {
					ScrollMainWindowToTile(d->xy);
				}
			}
			return;
		}
		if (widget < WID_DY_BUILD_FIRST || widget > WID_DY_BUILD_LAST) return;

		if (HandlePlacePushButton(this, widget, SPR_CURSOR_BUS_STATION, HT_RECT)) {
			this->placing = static_cast<DepotFacility>(widget - WID_DY_BUILD_FIRST);
		}
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		if (this->placing == DepotFacility::End) return;
		Command<Commands::BuildObject>::Post(STR_ERROR_CAN_T_BUILD_DEPOT_FACILITY, CcPlaySound_CONSTRUCTION_OTHER, tile, GetObjectTypeOfDepotFacility(this->placing), 0);
	}

	void OnPlaceObjectAbort() override
	{
		this->placing = DepotFacility::End;
		this->RaiseButtons();
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		if (!Depot::IsValidID(this->GetDepot())) {
			this->Close();
			return;
		}
		this->UpdateButtons();
		this->SetDirty();
	}

	/** Refresh now and then: buses come and go and charge. */
	const IntervalTimer<TimerWindow> refresh_interval = {std::chrono::seconds(2), [this](auto) {
		this->SetWidgetDirty(WID_DY_INFO);
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_depot_yard_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_DY_CAPTION), SetToolTip(STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHIMGBTN, Colours::Grey, WID_DY_LOCATION), SetAspect(WidgetDimensions::ASPECT_LOCATION), SetSpriteTip(SPR_GOTO_LOCATION, STR_DEPOT_YARD_LOCATION_TOOLTIP),
		NWidget(WWT_SHADEBOX, Colours::Grey),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_DY_INFO), SetFill(1, 1), EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_DY_BUILD_FIRST + 0), SetFill(1, 0),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_DY_BUILD_FIRST + 1), SetFill(1, 0),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_DY_BUILD_FIRST + 2), SetFill(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_DY_BUILD_FIRST + 3), SetFill(1, 0),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_DY_BUILD_FIRST + 4), SetFill(1, 0),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_DY_BUILD_FIRST + 5), SetFill(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_DY_BUILD_FIRST + 6), SetFill(1, 0),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_DY_BUILD_FIRST + 7), SetFill(1, 0),
		NWidget(WWT_TEXTBTN, Colours::Grey, WID_DY_BUILD_FIRST + 8), SetFill(1, 0),
	EndContainer(),
};

static WindowDesc _depot_yard_desc(
	WindowPosition::Automatic, "depot_yard", 0, 0,
	WindowClass::DepotYard, WindowClass::None,
	{},
	_nested_depot_yard_widgets
);

/**
 * Show the yard of a road depot.
 * @param depot The depot.
 */
void ShowDepotYardWindow(DepotID depot)
{
	if (!Depot::IsValidID(depot)) return;
	AllocateWindowDescFront<DepotYardWindow>(_depot_yard_desc, depot);
}
