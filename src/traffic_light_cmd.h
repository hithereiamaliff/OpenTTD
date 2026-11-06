/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file traffic_light_cmd.h Command definitions for traffic lights. */

#ifndef TRAFFIC_LIGHT_CMD_H
#define TRAFFIC_LIGHT_CMD_H

#include "command_type.h"
#include "traffic_light.h"

CommandCost CmdBuildTrafficLight(DoCommandFlags flags, TileIndex tile);
CommandCost CmdRemoveTrafficLight(DoCommandFlags flags, TileIndex tile);
CommandCost CmdSetTrafficLightBusPriority(DoCommandFlags flags, TileIndex tile, bool on);
CommandCost CmdConfigureTrafficLight(DoCommandFlags flags, TileIndex tile, TrafficLightMode mode, uint16_t green_x, uint16_t green_y, uint16_t min_green, uint16_t offset);

DEF_CMD_TRAIT(Commands::BuildTrafficLight, CmdBuildTrafficLight, {}, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::RemoveTrafficLight, CmdRemoveTrafficLight, {}, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::ConfigureTrafficLight, CmdConfigureTrafficLight, {}, CommandType::OtherManagement)
DEF_CMD_TRAIT(Commands::SetTrafficLightBusPriority, CmdSetTrafficLightBusPriority, {}, CommandType::LandscapeConstruction)

#endif /* TRAFFIC_LIGHT_CMD_H */
