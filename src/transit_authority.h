/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file transit_authority.h Town transit authorities: budgets, tenders and contracts for lines.
 *
 * Towns of some size have a yearly transit budget. Now and then a town
 * invites tenders for one of a company's lines that serve it: either a
 * gross-cost contract (the town keeps the fares and pays a monthly fee, like
 * Singapore's Bus Contracting Model and Malaysia's BAS.MY) or a free line
 * (riders ride free and the town pays the fee, like Penang's CAT). The
 * company bids a margin on top of the line's costs; simulated rival
 * operators bid too, and the town weighs quality against price 60:40. The
 * contract runs five years: the fee shrinks with missed departures, and
 * regularity and punctuality earn bonuses or penalties. Contracts that met
 * their targets are extended; others go back to tender, and lines too dear
 * for their riders are dropped after a review.
 */

#ifndef TRANSIT_AUTHORITY_H
#define TRANSIT_AUTHORITY_H

#include "transit_authority_type.h"

struct OrderList;
struct Town;
struct Vehicle;

Money GetTransitBusYearCost();
Money GetTownTransitBudget(const Town *t);
Money GetTownTransitCommitted(const Town *t);
Money GetTownTransitAvailable(const Town *t);

std::vector<TransitContract> &GetTransitContracts();
TransitContract *GetTransitContract(uint32_t id);
const TransitContract *GetLineContract(const OrderList *list);
bool IsFeederOffer(const TransitContract &c);
const TransitContract *GetTownFeederOffer(TownID town);
std::string GetTransitLineDescription(const OrderList *list);
Money GetTransitLineCostBasis(const OrderList *list);
uint GetTransitLineQuality(const OrderList *list);

Money TakeContractFares(const Vehicle *front, Money fares);
void TransitMonthlyLoop();
void InitializeTransitAuthorities();

void ShowTransitAuthorityWindow(TownID town);

#endif /* TRANSIT_AUTHORITY_H */
