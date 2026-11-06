/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file street_life.cpp Street life: cars, vans, lorries and motorbikes on the roads, drawn as scenery. */

#include "stdafx.h"
#include "street_life.h"
#include "core/random_func.hpp"
#include "genworld.h"
#include "landscape.h"
#include "map_func.h"
#include "network/network.h"
#include "openttd.h"
#include "road.h"
#include "road_map.h"
#include "settings_type.h"
#include "spritecache.h"
#include "station_base.h"
#include "station_map.h"
#include "cargotype.h"
#include "road_func.h"
#include "roadstop_base.h"
#include "tile_cmd.h"
#include "timer/timer.h"
#include "timer/timer_game_tick.h"
#include "town.h"
#include "town_traffic.h"
#include "track_func.h"
#include "traffic_light.h"
#include "vehicle_base.h"
#include "vehicle_func.h"
#include "viewport_func.h"
#include "window_gui.h"
#include "zoom_func.h"

#include "table/sprites.h"

#include "safeguards.h"

/** Positions are in 1/256 of a world unit. */
static constexpr int32_t UNIT = 256;
/** Length of a tile side in those. */
static constexpr int32_t TILE_LENGTH = TILE_SIZE * UNIT;
/** Most street life vehicles at once. */
static constexpr size_t MAX_STREET_VEHICLES = 1500;
/** Furthest zoom at which street life is shown. */
static constexpr ZoomLevel STREET_LIFE_MAX_ZOOM = ZoomLevel::Out2x;
/** How far ahead vehicles look for others. */
static constexpr int32_t LOOKAHEAD = 7 * UNIT;
/** Vehicles beside the line ahead closer than this are in the same lane. */
static constexpr int32_t SAME_LANE = 3 * UNIT / 2;
/** Speed gained per tick (speeds are in 1/256 units per tick, the same as road vehicles' internal km-ish/h * 2). */
static constexpr int ACCELERATION = 5;
/** Top speed when turning (about 30 km/h). */
static constexpr int TURNING_SPEED = 60;
/** After standing this long behind other street vehicles, a vehicle squeezes past them for a moment, so knots untangle. */
static constexpr uint16_t SQUEEZE_AFTER = 60;
/** Virtual screen distance outside the view in which vehicles are kept and new ones appear (about three tiles). */
static constexpr int VIEW_MARGIN = 3 * TILE_PIXELS * 2 * ZOOM_BASE;

/** Kinds of street life vehicles, in the order of their sprites. */
enum class StreetVehicleKind : uint8_t {
	Car,
	Van,
	Lorry,
	Motorbike,
	End,
};

/** Properties of a kind of street life vehicle. */
struct StreetVehicleKindInfo {
	uint8_t length; ///< Length in world units.
	uint8_t min_kmh; ///< Lowest top speed.
	uint8_t max_kmh; ///< Highest top speed.
	uint8_t share; ///< How common it is, out of 100.
};

static constexpr StreetVehicleKindInfo STREET_VEHICLE_KINDS[] = {
	{4, 70, 100, 64}, // Car
	{5, 70, 90, 12}, // Van
	{6, 55, 70, 8}, // Lorry
	{3, 70, 95, 16}, // Motorbike
};
static_assert(std::size(STREET_VEHICLE_KINDS) == to_underlying(StreetVehicleKind::End));

/** Colours of street life vehicles, often white, grey and dark ones like real traffic. */
static constexpr Colours STREET_VEHICLE_COLOURS[] = {
	Colours::White, Colours::White, Colours::White, Colours::White, Colours::Grey, Colours::Grey, Colours::Grey,
	Colours::DarkBlue, Colours::DarkBlue, Colours::Red, Colours::Red, Colours::Blue, Colours::Cream, Colours::Brown,
	Colours::DarkGreen, Colours::Yellow, Colours::Orange, Colours::LightBlue, Colours::Mauve, Colours::Purple,
};

/** A point on a path through a tile, tile local, in 1/256 units. */
struct StreetPoint {
	int32_t x;
	int32_t y;
};

/** A street life vehicle. */
struct StreetVehicle {
	TileIndex tile = INVALID_TILE; ///< Tile it drives on.
	DiagDirection from = DiagDirection::NE; ///< Side of the tile it came in by.
	DiagDirection to = DiagDirection::SW; ///< Side of the tile it leaves by.
	std::array<StreetPoint, 4> path{}; ///< Its way through the tile.
	std::array<int32_t, 3> segment{}; ///< Length of each part of the way.
	uint8_t points = 0; ///< Points on the way.
	int32_t progress = 0; ///< Distance driven on this tile.
	int32_t length = 0; ///< Length of the way through this tile.
	int32_t x = 0; ///< World position, in 1/256 units.
	int32_t y = 0; ///< World position, in 1/256 units.
	int z = 0; ///< Height, in world units.
	int32_t hx = 0; ///< Heading, scaled to 256.
	int32_t hy = 0; ///< Heading, scaled to 256.
	int speed = 0; ///< Current speed, in 1/256 units per tick.
	int top_speed = 0; ///< Own top speed.
	uint16_t waited = 0; ///< Ticks standing still.
	uint8_t squeeze = 0; ///< Ticks left in which it ignores other street vehicles.
	StreetVehicleKind kind = StreetVehicleKind::Car;
	Colours colour = Colours::White;
	Direction direction = Direction::NE; ///< Facing, for its sprite.
	bool turning = false; ///< On the diagonal part of a turn.
	Rect coord{}; ///< Where on the screen it is drawn, in virtual coordinates.
};

/** A road tile in view, with how many vehicles it should carry. */
struct VisibleRoad {
	TileIndex tile; ///< The tile.
	uint16_t density; ///< Vehicles it should carry, in 1/100.
	bool edge; ///< Near the edge of the view or just outside it: new vehicles appear here.
};

/** Someone walking along a town pavement. */
struct Pedestrian {
	TileIndex tile = INVALID_TILE; ///< Tile they walk on.
	DiagDirection from = DiagDirection::NE; ///< Side of the tile they came in by.
	DiagDirection to = DiagDirection::SW; ///< Side of the tile they leave by.
	std::array<StreetPoint, 4> path{}; ///< Their way along the pavement through the tile.
	std::array<int32_t, 3> segment{}; ///< Length of each part of the way.
	uint8_t points = 0; ///< Points on the way.
	int32_t progress = 0; ///< Distance walked on this tile.
	int32_t length = 0; ///< Length of the way through this tile.
	int32_t x = 0; ///< World position, in 1/256 units.
	int32_t y = 0; ///< World position, in 1/256 units.
	int z = 0; ///< Height, in world units.
	int speed = 0; ///< Walking speed, in 1/256 units per tick.
	bool left = false; ///< Walks on the pavement on their left.
	uint8_t look = 0; ///< Which of the people sprites.
	Colours colour = Colours::Blue; ///< Clothes.
	Rect coord{}; ///< Where on the screen they are drawn, in virtual coordinates.
};

/** People waiting at a bus stop tile in view. */
struct StopCrowd {
	TileIndex tile; ///< The road stop tile.
	uint8_t people; ///< How many are drawn.
};

static std::vector<Pedestrian> _pedestrians; ///< All people walking.
static std::vector<StopCrowd> _stop_crowds; ///< People waiting at bus stops in view, sorted by tile.
static std::vector<VisibleRoad> _visible_pavements; ///< Town pavements in or just outside the views.
static uint32_t _visible_pavement_target = 0; ///< People the pavements in view should have, in 1/100.

static std::vector<StreetVehicle> _street_vehicles; ///< All street life vehicles.
static std::vector<std::pair<TileIndex, uint32_t>> _street_index; ///< Street vehicles by tile, sorted; built each tick.
static std::vector<Rect> _street_views; ///< Virtual screen rectangles of the views that show street life.
static std::vector<VisibleRoad> _visible_roads; ///< Road tiles in or just outside those views.
static uint32_t _visible_target = 0; ///< Vehicles the roads in view should carry, in 1/100.
static uint32_t _visible_roads_age = 0; ///< Ticks since the list of roads in view was made.

/**
 * May street life vehicles drive on a tile?
 * @param tile The tile.
 * @return True for roads, level crossings, drive-through road stops and road waypoints with road (not just tram track).
 */
static bool IsStreetTile(TileIndex tile)
{
	switch (GetTileType(tile)) {
		case TileType::Road:
			if (IsRoadDepot(tile) || !HasTileRoadType(tile, RoadTramType::Road)) return false;
			/* Town traffic stays off bus-only roads. */
			if (GetRoadTypeRoad(tile) == ROADTYPE_BUS) return false;
			return !IsNormalRoad(tile) || !HasRoadWorks(tile);

		case TileType::Station:
			return (IsDriveThroughStopTile(tile) || IsRoadWaypoint(tile)) && HasTileRoadType(tile, RoadTramType::Road) && GetRoadTypeRoad(tile) != ROADTYPE_BUS;

		default:
			return false;
	}
}

/**
 * Directions road vehicles may drive on a tile.
 * @param tile The tile.
 * @return The trackdirs.
 */
static TrackdirBits GetStreetTrackdirs(TileIndex tile)
{
	return GetTileTrackStatus(tile, TransportType::Road, RoadTramType::Road).trackdirs;
}

/**
 * Can a street vehicle drive onto a tile?
 * @param tile The tile.
 * @param moving The direction it drives in.
 * @return True if the tile is a street with a way on from this side.
 */
static bool CanEnterStreet(TileIndex tile, DiagDirection moving)
{
	if (!IsValidTile(tile) || !IsStreetTile(tile)) return false;
	return GetStreetTrackdirs(tile).Any(DiagdirReachesTrackdirs(moving));
}

/**
 * How many street vehicles a road tile should carry.
 * @param tile The tile.
 * @return Vehicles, in 1/100.
 */
static uint16_t GetStreetDensity(TileIndex tile)
{
	static constexpr uint16_t BY_LEVEL[ROAD_CONGESTION_MAX + 1] = {30, 60, 100, 140, 180, 230, 280, 340};
	uint8_t level = (IsTileType(tile, TileType::Road) && !IsRoadDepot(tile)) ? GetRoadCongestion(tile) : 0;

	const Town *t = ClosestTownFromTile(tile, UINT_MAX);
	HouseZone zone = (t == nullptr) ? HouseZone::TownEdge : GetTownRadiusGroup(t, tile);
	switch (zone) {
		case HouseZone::TownEdge: return level == 0 ? 4 : BY_LEVEL[level] / 2;
		case HouseZone::TownOutskirt: return BY_LEVEL[level] * 6 / 10;
		default: return BY_LEVEL[level];
	}
}

/**
 * The lane across the road of vehicles moving in a direction: three units
 * either side of the middle of the road, on the side the game drives on.
 * (Real road vehicles use 5 and 9, but their sprites sit off their position;
 * street vehicles are drawn around their middle.)
 * @param moving The direction.
 * @return The position across the tile, in 1/256 units.
 */
static int32_t GetLane(DiagDirection moving)
{
	bool right = _settings_game.vehicle.road_side == RoadVehicleDrivingSide::Right;
	bool low = (moving == DiagDirection::NE || moving == DiagDirection::SE) != right;
	return (low ? 5 : 11) * UNIT;
}

/**
 * A point on the edge of a tile.
 * @param side The side of the tile.
 * @param across Position along that side.
 * @return The point.
 */
static StreetPoint GetEdgePoint(DiagDirection side, int32_t across)
{
	switch (side) {
		case DiagDirection::NE: return {0, across};
		case DiagDirection::SW: return {TILE_LENGTH, across};
		case DiagDirection::NW: return {across, 0};
		case DiagDirection::SE: return {across, TILE_LENGTH};
		default: NOT_REACHED();
	}
}

/**
 * Lay out the way of a vehicle through its tile, from the side it came in by to the side it leaves by.
 * @param v The vehicle.
 */
static void BuildStreetPath(StreetVehicle &v)
{
	DiagDirection in = ReverseDiagDir(v.from);
	TileIndexDiffC din = TileIndexDiffCByDiagDir(in);
	TileIndexDiffC dout = TileIndexDiffCByDiagDir(v.to);
	StreetPoint start = GetEdgePoint(v.from, GetLane(in));
	StreetPoint end = GetEdgePoint(v.to, GetLane(v.to));

	if (v.to == in) {
		/* Straight on. */
		v.path = {start, end, end, end};
		v.points = 2;
	} else if (v.to == v.from) {
		/* Turn around halfway. */
		StreetPoint a{start.x + din.x * 6 * UNIT, start.y + din.y * 6 * UNIT};
		StreetPoint b = a;
		if (DiagDirToAxis(in) == Axis::X) {
			b.y = GetLane(v.to);
		} else {
			b.x = GetLane(v.to);
		}
		v.path = {start, a, b, end};
		v.points = 4;
	} else {
		/* Turn the corner, cutting it a little. */
		StreetPoint corner = (DiagDirToAxis(in) == Axis::X) ? StreetPoint{end.x, start.y} : StreetPoint{start.x, end.y};
		static constexpr int32_t CUT = 2 * UNIT;
		StreetPoint a{corner.x - din.x * CUT, corner.y - din.y * CUT};
		StreetPoint b{corner.x + dout.x * CUT, corner.y + dout.y * CUT};
		v.path = {start, a, b, end};
		v.points = 4;
	}

	v.length = 0;
	for (uint i = 0; i + 1 < v.points; i++) {
		int32_t dx = v.path[i + 1].x - v.path[i].x;
		int32_t dy = v.path[i + 1].y - v.path[i].y;
		v.segment[i] = (dx == 0 || dy == 0) ? std::abs(dx) + std::abs(dy) : static_cast<int32_t>(IntSqrt(static_cast<uint32_t>(dx * dx + dy * dy)));
		v.length += v.segment[i];
	}
	v.length = std::max(v.length, 1);
}

/**
 * The direction of a move.
 * @param dx Sign of the move along x.
 * @param dy Sign of the move along y.
 * @return The direction.
 */
static Direction GetMoveDirection(int dx, int dy)
{
	if (dx < 0) return dy < 0 ? Direction::N : (dy > 0 ? Direction::E : Direction::NE);
	if (dx > 0) return dy < 0 ? Direction::W : (dy > 0 ? Direction::S : Direction::SW);
	return dy > 0 ? Direction::SE : Direction::NW;
}

/**
 * Work out where a vehicle is from how far it drove along its way.
 * @param v The vehicle.
 */
static void UpdateStreetPosition(StreetVehicle &v)
{
	int32_t p = v.progress;
	uint i = 0;
	while (i + 2 < v.points && p > v.segment[i]) {
		p -= v.segment[i];
		i++;
	}
	const StreetPoint &a = v.path[i];
	const StreetPoint &b = v.path[i + 1];
	int32_t seg = std::max(v.segment[i], 1);
	p = std::min(p, seg);
	v.x = static_cast<int32_t>(TileX(v.tile) * TILE_LENGTH) + a.x + (b.x - a.x) * p / seg;
	v.y = static_cast<int32_t>(TileY(v.tile) * TILE_LENGTH) + a.y + (b.y - a.y) * p / seg;

	int dx = (b.x > a.x) - (b.x < a.x);
	int dy = (b.y > a.y) - (b.y < a.y);
	v.direction = GetMoveDirection(dx, dy);
	v.turning = dx != 0 && dy != 0;
	v.hx = dx * (v.turning ? 181 : 256);
	v.hy = dy * (v.turning ? 181 : 256);
	v.z = GetSlopePixelZ(v.x / UNIT, v.y / UNIT, true);
}

/**
 * The sprite of a street vehicle.
 * @param v The vehicle.
 * @return The sprite.
 */
static SpriteID GetStreetVehicleSprite(const StreetVehicle &v)
{
	return SPR_STREET_LIFE_VEHICLES + to_underlying(v.kind) * 8 + to_underlying(v.direction);
}

/**
 * Where a street vehicle is on the screen.
 * @param v The vehicle.
 * @return Its rectangle, in virtual coordinates.
 */
static Rect GetStreetVehicleRect(const StreetVehicle &v)
{
	const Sprite *spr = GetSprite(GetStreetVehicleSprite(v), SpriteType::Normal);
	Point pt = RemapCoords(v.x / UNIT, v.y / UNIT, v.z);
	const int margin = 2 * static_cast<int>(ZOOM_BASE);
	return {pt.x + spr->x_offs, pt.y + spr->y_offs, pt.x + spr->x_offs + spr->width + margin, pt.y + spr->y_offs + spr->height + margin};
}

/**
 * Redraw where a street vehicle was and is.
 * @param v The vehicle.
 */
static void RedrawStreetVehicle(StreetVehicle &v)
{
	Rect now = GetStreetVehicleRect(v);
	if (now.left == v.coord.left && now.top == v.coord.top && now.right == v.coord.right && now.bottom == v.coord.bottom) return;
	MarkAllViewportsDirty(std::min(now.left, v.coord.left), std::min(now.top, v.coord.top), std::max(now.right, v.coord.right), std::max(now.bottom, v.coord.bottom));
	v.coord = now;
}

/**
 * Pick the side by which a vehicle leaves the tile it just came onto: straight
 * on more often than turning, busier roads more often than quiet ones; turn
 * around at dead ends and where the way on isn't a street.
 * @param v The vehicle, with its tile and the side it came in by.
 */
static void ChooseStreetExit(StreetVehicle &v)
{
	DiagDirection in = ReverseDiagDir(v.from);
	std::array<DiagDirection, 4> options{};
	std::array<uint, 4> weights{};
	uint count = 0;
	uint total = 0;
	for (Trackdir td : GetStreetTrackdirs(v.tile) & DiagdirReachesTrackdirs(in)) {
		DiagDirection exit = TrackdirToExitdir(td);
		if (exit == v.from || std::find(options.begin(), options.begin() + count, exit) != options.begin() + count) continue;
		TileIndex next = TileAddByDiagDir(v.tile, exit);
		if (!CanEnterStreet(next, exit)) continue;
		uint weight = (exit == in ? 3 : 1) * (GetStreetDensity(next) + 10);
		options[count] = exit;
		weights[count] = weight;
		total += weight;
		count++;
	}
	if (count == 0) {
		v.to = v.from;
		return;
	}
	uint pick = InteractiveRandomRange(total);
	for (uint i = 0; i < count; i++) {
		if (pick < weights[i]) {
			v.to = options[i];
			return;
		}
		pick -= weights[i];
	}
	v.to = options[count - 1];
}

/**
 * Is anything in the way of a street vehicle, and how far ahead?
 * @param v The vehicle.
 * @param self Its index.
 * @param[out] street Whether the nearest thing in the way is another street vehicle.
 * @return Free distance to the nearest thing ahead in its lane, or #LOOKAHEAD if nothing.
 */
static int32_t GetStreetGapAhead(const StreetVehicle &v, uint32_t self, bool &street)
{
	int32_t gap = LOOKAHEAD;
	street = false;
	const int32_t own_half = STREET_VEHICLE_KINDS[to_underlying(v.kind)].length * UNIT / 2;

	/* Keep the distance to whatever drives ahead the same way; brake for crossing traffic only when about to touch it. */
	auto consider = [&](int32_t ox, int32_t oy, int32_t ohx, int32_t ohy, int32_t other_half, bool is_street) {
		int32_t rx = ox - v.x;
		int32_t ry = oy - v.y;
		int32_t ahead = (rx * v.hx + ry * v.hy) / 256;
		if (ahead <= 0 || ahead > LOOKAHEAD + other_half + own_half) return;
		int32_t beside = std::abs(rx * v.hy - ry * v.hx) / 256;
		int32_t free = ahead - own_half - other_half;
		bool same_way = ohx * v.hx + ohy * v.hy > 0;
		if (same_way ? beside > SAME_LANE : (beside > UNIT || free > UNIT)) return;
		if (free < gap) {
			gap = free;
			street = is_street;
		}
	};

	TileIndex next = TileAddByDiagDir(v.tile, v.to);
	for (TileIndex tile : {v.tile, next}) {
		if (!IsValidTile(tile)) continue;

		if (v.squeeze == 0) {
			auto range = std::equal_range(_street_index.begin(), _street_index.end(), std::make_pair(tile, uint32_t{0}),
					[](const auto &a, const auto &b) { return a.first < b.first; });
			for (auto it = range.first; it != range.second; ++it) {
				if (it->second == self) continue;
				const StreetVehicle &o = _street_vehicles[it->second];
				consider(o.x, o.y, o.hx, o.hy, STREET_VEHICLE_KINDS[to_underlying(o.kind)].length * UNIT / 2, true);
			}
		}

		for (const Vehicle *u : VehiclesOnTile(tile)) {
			if (u->type != VehicleType::Road) continue;
			int32_t ux = (u->x_pos + u->bounds.origin.x) * UNIT + u->bounds.extent.x * UNIT / 2;
			int32_t uy = (u->y_pos + u->bounds.origin.y) * UNIT + u->bounds.extent.y * UNIT / 2;
			int32_t uhx = 0, uhy = 0;
			switch (u->direction) {
				case Direction::N: uhx = -181; uhy = -181; break;
				case Direction::NE: uhx = -256; break;
				case Direction::E: uhx = -181; uhy = 181; break;
				case Direction::SE: uhy = 256; break;
				case Direction::S: uhx = 181; uhy = 181; break;
				case Direction::SW: uhx = 256; break;
				case Direction::W: uhx = 181; uhy = -181; break;
				default: uhy = -256; break;
			}
			consider(ux, uy, uhx, uhy, std::max(u->bounds.extent.x, u->bounds.extent.y) * UNIT / 2, false);
		}
	}
	return gap;
}

/**
 * May a street vehicle drive onto the next tile now: green light, open level crossing?
 * @param next The tile.
 * @param moving The direction it drives in.
 * @return True if it may.
 */
static bool MayEnterStreet(TileIndex next, DiagDirection moving)
{
	if (!IsValidTile(next) || !IsTileType(next, TileType::Road)) return true;
	if (IsNormalRoad(next) && HasTrafficLight(next)) return TrafficLightShowsGreen(next, DiagDirToAxis(moving));
	if (IsLevelCrossing(next)) return !IsCrossingBarred(next);
	return true;
}

/**
 * The speed limit of a road for street vehicles: the road type's and the town traffic's.
 * @param tile The tile.
 * @return The limit, in 1/256 units per tick.
 */
static int GetStreetSpeedLimit(TileIndex tile)
{
	int limit = GetRoadCongestionSpeedLimit(tile);
	RoadType rt = GetRoadTypeRoad(tile);
	if (rt != INVALID_ROADTYPE && GetRoadTypeInfo(rt)->max_speed != 0) limit = std::min<int>(limit, GetRoadTypeInfo(rt)->max_speed);
	return limit;
}

/**
 * Drive a street vehicle for a tick.
 * @param v The vehicle.
 * @param self Its index.
 * @return False if it left the streets and should go.
 */
static bool MoveStreetVehicle(StreetVehicle &v, uint32_t self)
{
	if (!IsStreetTile(v.tile)) return false;

	bool street = false;
	int32_t gap = GetStreetGapAhead(v, self, street);

	/* Stop before red lights and closed level crossings. */
	int32_t to_edge = v.length - v.progress;
	bool red = false;
	if (to_edge < 3 * UNIT && v.waited < 900 && !MayEnterStreet(TileAddByDiagDir(v.tile, v.to), v.to)) {
		gap = std::min(gap, to_edge - UNIT / 2);
		red = true;
	}

	int limit = std::min(v.top_speed, GetStreetSpeedLimit(v.tile));
	if (v.turning || v.to == v.from) limit = std::min(limit, TURNING_SPEED);
	int allowed = gap <= UNIT / 4 ? 0 : gap / 5;
	int target = std::min(limit, allowed);
	v.speed = (v.speed < target) ? std::min(target, v.speed + ACCELERATION) : target;

	if (v.speed == 0) {
		if (v.waited < UINT16_MAX) v.waited++;
		if (street && !red && v.waited >= SQUEEZE_AFTER) v.squeeze = 30;
	} else {
		v.waited = 0;
	}
	if (v.squeeze > 0) v.squeeze--;

	v.progress += v.speed;
	while (v.progress >= v.length) {
		TileIndex next = TileAddByDiagDir(v.tile, v.to);
		if (!CanEnterStreet(next, v.to)) return false;
		v.progress -= v.length;
		v.tile = next;
		v.from = ReverseDiagDir(v.to);
		ChooseStreetExit(v);
		BuildStreetPath(v);
	}
	UpdateStreetPosition(v);
	return true;
}

/**
 * Is a street vehicle where someone could see it, give or take the margin?
 * @param r Its screen rectangle.
 * @param margin Extra room around the views.
 * @return True if so.
 */
static bool IsInStreetView(const Rect &r, int margin)
{
	for (const Rect &view : _street_views) {
		if (r.right >= view.left - margin && r.left <= view.right + margin && r.bottom >= view.top - margin && r.top <= view.bottom + margin) return true;
	}
	return false;
}

/**
 * Collect the views that show street life.
 * @return True if they changed.
 */
static bool UpdateStreetViews()
{
	std::vector<Rect> views;
	if (_settings_client.gui.street_life != to_underlying(StreetLifeSetting::Off)) {
		for (const Window *w : Window::Iterate()) {
			if (w->viewport == nullptr || w->viewport->zoom > STREET_LIFE_MAX_ZOOM) continue;
			const Viewport &vp = *w->viewport;
			views.push_back({vp.virtual_left, vp.virtual_top, vp.virtual_left + vp.virtual_width, vp.virtual_top + vp.virtual_height});
		}
	}
	bool changed = views.size() != _street_views.size() || !std::equal(views.begin(), views.end(), _street_views.begin(),
			[](const Rect &a, const Rect &b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; });
	_street_views = std::move(views);
	return changed;
}

/** Most people walking at once. */
static constexpr size_t MAX_PEDESTRIANS = 1500;
/** Most people drawn waiting at one bus stop tile. */
static constexpr uint MAX_CROWD = 8;
/** Waiting passengers per person drawn at a stop. */
static constexpr uint PASSENGERS_PER_PERSON = 8;

/**
 * Are people shown at all?
 * @return True if the setting asks for them.
 */
static bool ShowPeople()
{
	return _settings_client.gui.street_life == to_underlying(StreetLifeSetting::All);
}

/**
 * Does a tile have a town pavement to walk on?
 * @param tile The tile.
 * @return True for plain roads with paved sidewalks.
 */
static bool IsPavementTile(TileIndex tile)
{
	if (!IsValidTile(tile) || !IsTileType(tile, TileType::Road) || !IsNormalRoad(tile) || !HasTileRoadType(tile, RoadTramType::Road)) return false;
	Roadside rs = GetRoadside(tile);
	return rs == Roadside::Paved || rs == Roadside::StreetLights || rs == Roadside::Trees;
}

/**
 * How many people a pavement tile should have.
 * @param tile The tile.
 * @return People, in 1/100.
 */
static uint16_t GetPavementDensity(TileIndex tile)
{
	const Town *t = ClosestTownFromTile(tile, UINT_MAX);
	if (t == nullptr) return 0;
	switch (GetTownRadiusGroup(t, tile)) {
		case HouseZone::TownCentre: return 320;
		case HouseZone::TownInnerSuburb: return 180;
		case HouseZone::TownOuterSuburb: return 90;
		case HouseZone::TownOutskirt: return 35;
		default: return 10;
	}
}

/**
 * The pavement on one side of someone walking in a direction.
 * @param moving The direction.
 * @param left The pavement on their left (else their right).
 * @return The position across the tile, in 1/256 units.
 */
static int32_t GetPavement(DiagDirection moving, bool left)
{
	bool low = (moving == DiagDirection::NE || moving == DiagDirection::SE) == left;
	return low ? 3 * UNIT / 2 : TILE_LENGTH - 3 * UNIT / 2;
}

/**
 * Lay out someone's way along the pavement through their tile.
 * @param p The person.
 */
static void BuildWalkPath(Pedestrian &p)
{
	DiagDirection in = ReverseDiagDir(p.from);
	TileIndexDiffC din = TileIndexDiffCByDiagDir(in);
	StreetPoint start = GetEdgePoint(p.from, GetPavement(in, p.left));
	if (p.to == p.from) {
		/* Turn around and walk back on the same pavement. */
		StreetPoint a{start.x + din.x * 4 * UNIT, start.y + din.y * 4 * UNIT};
		p.path = {start, a, start, start};
		p.points = 3;
	} else {
		StreetPoint end = GetEdgePoint(p.to, GetPavement(p.to, p.left));
		if (p.to == in) {
			p.path = {start, end, end, end};
			p.points = 2;
		} else {
			StreetPoint corner = (DiagDirToAxis(in) == Axis::X) ? StreetPoint{end.x, start.y} : StreetPoint{start.x, end.y};
			p.path = {start, corner, end, end};
			p.points = 3;
		}
	}
	p.length = 0;
	for (uint i = 0; i + 1 < p.points; i++) {
		p.segment[i] = std::abs(p.path[i + 1].x - p.path[i].x) + std::abs(p.path[i + 1].y - p.path[i].y);
		p.length += p.segment[i];
	}
	p.length = std::max(p.length, 1);
}

/**
 * Pick the side by which someone leaves the tile they just came onto: along
 * the pavement, straight on more often than round a corner; back the way
 * they came where the pavement ends.
 * @param p The person, with their tile and the side they came in by.
 */
static void ChooseWalkExit(Pedestrian &p)
{
	DiagDirection in = ReverseDiagDir(p.from);
	RoadBits bits = GetRoadBits(p.tile, RoadTramType::Road);
	std::array<DiagDirection, 4> options{};
	std::array<uint, 4> weights{};
	uint count = 0;
	uint total = 0;
	for (DiagDirection exit : EnumRange(DiagDirection::End)) {
		if (exit == p.from || !bits.Any(DiagDirToRoadBits(exit))) continue;
		TileIndex next = TileAddByDiagDir(p.tile, exit);
		if (!IsPavementTile(next) || !GetRoadBits(next, RoadTramType::Road).Any(DiagDirToRoadBits(ReverseDiagDir(exit)))) continue;
		options[count] = exit;
		weights[count] = exit == in ? 2 : 1;
		total += weights[count];
		count++;
	}
	if (count == 0) {
		p.to = p.from;
		return;
	}
	uint pick = InteractiveRandomRange(total);
	for (uint i = 0; i < count; i++) {
		if (pick < weights[i]) {
			p.to = options[i];
			return;
		}
		pick -= weights[i];
	}
	p.to = options[count - 1];
}

/**
 * Work out where someone is from how far they walked along their way.
 * @param p The person.
 */
static void UpdateWalkPosition(Pedestrian &p)
{
	int32_t d = p.progress;
	uint i = 0;
	while (i + 2 < p.points && d > p.segment[i]) {
		d -= p.segment[i];
		i++;
	}
	const StreetPoint &a = p.path[i];
	const StreetPoint &b = p.path[i + 1];
	int32_t seg = std::max(p.segment[i], 1);
	d = std::min(d, seg);
	p.x = static_cast<int32_t>(TileX(p.tile) * TILE_LENGTH) + a.x + (b.x - a.x) * d / seg;
	p.y = static_cast<int32_t>(TileY(p.tile) * TILE_LENGTH) + a.y + (b.y - a.y) * d / seg;
	p.z = GetSlopePixelZ(p.x / UNIT, p.y / UNIT, true);
}

/**
 * The sprite of someone walking or standing.
 * @param look Which person.
 * @param step Walking step (0 or 1), or 2 for standing.
 * @return The sprite.
 */
static SpriteID GetPersonSprite(uint8_t look, uint step)
{
	return SPR_STREET_LIFE_PEOPLE + (look % 2) * 3 + step;
}

/**
 * The sprite of someone walking.
 * @param p The person.
 * @return The sprite.
 */
static SpriteID GetPedestrianSprite(const Pedestrian &p)
{
	return GetPersonSprite(p.look, (p.progress / (3 * UNIT / 2)) & 1);
}

/**
 * Where a sprite at a world position is on the screen.
 * @param sprite The sprite.
 * @param x World x, in 1/256 units.
 * @param y World y, in 1/256 units.
 * @param z Height.
 * @return The rectangle, in virtual coordinates.
 */
static Rect GetSpriteRectAt(SpriteID sprite, int32_t x, int32_t y, int z)
{
	const Sprite *spr = GetSprite(sprite, SpriteType::Normal);
	Point pt = RemapCoords(x / UNIT, y / UNIT, z);
	const int margin = 2 * static_cast<int>(ZOOM_BASE);
	return {pt.x + spr->x_offs, pt.y + spr->y_offs, pt.x + spr->x_offs + spr->width + margin, pt.y + spr->y_offs + spr->height + margin};
}

/**
 * Walk someone for a tick.
 * @param p The person.
 * @return False if they left the pavements (or went inside) and should go.
 */
static bool MovePedestrian(Pedestrian &p)
{
	if (!IsPavementTile(p.tile)) return false;
	p.progress += p.speed;
	while (p.progress >= p.length) {
		TileIndex next = TileAddByDiagDir(p.tile, p.to);
		if (!IsPavementTile(next)) return false;
		/* Now and then someone goes into a building. */
		if (InteractiveRandomRange(60) == 0) return false;
		if (p.to == p.from) p.left = !p.left;
		p.progress -= p.length;
		p.tile = next;
		p.from = ReverseDiagDir(p.to);
		ChooseWalkExit(p);
		BuildWalkPath(p);
	}
	UpdateWalkPosition(p);
	Rect now = GetSpriteRectAt(GetPedestrianSprite(p), p.x, p.y, p.z);
	if (now.left != p.coord.left || now.top != p.coord.top || now.right != p.coord.right || now.bottom != p.coord.bottom) {
		MarkAllViewportsDirty(std::min(now.left, p.coord.left), std::min(now.top, p.coord.top), std::max(now.right, p.coord.right), std::max(now.bottom, p.coord.bottom));
		p.coord = now;
	}
	return true;
}

/**
 * Put someone somewhere on a pavement tile.
 * @param tile The tile.
 * @return True if they were put there.
 */
static bool SpawnPedestrian(TileIndex tile)
{
	RoadBits bits = GetRoadBits(tile, RoadTramType::Road);
	std::array<DiagDirection, 4> sides{};
	uint count = 0;
	for (DiagDirection side : EnumRange(DiagDirection::End)) {
		if (bits.Any(DiagDirToRoadBits(side))) sides[count++] = side;
	}
	if (count == 0) return false;

	Pedestrian p;
	p.tile = tile;
	p.from = sides[InteractiveRandomRange(count)];
	p.left = InteractiveRandomRange(2) == 0;
	ChooseWalkExit(p);
	BuildWalkPath(p);
	p.progress = InteractiveRandomRange(p.length);
	p.speed = 12 + InteractiveRandomRange(9);
	p.look = static_cast<uint8_t>(InteractiveRandomRange(2));
	p.colour = static_cast<Colours>(InteractiveRandomRange(to_underlying(Colours::End)));
	UpdateWalkPosition(p);
	p.coord = GetSpriteRectAt(GetPedestrianSprite(p), p.x, p.y, p.z);
	MarkAllViewportsDirty(p.coord.left, p.coord.top, p.coord.right, p.coord.bottom);
	_pedestrians.push_back(p);
	return true;
}

/**
 * Walk everyone for a tick, let people out of sight go and new ones come.
 */
static void PedestriansTick()
{
	if (!ShowPeople()) {
		for (const Pedestrian &p : _pedestrians) MarkAllViewportsDirty(p.coord.left, p.coord.top, p.coord.right, p.coord.bottom);
		_pedestrians.clear();
		return;
	}

	uint in_view = 0;
	size_t n = 0;
	for (size_t i = 0; i < _pedestrians.size(); i++) {
		Pedestrian &p = _pedestrians[i];
		if (!MovePedestrian(p) || !IsInStreetView(p.coord, VIEW_MARGIN)) {
			MarkAllViewportsDirty(p.coord.left, p.coord.top, p.coord.right, p.coord.bottom);
			continue;
		}
		if (IsInStreetView(p.coord, 0)) in_view++;
		if (n != i) _pedestrians[n] = p;
		n++;
	}
	_pedestrians.resize(n);

	uint target = _visible_pavement_target / 100;
	if (in_view >= target || _visible_pavements.empty()) return;
	bool fill = in_view * 10 < target * 6;
	uint wanted = std::min<uint>(fill ? 8 : 1, target - in_view);
	for (uint attempt = 0; attempt < 24 * wanted && wanted > 0 && _pedestrians.size() < MAX_PEDESTRIANS; attempt++) {
		const VisibleRoad &road = _visible_pavements[InteractiveRandomRange(static_cast<uint>(_visible_pavements.size()))];
		if (InteractiveRandomRange((fill || road.edge) ? 320 : 1280) >= road.density) continue;
		if (SpawnPedestrian(road.tile)) wanted--;
	}
}

/**
 * How many people to draw waiting at a bus stop tile: one per few waiting
 * passengers of its station, spread over the station's bus stops.
 * @param tile The road stop tile.
 * @return The number of people.
 */
static uint8_t GetStopCrowd(TileIndex tile)
{
	if (!IsStationRoadStopTile(tile) || GetRoadStopType(tile) != RoadStopType::Bus) return 0;
	const Station *st = Station::GetByTile(tile);
	uint waiting = 0;
	for (const CargoSpec *cs : CargoSpec::Iterate()) {
		if (!cs->IsValid() || !IsCargoInClass(cs->Index(), CargoClass::Passengers)) continue;
		const GoodsEntry &ge = st->goods[cs->Index()];
		if (ge.HasData()) waiting += ge.GetData().cargo.TotalCount();
	}
	uint stops = 0;
	for (const RoadStop *rs = st->bus_stops; rs != nullptr; rs = rs->next) stops++;
	return static_cast<uint8_t>(std::min<uint>(MAX_CROWD, CeilDiv(waiting / std::max(1U, stops), PASSENGERS_PER_PERSON)));
}

/**
 * Where someone of a crowd at a bus stop stands: at the kerb on the far side of the road.
 * @param tile The road stop tile.
 * @param i Which of them.
 * @return World position, in 1/256 units.
 */
static StreetPoint GetCrowdPosition(TileIndex tile, uint i)
{
	Axis axis = IsDriveThroughStopTile(tile) ? GetDriveThroughStopAxis(tile) : DiagDirToAxis(GetBayRoadStopDir(tile));
	uint32_t hash = (tile.base() * 2654435761U) ^ (i * 40503U);
	/* Two staggered rows at the kerb on the far side of the road, where the shelters don't hide them. */
	int32_t along = 2 * UNIT + static_cast<int32_t>(i / 2) * (5 * UNIT / 2) + static_cast<int32_t>(i % 2) * UNIT + static_cast<int32_t>(hash % (UNIT / 2));
	int32_t across = (i % 2 == 0) ? 5 * UNIT / 2 : 7 * UNIT / 2;
	int32_t lx = axis == Axis::X ? along : across;
	int32_t ly = axis == Axis::X ? across : along;
	return {static_cast<int32_t>(TileX(tile) * TILE_LENGTH) + lx, static_cast<int32_t>(TileY(tile) * TILE_LENGTH) + ly};
}

/** List the road tiles in and just outside the views, and how much traffic they should show. */
static void FindVisibleRoads()
{
	_visible_roads.clear();
	_visible_target = 0;
	_visible_roads_age = 0;
	_visible_pavements.clear();
	_visible_pavement_target = 0;
	std::vector<StopCrowd> crowds;
	bool people = ShowPeople();

	for (const Rect &view : _street_views) {
		/* The tiles under the corners of the view at sea level, plus room for hills. */
		int min_x = INT_MAX, min_y = INT_MAX, max_x = INT_MIN, max_y = INT_MIN;
		for (Point corner : {Point{view.left, view.top}, Point{view.right, view.top}, Point{view.left, view.bottom}, Point{view.right, view.bottom}}) {
			Point world = InverseRemapCoords(corner.x, corner.y);
			min_x = std::min(min_x, world.x);
			min_y = std::min(min_y, world.y);
			max_x = std::max(max_x, world.x);
			max_y = std::max(max_y, world.y);
		}
		int x0 = Clamp(min_x / static_cast<int>(TILE_SIZE) - 4, 0, static_cast<int>(Map::MaxX()));
		int y0 = Clamp(min_y / static_cast<int>(TILE_SIZE) - 4, 0, static_cast<int>(Map::MaxY()));
		int x1 = Clamp(max_x / static_cast<int>(TILE_SIZE) + 20, 0, static_cast<int>(Map::MaxX()));
		int y1 = Clamp(max_y / static_cast<int>(TILE_SIZE) + 20, 0, static_cast<int>(Map::MaxY()));

		int band_x = (view.right - view.left) / 8;
		int band_y = (view.bottom - view.top) / 8;
		for (int y = y0; y <= y1; y++) {
			for (int x = x0; x <= x1; x++) {
				TileIndex tile = TileXY(x, y);
				if (!IsStreetTile(tile) && !(people && IsBayRoadStopTile(tile))) continue;
				Point pt = RemapCoords(x * TILE_SIZE + TILE_SIZE / 2, y * TILE_SIZE + TILE_SIZE / 2, TileHeight(tile) * TILE_HEIGHT);
				bool inside = pt.x >= view.left && pt.x <= view.right && pt.y >= view.top && pt.y <= view.bottom;
				bool near = pt.x >= view.left - VIEW_MARGIN && pt.x <= view.right + VIEW_MARGIN && pt.y >= view.top - VIEW_MARGIN && pt.y <= view.bottom + VIEW_MARGIN;
				if (!near) continue;
				bool edge = !inside || pt.x < view.left + band_x || pt.x > view.right - band_x || pt.y < view.top + band_y || pt.y > view.bottom - band_y;
				if (IsStreetTile(tile)) {
					uint16_t density = GetStreetDensity(tile);
					_visible_roads.push_back({tile, density, edge});
					if (inside) _visible_target += density;
				}

				if (!people) continue;
				if (IsPavementTile(tile)) {
					uint16_t walkers = GetPavementDensity(tile);
					_visible_pavements.push_back({tile, walkers, edge});
					if (inside) _visible_pavement_target += walkers;
				} else if (uint8_t crowd = GetStopCrowd(tile); crowd > 0) {
					crowds.push_back({tile, crowd});
				}
			}
		}
	}

	/* Redraw bus stops whose crowd changed. */
	std::sort(crowds.begin(), crowds.end(), [](const StopCrowd &a, const StopCrowd &b) { return a.tile < b.tile; });
	crowds.erase(std::unique(crowds.begin(), crowds.end(), [](const StopCrowd &a, const StopCrowd &b) { return a.tile == b.tile; }), crowds.end());
	auto find = [](const std::vector<StopCrowd> &list, TileIndex tile) -> uint8_t {
		auto it = std::lower_bound(list.begin(), list.end(), tile, [](const StopCrowd &c, TileIndex t) { return c.tile < t; });
		return (it != list.end() && it->tile == tile) ? it->people : 0;
	};
	for (const StopCrowd &c : crowds) {
		if (find(_stop_crowds, c.tile) != c.people) MarkTileDirtyByTile(c.tile);
	}
	for (const StopCrowd &c : _stop_crowds) {
		if (find(crowds, c.tile) == 0) MarkTileDirtyByTile(c.tile);
	}
	_stop_crowds = std::move(crowds);
}

/**
 * Put a new street vehicle somewhere on a road tile, if there is room.
 * @param tile The tile.
 * @return True if it was put there.
 */
static bool SpawnStreetVehicle(TileIndex tile)
{
	TrackdirBits trackdirs = GetStreetTrackdirs(tile);
	std::array<DiagDirection, 4> sides{};
	uint count = 0;
	for (DiagDirection side : EnumRange(DiagDirection::End)) {
		if (trackdirs.Any(DiagdirReachesTrackdirs(ReverseDiagDir(side)))) sides[count++] = side;
	}
	if (count == 0) return false;

	StreetVehicle v;
	v.tile = tile;
	v.from = sides[InteractiveRandomRange(count)];
	ChooseStreetExit(v);
	BuildStreetPath(v);
	v.progress = InteractiveRandomRange(v.length);

	uint pick = InteractiveRandomRange(100);
	for (uint k = 0; k < std::size(STREET_VEHICLE_KINDS); k++) {
		if (pick < STREET_VEHICLE_KINDS[k].share) {
			v.kind = static_cast<StreetVehicleKind>(k);
			break;
		}
		pick -= STREET_VEHICLE_KINDS[k].share;
	}
	const StreetVehicleKindInfo &info = STREET_VEHICLE_KINDS[to_underlying(v.kind)];
	v.top_speed = 2 * (info.min_kmh + InteractiveRandomRange(info.max_kmh - info.min_kmh + 1));
	v.colour = STREET_VEHICLE_COLOURS[InteractiveRandomRange(static_cast<uint>(std::size(STREET_VEHICLE_COLOURS)))];
	UpdateStreetPosition(v);

	/* Not on top of another vehicle. */
	bool street;
	StreetVehicle probe = v;
	for (int32_t dir : {1, -1}) {
		probe.hx = v.hx * dir;
		probe.hy = v.hy * dir;
		if (GetStreetGapAhead(probe, UINT32_MAX, street) < UNIT) return false;
	}

	v.speed = std::min(v.top_speed, GetStreetSpeedLimit(tile)) / 2;
	v.coord = GetStreetVehicleRect(v);
	MarkAllViewportsDirty(v.coord.left, v.coord.top, v.coord.right, v.coord.bottom);
	_street_vehicles.push_back(v);
	_street_index.emplace_back(tile, static_cast<uint32_t>(_street_vehicles.size() - 1));
	std::sort(_street_index.begin(), _street_index.end());
	return true;
}

/**
 * Bring the number of vehicles in view up to what the roads there should carry.
 * At first they fill the roads anywhere in view; after that one at a time,
 * mostly near the edges of the view (or just outside), so few appear out of
 * nowhere in the middle.
 * @param in_view Vehicles in view now.
 */
static void SpawnStreetVehicles(uint in_view)
{
	uint target = _visible_target / 100;
	if (in_view >= target || _visible_roads.empty()) return;

	bool fill = in_view * 10 < target * 6;
	uint wanted = std::min<uint>(fill ? 8 : 1, target - in_view);
	for (uint attempt = 0; attempt < 24 * wanted && wanted > 0 && _street_vehicles.size() < MAX_STREET_VEHICLES; attempt++) {
		const VisibleRoad &road = _visible_roads[InteractiveRandomRange(static_cast<uint>(_visible_roads.size()))];
		/* After the first fill most new vehicles come in from the edges; some pull out from the roadside in view. */
		if (InteractiveRandomRange((fill || road.edge) ? 340 : 1360) >= road.density) continue;
		if (SpawnStreetVehicle(road.tile)) wanted--;
	}
}

/** Remove all street life, redrawing where it was. */
static void ClearStreetLife()
{
	for (const StreetVehicle &v : _street_vehicles) MarkAllViewportsDirty(v.coord.left, v.coord.top, v.coord.right, v.coord.bottom);
	_street_vehicles.clear();
	_street_index.clear();
	_visible_roads.clear();
	_visible_target = 0;
	for (const Pedestrian &p : _pedestrians) MarkAllViewportsDirty(p.coord.left, p.coord.top, p.coord.right, p.coord.bottom);
	_pedestrians.clear();
	for (const StopCrowd &c : _stop_crowds) MarkTileDirtyByTile(c.tile);
	_stop_crowds.clear();
	_visible_pavements.clear();
	_visible_pavement_target = 0;
}

/** Drive all street life for a tick, let vehicles out of sight go and new ones come. */
static void StreetLifeTick()
{
	if (_network_dedicated || _generating_world || _game_mode == GameMode::Editor || _game_mode == GameMode::Bootstrap) return;

	bool views_changed = UpdateStreetViews();
	if (_street_views.empty()) {
		if (!_street_vehicles.empty() || !_pedestrians.empty() || !_stop_crowds.empty()) ClearStreetLife();
		return;
	}
	if (views_changed || ++_visible_roads_age >= 128) FindVisibleRoads();

	_street_index.clear();
	for (uint32_t i = 0; i < _street_vehicles.size(); i++) _street_index.emplace_back(_street_vehicles[i].tile, i);
	std::sort(_street_index.begin(), _street_index.end());

	uint in_view = 0;
	std::vector<bool> keep(_street_vehicles.size(), true);
	for (uint32_t i = 0; i < _street_vehicles.size(); i++) {
		StreetVehicle &v = _street_vehicles[i];
		if (!MoveStreetVehicle(v, i) || !IsInStreetView(v.coord, VIEW_MARGIN)) {
			keep[i] = false;
			MarkAllViewportsDirty(v.coord.left, v.coord.top, v.coord.right, v.coord.bottom);
			continue;
		}
		RedrawStreetVehicle(v);
		if (IsInStreetView(v.coord, 0)) in_view++;
	}

	size_t n = 0;
	for (size_t i = 0; i < _street_vehicles.size(); i++) {
		if (keep[i]) _street_vehicles[n++] = _street_vehicles[i];
	}
	_street_vehicles.resize(n);
	if (n != keep.size()) {
		_street_index.clear();
		for (uint32_t i = 0; i < _street_vehicles.size(); i++) _street_index.emplace_back(_street_vehicles[i].tile, i);
		std::sort(_street_index.begin(), _street_index.end());
	}

	SpawnStreetVehicles(in_view);
	PedestriansTick();
}

/** Street life moves with the game. */
static const IntervalTimer<TimerGameTick> _street_life_ticker({TimerGameTick::Priority::None, 1}, [](auto) {
	StreetLifeTick();
});

/** Bounding box of street vehicles, for sorting them among other sprites. */
static constexpr SpriteBounds STREET_VEHICLE_BOUNDS{{-1, -1, 0}, {3, 3, 5}, {}};
/** Bounding box of people. */
static constexpr SpriteBounds PERSON_BOUNDS{{0, 0, 0}, {1, 1, 6}, {}};

/**
 * Add the street life that shows in part of a view to what is drawn.
 * @param dpi The part being drawn.
 */
void ViewportAddStreetLife(DrawPixelInfo *dpi)
{
	if (_street_vehicles.empty() && _pedestrians.empty() && _stop_crowds.empty()) return;
	const int l = dpi->left;
	const int r = dpi->left + dpi->width;
	const int t = dpi->top;
	const int b = dpi->top + dpi->height;
	for (const StreetVehicle &v : _street_vehicles) {
		if (v.coord.right < l || v.coord.left > r || v.coord.bottom < t || v.coord.top > b) continue;
		AddSortableSpriteToDraw(GetStreetVehicleSprite(v), GetColourPalette(v.colour), v.x / UNIT, v.y / UNIT, v.z, STREET_VEHICLE_BOUNDS);
	}
	for (const Pedestrian &p : _pedestrians) {
		if (p.coord.right < l || p.coord.left > r || p.coord.bottom < t || p.coord.top > b) continue;
		AddSortableSpriteToDraw(GetPedestrianSprite(p), GetColourPalette(p.colour), p.x / UNIT, p.y / UNIT, p.z, PERSON_BOUNDS);
	}
	for (const StopCrowd &c : _stop_crowds) {
		if (!IsStationRoadStopTile(c.tile)) continue;
		for (uint i = 0; i < c.people; i++) {
			StreetPoint pos = GetCrowdPosition(c.tile, i);
			uint32_t hash = (c.tile.base() * 2246822519U) ^ (i * 3266489917U);
			SpriteID sprite = GetPersonSprite(static_cast<uint8_t>(hash & 1), 2);
			int z = GetSlopePixelZ(pos.x / UNIT, pos.y / UNIT, true);
			Rect rect = GetSpriteRectAt(sprite, pos.x, pos.y, z);
			if (rect.right < l || rect.left > r || rect.bottom < t || rect.top > b) continue;
			AddSortableSpriteToDraw(sprite, GetColourPalette(static_cast<Colours>((hash >> 8) % to_underlying(Colours::End))), pos.x / UNIT, pos.y / UNIT, z, PERSON_BOUNDS);
		}
	}
}

/** Forget all street life; used when starting or loading a game. */
void InitializeStreetLife()
{
	_pedestrians.clear();
	_stop_crowds.clear();
	_visible_pavements.clear();
	_visible_pavement_target = 0;
	_street_vehicles.clear();
	_street_index.clear();
	_street_views.clear();
	_visible_roads.clear();
	_visible_target = 0;
	_visible_roads_age = 0;
}

/** The street life setting changed: clear the streets when it was switched off. */
void OnStreetLifeSettingChanged()
{
	if (_settings_client.gui.street_life == to_underlying(StreetLifeSetting::Off)) ClearStreetLife();
	MarkWholeScreenDirty();
}

/**
 * How many street life vehicles there are, for testing.
 * @return The number.
 */
size_t GetStreetLifeVehicleCount()
{
	return _street_vehicles.size();
}
