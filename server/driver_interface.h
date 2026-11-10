// Copyright (C) 2003 Bob Ham <rah@bash.sh>
// Copyright (C) 2008 Nedko Arnaudov <nedko@arnaudov.name>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#pragma once

#include "jslist.h"
#include <array>
#include <cstdint>
#include <limits.h>

#include "JackCompilerDeps.h"
#include "JackSystemDeps.h"

#ifdef __cplusplus
extern "C" {
#endif

inline constexpr std::int32_t kDriverNameMax = 15;
inline constexpr std::int32_t kDriverParamNameMax = 15;
inline constexpr std::int32_t kDriverParamStringMax = 127;
inline constexpr std::int32_t kDriverParamDesc = 255;
inline constexpr std::int32_t kPathMax = 511;

inline constexpr std::uint32_t kConstraintFlagRange = ((std::uint32_t)1); // < if set, constraint is a range (min-max)
inline constexpr std::uint32_t kConstraintFlagStrict = ((std::uint32_t)2); // < if set, constraint is strict, i.e. supplying non-matching value will not work
inline constexpr std::uint32_t kConstraintFlagFakeValue = ((std::uint32_t)4); // < if set, values have no user meaningful meaning

// Driver parameter types
typedef enum {
    kDriverParamInt = 1,
    kDriverParamUInt,
    kDriverParamChar,
    kDriverParamString,
    kDriverParamBool
} DriverParamType;

// Driver types
typedef enum {
    kDriverMaster = 1,
    kDriverSlave,
    kDriverNone,
} DriverType;

// Driver parameter value
typedef union {
    std::uint32_t m_ui;
    std::int32_t m_i;
    char m_c;
    std::array<char, kDriverParamStringMax + 1> m_str;
} DriverParamValue;

typedef struct {
    DriverParamValue m_value;
    std::array<char, 64> m_shortDesc;  // < A short (~30 chars) description for the user
} DriverParamValueEnum;

struct jack_constraint_enum_uint32_descriptor {
    std::uint32_t m_value;
    const char* m_shortDesc;
};

struct jack_constraint_enum_sint32_descriptor {
    std::int32_t m_value;
    const char* m_shortDesc;
};

struct jack_constraint_enum_char_descriptor {
    char m_value;
    const char* m_shortDesc;
};

struct jack_constraint_enum_str_descriptor {
    const char* m_value;
    const char* m_shortDesc;
};

typedef struct {
    std::uint32_t m_flags;  // < JACK_CONSTRAINT_FLAG_XXX
    union {
        struct {
            DriverParamValue m_min;
            DriverParamValue m_max;
        } m_range;  // < valid when JACK_CONSTRAINT_FLAG_RANGE flag is set

        struct {
            std::uint32_t m_count;
            DriverParamValueEnum* m_possibleValuesArray;
        } m_enumeration;  // < valid when JACK_CONSTRAINT_FLAG_RANGE flag is not set
    } m_constraint;
} DriverParamConstraintDesc;

// A driver parameter descriptor
typedef struct {
    std::array<char, kDriverNameMax + 1> m_name;  // < The parameter's name
    char m_character;  // < The parameter's character (for getopt, etc)
    DriverParamType m_type;  // < The parameter's type
    DriverParamValue m_value;  // < The parameter's (default) value
    DriverParamConstraintDesc* m_constraint;  // < Pointer to parameter constraint descriptor. nullptr if there is no constraint
    std::array<char, 64> m_shortDesc;  // < A short (~30 chars) description for the user
    std::array<char, 1024> m_longDesc;  // < A longer description for the user
} DriverParamDesc;

// A driver parameter
typedef struct {
    char m_character;
    DriverParamValue m_value;
} DriverParam;

// A struct for describing a jack driver
typedef struct {
    std::array<char, kDriverNameMax + 1> m_name;  // < The driver's canonical name
    DriverType m_type;  // < The driver's type
    std::array<char, kDriverParamDesc + 1> m_desc;  // < The driver's extended description
    std::array<char, kPathMax + 1> m_file;  // < The filename of the driver's shared object file
    std::uint32_t m_nparams;  // < The number of parameters the driver has
    DriverParamDesc* m_params;  // < An array of parameter descriptors
} DriverDesc;

typedef struct {
    std::uint32_t m_size;  // size of the param array, in elements
} DriverDescFiller;


// To be used by drivers

SERVER_EXPORT DriverDesc*  // Newly allocated driver descriptor, nullptr on failure
jack_driver_descriptor_construct(
    const char* name,  // Driver name
    DriverType type,  // Driver type
    const char* description,  // Driver description
    DriverDescFiller* filler);  // Pointer to stack var to be supplied to jack_driver_descriptor_add_parameter() as well.
    // Can be nullptr for drivers that have no parameters.

SERVER_EXPORT std::int32_t  // 0 on failure
jack_driver_descriptor_add_parameter(
    DriverDesc* driverDescr,  // Pointer to driver descriptor as returned by jack_driver_descriptor_construct()
    DriverDescFiller* filler,  // Pointer to the stack var that was supplied to jack_driver_descriptor_add_parameter().
    const char* name,  // Parameter's name
    char character,  // Parameter's character (for getopt, etc)
    DriverParamType type,  // The parameter's type
    const DriverParamValue* valuePtr,  // Pointer to parameter's (default) value
    DriverParamConstraintDesc* constraint,  // Pointer to parameter constraint descriptor. nullptr if there is no constraint
    const char* shortDesc,  // A short (~30 chars) description for the user
    const char* longDesc);  // A longer description for the user, if nullptr short_desc will be used

SERVER_EXPORT
std::int32_t jack_constraint_add_enum(
    DriverParamConstraintDesc** constraintPtrPtr,
    std::uint32_t* arraySizePtr,
    DriverParamValue* valuePtr,
    const char* shortDesc);

SERVER_EXPORT
void jack_constraint_free(DriverParamConstraintDesc* constraintPtr);

#define JACK_CONSTRAINT_COMPOSE_ENUM(type) \
    SERVER_EXPORT                          \
    DriverParamConstraintDesc*   \
    jack_constraint_compose_enum_##type(   \
        std::uint32_t flags,                    \
        struct jack_constraint_enum_##type##_descriptor* descr_array_ptr)

JACK_CONSTRAINT_COMPOSE_ENUM(uint32);
JACK_CONSTRAINT_COMPOSE_ENUM(sint32);
JACK_CONSTRAINT_COMPOSE_ENUM(char);
JACK_CONSTRAINT_COMPOSE_ENUM(str);

#ifdef __cplusplus
}
#endif
