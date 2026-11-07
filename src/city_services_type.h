/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file city_services_type.h Types for city services: police, fire, health, schools and parks; hospitals, universities, stadiums and hawker centres. */

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
	Hospital, ///< Hospital: health care for a whole town, and visitors (two by two tiles).
	University, ///< University: education for a whole town, students and staff (two by two tiles).
	Stadium, ///< Stadium: crowds on match days (two by two tiles).
	Market, ///< Hawker centre or market: food and daily needs.
	End, ///< End marker.
};

/** Number of kinds of city service. */
static constexpr uint CITY_SERVICE_COUNT = to_underlying(CityService::End);
/** The first kinds, from the first part of the city services; their object types come first. */
static constexpr uint BASIC_CITY_SERVICE_COUNT = 5;

/**
 * Object types of the city services. They sit at the top of the object type
 * range, which NewGRF objects never get (see _object_mngr), so they never
 * clash with the NewGRF objects of a savegame. 16 are reserved, a few used.
 */
static constexpr ObjectType OBJECT_CITY_SERVICE_FIRST = NUM_OBJECTS - 16;
/** Object types of the bigger city services, from the hospital on: the 16 below those. */
static constexpr ObjectType OBJECT_CIVIC_SERVICE_FIRST = NUM_OBJECTS - 32;

/**
 * Is an object type a city service?
 * @param type The object type.
 * @return True for the city service types.
 */
inline bool IsCityServiceObjectType(ObjectType type)
{
	if (type >= OBJECT_CITY_SERVICE_FIRST) return type < OBJECT_CITY_SERVICE_FIRST + BASIC_CITY_SERVICE_COUNT;
	return type >= OBJECT_CIVIC_SERVICE_FIRST && type < OBJECT_CIVIC_SERVICE_FIRST + (CITY_SERVICE_COUNT - BASIC_CITY_SERVICE_COUNT);
}

/**
 * The service of a city service object type.
 * @param type The object type.
 * @pre IsCityServiceObjectType(type)
 * @return The service.
 */
inline CityService GetCityServiceOfObjectType(ObjectType type)
{
	if (type >= OBJECT_CITY_SERVICE_FIRST) return static_cast<CityService>(type - OBJECT_CITY_SERVICE_FIRST);
	return static_cast<CityService>(BASIC_CITY_SERVICE_COUNT + type - OBJECT_CIVIC_SERVICE_FIRST);
}

/**
 * The object type of a city service.
 * @param service The service.
 * @return The object type.
 */
inline ObjectType GetObjectTypeOfCityService(CityService service)
{
	if (to_underlying(service) < BASIC_CITY_SERVICE_COUNT) return OBJECT_CITY_SERVICE_FIRST + to_underlying(service);
	return OBJECT_CIVIC_SERVICE_FIRST + (to_underlying(service) - BASIC_CITY_SERVICE_COUNT);
}

/** How much of a town's population lives within reach of each city service, updated monthly. */
struct CityServiceCoverage {
	std::array<uint8_t, CITY_SERVICE_COUNT> percent{}; ///< Share of the population covered, per service, 0-100.
};

#endif /* CITY_SERVICES_TYPE_H */
