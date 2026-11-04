/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file road_junction.cpp Right of way for road vehicles at junctions.
 *
 * In regular OpenTTD road vehicles drive through each other at junctions. With
 * the "give way" setting, a junction is shared per road axis: a vehicle only
 * drives onto it when no vehicle that came in over the crossing road is on it.
 *
 * A vehicle also stays out of a junction when the road it leaves by is backed
 * up right behind the junction ("don't block the box"), so it can't end up
 * stuck on the junction in front of crossing traffic.
 *
 * Without traffic lights, vehicles waiting at a junction take turns: whoever
 * has waited longest (the vehicle's blocked counter) goes first, ties broken by
 * vehicle index. So a busy junction serves the two roads one vehicle at a time,
 * while traffic lights let a whole queue through on green.
 *
 * A vehicle that still gets stuck on a junction stops holding up crossing
 * traffic after a short while, so a backed-up road can't cause a gridlock.
 *
 * Nothing extra is saved: the decision only depends on where vehicles are and
 * on their blocked counter, which savegames already contain. A vehicle that has
 * waited far too long goes anyway, so a gridlock can never last forever.
 */

#include "stdafx.h"
#include "road_junction.h"
#include "direction_func.h"
#include "map_func.h"
#include "order_type.h"
#include "road_map.h"
#include "roadveh.h"
#include "settings_type.h"
#include "track_func.h"
#include "traffic_light.h"
#include "vehicle_func.h"
#include "core/bitmath_func.hpp"
#include "debug.h"

#include "safeguards.h"

/** After this many attempts to enter a junction, a waiting vehicle goes anyway. */
static constexpr uint16_t GIVE_WAY_PATIENCE = 400;

/**
 * A vehicle that has been stuck on a junction for this many attempts to move on
 * (because the road it leaves by is backed up) no longer holds up crossing
 * traffic; that squeezes past, as in regular OpenTTD. This keeps a backed-up
 * road from turning into a gridlock.
 */
static constexpr uint16_t STUCK_ON_JUNCTION = 32;

/**
 * Is the tile a junction where right of way matters (three or four road pieces)?
 * @param tile A plain road tile.
 * @return True for a junction.
 */
static bool IsRoadJunction(TileIndex tile)
{
	return GetAllRoadBits(tile).Count() >= 3;
}

/**
 * Is a vehicle held up, i.e. not driving on freely? Its speed is no good
 * measure: a vehicle that has to wait gets speed 0, but speeds up again between
 * its attempts to move on. Its blocked counter only resets once it moves freely.
 * @param front The front of the vehicle.
 * @return True if it is stopped or blocked.
 */
static bool IsHeldUp(const RoadVehicle *front)
{
	return front->cur_speed == 0 || front->blocked_ctr > 0;
}

/**
 * The axis along which a vehicle part on a plain road tile came onto it.
 * @param rv The vehicle part.
 * @return The axis, or Axis::Invalid when it is turning around or not on the road surface.
 */
static Axis GetEntryAxis(const RoadVehicle *rv)
{
	if (rv->state > RVSB_TRACKDIR_MASK) return Axis::Invalid;
	Trackdir td = static_cast<Trackdir>(rv->state);
	if (IsReversingRoadTrackdir(td)) return Axis::Invalid;
	/* The vehicle came in the opposite way of where the reversed trackdir leaves; the axis is the same. */
	return DiagDirToAxis(TrackdirToExitdir(ReverseTrackdir(td)));
}

/**
 * The direction a vehicle part is heading to leave its current tile.
 * @param rv The vehicle part.
 * @return The direction, or DiagDirection::Invalid when unknown (depot, tunnel, bridge, bay road stop, turning around).
 */
static DiagDirection GetHeading(const RoadVehicle *rv)
{
	uint8_t state = rv->state;
	if (state == RVSB_IN_DEPOT || state == RVSB_WORMHOLE) return DiagDirection::Invalid;
	if (HasBit(state, RVS_IN_DT_ROAD_STOP)) {
		state &= RVSB_ROAD_STOP_TRACKDIR_MASK;
	} else if (state > RVSB_TRACKDIR_MASK) {
		return DiagDirection::Invalid;
	}
	Trackdir td = static_cast<Trackdir>(state);
	if (IsReversingRoadTrackdir(td)) return DiagDirection::Invalid;
	return TrackdirToExitdir(td);
}

/**
 * Is the tile a plain road junction (three or four road pieces)?
 * @param tile The tile.
 * @return True for a junction.
 */
bool IsRoadJunctionTile(TileIndex tile)
{
	return IsNormalRoadTile(tile) && IsRoadJunction(tile);
}

/**
 * How many vehicles are held up on and right in front of a junction? The
 * pathfinder uses this to let vehicles drive around jams. Vehicles loading
 * at a stop next to the junction don't count.
 * @param junction The junction tile.
 * @return The number of held up vehicles.
 */
uint CountVehiclesHeldUpAt(TileIndex junction)
{
	auto held_up = [](const Vehicle *u) {
		if (u->type != VehicleType::Road || !u->IsFrontEngine() || u->vehstatus.Test(VehState::Crashed)) return false;
		const RoadVehicle *rv = RoadVehicle::From(u);
		if (rv->current_order.IsType(OT_LOADING)) return false;
		return rv->blocked_ctr > 0 || rv->cur_speed * 4 < rv->vcache.cached_max_speed;
	};

	uint count = 0;
	for (const Vehicle *u : VehiclesOnTile(junction)) {
		if (held_up(u)) count++;
	}
	for (DiagDirection side : {DiagDirection::NE, DiagDirection::SE, DiagDirection::SW, DiagDirection::NW}) {
		TileIndex approach = TileAddByDiagDir(junction, side);
		if (!IsValidTile(approach)) continue;
		for (const Vehicle *u : VehiclesOnTile(approach)) {
			if (held_up(u) && GetHeading(RoadVehicle::From(u)) == ReverseDiagDir(side)) count++;
		}
	}
	return count;
}

/**
 * Is a vehicle that came in over the crossing road on the junction?
 * @param v The vehicle that wants to enter.
 * @param tile The junction tile.
 * @param axis The axis along which \a v enters.
 * @return True if \a v has to wait for it.
 */
static bool HasCrossingTrafficOnJunction(const RoadVehicle *v, TileIndex tile, Axis axis)
{
	for (const Vehicle *u : VehiclesOnTile(tile)) {
		if (u->type != VehicleType::Road || u->First() == v) continue;
		if (u->vehstatus.Test(VehState::Crashed)) continue;
		const RoadVehicle *front = RoadVehicle::From(u->First());
		if (front->blocked_ctr >= STUCK_ON_JUNCTION) continue;
		/* A vehicle turning around blocks everyone. */
		if (GetEntryAxis(RoadVehicle::From(u)) != axis) return true;
	}
	return false;
}

/**
 * Find the vehicle waiting at the junction on one of its approaches.
 * @param junction The junction tile.
 * @param side The side of the junction the approach is on.
 * @return The front vehicle stopped there to enter the junction, or nullptr.
 */
static const RoadVehicle *GetVehicleWaitingAt(TileIndex junction, DiagDirection side)
{
	TileIndex approach = TileAddByDiagDir(junction, side);
	if (!IsValidTile(approach)) return nullptr;

	const DiagDirection towards_junction = ReverseDiagDir(side);
	const int centre_x = TileX(junction) * TILE_SIZE + TILE_SIZE / 2;
	const int centre_y = TileY(junction) * TILE_SIZE + TILE_SIZE / 2;

	/* The vehicle part heading for the junction that is closest to it. */
	const RoadVehicle *closest = nullptr;
	int closest_distance = INT_MAX;
	for (const Vehicle *u : VehiclesOnTile(approach)) {
		if (u->type != VehicleType::Road || u->vehstatus.Test(VehState::Crashed)) continue;
		const RoadVehicle *rv = RoadVehicle::From(u);
		if (GetHeading(rv) != towards_junction) continue;

		int distance = abs(rv->x_pos - centre_x) + abs(rv->y_pos - centre_y);
		if (distance < closest_distance || (distance == closest_distance && rv->index < closest->index)) {
			closest = rv;
			closest_distance = distance;
		}
	}

	/* If that is not a front engine, the vehicle it belongs to is already driving onto the junction. */
	if (closest == nullptr || !closest->IsFrontEngine()) return nullptr;
	if (closest->blocked_ctr == 0) return nullptr;
	return closest;
}

/**
 * How far a vehicle part next to a junction is from the junction's edge.
 * @param u The vehicle part, on the tile on \a side of the junction.
 * @param junction The junction tile.
 * @param side The side of the junction the vehicle part is on.
 * @return The distance in sub tile units; 0 means right at the edge.
 */
static int DistanceFromJunction(const Vehicle *u, TileIndex junction, DiagDirection side)
{
	bool x_axis = DiagDirToAxis(side) == Axis::X;
	int centre = (x_axis ? TileX(junction) : TileY(junction)) * TILE_SIZE + TILE_SIZE / 2;
	int position = x_axis ? u->x_pos : u->y_pos;
	return std::abs(position - centre) - TILE_SIZE / 2;
}

/** A stopped vehicle closer than this to the junction leaves no room behind it. */
static constexpr int EXIT_ROOM_NEEDED = 12;

/**
 * Would a vehicle get stuck on the junction because the road it leaves by is
 * backed up? Uses the vehicle's cached route; when that doesn't cover the
 * junction, assume there is room.
 * @param v The vehicle that wants to cross the junction.
 * @param junction The junction tile.
 * @return True if a stopped vehicle sits right behind the exit.
 */
static bool IsExitBlocked(const RoadVehicle *v, TileIndex junction)
{
	if (v->path.empty() || v->path.back().tile != junction) return false;
	Trackdir td = v->path.back().trackdir;
	if (!IsValidTrackdirForRoadVehicle(td) || IsReversingRoadTrackdir(td)) return false;

	DiagDirection exitdir = TrackdirToExitdir(td);
	TileIndex exit_tile = TileAddByDiagDir(junction, exitdir);
	if (!IsValidTile(exit_tile)) return false;

	for (const Vehicle *u : VehiclesOnTile(exit_tile)) {
		if (u->type != VehicleType::Road || u->First() == v) continue;
		if (u->vehstatus.Test(VehState::Crashed) || !IsHeldUp(RoadVehicle::From(u->First()))) continue;
		/* Traffic in the other lane is coming towards the junction. */
		if (GetHeading(RoadVehicle::From(u)) == ReverseDiagDir(exitdir)) continue;
		if (DistanceFromJunction(u, junction, exitdir) < EXIT_ROOM_NEEDED) return true;
	}
	return false;
}

/**
 * Has a waiting vehicle the right of way over another one?
 * @param a The one vehicle.
 * @param b The other vehicle.
 * @return True if \a a goes first.
 */
static bool HasRightOfWay(const RoadVehicle *a, const RoadVehicle *b)
{
	if (a->blocked_ctr != b->blocked_ctr) return a->blocked_ctr > b->blocked_ctr;
	return a->index < b->index;
}

/**
 * Must a vehicle without traffic lights give way to a vehicle waiting on the crossing road?
 * @param v The vehicle that wants to enter.
 * @param tile The junction tile.
 * @param axis The axis along which \a v enters.
 * @return True if a vehicle on the crossing road goes first.
 */
static bool MustGiveWayToCrossingRoad(const RoadVehicle *v, TileIndex tile, Axis axis)
{
	RoadBits bits = GetAllRoadBits(tile);
	DiagDirection side = AxisToDiagDir(OtherAxis(axis));
	for (DiagDirection d : {side, ReverseDiagDir(side)}) {
		if (!bits.Any(DiagDirToRoadBits(d))) continue;
		const RoadVehicle *w = GetVehicleWaitingAt(tile, d);
		/* A vehicle that can't leave the junction anyway doesn't hold up the others. */
		if (w != nullptr && HasRightOfWay(w, v) && !IsExitBlocked(w, tile)) return true;
	}
	return false;
}

/**
 * Let a vehicle wait in front of a junction.
 * @param v The waiting vehicle.
 * @return False, so callers can return the result.
 */
static bool WaitAtJunction(RoadVehicle *v)
{
	if (v->blocked_ctr < UINT16_MAX) v->blocked_ctr++;
	return false;
}

/**
 * A road vehicle is about to drive onto a plain road tile: may it go now?
 * Traffic lights decide at junctions that have them; with the give-way
 * setting, vehicles also wait for crossing traffic on the junction and, without
 * lights, take turns with vehicles waiting on the crossing road.
 * Only call this when the vehicle is about to enter, as it counts waiting time
 * and tells traffic lights about waiting and passing vehicles.
 * @param v The (front) vehicle.
 * @param tile The plain road tile it is about to enter.
 * @param enterdir The direction the vehicle is travelling in.
 * @return True if it may enter.
 */
bool RoadVehicleMayEnterTile(RoadVehicle *v, TileIndex tile, DiagDirection enterdir)
{
	const Axis axis = DiagDirToAxis(enterdir);
	const bool give_way = _settings_game.vehicle.road_give_way;

	if (HasTrafficLight(tile)) {
		TrafficLight *tl = GetTrafficLight(tile);
		if (tl == nullptr) return true;
		if (!tl->IsGreenFor(axis)) {
			/* Red lights are never ignored; they switch after a while anyway. */
			SetBit(tl->demand, to_underlying(axis));
			return false;
		}
		if (give_way && v->blocked_ctr < GIVE_WAY_PATIENCE) {
			if (HasCrossingTrafficOnJunction(v, tile, axis)) return WaitAtJunction(v);
			if (IsExitBlocked(v, tile)) return WaitAtJunction(v);
		}
		tl->entered = true;
		return true;
	}

	if (!give_way || !IsRoadJunction(tile)) return true;
	if (v->blocked_ctr >= GIVE_WAY_PATIENCE) {
		Debug(Facility::Misc, Severity::Debug1, "Road vehicle {} stops giving way at {}x{} after waiting too long", v->unitnumber, TileX(tile), TileY(tile));
		return true;
	}
	if (HasCrossingTrafficOnJunction(v, tile, axis)) return WaitAtJunction(v);
	/* Don't block the junction for crossing traffic when the way out is backed up. */
	if (IsExitBlocked(v, tile)) return WaitAtJunction(v);
	if (MustGiveWayToCrossingRoad(v, tile, axis)) return WaitAtJunction(v);
	return true;
}
