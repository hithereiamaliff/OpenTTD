/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file depot_yard_type.h Types for depot yards: the facilities a company builds around its road depots. */

#ifndef DEPOT_YARD_TYPE_H
#define DEPOT_YARD_TYPE_H

#include "city_services_type.h"

/**
 * Facilities of a depot yard. They are built on tiles next to a road depot, or
 * next to another facility of its yard, and serve the vehicles based there.
 */
enum class DepotFacility : uint8_t {
	BusPark, ///< Parking for vehicles.
	SolarBusPark, ///< Parking under solar roofs, which power electric buses.
	MultiStoreyPark, ///< Parking for many vehicles on a few floors.
	ChargingBay, ///< Two chargers for electric buses.
	FastChargingBay, ///< Two fast chargers.
	Substation, ///< A bigger grid connection for the chargers.
	RepairBay, ///< A workshop that keeps vehicles in shape.
	WashFuelLane, ///< Bulk fuel and a bus wash.
	StaffBuilding, ///< Canteen and lounge for the drivers and staff.
	End, ///< End marker.
};

/** Number of kinds of depot facility. */
static constexpr uint DEPOT_FACILITY_COUNT = to_underlying(DepotFacility::End);

/** Object types of the depot facilities: in the reserved range, right after the city services. */
static constexpr ObjectType OBJECT_DEPOT_FACILITY_FIRST = OBJECT_CITY_SERVICE_FIRST + BASIC_CITY_SERVICE_COUNT;
static_assert(OBJECT_DEPOT_FACILITY_FIRST + DEPOT_FACILITY_COUNT <= NUM_OBJECTS);

/**
 * Is an object type a depot facility?
 * @param type The object type.
 * @return True for the depot facility types.
 */
inline bool IsDepotFacilityObjectType(ObjectType type)
{
	return type >= OBJECT_DEPOT_FACILITY_FIRST && type < OBJECT_DEPOT_FACILITY_FIRST + DEPOT_FACILITY_COUNT;
}

/**
 * The facility of a depot facility object type.
 * @param type The object type.
 * @pre IsDepotFacilityObjectType(type)
 * @return The facility.
 */
inline DepotFacility GetDepotFacilityOfObjectType(ObjectType type)
{
	return static_cast<DepotFacility>(type - OBJECT_DEPOT_FACILITY_FIRST);
}

/**
 * The object type of a depot facility.
 * @param facility The facility.
 * @return The object type.
 */
inline ObjectType GetObjectTypeOfDepotFacility(DepotFacility facility)
{
	return OBJECT_DEPOT_FACILITY_FIRST + to_underlying(facility);
}

#endif /* DEPOT_YARD_TYPE_H */
