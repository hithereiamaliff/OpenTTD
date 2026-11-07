/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file bus_priority_cmd.h Command definitions for bus priority. */

#ifndef BUS_PRIORITY_CMD_H
#define BUS_PRIORITY_CMD_H

#include "command_type.h"
#include "bus_priority.h"

CommandCost CmdBuildBusLane(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, BusLane lane, bool remove);
CommandCost CmdSetGiveWayStop(DoCommandFlags flags, TileIndex tile, bool remove);
CommandCost CmdSetBrtStation(DoCommandFlags flags, TileIndex tile, bool remove);

DEF_CMD_TRAIT(Commands::BuildBusLane, CmdBuildBusLane, {}, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::SetGiveWayStop, CmdSetGiveWayStop, {}, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::SetBrtStation, CmdSetBrtStation, {}, CommandType::LandscapeConstruction)

#endif /* BUS_PRIORITY_CMD_H */
