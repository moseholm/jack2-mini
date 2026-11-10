// Copyright (C) 2001-2005 Paul Davis
// Copyright (C) 2004-2008 Grame
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "DriverLoader.h"
#include "CString.h"
#include "Constants.h"
#include "DriverInfo.h"
#include "Error.h"
#include "SystemDeps.h"
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <dirent.h>


void printDriverOptions(DriverDesc* desc, FILE* file)
{
    std::uint64_t i;
    std::array<char, kDriverParamStringMax + 1> argDefault { };

    for (i = 0; i < desc->m_nparams; i++) {
        switch (desc->m_params[i].m_type) {
        case kDriverParamInt:
            jackmini::formatCString(argDefault, "{}", desc->m_params[i].m_value.m_i);
            break;
        case kDriverParamUInt:
            jackmini::formatCString(argDefault, "{}", desc->m_params[i].m_value.m_ui);
            break;
        case kDriverParamChar:
            jackmini::formatCString(argDefault, "{}", desc->m_params[i].m_value.m_c);
            break;
        case kDriverParamString:
            if (strcmp(desc->m_params[i].m_value.m_str.data(), "") != 0) {
                jackmini::formatCString(argDefault, "{}", desc->m_params[i].m_value.m_str.data());
            } else {
                jackmini::formatCString(argDefault, "none");
            }
            break;
        case kDriverParamBool:
            jackmini::formatCString(argDefault, "{}", desc->m_params[i].m_value.m_i ? "true" : "false");
            break;
        }

        fprintf(file, "\t-%c, --%s \t%s (default: %s)\n",
            desc->m_params[i].m_character,
            desc->m_params[i].m_name,
            desc->m_params[i].m_longDesc,
            argDefault.data());
    }
}

void printDriverParamUsage(DriverDesc* desc, std::uint64_t param, FILE* file)
{
    fprintf(file, "Usage information for the '%s' parameter for driver '%s':\n",
        desc->m_params[param].m_name, desc->m_name);
    fprintf(file, "%s\n", desc->m_params[param].m_longDesc);
}

DriverDesc* findDriverDescriptor(JSListC* drivers, const char* name)
{
    DriverDesc* desc = 0;
    JSListC* node;

    for (node = drivers; node; node = jack_slist_next(node)) {
        desc = (DriverDesc*)node->m_data;

        if (strcmp(desc->m_name.data(), name) != 0) {
            desc = nullptr;
        } else {
            break;
        }
    }

    return desc;
}

// The names of the entries in a directory.  If the listing fails, ec is set
// and the names read so far are returned.
static std::vector<std::string> listDirectory(const char* dirPath, std::error_code& ec)
{
    std::vector<std::string> names;
    for (std::filesystem::directory_iterator it(dirPath, ec), end; !ec && it != end; it.increment(ec)) {
        names.push_back(it->path().filename().string());
    }
    return names;
}

// true if the file name has an extension that starts with "so", as in "jack_x.so"
static bool hasSharedObjectExtension(std::string_view name)
{
    const auto dot = name.rfind('.');
    return dot != std::string_view::npos && name.substr(dot + 1).starts_with("so");
}

static void* checkSymbol(const char* sofile, const char* symbol, const char* driverDir, void** resDllhandle = nullptr)
{
    void* dlhandle;
    void* res = nullptr;
    std::array<char, 1024> filename { };
    jackmini::formatCString(std::span<char>(filename.data(), 1022), "{}/{}", driverDir, sofile);

    if ((dlhandle = loadDriverModule(filename.data())) == nullptr) {
        jack_error("Could not open component .so '%s': %s", filename.data(), dlerror());
    } else {
        res = (void*)getDriverProc(dlhandle, symbol);
        if (resDllhandle) {
            *resDllhandle = dlhandle;
        } else {
            unloadDriverModule(dlhandle);
        }
    }

    return res;
}

// Unloads a driver module when the owning pointer goes out of scope.
struct DriverModuleCloser {
    void operator()(void* handle) const
    {
        if (handle) {
            unloadDriverModule(handle);
        }
    }
};

// Loads the module of a driver file, checks its interface version, and returns its descriptor. A driver whose name is
// already in the list is refused, so the first one is used.
static DriverDesc* loadDescriptor(JSListC* drivers, const char* sofile, const char* driverDir)
{
    DriverDesc* descriptor = nullptr;
    DriverDesc* otherDescriptor;
    std::array<char, 1024> filename { };
    JSListC* node;
    void* rawHandle = nullptr;

    jackmini::formatCString(std::span<char>(filename.data(), 1022), "{}/{}", driverDir, sofile);

    const auto getModule = reinterpret_cast<jackmini::driver::ModuleFunction>(
        checkSymbol(sofile, jackmini::driver::kModuleSymbol, driverDir, &rawHandle));
    std::unique_ptr<void, DriverModuleCloser> dlhandle(rawHandle);
    const jackmini::driver::Module* module = getModule ? getModule() : nullptr;

    if (module == nullptr) {
        jack_error("load_descriptor : dll %s is not a driver", sofile);
        return nullptr;
    }

    if (module->m_version != jackmini::driver::kModuleVersion) {
        jack_error("Driver from '%s' has interface version %u, this server needs version %u",
            filename.data(), module->m_version, jackmini::driver::kModuleVersion);
        return nullptr;
    }

    if ((descriptor = module->m_describe()) == nullptr) {
        jack_error("Driver from '%s' returned NULL descriptor", filename.data());
        return nullptr;
    }

    // check it doesn't exist already
    for (node = drivers; node; node = jack_slist_next(node)) {
        otherDescriptor = (DriverDesc*)node->m_data;
        if (strcmp(descriptor->m_name.data(), otherDescriptor->m_name.data()) == 0) {
            jack_error("The drivers in '%s' and '%s' both have the name '%s'; using the first",
                otherDescriptor->m_file, filename.data(), otherDescriptor->m_name);
            // FIXME: delete the descriptor
            return nullptr;
        }
    }

    strncpy(descriptor->m_file.data(), filename.data(), kPathMax);
    return descriptor;
}


JSListC* driversLoad(JSListC* drivers)
{
    JSListC* driverList = nullptr;
    DriverDesc* desc = nullptr;

    const char* driverDir;
    if ((driverDir = getenv("JACK_DRIVER_DIR")) == 0) {
        driverDir = kAddonDir;
    }

    // search through the driver_dir and add get descriptors
    // from the .so files in it
    std::error_code ec;
    const auto entries = listDirectory(driverDir, ec);
    if (ec) {
        jack_error("Could not open driver directory %s: %s",
            driverDir, ec.message().c_str());
        return nullptr;
    }

    for (const std::string& name : entries) {

        // check the filename is of the right format
        if (!std::string_view(name).starts_with("jack_")) {
            continue;
        }
        if (!hasSharedObjectExtension(name)) {
            continue;
        }

        desc = loadDescriptor(drivers, name.c_str(), driverDir);
        if (desc) {
            driverList = jack_slist_append(driverList, desc);
        } else {
            jack_error("Driver file \"%s\" does not give a usable driver descriptor", name.c_str());
        }
    }

    if (!driverList) {
        jack_error("Could not find any drivers in %s!", driverDir);
        return nullptr;
    }

    return driverList;
}


jackmini::DriverClientInterface* DriverInfo::open(DriverDesc* driverDesc,
    jackmini::LockedEngine* engine,
    jackmini::Synchro* synchro,
    const JSListC* params)
{
    const char* errstr;

    m_handle = loadDriverModule(driverDesc->m_file.data());

    if (m_handle == nullptr) {
        if ((errstr = dlerror()) != 0) {
            jack_error("Can't load \"%s\": %s", driverDesc->m_file, errstr);

        } else {
            jack_error("Error loading driver shared object %s", driverDesc->m_file);
        }
        return nullptr;
    }


    const auto getModule = reinterpret_cast<jackmini::driver::ModuleFunction>(getDriverProc(m_handle, jackmini::driver::kModuleSymbol));
    m_module = getModule ? getModule() : nullptr;
    if (m_module == nullptr) {
        jack_error("No driver module entry point in shared object %s", driverDesc->m_file);
        return nullptr;
    }

    if (m_module->m_version != jackmini::driver::kModuleVersion) {
        jack_error("Driver %s has interface version %u, this server needs version %u",
            driverDesc->m_file, m_module->m_version, jackmini::driver::kModuleVersion);
        return nullptr;
    }

    m_backend = m_module->m_initialize(engine, synchro, params);
    return m_backend;
}

DriverInfo::~DriverInfo()
{
    delete m_backend;
    if (m_handle) {
        unloadDriverModule(m_handle);
    }
}

SERVER_EXPORT DriverDesc* jack_driver_descriptor_construct(
    const char* name,
    DriverType type,
    const char* description,
    DriverDescFiller* fillerPtr)
{
    size_t nameLen;
    size_t descriptionLen;
    DriverDesc* descPtr;

    nameLen = strlen(name);
    descriptionLen = strlen(description);

    if (nameLen > sizeof(descPtr->m_name) - 1 || descriptionLen > sizeof(descPtr->m_desc) - 1) {
        assert(false);
        return 0;
    }

    descPtr = (DriverDesc*)calloc(1, sizeof(DriverDesc));
    if (descPtr == nullptr) {
        jack_error("Error calloc() failed to allocate memory for driver descriptor struct");
        return 0;
    }

    memcpy(descPtr->m_name.data(), name, nameLen + 1);
    memcpy(descPtr->m_desc.data(), description, descriptionLen + 1);

    descPtr->m_nparams = 0;
    descPtr->m_type = type;

    if (fillerPtr != nullptr) {
        fillerPtr->m_size = 0;
    }

    return descPtr;
}

SERVER_EXPORT std::int32_t jack_driver_descriptor_add_parameter(
    DriverDesc* descPtr,
    DriverDescFiller* fillerPtr,
    const char* name,
    char character,
    DriverParamType type,
    const DriverParamValue* valuePtr,
    DriverParamConstraintDesc* constraint,
    const char* shortDesc,
    const char* longDesc)
{
    size_t nameLen;
    size_t shortDescLen;
    size_t longDescLen;
    DriverParamDesc* paramPtr;
    size_t newsize;

    nameLen = strlen(name);
    shortDescLen = strlen(shortDesc);

    if (longDesc != nullptr) {
        longDescLen = strlen(longDesc);
    } else {
        longDesc = shortDesc;
        longDescLen = shortDescLen;
    }

    if (nameLen > sizeof(paramPtr->m_name) - 1 || shortDescLen > sizeof(paramPtr->m_shortDesc) - 1 || longDescLen > sizeof(paramPtr->m_longDesc) - 1) {
        assert(false);
        return 0;
    }

    if (descPtr->m_nparams == fillerPtr->m_size) {
        newsize = fillerPtr->m_size + 20; // most drivers have less than 20 parameters
        paramPtr = (DriverParamDesc*)realloc(descPtr->m_params, newsize * sizeof(DriverParamDesc));
        if (paramPtr == nullptr) {
            jack_error("Error realloc() failed for parameter array of %zu elements", newsize);
            return false;
        }
        fillerPtr->m_size = newsize;
        descPtr->m_params = paramPtr;
    }

    assert(desc_ptr->nparams < filler_ptr->size);
    paramPtr = descPtr->m_params + descPtr->m_nparams;

    memcpy(paramPtr->m_name.data(), name, nameLen + 1);
    paramPtr->m_character = character;
    paramPtr->m_type = type;
    paramPtr->m_value = *valuePtr;
    paramPtr->m_constraint = constraint;
    memcpy(paramPtr->m_shortDesc.data(), shortDesc, shortDescLen + 1);
    memcpy(paramPtr->m_longDesc.data(), longDesc, longDescLen + 1);

    descPtr->m_nparams++;
    return true;
}

SERVER_EXPORT
std::int32_t jack_constraint_add_enum(
    DriverParamConstraintDesc** constraintPtrPtr,
    std::uint32_t* arraySizePtr,
    DriverParamValue* valuePtr,
    const char* shortDesc)
{
    DriverParamConstraintDesc* constraintPtr;
    std::uint32_t arraySize;
    DriverParamValueEnum* possibleValuePtr;
    size_t len;

    len = strlen(shortDesc) + 1;
    if (len > sizeof(possibleValuePtr->m_shortDesc)) {
        assert(false);
        return false;
    }

    constraintPtr = *constraintPtrPtr;
    if (constraintPtr == nullptr) {
        constraintPtr = (DriverParamConstraintDesc*)calloc(1, sizeof(DriverParamConstraintDesc));
        if (constraintPtr == nullptr) {
            jack_error("calloc() failed to allocate memory for param constraint struct");
            return false;
        }

        arraySize = 0;
    } else {
        arraySize = *arraySizePtr;
    }

    if (constraintPtr->m_constraint.m_enumeration.m_count == arraySize) {
        arraySize += 10;
        possibleValuePtr = (DriverParamValueEnum*)realloc(
            constraintPtr->m_constraint.m_enumeration.m_possibleValuesArray,
            sizeof(DriverParamValueEnum) * arraySize);
        if (possibleValuePtr == nullptr) {
            jack_error("realloc() failed to (re)allocate memory for possible values array");
            return false;
        }
        constraintPtr->m_constraint.m_enumeration.m_possibleValuesArray = possibleValuePtr;
    } else {
        possibleValuePtr = constraintPtr->m_constraint.m_enumeration.m_possibleValuesArray;
    }

    possibleValuePtr += constraintPtr->m_constraint.m_enumeration.m_count;
    constraintPtr->m_constraint.m_enumeration.m_count++;

    possibleValuePtr->m_value = *valuePtr;
    memcpy(possibleValuePtr->m_shortDesc.data(), shortDesc, len);

    *constraintPtrPtr = constraintPtr;
    *arraySizePtr = arraySize;

    return true;
}

SERVER_EXPORT
void jack_constraint_free(
    DriverParamConstraintDesc* constraintPtr)
{
    if (constraintPtr != nullptr) {
        if ((constraintPtr->m_flags & kConstraintFlagRange) == 0) {
            free(constraintPtr->m_constraint.m_enumeration.m_possibleValuesArray);
        }

        free(constraintPtr);
    }
}

#define JACK_CONSTRAINT_COMPOSE_ENUM_IMPL(type, copy)               \
    JACK_CONSTRAINT_COMPOSE_ENUM(type)                              \
    {                                                               \
        DriverParamConstraintDesc* constraint_ptr;                  \
        std::uint32_t array_size;                                   \
        DriverParamValue value;                                     \
        struct jack_constraint_enum_##type##_descriptor* descr_ptr; \
                                                                    \
        constraint_ptr = nullptr;                                   \
        for (descr_ptr = descr_array_ptr;                           \
            descr_ptr->m_value;                                     \
            descr_ptr++) {                                          \
            copy;                                                   \
            if (!jack_constraint_add_enum(                          \
                    &constraint_ptr,                                \
                    &array_size,                                    \
                    &value,                                         \
                    descr_ptr->m_shortDesc)) {                      \
                jack_constraint_free(constraint_ptr);               \
                return nullptr;                                     \
            }                                                       \
        }                                                           \
                                                                    \
        constraint_ptr->m_flags = flags;                            \
                                                                    \
        return constraint_ptr;                                      \
    }

JACK_CONSTRAINT_COMPOSE_ENUM_IMPL(uint32, value.m_c = descr_ptr->m_value);
JACK_CONSTRAINT_COMPOSE_ENUM_IMPL(sint32, value.m_c = descr_ptr->m_value);
JACK_CONSTRAINT_COMPOSE_ENUM_IMPL(char, value.m_c = descr_ptr->m_value);
JACK_CONSTRAINT_COMPOSE_ENUM_IMPL(str, strcpy(value.m_str.data(), descr_ptr->m_value));
