/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file town_budget_cmd.h Command definitions for towns' ordinances. */

#ifndef TOWN_BUDGET_CMD_H
#define TOWN_BUDGET_CMD_H

#include "command_type.h"
#include "town_budget_type.h"
#include "town_type.h"

CommandCost CmdProposeOrdinance(DoCommandFlags flags, TownID town_id, Ordinance ordinance);
CommandCost CmdSetTownOrdinance(DoCommandFlags flags, TownID town_id, Ordinance ordinance, bool in_force);

DEF_CMD_TRAIT(Commands::ProposeOrdinance, CmdProposeOrdinance, {}, CommandType::OtherManagement)
DEF_CMD_TRAIT(Commands::SetTownOrdinance, CmdSetTownOrdinance, CommandFlag::Deity, CommandType::OtherManagement)

#endif /* TOWN_BUDGET_CMD_H */
