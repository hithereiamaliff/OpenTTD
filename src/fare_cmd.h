/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fare_cmd.h Command definitions for passenger fares. */

#ifndef FARE_CMD_H
#define FARE_CMD_H

#include "command_type.h"
#include "fare_type.h"

CommandCost CmdSetCompanyFare(DoCommandFlags flags, FareMode mode, FareSetting setting, uint16_t value);

DEF_CMD_TRAIT(Commands::SetCompanyFare, CmdSetCompanyFare, {}, CommandType::CompanySetting)

#endif /* FARE_CMD_H */
