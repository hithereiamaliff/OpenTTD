/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file road_traffic.cpp How busy and congested roads are, for the traffic map.
 *
 * Every few ticks, every road vehicle on the road tells whether it drives
 * freely or is held up. Per tile this adds up to two slowly fading scores.
 * This is only for display: it is not saved and never influences the game, so
 * it doesn't matter that each player in a network game collects its own.
 */

#include "stdafx.h"
#include "road_traffic.h"
#include "network/network.h"
#include "order_type.h"
#include "road_map.h"
#include "roadveh.h"
#include "town_traffic.h"
#include "timer/timer.h"
#include "timer/timer_game_tick.h"
#include "timer/timer_window.h"
#include "gfx_func.h"

#include "safeguards.h"

/** Scores of one road tile. */
struct RoadTrafficScore {
	uint16_t held_up = 0; ///< Score of vehicles standing or crawling here.
	uint16_t passing = 0; ///< Score of vehicles driving on here.
};

/** Recent traffic per road tile; tiles without traffic are left out. */
static std::unordered_map<TileIndex, RoadTrafficScore> _road_traffic;

/** How often to look at the vehicles, in ticks. */
static constexpr uint TRAFFIC_SAMPLE_INTERVAL = 16;
/** Score a vehicle adds per look; scores lose 1/16 per look, so one vehicle always there scores about 240. */
static constexpr uint16_t TRAFFIC_SAMPLE_WEIGHT = 16;
/** Highest score. */
static constexpr uint16_t TRAFFIC_SCORE_MAX = 4000;

/**
 * Does this vehicle count as held up: waiting, or crawling below a quarter of its top speed?
 * @param v The front of the vehicle.
 * @return True if it is held up.
 */
static bool IsHeldUp(const RoadVehicle *v)
{
	return v->blocked_ctr > 0 || v->cur_speed * 4 < v->vcache.cached_max_speed;
}

/** Look at all road vehicles and update the scores. */
static void SampleRoadTraffic()
{
	if (_network_dedicated) return;

	for (const RoadVehicle *v : RoadVehicle::Iterate()) {
		if (!v->IsFrontEngine() || v->IsInDepot() || v->state == RVSB_WORMHOLE) continue;
		if (v->vehstatus.Any({VehState::Stopped, VehState::Crashed}) || v->breakdown_ctr != 0) continue;
		/* Vehicles loading at a station are not traffic. */
		if (v->current_order.IsType(OT_LOADING)) continue;

		RoadTrafficScore &score = _road_traffic[v->tile];
		uint16_t &value = IsHeldUp(v) ? score.held_up : score.passing;
		value = std::min<uint16_t>(value + TRAFFIC_SAMPLE_WEIGHT, TRAFFIC_SCORE_MAX);
	}

	/* Let older observations fade away. */
	for (auto it = _road_traffic.begin(); it != _road_traffic.end(); /* nothing */) {
		RoadTrafficScore &score = it->second;
		score.held_up -= score.held_up / 16 + (score.held_up > 0 ? 1 : 0);
		score.passing -= score.passing / 16 + (score.passing > 0 ? 1 : 0);
		if (score.held_up == 0 && score.passing == 0) {
			it = _road_traffic.erase(it);
		} else {
			++it;
		}
	}
}

/** Sample the road traffic regularly. */
static const IntervalTimer<TimerGameTick> _road_traffic_sampler({TimerGameTick::Priority::None, TRAFFIC_SAMPLE_INTERVAL}, [](auto) {
	SampleRoadTraffic();
});

/**
 * How held up have vehicles on a road tile been lately?
 * @param tile The tile.
 * @return The traffic level.
 */
static RoadTrafficLevel GetVehicleTrafficLevel(TileIndex tile)
{
	auto it = _road_traffic.find(tile);
	if (it == _road_traffic.end()) return RoadTrafficLevel::Quiet;

	const RoadTrafficScore &score = it->second;
	if (score.held_up >= 300) return RoadTrafficLevel::Jammed;
	if (score.held_up >= 120) return RoadTrafficLevel::Slow;
	if (score.held_up >= 40) return RoadTrafficLevel::Waiting;
	if (score.passing >= 16 || score.held_up > 0) return RoadTrafficLevel::Flowing;
	return RoadTrafficLevel::Quiet;
}

/**
 * How busy is a road tile: the busier of what the vehicles on it went
 * through lately and the town traffic on it.
 * @param tile The tile.
 * @return The traffic level.
 */
RoadTrafficLevel GetRoadTrafficLevel(TileIndex tile)
{
	static constexpr RoadTrafficLevel TOWN_TRAFFIC_LEVEL[ROAD_CONGESTION_MAX + 1] = {
		RoadTrafficLevel::Quiet, RoadTrafficLevel::Flowing, RoadTrafficLevel::Waiting, RoadTrafficLevel::Waiting,
		RoadTrafficLevel::Slow, RoadTrafficLevel::Slow, RoadTrafficLevel::Jammed, RoadTrafficLevel::Jammed,
	};
	RoadTrafficLevel level = GetVehicleTrafficLevel(tile);
	if (IsTileType(tile, TileType::Road) && !IsRoadDepot(tile)) level = std::max(level, TOWN_TRAFFIC_LEVEL[GetRoadCongestion(tile)]);
	return level;
}

/** Whether road tiles in the main view are coloured by traffic. */
static bool _show_road_traffic_overlay = false;

/**
 * Are road tiles in the main view coloured by traffic?
 * @return True if the overlay is shown.
 */
bool IsRoadTrafficOverlayShown()
{
	return _show_road_traffic_overlay;
}

/**
 * Colour road tiles in the main view by traffic, or stop doing so.
 * @param show Whether to show the overlay.
 */
void ShowRoadTrafficOverlay(bool show)
{
	if (_show_road_traffic_overlay == show) return;
	_show_road_traffic_overlay = show;
	MarkWholeScreenDirty();
}

/** While the overlay is shown, redraw now and then so it follows the traffic. */
static const IntervalTimer<TimerWindow> _road_traffic_overlay_refresh(std::chrono::seconds(1), [](auto) {
	if (_show_road_traffic_overlay) MarkWholeScreenDirty();
});

/** Forget all traffic; used when starting or loading a game. */
void InitializeRoadTraffic()
{
	_road_traffic.clear();
}
