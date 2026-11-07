/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file commuting_type.h Types for commuting: where a town's people work, and how they get there. */

#ifndef COMMUTING_TYPE_H
#define COMMUTING_TYPE_H

/** A town's commuters: how many, how they travel, and the roads they drive along (saved in chunk TCOM). */
struct TownCommuting {
	bool valid = false; ///< The figures were worked out at least once.
	uint8_t cell_size = 4; ///< Size of the cells of the grid, in tiles.
	uint32_t commuters = 0; ///< People who go to work on a day.
	uint32_t by_transit = 0; ///< Of those, the ones who take transit.
	uint32_t car_time = 0; ///< Average time door to door by car, in ticks.
	uint32_t transit_time = 0; ///< Average time door to door by transit, of those who take it, in ticks; 0 if nobody does.
	uint16_t grid_x = 0; ///< First column of the grid, in cells from the map's edge.
	uint16_t grid_y = 0; ///< First row of the grid, in cells from the map's edge.
	uint16_t grid_w = 0; ///< Columns of the grid.
	uint16_t grid_h = 0; ///< Rows of the grid.
	std::vector<uint8_t> home_share; ///< Per cell: share of the commutes from homes there taken by transit, in percent.
	std::vector<uint8_t> job_share; ///< Per cell: share of the commutes to jobs there taken by transit, in percent.
	std::vector<uint8_t> home_appeal; ///< Per cell: share of the commutes from homes there the journey times alone send to transit, in percent.
	std::vector<uint8_t> job_appeal; ///< Per cell: share of the commutes to jobs there the journey times alone send to transit, in percent.
	std::vector<uint16_t> link_towns; ///< Other towns its people work in.
	std::vector<uint32_t> link_commuters; ///< For each of those, its people who work there, a day.
	std::vector<uint32_t> link_transit; ///< For each of those, the ones who take transit.
	std::vector<uint32_t> flow_tiles; ///< Road tiles commuters drive along, in ascending order.
	std::vector<uint16_t> flow_cars; ///< For each of those tiles, commuters' cars a day in one direction.
};

#endif /* COMMUTING_TYPE_H */
