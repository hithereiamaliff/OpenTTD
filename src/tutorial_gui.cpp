/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file tutorial_gui.cpp City fork: the interactive tutorial.
 *
 * A window walks the player through the game step by step: it says what to
 * do, makes the button to click flash (the highlight game scripts use), and
 * moves on by itself once the step is done. It works in any game, an old
 * OpenTTD save included, because it only looks at the game and its windows;
 * it changes nothing in the game itself. Where the player is, and how much
 * of the step they did, goes in openttd.cfg (gui.tutorial_*), not in the
 * savegame.
 */

#include "stdafx.h"
#include "tutorial_gui.h"
#include "company_base.h"
#include "company_func.h"
#include "core/geometry_func.hpp"
#include "depot_base.h"
#include "fare_type.h"
#include "gfx_func.h"
#include "openttd.h"
#include "order_base.h"
#include "road_map.h"
#include "roadveh.h"
#include "settings_type.h"
#include "station_base.h"
#include "strings_func.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"
#include "timer/timer_window.h"
#include "transport_type.h"
#include "vehicle_gui_base.h"
#include "vehiclelist.h"
#include "video/video_driver.hpp"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"

#include "widgets/build_vehicle_widget.h"
#include "widgets/company_widget.h"
#include "widgets/depot_widget.h"
#include "widgets/fios_widget.h"
#include "widgets/genworld_widget.h"
#include "widgets/group_widget.h"
#include "widgets/intro_widget.h"
#include "widgets/line_widget.h"
#include "widgets/order_widget.h"
#include "widgets/road_widget.h"
#include "widgets/station_widget.h"
#include "widgets/toolbar_widget.h"
#include "widgets/town_widget.h"
#include "widgets/tutorial_widget.h"
#include "widgets/vehicle_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/** The chapters of the tutorial. */
enum class TutorialChapter : uint8_t {
	Start, ///< Start a game: from the main menu to a new game. Goes on into Bus.
	Bus, ///< Your first bus line.
	New, ///< What's new for OpenTTD players.
};

/** How a step knows that it is done. */
enum class TutorialCheck : uint8_t {
	Read, ///< Nothing to do: the player reads it and clicks Next.
	Reach, ///< Done when the measure reaches the goal.
	Add, ///< Done when the measure grew by the goal since the step began.
	Change, ///< Done when the measure changed since the step began.
};

/** A button that flashes: a widget of the windows of a class. */
struct TutorialHighlight {
	WindowClass cls; ///< Class of the window.
	WindowNumber number; ///< Number of the window.
	WidgetID widget; ///< The widget.

	bool operator==(const TutorialHighlight &other) const
	{
		return this->cls == other.cls && this->number == other.number && this->widget == other.widget;
	}
};

using TutorialHighlights = std::vector<TutorialHighlight>;

/** A step of the tutorial. */
struct TutorialStep {
	std::string_view id; ///< Name of the step in openttd.cfg; don't change it, or players lose their place.
	TutorialChapter chapter; ///< Chapter the step belongs to.
	StringID title; ///< What to do, in a few words.
	StringID text; ///< How to do it, with the labels on screen.
	TutorialCheck check; ///< How it knows that the step is done.
	int goal; ///< For Reach and Add: the value or growth that completes the step.
	int (*measure)(); ///< What the step watches; nullptr for Read.
	StringID progress; ///< Progress line with {NUM} done and {NUM} goal; STR_NULL for none.
	void (*highlight)(TutorialHighlights &hl); ///< Adds the buttons to flash; nullptr for none.
};

/** Colour of the flashing frame around the button to click. */
static constexpr TextColour TUTORIAL_HIGHLIGHT_COLOUR = TextColour::Yellow;

/** Width of the text, in unscaled pixels. */
static constexpr uint TUTORIAL_TEXT_WIDTH = 300;

/**
 * Whether the player is in a game with a company of their own.
 * @return True for a normal game, not the main menu or the scenario editor.
 */
static bool TutorialInGame()
{
	return _game_mode == GameMode::Normal && Company::IsValidID(_local_company);
}

/**
 * Make a widget of every open window of a class flash.
 * @param hl The buttons to flash.
 * @param cls Class of the windows.
 * @param widget The widget.
 */
static void HighlightAll(TutorialHighlights &hl, WindowClass cls, WidgetID widget)
{
	for (const Window *w : Window::Iterate()) {
		if (w->window_class == cls) hl.push_back({cls, w->window_number, widget});
	}
}

/**
 * Make a button of the main toolbar flash.
 * @param hl The buttons to flash.
 * @param widget The button.
 */
static void HighlightToolbar(TutorialHighlights &hl, WidgetID widget)
{
	/* The scenario editor's toolbar has other buttons. */
	if (_game_mode != GameMode::Normal) return;
	hl.push_back({WindowClass::MainToolbar, 0, widget});
}

/**
 * The open Road Construction toolbar.
 * @return The toolbar, or nullptr.
 */
static const Window *FindRoadToolbar()
{
	return FindWindowById(WindowClass::BuildToolbar, TransportType::Road);
}

/**
 * Make a button of the Road Construction toolbar flash, or the main toolbar's
 * Build roads button while the toolbar isn't open.
 * @param hl The buttons to flash.
 * @param widget The button of the Road Construction toolbar.
 */
static void HighlightRoadTool(TutorialHighlights &hl, WidgetID widget)
{
	const Window *w = FindRoadToolbar();
	if (w == nullptr) {
		HighlightToolbar(hl, WID_TN_ROADS);
	} else if (!w->IsWidgetLowered(widget)) {
		hl.push_back({WindowClass::BuildToolbar, w->window_number, widget});
	}
}

/**
 * Make the Group by list of the road vehicle lists flash while they don't
 * show lines, or the main toolbar's Road vehicles button while none is open.
 * @param hl The buttons to flash.
 * @return True if a list shows lines.
 */
static bool HighlightLinesList(TutorialHighlights &hl)
{
	bool found = false;
	bool lines = false;
	for (const Window *w : Window::Iterate()) {
		if (w->window_class != WindowClass::RoadVehicleList) continue;
		found = true;
		const BaseVehicleListWindow *list = static_cast<const BaseVehicleListWindow *>(w);
		if (list->grouping == BaseVehicleListWindow::GB_SHARED_ORDERS) {
			lines = true;
			continue;
		}
		/* The group window and the plain list have the list in different places. */
		WidgetID widget = (list->vli.type == VehicleListType::Group) ? (WidgetID)WID_GL_GROUP_BY_DROPDOWN : (WidgetID)WID_VL_GROUP_BY_PULLDOWN;
		hl.push_back({w->window_class, w->window_number, widget});
	}
	if (!found) HighlightToolbar(hl, WID_TN_ROADVEHS);
	return lines;
}

/**
 * Whether a window of a class is open.
 * @tparam Tcls The class.
 * @return 1 if one is open, else 0.
 */
template <WindowClass Tcls>
static int MeasureWindow()
{
	return FindWindowByClass(Tcls) != nullptr ? 1 : 0;
}

/**
 * Whether the Road Construction toolbar is open.
 * @return 1 if it is, else 0.
 */
static int MeasureRoadToolbar()
{
	return FindRoadToolbar() != nullptr ? 1 : 0;
}

/**
 * Whether the game started.
 * @return 1 in a game, else 0.
 */
static int MeasureInGame()
{
	return _game_mode == GameMode::Normal ? 1 : 0;
}

/**
 * Whether the World Generation window is open.
 * @return 1 if it is, else 0.
 */
static int MeasureWorldGeneration()
{
	return FindWindowByClass(WindowClass::GenerateLandscape) != nullptr ? 1 : 0;
}

/**
 * The start year of the next new game.
 * @return The year.
 */
static int MeasureStartYear()
{
	return _settings_newgame.game_creation.starting_year.base();
}

/**
 * The company's road vehicles that carry passengers.
 * @param func Called for each.
 */
template <typename F>
static void ForEachBus(F func)
{
	for (const RoadVehicle *rv : RoadVehicle::Iterate()) {
		if (rv->owner != _local_company || !rv->IsFrontEngine() || !rv->IsBus()) continue;
		func(rv);
	}
}

/**
 * The company's stations with a bus stop.
 * @return How many.
 */
static int CountBusStops()
{
	int count = 0;
	for (const Station *st : Station::Iterate()) {
		if (st->owner == _local_company && st->facilities.Test(StationFacility::BusStop)) count++;
	}
	return count;
}

/**
 * The company's road depots.
 * @return How many.
 */
static int CountRoadDepots()
{
	int count = 0;
	for (const Depot *d : Depot::Iterate()) {
		if (IsRoadDepotTile(d->xy) && GetTileOwner(d->xy) == _local_company) count++;
	}
	return count;
}

/**
 * The company's buses.
 * @return How many.
 */
static int CountBuses()
{
	int count = 0;
	ForEachBus([&count](const RoadVehicle *) { count++; });
	return count;
}

/**
 * The company's buses with at least three orders.
 * @return How many.
 */
static int CountBusesWithOrders()
{
	int count = 0;
	ForEachBus([&count](const RoadVehicle *rv) { if (rv->GetNumOrders() >= 3) count++; });
	return count;
}

/**
 * The company's buses that aren't stopped.
 * @return How many.
 */
static int CountRunningBuses()
{
	int count = 0;
	ForEachBus([&count](const RoadVehicle *rv) { if (!rv->vehstatus.Test(VehState::Stopped)) count++; });
	return count;
}

/**
 * The company's lines (order lists) that match.
 * @param func Whether a line counts.
 * @return How many.
 */
template <typename F>
static int CountLines(F func)
{
	int count = 0;
	for (const OrderList *list : OrderList::Iterate()) {
		const Vehicle *v = list->GetFirstSharedVehicle();
		if (v == nullptr || v->owner != _local_company) continue;
		if (func(list->line)) count++;
	}
	return count;
}

/**
 * The company's lines with a number.
 * @return How many.
 */
static int CountNumberedLines()
{
	return CountLines([](const LineInfo &line) { return !line.number.empty(); });
}

/**
 * The company's lines with a target frequency and even spacing.
 * @return How many.
 */
static int CountSpacedLines()
{
	return CountLines([](const LineInfo &line) { return line.target_gap > 0 && line.hold_for_spacing; });
}

/**
 * Today, in days.
 * @return The economy date.
 */
static int MeasureDate()
{
	return TimerGameEconomy::date.base();
}

/**
 * The concession fare of the company's buses.
 * @return The concession.
 */
static int MeasureBusConcession()
{
	return to_underlying(Company::Get(_local_company)->fares[to_underlying(FareMode::Bus)].concession);
}

/** The steps of the tutorial, chapter by chapter. */
static const TutorialStep _tutorial_steps[] = {
	/* Start a game: from the main menu. Based on the guide's "Your first game". */
	{"start.new_game", TutorialChapter::Start, STR_TUTORIAL_START_NEW_GAME_TITLE, STR_TUTORIAL_START_NEW_GAME_TEXT, TutorialCheck::Reach, 1, MeasureWorldGeneration, STR_NULL,
		[](TutorialHighlights &hl) { hl.push_back({WindowClass::SelectGame, 0, WID_SGI_GENERATE_GAME}); }},
	{"start.climate", TutorialChapter::Start, STR_TUTORIAL_START_CLIMATE_TITLE, STR_TUTORIAL_START_CLIMATE_TEXT, TutorialCheck::Read, 0, nullptr, STR_NULL,
		[](TutorialHighlights &hl) { HighlightAll(hl, WindowClass::GenerateLandscape, WID_GL_TEMPERATE); HighlightAll(hl, WindowClass::GenerateLandscape, WID_GL_TROPICAL); }},
	{"start.year", TutorialChapter::Start, STR_TUTORIAL_START_YEAR_TITLE, STR_TUTORIAL_START_YEAR_TEXT, TutorialCheck::Reach, 2000, MeasureStartYear, STR_TUTORIAL_PROGRESS_START_YEAR,
		[](TutorialHighlights &hl) { HighlightAll(hl, WindowClass::GenerateLandscape, WID_GL_START_DATE_TEXT); HighlightAll(hl, WindowClass::GenerateLandscape, WID_GL_START_DATE_UP); }},
	{"start.generate", TutorialChapter::Start, STR_TUTORIAL_START_GENERATE_TITLE, STR_TUTORIAL_START_GENERATE_TEXT, TutorialCheck::Reach, 1, MeasureInGame, STR_NULL,
		[](TutorialHighlights &hl) { HighlightAll(hl, WindowClass::GenerateLandscape, WID_GL_GENERATE_BUTTON); }},

	/* Your first bus line. Based on the guide's tutorial "Your first city bus network". */
	{"bus.intro", TutorialChapter::Bus, STR_TUTORIAL_BUS_INTRO_TITLE, STR_TUTORIAL_BUS_INTRO_TEXT, TutorialCheck::Read, 0, nullptr, STR_NULL, nullptr},
	{"bus.town_directory", TutorialChapter::Bus, STR_TUTORIAL_BUS_TOWN_DIRECTORY_TITLE, STR_TUTORIAL_BUS_TOWN_DIRECTORY_TEXT, TutorialCheck::Reach, 1, MeasureWindow<WindowClass::TownDirectory>, STR_NULL,
		[](TutorialHighlights &hl) { HighlightToolbar(hl, WID_TN_TOWNS); }},
	{"bus.pick_town", TutorialChapter::Bus, STR_TUTORIAL_BUS_PICK_TOWN_TITLE, STR_TUTORIAL_BUS_PICK_TOWN_TEXT, TutorialCheck::Reach, 1, MeasureWindow<WindowClass::TownView>, STR_NULL, nullptr},
	{"bus.road_toolbar", TutorialChapter::Bus, STR_TUTORIAL_BUS_ROAD_TOOLBAR_TITLE, STR_TUTORIAL_BUS_ROAD_TOOLBAR_TEXT, TutorialCheck::Reach, 1, MeasureRoadToolbar, STR_NULL,
		[](TutorialHighlights &hl) { HighlightToolbar(hl, WID_TN_ROADS); }},
	{"bus.stops", TutorialChapter::Bus, STR_TUTORIAL_BUS_STOPS_TITLE, STR_TUTORIAL_BUS_STOPS_TEXT, TutorialCheck::Add, 3, CountBusStops, STR_TUTORIAL_PROGRESS_BUS_STOPS,
		[](TutorialHighlights &hl) { HighlightRoadTool(hl, WID_ROT_BUS_STATION); }},
	{"bus.depot", TutorialChapter::Bus, STR_TUTORIAL_BUS_DEPOT_TITLE, STR_TUTORIAL_BUS_DEPOT_TEXT, TutorialCheck::Add, 1, CountRoadDepots, STR_NULL,
		[](TutorialHighlights &hl) { HighlightRoadTool(hl, WID_ROT_DEPOT); }},
	{"bus.buy", TutorialChapter::Bus, STR_TUTORIAL_BUS_BUY_TITLE, STR_TUTORIAL_BUS_BUY_TEXT, TutorialCheck::Add, 1, CountBuses, STR_NULL,
		[](TutorialHighlights &hl) { HighlightAll(hl, WindowClass::VehicleDepot, WID_D_BUILD); HighlightAll(hl, WindowClass::BuildVehicle, WID_BV_BUILD); }},
	{"bus.orders", TutorialChapter::Bus, STR_TUTORIAL_BUS_ORDERS_TITLE, STR_TUTORIAL_BUS_ORDERS_TEXT, TutorialCheck::Add, 1, CountBusesWithOrders, STR_NULL,
		[](TutorialHighlights &hl) {
			if (FindWindowByClass(WindowClass::VehicleOrders) == nullptr) HighlightAll(hl, WindowClass::VehicleView, WID_VV_SHOW_ORDERS);
			HighlightAll(hl, WindowClass::VehicleOrders, WID_O_GOTO);
		}},
	{"bus.share", TutorialChapter::Bus, STR_TUTORIAL_BUS_SHARE_TITLE, STR_TUTORIAL_BUS_SHARE_TEXT, TutorialCheck::Add, 3, CountBuses, STR_TUTORIAL_PROGRESS_BUS_SHARE,
		[](TutorialHighlights &hl) { HighlightAll(hl, WindowClass::VehicleView, WID_VV_CLONE); }},
	{"bus.start", TutorialChapter::Bus, STR_TUTORIAL_BUS_START_TITLE, STR_TUTORIAL_BUS_START_TEXT, TutorialCheck::Add, 1, CountRunningBuses, STR_NULL,
		[](TutorialHighlights &hl) { HighlightAll(hl, WindowClass::VehicleDepot, WID_D_START_ALL); }},
	{"bus.line", TutorialChapter::Bus, STR_TUTORIAL_BUS_LINE_TITLE, STR_TUTORIAL_BUS_LINE_TEXT, TutorialCheck::Reach, 1, MeasureWindow<WindowClass::Line>, STR_NULL,
		[](TutorialHighlights &hl) { HighlightLinesList(hl); }},
	{"bus.line_number", TutorialChapter::Bus, STR_TUTORIAL_BUS_LINE_NUMBER_TITLE, STR_TUTORIAL_BUS_LINE_NUMBER_TEXT, TutorialCheck::Add, 1, CountNumberedLines, STR_NULL,
		[](TutorialHighlights &hl) {
			if (FindWindowByClass(WindowClass::Line) == nullptr) {
				HighlightLinesList(hl);
			} else {
				HighlightAll(hl, WindowClass::Line, WID_LN_NUMBER);
				HighlightAll(hl, WindowClass::Line, WID_LN_NAME);
			}
		}},
	{"bus.wait", TutorialChapter::Bus, STR_TUTORIAL_BUS_WAIT_TITLE, STR_TUTORIAL_BUS_WAIT_TEXT, TutorialCheck::Add, 30, MeasureDate, STR_TUTORIAL_PROGRESS_DAYS,
		[](TutorialHighlights &hl) { HighlightToolbar(hl, WID_TN_FAST_FORWARD); }},
	{"bus.spacing", TutorialChapter::Bus, STR_TUTORIAL_BUS_SPACING_TITLE, STR_TUTORIAL_BUS_SPACING_TEXT, TutorialCheck::Add, 1, CountSpacedLines, STR_NULL,
		[](TutorialHighlights &hl) {
			if (FindWindowByClass(WindowClass::Line) == nullptr) {
				HighlightLinesList(hl);
			} else {
				HighlightAll(hl, WindowClass::Line, WID_LN_TARGET);
				HighlightAll(hl, WindowClass::Line, WID_LN_HOLD);
			}
		}},
	{"bus.fares", TutorialChapter::Bus, STR_TUTORIAL_BUS_FARES_TITLE, STR_TUTORIAL_BUS_FARES_TEXT, TutorialCheck::Change, 0, MeasureBusConcession, STR_NULL,
		[](TutorialHighlights &hl) {
			if (FindWindowByClass(WindowClass::Fares) != nullptr) return;
			if (FindWindowByClass(WindowClass::Finances) != nullptr) {
				HighlightAll(hl, WindowClass::Finances, WID_CF_FARES);
			} else {
				HighlightToolbar(hl, WID_TN_FINANCES);
			}
		}},
	{"bus.departures", TutorialChapter::Bus, STR_TUTORIAL_BUS_DEPARTURES_TITLE, STR_TUTORIAL_BUS_DEPARTURES_TEXT, TutorialCheck::Reach, 1, MeasureWindow<WindowClass::Departures>, STR_NULL,
		[](TutorialHighlights &hl) { HighlightAll(hl, WindowClass::StationView, WID_SV_DEPARTURES); }},
	{"bus.town", TutorialChapter::Bus, STR_TUTORIAL_BUS_TOWN_TITLE, STR_TUTORIAL_BUS_TOWN_TEXT, TutorialCheck::Reach, 1, MeasureWindow<WindowClass::TransitAuthority>, STR_NULL,
		[](TutorialHighlights &hl) { HighlightAll(hl, WindowClass::TownView, WID_TV_TRANSIT); }},
	{"bus.done", TutorialChapter::Bus, STR_TUTORIAL_BUS_DONE_TITLE, STR_TUTORIAL_BUS_DONE_TEXT, TutorialCheck::Read, 0, nullptr, STR_NULL, nullptr},

	/* What's new for OpenTTD players. Based on the guide's "Your first game" and "Saves". */
	{"new.old_save", TutorialChapter::New, STR_TUTORIAL_NEW_OLD_SAVE_TITLE, STR_TUTORIAL_NEW_OLD_SAVE_TEXT, TutorialCheck::Read, 0, nullptr, STR_NULL, nullptr},
	{"new.settings", TutorialChapter::New, STR_TUTORIAL_NEW_SETTINGS_TITLE, STR_TUTORIAL_NEW_SETTINGS_TEXT, TutorialCheck::Read, 0, nullptr, STR_NULL,
		[](TutorialHighlights &hl) { if (FindWindowByClass(WindowClass::GameOptions) == nullptr) HighlightToolbar(hl, WID_TN_SETTINGS); }},
	{"new.zoning", TutorialChapter::New, STR_TUTORIAL_NEW_ZONING_TITLE, STR_TUTORIAL_NEW_ZONING_TEXT, TutorialCheck::Reach, 1, MeasureWindow<WindowClass::TownZoning>, STR_NULL,
		[](TutorialHighlights &hl) { HighlightToolbar(hl, WID_TN_TOWNS); }},
	{"new.services", TutorialChapter::New, STR_TUTORIAL_NEW_SERVICES_TITLE, STR_TUTORIAL_NEW_SERVICES_TEXT, TutorialCheck::Reach, 1, MeasureWindow<WindowClass::CityServices>, STR_NULL,
		[](TutorialHighlights &hl) { HighlightToolbar(hl, WID_TN_TOWNS); }},
	{"new.states", TutorialChapter::New, STR_TUTORIAL_NEW_STATES_TITLE, STR_TUTORIAL_NEW_STATES_TEXT, TutorialCheck::Reach, 1, MeasureWindow<WindowClass::States>, STR_NULL,
		[](TutorialHighlights &hl) { HighlightToolbar(hl, WID_TN_TOWNS); }},
	{"new.town_window", TutorialChapter::New, STR_TUTORIAL_NEW_TOWN_WINDOW_TITLE, STR_TUTORIAL_NEW_TOWN_WINDOW_TEXT, TutorialCheck::Reach, 1, MeasureWindow<WindowClass::TownView>, STR_NULL, nullptr},
	{"new.transit", TutorialChapter::New, STR_TUTORIAL_NEW_TRANSIT_TITLE, STR_TUTORIAL_NEW_TRANSIT_TEXT, TutorialCheck::Reach, 1, MeasureWindow<WindowClass::TransitAuthority>, STR_NULL,
		[](TutorialHighlights &hl) { HighlightAll(hl, WindowClass::TownView, WID_TV_TRANSIT); }},
	{"new.budget", TutorialChapter::New, STR_TUTORIAL_NEW_BUDGET_TITLE, STR_TUTORIAL_NEW_BUDGET_TEXT, TutorialCheck::Reach, 1, MeasureWindow<WindowClass::TownBudget>, STR_NULL,
		[](TutorialHighlights &hl) { HighlightAll(hl, WindowClass::TownView, WID_TV_BUDGET); }},
	{"new.road_tools", TutorialChapter::New, STR_TUTORIAL_NEW_ROAD_TOOLS_TITLE, STR_TUTORIAL_NEW_ROAD_TOOLS_TEXT, TutorialCheck::Read, 0, nullptr, STR_NULL,
		[](TutorialHighlights &hl) {
			const Window *w = FindRoadToolbar();
			if (w == nullptr) {
				HighlightToolbar(hl, WID_TN_ROADS);
				return;
			}
			for (WidgetID widget : {WID_ROT_TRAFFIC_LIGHTS, WID_ROT_TRAFFIC_OVERLAY, WID_ROT_STOP_CHARGER, WID_ROT_BUS_PRIORITY, WID_ROT_TOLLS}) {
				hl.push_back({WindowClass::BuildToolbar, w->window_number, widget});
			}
		}},
	{"new.lines", TutorialChapter::New, STR_TUTORIAL_NEW_LINES_TITLE, STR_TUTORIAL_NEW_LINES_TEXT, TutorialCheck::Read, 0, nullptr, STR_NULL, nullptr},
	{"new.done", TutorialChapter::New, STR_TUTORIAL_NEW_DONE_TITLE, STR_TUTORIAL_NEW_DONE_TEXT, TutorialCheck::Read, 0, nullptr, STR_NULL, nullptr},
};

/**
 * Find a step by the name it has in openttd.cfg.
 * @param id The name.
 * @return Its index, or std::nullopt if there's no such step (any more).
 */
static std::optional<size_t> FindTutorialStep(std::string_view id)
{
	for (size_t i = 0; i < std::size(_tutorial_steps); i++) {
		if (_tutorial_steps[i].id == id) return i;
	}
	return std::nullopt;
}

/**
 * The first step of a chapter.
 * @param chapter The chapter.
 * @return Index of the step.
 */
static size_t FirstStepOf(TutorialChapter chapter)
{
	for (size_t i = 0; i < std::size(_tutorial_steps); i++) {
		if (_tutorial_steps[i].chapter == chapter) return i;
	}
	NOT_REACHED();
}

/**
 * Name of a chapter.
 * @param chapter The chapter.
 * @return Its name.
 */
static StringID GetChapterName(TutorialChapter chapter)
{
	switch (chapter) {
		case TutorialChapter::Start: return STR_TUTORIAL_CHAPTER_START;
		case TutorialChapter::Bus: return STR_TUTORIAL_CHAPTER_BUS;
		case TutorialChapter::New: return STR_TUTORIAL_CHAPTER_NEW;
		default: NOT_REACHED();
	}
}

/** What the tutorial window shows. */
enum class TutorialPage : uint8_t {
	Welcome, ///< Offer on the first start.
	OldSave, ///< Offer when an OpenTTD save is loaded for the first time.
	Contents, ///< The chapters.
	Step, ///< A step.
};

/** The tutorial window. */
struct TutorialWindow : Window {
	TutorialPage page = TutorialPage::Contents; ///< What the window shows.
	size_t step = 0; ///< Index of the step shown, for #TutorialPage::Step.
	int baseline = 0; ///< The step's measure when it began, for Add and Change.
	bool auto_advance = true; ///< Move on by itself when the step is done; off after Back.
	TutorialHighlights highlights{}; ///< The buttons flashing now.
	std::string progress{}; ///< Last progress line drawn, to redraw when it changes.

	TutorialWindow(WindowDesc &desc, TutorialPage page) : Window(desc), page(page)
	{
		this->CreateNestedTree();
		this->UpdateButtons();
		this->FinishInitNested(0);
	}

	Point OnInitialPosition(int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		int gap = ScaleGUITrad(4);
		/* In the main menu: next to it, if there is room. */
		if (const Window *menu = FindWindowById(WindowClass::SelectGame, 0); menu != nullptr) {
			int x = menu->left + menu->width + gap;
			if (x + sm_width <= _screen.width) return {x, menu->top};
		}
		/* In a game: at the top right, under the main toolbar. */
		const Window *toolbar = FindWindowById(WindowClass::MainToolbar, 0);
		int y = (toolbar != nullptr) ? toolbar->top + toolbar->height + gap : gap;
		return {std::max(0, _screen.width - sm_width - gap), y};
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		this->SetHighlights({});
		this->Window::Close();
	}

	/** The step shown. */
	const TutorialStep &CurrentStep() const
	{
		return _tutorial_steps[this->step];
	}

	/**
	 * Whether the step shown can't be done here: it needs a game, or (Start) the main menu.
	 * @return True if it can't.
	 */
	bool StepWaitsForGame() const
	{
		if (this->CurrentStep().chapter == TutorialChapter::Start) return false;
		return !TutorialInGame();
	}

	/**
	 * Whether the step shown is done.
	 * @return True if it is.
	 */
	bool IsStepDone() const
	{
		const TutorialStep &s = this->CurrentStep();
		if (s.chapter == TutorialChapter::Start) {
			/* Once a game runs, the steps before it are done. */
			if (_game_mode == GameMode::Normal) return true;
			if (_game_mode != GameMode::Menu) return false;
		} else if (!TutorialInGame()) {
			return false;
		}

		switch (s.check) {
			case TutorialCheck::Read: return false;
			case TutorialCheck::Reach: return s.measure() >= s.goal;
			case TutorialCheck::Add: return s.measure() - this->baseline >= s.goal;
			case TutorialCheck::Change: return s.measure() != this->baseline;
			default: NOT_REACHED();
		}
	}

	/**
	 * Can the step's measure be taken now?
	 * @return True in the main menu for Start a game, in a game for the others.
	 */
	bool CanMeasure() const
	{
		const TutorialStep &s = this->CurrentStep();
		if (s.measure == nullptr) return false;
		return (s.chapter == TutorialChapter::Start) ? _game_mode == GameMode::Menu : TutorialInGame();
	}

	/**
	 * Remember the step's measure as it is now, for Add and Change. For Add, what the player did
	 * at the step before (Continue, the tutorial opened again, a game loaded) stays counted.
	 */
	void TakeBaseline()
	{
		const TutorialStep &s = this->CurrentStep();
		this->baseline = this->CanMeasure() ? s.measure() : 0;
		if (s.check != TutorialCheck::Add) return;
		if (_settings_client.gui.tutorial_progress_step == s.id) {
			if (this->CanMeasure()) this->baseline -= _settings_client.gui.tutorial_progress;
		} else if (this->auto_advance) {
			/* Shown again after Back, a step only counts anew; the one the player came from keeps its count. */
			_settings_client.gui.tutorial_progress_step = std::string{s.id};
			_settings_client.gui.tutorial_progress = 0;
		}
	}

	/** Keep the count of what the player did at the step, for when they come back to it. */
	void SaveProgress() const
	{
		const TutorialStep &s = this->CurrentStep();
		if (s.check != TutorialCheck::Add || !this->auto_advance || !this->CanMeasure() || _settings_client.gui.tutorial_progress_step != s.id) return;
		_settings_client.gui.tutorial_progress = static_cast<uint16_t>(Clamp(s.measure() - this->baseline, 0, s.goal));
	}

	/** Start the steps afresh: a chapter from its start, or the tutorial finished. */
	static void ForgetProgress()
	{
		_settings_client.gui.tutorial_progress_step.clear();
		_settings_client.gui.tutorial_progress = 0;
	}

	/**
	 * Show a step.
	 * @param index Index of the step.
	 * @param forward True when going forward, so the step moves on by itself once done; false after Back.
	 */
	void GoToStep(size_t index, bool forward)
	{
		this->page = TutorialPage::Step;
		this->auto_advance = forward;
		this->step = index;
		this->TakeBaseline();

		/* Steps done already are passed by, such as starting a game while in one. */
		while (this->auto_advance && this->IsStepDone() && this->HasNextStep()) {
			this->step = this->NextStep();
			this->TakeBaseline();
		}

		_settings_client.gui.tutorial_step = std::string{this->CurrentStep().id};
		_settings_client.gui.tutorial_dismissed = false;
		this->Refresh();
	}

	/**
	 * Whether there's a step after the one shown, in the same chapter or the one it goes on into.
	 * @return True if there is.
	 */
	bool HasNextStep() const
	{
		size_t next = this->step + 1;
		if (next >= std::size(_tutorial_steps)) return false;
		TutorialChapter from = this->CurrentStep().chapter;
		TutorialChapter to = _tutorial_steps[next].chapter;
		/* Start a game goes on into the first bus line. */
		return to == from || (from == TutorialChapter::Start && to == TutorialChapter::Bus);
	}

	/** Index of the next step; see #HasNextStep. */
	size_t NextStep() const
	{
		return this->step + 1;
	}

	/**
	 * Whether there's a step before the one shown, in the same chapter.
	 * @return True if there is.
	 */
	bool HasPreviousStep() const
	{
		return this->step > 0 && _tutorial_steps[this->step - 1].chapter == this->CurrentStep().chapter;
	}

	/**
	 * Number of the step in its chapter, and the chapter's number of steps.
	 * @return The step's number from 1, and the count.
	 */
	std::pair<int, int> StepNumber() const
	{
		TutorialChapter chapter = this->CurrentStep().chapter;
		int number = 0;
		int count = 0;
		for (size_t i = 0; i < std::size(_tutorial_steps); i++) {
			if (_tutorial_steps[i].chapter != chapter) continue;
			count++;
			if (i <= this->step) number++;
		}
		return {number, count};
	}

	/** Leave the tutorial; it keeps its place and doesn't come back by itself. */
	void Leave()
	{
		_settings_client.gui.tutorial_dismissed = true;
		this->Close();
	}

	/** Switch the buttons to the page shown. */
	void UpdateButtons()
	{
		bool offer = this->page == TutorialPage::Welcome || this->page == TutorialPage::OldSave;
		this->GetWidget<NWidgetStacked>(WID_TUT_SEL_STEP)->SetDisplayedPlane(this->page == TutorialPage::Step ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_TUT_SEL_OFFER)->SetDisplayedPlane(offer ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_TUT_SEL_CONTENTS)->SetDisplayedPlane(this->page == TutorialPage::Contents ? 0 : SZSP_NONE);

		if (this->page == TutorialPage::Step) {
			this->SetWidgetDisabledState(WID_TUT_BACK, !this->HasPreviousStep());
			this->GetWidget<NWidgetCore>(WID_TUT_NEXT)->SetToolTip(this->HasNextStep() ? STR_TUTORIAL_NEXT_TOOLTIP : STR_TUTORIAL_FINISH_TOOLTIP);
		}
		if (this->page == TutorialPage::Contents) {
			auto stored = FindTutorialStep(_settings_client.gui.tutorial_step);
			this->SetWidgetDisabledState(WID_TUT_CONTINUE, !stored.has_value());
			/* Starting a game is done from the main menu. */
			this->SetWidgetDisabledState(WID_TUT_CHAPTER_START, _game_mode != GameMode::Menu);
		}
	}

	/** Redo the window for what it shows now. */
	void Refresh()
	{
		this->progress = this->GetStatusText();
		this->UpdateButtons();
		this->ReInit();
		this->UpdateHighlights(true);
	}

	/**
	 * Make the buttons flash that the page asks the player to click.
	 * @param wanted The buttons that should flash.
	 */
	void SetHighlights(const TutorialHighlights &wanted)
	{
		for (const TutorialHighlight &h : this->highlights) {
			if (std::ranges::find(wanted, h) != wanted.end()) continue;
			Window *w = FindWindowById(h.cls, h.number);
			if (w != nullptr && w->IsWidgetHighlighted(h.widget)) w->SetWidgetHighlight(h.widget, TextColour::Invalid);
		}
		for (const TutorialHighlight &h : wanted) {
			Window *w = FindWindowById(h.cls, h.number);
			/* Clicking a highlighted button switches its highlight off; light it again while it is wanted. */
			if (w != nullptr && w->GetWidget<NWidgetBase>(h.widget) != nullptr && !w->IsWidgetHighlighted(h.widget)) {
				w->SetWidgetHighlight(h.widget, TUTORIAL_HIGHLIGHT_COLOUR);
			}
		}
		this->highlights = wanted;
	}

	/**
	 * Work out which buttons should flash now.
	 * @param moved_on True when the page changed, so the window gets out of the way of what the new page is about.
	 */
	void UpdateHighlights(bool moved_on = false)
	{
		TutorialHighlights wanted;
		if (this->page == TutorialPage::Step) {
			if (this->StepWaitsForGame()) {
				wanted.push_back({WindowClass::SelectGame, 0, WID_SGI_GENERATE_GAME});
				wanted.push_back({WindowClass::SelectGame, 0, WID_SGI_LOAD_GAME});
				HighlightAll(wanted, WindowClass::GenerateLandscape, WID_GL_GENERATE_BUTTON);
				HighlightAll(wanted, WindowClass::SaveLoad, WID_SL_LOAD_BUTTON);
			} else if (this->CurrentStep().highlight != nullptr && !this->IsStepDone()) {
				this->CurrentStep().highlight(wanted);
			}
		}
		/* Only when they change, so a player who moves the window keeps it where they put it. */
		bool changed = wanted != this->highlights;
		this->SetHighlights(wanted);
		if (changed || moved_on) this->AvoidTargets(wanted);
	}

	/**
	 * Move the window out of the way of the windows with the buttons to click,
	 * and of construction toolbars, as it is above them and would hide them.
	 * @param wanted The buttons that flash.
	 */
	void AvoidTargets(const TutorialHighlights &wanted)
	{
		std::vector<Rect> targets;
		for (const TutorialHighlight &h : wanted) {
			if (h.cls == WindowClass::MainToolbar) continue; // The toolbar is above the tutorial.
			const Window *w = FindWindowById(h.cls, h.number);
			if (w != nullptr) targets.push_back({w->left, w->top, w->left + w->width - 1, w->top + w->height - 1});
		}
		/* Toolbars that open at the top right, such as Road Construction and Zoning, where the tutorial is;
		 * and the big windows the tutorial asks the player to use. */
		for (const Window *w : Window::Iterate()) {
			if (w == this) continue;
			bool avoid = w->window_desc.flags.Test(WindowDefaultFlag::Construction);
			switch (w->window_class) {
				case WindowClass::GameOptions:
				case WindowClass::GenerateLandscape:
				case WindowClass::SaveLoad:
					avoid = true;
					break;
				default: break;
			}
			if (avoid) targets.push_back({w->left, w->top, w->left + w->width - 1, w->top + w->height - 1});
		}

		auto overlaps = [this, &targets](int x, int y) {
			for (const Rect &r : targets) {
				if (x <= r.right && r.left <= x + this->width - 1 && y <= r.bottom && r.top <= y + this->height - 1) return true;
			}
			return false;
		};
		if (!overlaps(this->left, this->top)) return;

		/* Places beside those windows, then the corners between the toolbar and the status bar. */
		int gap = ScaleGUITrad(4);
		const Window *toolbar = FindWindowById(WindowClass::MainToolbar, 0);
		const Window *statusbar = FindWindowById(WindowClass::Statusbar, 0);
		int min_y = (toolbar != nullptr) ? toolbar->top + toolbar->height + gap : gap;
		int max_y = std::max(min_y, ((statusbar != nullptr) ? statusbar->top : _screen.height) - this->height - gap);
		int max_x = _screen.width - this->width - gap;

		std::vector<Point> places;
		for (const Rect &r : targets) {
			places.push_back({r.right + 1 + gap, r.top});
			places.push_back({r.left - gap - this->width, r.top});
			places.push_back({r.left, r.bottom + 1 + gap});
			places.push_back({r.left, r.top - gap - this->height});
		}
		places.push_back({max_x, min_y});
		places.push_back({max_x, max_y});
		places.push_back({gap, min_y});
		places.push_back({gap, max_y});

		for (Point p : places) {
			if (p.x < gap || p.x > max_x) continue;
			p.y = Clamp(p.y, min_y, max_y);
			if (overlaps(p.x, p.y)) continue;
			this->SetDirty();
			this->left = p.x;
			this->top = p.y;
			this->SetDirty();
			return;
		}
	}

	/**
	 * The line under a step's text: what it waits for, or how far the player got.
	 * @return The line.
	 */
	std::string GetStatusText() const
	{
		if (this->page != TutorialPage::Step) return {};
		const TutorialStep &s = this->CurrentStep();
		if (this->StepWaitsForGame()) return GetString(STR_TUTORIAL_STATUS_NEEDS_GAME);
		if (s.check == TutorialCheck::Read) {
			return GetString(this->HasNextStep() ? STR_TUTORIAL_STATUS_READ : STR_TUTORIAL_STATUS_LAST);
		}
		if (this->IsStepDone()) return GetString(STR_TUTORIAL_STATUS_DONE);
		if (s.progress != STR_NULL) {
			if (s.check == TutorialCheck::Add) return GetString(s.progress, std::max(0, s.measure() - this->baseline), s.goal);
			return GetString(s.progress, s.measure(), s.goal);
		}
		return GetString(STR_TUTORIAL_STATUS_WAITING);
	}

	/**
	 * The parts of the page's text: a heading, the text and a status line.
	 * @return The parts; empty ones aren't drawn.
	 */
	std::array<std::string, 3> GetTextParts() const
	{
		switch (this->page) {
			case TutorialPage::Welcome:
				return {GetString(STR_TUTORIAL_TITLE, STR_TUTORIAL_WELCOME_TITLE), GetString(STR_TUTORIAL_WELCOME_TEXT), {}};
			case TutorialPage::OldSave:
				return {GetString(STR_TUTORIAL_TITLE, STR_TUTORIAL_OLD_SAVE_TITLE), GetString(STR_TUTORIAL_OLD_SAVE_TEXT), {}};
			case TutorialPage::Contents: {
				auto stored = FindTutorialStep(_settings_client.gui.tutorial_step);
				std::string status = stored.has_value() ? GetString(STR_TUTORIAL_CONTENTS_STOPPED_AT, GetChapterName(_tutorial_steps[*stored].chapter), _tutorial_steps[*stored].title) : std::string{};
				return {GetString(STR_TUTORIAL_TITLE, STR_TUTORIAL_CONTENTS_TITLE), GetString(STR_TUTORIAL_CONTENTS_TEXT), status};
			}
			default:
				return {GetString(STR_TUTORIAL_TITLE, this->CurrentStep().title), GetString(this->CurrentStep().text), this->progress};
		}
	}

	/**
	 * Height of the page's text.
	 * @param width Width of the text.
	 * @return The height.
	 */
	int GetTextHeight(int width) const
	{
		int height = 0;
		for (const std::string &part : this->GetTextParts()) {
			if (part.empty()) continue;
			if (height > 0) height += WidgetDimensions::scaled.vsep_wide;
			height += GetStringHeight(part, width);
		}
		return height;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_TUT_CLOSE: {
				Dimension d = GetScaledSpriteSize(SPR_CLOSEBOX);
				d.width += WidgetDimensions::scaled.closebox.Horizontal();
				d.height += WidgetDimensions::scaled.closebox.Vertical();
				size = maxdim(size, d);
				break;
			}

			case WID_TUT_TEXT:
				size.width = std::max<uint>(size.width, ScaleGUITrad(TUTORIAL_TEXT_WIDTH));
				size.height = std::max<uint>(size.height, this->GetTextHeight(size.width));
				break;
		}
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_TUT_CAPTION:
				switch (this->page) {
					case TutorialPage::Welcome:
					case TutorialPage::OldSave:
					case TutorialPage::Contents:
						return GetString(STR_TUTORIAL_CAPTION);
					default: {
						auto [number, count] = this->StepNumber();
						return GetString(STR_TUTORIAL_CAPTION_STEP, GetChapterName(this->CurrentStep().chapter), number, count);
					}
				}

			case WID_TUT_NEXT:
				return GetString(this->HasNextStep() ? STR_TUTORIAL_NEXT : STR_TUTORIAL_FINISH);

			case WID_TUT_START:
				return GetString(this->page == TutorialPage::OldSave ? STR_TUTORIAL_OLD_SAVE_START : STR_TUTORIAL_WELCOME_START);

			default:
				return this->Window::GetWidgetString(widget, stringid);
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_TUT_CLOSE: {
				/* Drawn like a close box; a close box of its own would also close the window when games start or load. */
				Point offset;
				Dimension d = GetSpriteSize(SPR_CLOSEBOX, &offset);
				d.width -= offset.x;
				d.height -= offset.y;
				int s = ScaleSpriteTrad(1); // Offset to account for shadow of SPR_CLOSEBOX.
				DrawSprite(SPR_CLOSEBOX, to_underlying(TextColour::Black) | (1U << PALETTE_TEXT_RECOLOUR), CentreBounds(r.left, r.right, d.width - s) - offset.x, CentreBounds(r.top, r.bottom, d.height - s) - offset.y);
				break;
			}

			case WID_TUT_TEXT: {
				int y = r.top;
				bool first = true;
				for (const std::string &part : this->GetTextParts()) {
					if (part.empty()) continue;
					if (!first) y += WidgetDimensions::scaled.vsep_wide;
					/* Lay out by measured height, not DrawString's return value (0 outside the redrawn part). */
					int height = GetStringHeight(part, r.Width());
					DrawStringMultiLine(r.left, r.right, y, y + height, part, TextColour::FromString);
					y += height;
					first = false;
				}
				break;
			}
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_TUT_CLOSE:
			case WID_TUT_EXIT:
			case WID_TUT_CONTENTS_EXIT:
				this->Leave();
				break;

			case WID_TUT_NOT_NOW:
				_settings_client.gui.tutorial_dismissed = true;
				this->Close();
				break;

			case WID_TUT_START:
				ForgetProgress();
				if (this->page == TutorialPage::OldSave) {
					this->GoToStep(FirstStepOf(TutorialChapter::New), true);
				} else {
					this->GoToStep(FirstStepOf(_game_mode == GameMode::Menu ? TutorialChapter::Start : TutorialChapter::Bus), true);
				}
				break;

			case WID_TUT_CONTENTS:
				this->page = TutorialPage::Contents;
				this->Refresh();
				break;

			case WID_TUT_BACK:
				if (this->HasPreviousStep()) this->GoToStep(this->step - 1, false);
				break;

			case WID_TUT_NEXT:
				if (this->HasNextStep()) {
					this->GoToStep(this->NextStep(), true);
				} else {
					/* The end of a chapter: Finish closes the tutorial, with nothing to come back to.
					 * Tutorial in the main menu or the Help menu opens its chapters again. */
					_settings_client.gui.tutorial_step.clear();
					ForgetProgress();
					this->Close();
				}
				break;

			case WID_TUT_CONTINUE:
				if (auto stored = FindTutorialStep(_settings_client.gui.tutorial_step); stored.has_value()) this->GoToStep(*stored, true);
				break;

			case WID_TUT_CHAPTER_START:
				ForgetProgress();
				this->GoToStep(FirstStepOf(TutorialChapter::Start), true);
				break;

			case WID_TUT_CHAPTER_BUS:
				ForgetProgress();
				this->GoToStep(FirstStepOf(TutorialChapter::Bus), true);
				break;

			case WID_TUT_CHAPTER_NEW:
				ForgetProgress();
				this->GoToStep(FirstStepOf(TutorialChapter::New), true);
				break;
		}
	}

	/** Look whether the step is done, and keep its buttons flashing. */
	const IntervalTimer<TimerWindow> check_interval = {std::chrono::milliseconds(250), [this](auto) {
		if (this->page != TutorialPage::Step) return;
		this->SaveProgress();
		if (this->auto_advance && this->IsStepDone() && this->HasNextStep()) {
			this->GoToStep(this->NextStep(), true);
			return;
		}
		std::string status = this->GetStatusText();
		if (status != this->progress) {
			this->progress = std::move(status);
			this->ReInit();
		}
		this->UpdateHighlights();
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_tutorial_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHBTN, Colours::LightBlue, WID_TUT_CLOSE), SetMinimalSize(11, 14), SetToolTip(STR_TUTORIAL_CLOSE_TOOLTIP), // The minimal size of a close box.
		NWidget(WWT_CAPTION, Colours::LightBlue, WID_TUT_CAPTION), SetToolTip(STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::LightBlue),
		NWidget(WWT_EMPTY, Colours::Invalid, WID_TUT_TEXT), SetPadding(WidgetDimensions::unscaled.frametext), SetFill(1, 0), SetMinimalSize(TUTORIAL_TEXT_WIDTH, 0),
	EndContainer(),
	NWidget(NWID_SELECTION, Colours::Invalid, WID_TUT_SEL_STEP),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			NWidget(WWT_PUSHTXTBTN, Colours::LightBlue, WID_TUT_CONTENTS), SetFill(1, 0), SetStringTip(STR_TUTORIAL_CONTENTS, STR_TUTORIAL_CONTENTS_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::LightBlue, WID_TUT_BACK), SetFill(1, 0), SetStringTip(STR_TUTORIAL_BACK, STR_TUTORIAL_BACK_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::LightBlue, WID_TUT_NEXT), SetFill(1, 0), SetToolTip(STR_TUTORIAL_NEXT_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::LightBlue, WID_TUT_EXIT), SetFill(1, 0), SetStringTip(STR_TUTORIAL_EXIT, STR_TUTORIAL_EXIT_TOOLTIP),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_SELECTION, Colours::Invalid, WID_TUT_SEL_OFFER),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			NWidget(WWT_PUSHTXTBTN, Colours::LightBlue, WID_TUT_START), SetFill(1, 0), SetToolTip(STR_TUTORIAL_START_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::LightBlue, WID_TUT_NOT_NOW), SetFill(1, 0), SetStringTip(STR_TUTORIAL_NOT_NOW, STR_TUTORIAL_NOT_NOW_TOOLTIP),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_SELECTION, Colours::Invalid, WID_TUT_SEL_CONTENTS),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_PUSHTXTBTN, Colours::LightBlue, WID_TUT_CHAPTER_START), SetFill(1, 0), SetStringTip(STR_TUTORIAL_CHAPTER_START_BUTTON, STR_TUTORIAL_CHAPTER_START_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::LightBlue, WID_TUT_CHAPTER_BUS), SetFill(1, 0), SetStringTip(STR_TUTORIAL_CHAPTER_BUS_BUTTON, STR_TUTORIAL_CHAPTER_BUS_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::LightBlue, WID_TUT_CHAPTER_NEW), SetFill(1, 0), SetStringTip(STR_TUTORIAL_CHAPTER_NEW_BUTTON, STR_TUTORIAL_CHAPTER_NEW_TOOLTIP),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
				NWidget(WWT_PUSHTXTBTN, Colours::LightBlue, WID_TUT_CONTINUE), SetFill(1, 0), SetStringTip(STR_TUTORIAL_CONTINUE, STR_TUTORIAL_CONTINUE_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, Colours::LightBlue, WID_TUT_CONTENTS_EXIT), SetFill(1, 0), SetStringTip(STR_TUTORIAL_EXIT, STR_TUTORIAL_EXIT_TOOLTIP),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/**
 * The tutorial window. It can't be closed by "close all windows"; it closes
 * with its own close box or Exit tutorial (the player leaves it), or when
 * the game switches to another game (then it comes back in the next one).
 */
static WindowDesc _tutorial_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::Tutorial, WindowClass::None,
	{WindowDefaultFlag::NoClose, WindowDefaultFlag::NoFocus},
	_nested_tutorial_widgets
);

/**
 * Open the tutorial window, or bring it to the front.
 * @param page What it shows first.
 * @return The window.
 */
static TutorialWindow *OpenTutorialWindow(TutorialPage page)
{
	/* Players who have used it don't need it offered on the first start. */
	_settings_client.gui.tutorial_seen = true;
	TutorialWindow *w = dynamic_cast<TutorialWindow *>(BringWindowToFrontById(WindowClass::Tutorial, 0));
	if (w == nullptr) w = new TutorialWindow(_tutorial_desc, page);
	return w;
}

/**
 * Open the tutorial (Help menu, main menu): at the step the player was at,
 * or at the chapters if they weren't at one.
 */
void ShowTutorial()
{
	TutorialWindow *w = OpenTutorialWindow(TutorialPage::Contents);
	auto stored = FindTutorialStep(_settings_client.gui.tutorial_step);
	if (stored.has_value()) {
		w->GoToStep(*stored, true);
	} else {
		_settings_client.gui.tutorial_dismissed = false;
		w->page = TutorialPage::Contents;
		w->Refresh();
	}
}

/** The main menu opened: offer the tutorial on the first start, or go on with it. */
void TutorialOnMainMenu()
{
	if (!VideoDriver::GetInstance()->HasGUI()) return;

	if (!_settings_client.gui.tutorial_seen) {
		OpenTutorialWindow(TutorialPage::Welcome);
		return;
	}

	/* Players in the middle of it see it again; steps that need a game say so, and point at New Game and Load Game. */
	auto stored = FindTutorialStep(_settings_client.gui.tutorial_step);
	if (!_settings_client.gui.tutorial_dismissed && stored.has_value()) {
		OpenTutorialWindow(TutorialPage::Step)->GoToStep(*stored, true);
	}
}

/**
 * A game started or was loaded: go on with the tutorial if the player is in
 * it, or offer it the first time an OpenTTD save is loaded.
 * @param openttd_save True if the game was loaded from a regular OpenTTD save (.sav).
 */
void TutorialOnGameStart(bool openttd_save)
{
	if (!VideoDriver::GetInstance()->HasGUI() || _game_mode != GameMode::Normal) return;

	auto stored = FindTutorialStep(_settings_client.gui.tutorial_step);
	if (!_settings_client.gui.tutorial_dismissed && stored.has_value()) {
		OpenTutorialWindow(TutorialPage::Step)->GoToStep(*stored, true);
		return;
	}

	if (openttd_save && !_settings_client.gui.tutorial_seen_old_save) {
		_settings_client.gui.tutorial_seen_old_save = true;
		OpenTutorialWindow(TutorialPage::OldSave);
	}
}
