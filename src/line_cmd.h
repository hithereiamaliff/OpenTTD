/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file line_cmd.h Command definitions for lines. */

#ifndef LINE_CMD_H
#define LINE_CMD_H

#include "command_type.h"
#include "line_type.h"
#include "vehicle_type.h"

CommandCost CmdSetLineProperty(DoCommandFlags flags, VehicleID veh_id, LineProperty property, uint32_t value, const std::string &text);

DEF_CMD_TRAIT(Commands::SetLineProperty, CmdSetLineProperty, {}, CommandType::RouteManagement)

#endif /* LINE_CMD_H */
