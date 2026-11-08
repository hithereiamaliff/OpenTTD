/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file state_cmd.h Command definitions for states. */

#ifndef STATE_CMD_H
#define STATE_CMD_H

#include "command_type.h"
#include "state_type.h"

CommandCost CmdRenameState(DoCommandFlags flags, StateID state_id, const std::string &text);
CommandCost CmdSetStateGovernment(DoCommandFlags flags, StateID state_id, Platform platform);

DEF_CMD_TRAIT(Commands::RenameState, CmdRenameState, CommandFlags({CommandFlag::Deity, CommandFlag::Server}), CommandType::OtherManagement)
DEF_CMD_TRAIT(Commands::SetStateGovernment, CmdSetStateGovernment, CommandFlags({CommandFlag::Deity}), CommandType::OtherManagement)

#endif /* STATE_CMD_H */
