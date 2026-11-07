/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file interchange_cmd.h Command definitions for bus interchanges and transport hubs. */

#ifndef INTERCHANGE_CMD_H
#define INTERCHANGE_CMD_H

#include "command_type.h"
#include "interchange.h"

CommandCost CmdUpgradeStation(DoCommandFlags flags, StationID station_id, InterchangeLevel level);

DEF_CMD_TRAIT(Commands::UpgradeStation, CmdUpgradeStation, {}, CommandType::LandscapeConstruction)

#endif /* INTERCHANGE_CMD_H */
