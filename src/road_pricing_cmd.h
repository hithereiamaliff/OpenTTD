/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file road_pricing_cmd.h Command definitions for road pricing. */

#ifndef ROAD_PRICING_CMD_H
#define ROAD_PRICING_CMD_H

#include "command_type.h"
#include "road_pricing_type.h"
#include "town_type.h"

CommandCost CmdBuildTollPlaza(DoCommandFlags flags, TileIndex tile, TollPlazaAction action);
CommandCost CmdSetTollPolicy(DoCommandFlags flags, TollPolicyProperty property, uint8_t value);
CommandCost CmdProposeCongestionCharge(DoCommandFlags flags, TownID town_id);

DEF_CMD_TRAIT(Commands::BuildTollPlaza, CmdBuildTollPlaza, {}, CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::SetTollPolicy, CmdSetTollPolicy, {}, CommandType::CompanySetting)
DEF_CMD_TRAIT(Commands::ProposeCongestionCharge, CmdProposeCongestionCharge, {}, CommandType::OtherManagement)

#endif /* ROAD_PRICING_CMD_H */
