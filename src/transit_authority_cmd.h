/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file transit_authority_cmd.h Command definitions for town transit contracts. */

#ifndef TRANSIT_AUTHORITY_CMD_H
#define TRANSIT_AUTHORITY_CMD_H

#include "command_type.h"

CommandCost CmdTransitBid(DoCommandFlags flags, uint32_t contract, uint8_t margin);
CommandCost CmdTransitEndContract(DoCommandFlags flags, uint32_t contract);

DEF_CMD_TRAIT(Commands::TransitBid, CmdTransitBid, {}, CommandType::OtherManagement)
DEF_CMD_TRAIT(Commands::TransitEndContract, CmdTransitEndContract, {}, CommandType::OtherManagement)

#endif /* TRANSIT_AUTHORITY_CMD_H */
