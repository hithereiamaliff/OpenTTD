/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file city_services_gui.cpp Toolbar to build city services: police, fire, health, schools and parks; hospitals, universities, stadiums and hawker centres. */

#include "stdafx.h"
#include "city_services.h"
#include "command_func.h"
#include "gui.h"
#include "hotkeys.h"
#include "object_cmd.h"
#include "road_cmd.h"
#include "tilehighlight_func.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"

#include "widgets/city_services_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/**
 * The city service a tool builds.
 * @param widget The tool's button.
 * @return The service.
 */
static CityService GetServiceOfTool(WidgetID widget)
{
	return static_cast<CityService>(widget - WID_CS_POLICE);
}

/** Toolbar to build city services. While a tool is in use, the main view shows that service's coverage. */
struct CityServicesWindow : Window {
	CityService service = CityService::End; ///< The service being placed.

	CityServicesWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		ShowCityServiceOverlay(CityService::End);
		this->Window::Close();
	}

	Point OnInitialPosition(int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		return AlignInitialConstructionToolbar(sm_width);
	}

	/** Outline the area the service would reach around the cursor. */
	void ShowReach() const
	{
		int radius = GetCityServiceRadius(this->service);
		const int size = GetCityServiceSize(this->service);
		SetTileSelectSize(size, size);
		SetTileSelectBigSize(-radius, -radius, 2 * radius, 2 * radius);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget < WID_CS_POLICE || widget > WID_CS_MARKET) return;

		CityService service = GetServiceOfTool(widget);
		if (HandlePlacePushButton(this, widget, GetCityServiceIcon(service), HT_RECT)) {
			this->service = service;
			this->ShowReach();
			ShowCityServiceOverlay(service);
		}
	}

	void OnPaint() override
	{
		this->DrawWidgets();
		if (this->service != CityService::End) this->ShowReach();
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		if (this->service == CityService::End) return;
		Command<Commands::BuildObject>::Post(STR_ERROR_CAN_T_BUILD_CITY_SERVICE, CcPlaySound_CONSTRUCTION_OTHER, tile, GetObjectTypeOfCityService(this->service), 0);
	}

	void OnPlaceObjectAbort() override
	{
		this->service = CityService::End;
		this->RaiseButtons();
		ShowCityServiceOverlay(CityService::End);
	}

	static inline HotkeyList hotkeys{"cityservices", {
		Hotkey('1', "police", WID_CS_POLICE),
		Hotkey('2', "fire", WID_CS_FIRE),
		Hotkey('3', "health", WID_CS_HEALTH),
		Hotkey('4', "education", WID_CS_EDUCATION),
		Hotkey('5', "park", WID_CS_PARK),
		Hotkey('6', "hospital", WID_CS_HOSPITAL),
		Hotkey('7', "university", WID_CS_UNIVERSITY),
		Hotkey('8', "stadium", WID_CS_STADIUM),
		Hotkey('9', "market", WID_CS_MARKET),
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_city_services_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_CS_CAPTION), SetStringTip(STR_CITY_SERVICES_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_CS_POLICE), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CITY_SERVICE_BASE + 0, STR_CITY_SERVICES_POLICE_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_CS_FIRE), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CITY_SERVICE_BASE + 1, STR_CITY_SERVICES_FIRE_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_CS_HEALTH), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CITY_SERVICE_BASE + 2, STR_CITY_SERVICES_HEALTH_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_CS_EDUCATION), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CITY_SERVICE_BASE + 3, STR_CITY_SERVICES_EDUCATION_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_CS_PARK), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CITY_SERVICE_BASE + 4, STR_CITY_SERVICES_PARK_TOOLTIP),
		NWidget(WWT_PANEL, Colours::DarkGreen), SetMinimalSize(4, 22), EndContainer(),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_CS_HOSPITAL), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CIVIC_SERVICE_BASE + 0, STR_CITY_SERVICES_HOSPITAL_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_CS_UNIVERSITY), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CIVIC_SERVICE_BASE + 1, STR_CITY_SERVICES_UNIVERSITY_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_CS_STADIUM), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CIVIC_SERVICE_BASE + 2, STR_CITY_SERVICES_STADIUM_TOOLTIP),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_CS_MARKET), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_CIVIC_SERVICE_BASE + 3, STR_CITY_SERVICES_MARKET_TOOLTIP),
	EndContainer(),
};

static WindowDesc _city_services_desc(
	WindowPosition::Manual, "toolbar_city_services", 0, 0,
	WindowClass::CityServices, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_city_services_widgets,
	&CityServicesWindow::hotkeys
);

/** Open the city services toolbar. */
void ShowCityServicesToolbar()
{
	AllocateWindowDescFront<CityServicesWindow>(_city_services_desc, 0);
}
