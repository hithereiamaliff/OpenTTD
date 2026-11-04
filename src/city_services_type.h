/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file city_services_type.h Types for city services: police, fire, health, schools and parks. */

#ifndef CITY_SERVICES_TYPE_H
#define CITY_SERVICES_TYPE_H

#include "object_type.h"

/** Kinds of city service a company can build for towns. */
enum class CityService : uint8_t {
	Police, ///< Police station.
	Fire, ///< Fire station.
	Health, ///< Clinic.
	Education, ///< School.
	Park, ///< Park.
	End, ///< End marker.
};

/** Number of kinds of city service. */
static constexpr uint CITY_SERVICE_COUNT = to_underlying(CityService::End);

/**
 * Object types of the city services. They sit at the top of the object type
 * range, which NewGRF objects never get (see _object_mngr), so they never
 * clash with the NewGRF objects of a savegame. 16 are reserved, a few used.
 */
static constexpr ObjectType OBJECT_CITY_SERVICE_FIRST = NUM_OBJECTS - 16;

/**
 * Is an object type a city service?
 * @param type The object type.
 * @return True for the city service types.
 */
inline bool IsCityServiceObjectType(ObjectType type)
{
	return type >= OBJECT_CITY_SERVICE_FIRST && type < OBJECT_CITY_SERVICE_FIRST + CITY_SERVICE_COUNT;
}

/**
 * The service of a city service object type.
 * @param type The object type.
 * @pre IsCityServiceObjectType(type)
 * @return The service.
 */
inline CityService GetCityServiceOfObjectType(ObjectType type)
{
	return static_cast<CityService>(type - OBJECT_CITY_SERVICE_FIRST);
}

/**
 * The object type of a city service.
 * @param service The service.
 * @return The object type.
 */
inline ObjectType GetObjectTypeOfCityService(CityService service)
{
	return OBJECT_CITY_SERVICE_FIRST + to_underlying(service);
}

/** How much of a town's population lives within reach of each city service, updated monthly. */
struct CityServiceCoverage {
	std::array<uint8_t, CITY_SERVICE_COUNT> percent{}; ///< Share of the population covered, per service, 0-100.
};

#endif /* CITY_SERVICES_TYPE_H */
