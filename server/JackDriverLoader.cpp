/*
Copyright (C) 2001-2005 Paul Davis
Copyright (C) 2004-2008 Grame

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include "JackDriverLoader.h"
#include "JackCString.h"
#include "JackConstants.h"
#include "JackDriverInfo.h"
#include "JackError.h"
#include "JackSystemDeps.h"
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <dirent.h>


void jack_print_driver_options(jack_driver_desc_t* desc, FILE* file)
{
    unsigned long i;
    std::array<char, JACK_DRIVER_PARAM_STRING_MAX + 1> arg_default { };

    for (i = 0; i < desc->nparams; i++) {
        switch (desc->params[i].type) {
        case JackDriverParamInt:
            jackmini::FormatCString(arg_default, "{}", desc->params[i].value.i);
            break;
        case JackDriverParamUInt:
            jackmini::FormatCString(arg_default, "{}", desc->params[i].value.ui);
            break;
        case JackDriverParamChar:
            jackmini::FormatCString(arg_default, "{}", desc->params[i].value.c);
            break;
        case JackDriverParamString:
            if (desc->params[i].value.str && strcmp(desc->params[i].value.str, "") != 0) {
                jackmini::FormatCString(arg_default, "{}", desc->params[i].value.str);
            } else {
                jackmini::FormatCString(arg_default, "none");
            }
            break;
        case JackDriverParamBool:
            jackmini::FormatCString(arg_default, "{}", desc->params[i].value.i ? "true" : "false");
            break;
        }

        fprintf(file, "\t-%c, --%s \t%s (default: %s)\n",
            desc->params[i].character,
            desc->params[i].name,
            desc->params[i].long_desc,
            arg_default.data());
    }
}

void jack_print_driver_param_usage(jack_driver_desc_t* desc, unsigned long param, FILE* file)
{
    fprintf(file, "Usage information for the '%s' parameter for driver '%s':\n",
        desc->params[param].name, desc->name);
    fprintf(file, "%s\n", desc->params[param].long_desc);
}

jack_driver_desc_t* jack_find_driver_descriptor(JSList* drivers, const char* name)
{
    jack_driver_desc_t* desc = 0;
    JSList* node;

    for (node = drivers; node; node = jack_slist_next(node)) {
        desc = (jack_driver_desc_t*)node->data;

        if (strcmp(desc->name, name) != 0) {
            desc = NULL;
        } else {
            break;
        }
    }

    return desc;
}

/* The names of the entries in a directory.  If the listing fails, ec is set
 * and the names read so far are returned. */
static std::vector<std::string> ListDirectory(const char* dir_path, std::error_code& ec)
{
    std::vector<std::string> names;
    for (std::filesystem::directory_iterator it(dir_path, ec), end; !ec && it != end; it.increment(ec)) {
        names.push_back(it->path().filename().string());
    }
    return names;
}

/* true if the file name has an extension that starts with "so", as in "jack_x.so" */
static bool HasSharedObjectExtension(std::string_view name)
{
    const auto dot = name.rfind('.');
    return dot != std::string_view::npos && name.substr(dot + 1).starts_with("so");
}

static void* check_symbol(const char* sofile, const char* symbol, const char* driver_dir, void** res_dllhandle = NULL)
{
    void* dlhandle;
    void* res = NULL;
    std::array<char, 1024> filename { };
    jackmini::FormatCString(std::span<char>(filename.data(), 1022), "{}/{}", driver_dir, sofile);

    if ((dlhandle = LoadDriverModule(filename.data())) == NULL) {
        jack_error("Could not open component .so '%s': %s", filename.data(), dlerror());
    } else {
        res = (void*)GetDriverProc(dlhandle, symbol);
        if (res_dllhandle) {
            *res_dllhandle = dlhandle;
        } else {
            UnloadDriverModule(dlhandle);
        }
    }

    return res;
}

/* Unloads a driver module when the owning pointer goes out of scope. */
struct DriverModuleCloser {
    void operator()(void* handle) const
    {
        if (handle) {
            UnloadDriverModule(handle);
        }
    }
};

/* Loads the module of a driver file, checks its interface version, and returns its descriptor. A driver whose name is
   already in the list is refused, so the first one is used. */
static jack_driver_desc_t* load_descriptor(JSList* drivers, const char* sofile, const char* driver_dir)
{
    jack_driver_desc_t* descriptor = NULL;
    jack_driver_desc_t* other_descriptor;
    std::array<char, 1024> filename { };
    JSList* node;
    void* raw_handle = NULL;

    jackmini::FormatCString(std::span<char>(filename.data(), 1022), "{}/{}", driver_dir, sofile);

    const auto get_module = reinterpret_cast<jackmini::driver::ModuleFunction>(
        check_symbol(sofile, jackmini::driver::kModuleSymbol, driver_dir, &raw_handle));
    std::unique_ptr<void, DriverModuleCloser> dlhandle(raw_handle);
    const jackmini::driver::Module* module = get_module ? get_module() : NULL;

    if (module == NULL) {
        jack_error("load_descriptor : dll %s is not a driver", sofile);
        return NULL;
    }

    if (module->version != jackmini::driver::kModuleVersion) {
        jack_error("Driver from '%s' has interface version %u, this server needs version %u",
            filename.data(), module->version, jackmini::driver::kModuleVersion);
        return NULL;
    }

    if ((descriptor = module->describe()) == NULL) {
        jack_error("Driver from '%s' returned NULL descriptor", filename.data());
        return NULL;
    }

    /* check it doesn't exist already */
    for (node = drivers; node; node = jack_slist_next(node)) {
        other_descriptor = (jack_driver_desc_t*)node->data;
        if (strcmp(descriptor->name, other_descriptor->name) == 0) {
            jack_error("The drivers in '%s' and '%s' both have the name '%s'; using the first",
                other_descriptor->file, filename.data(), other_descriptor->name);
            /* FIXME: delete the descriptor */
            return NULL;
        }
    }

    strncpy(descriptor->file, filename.data(), JACK_PATH_MAX);
    return descriptor;
}


JSList* jack_drivers_load(JSList* drivers)
{
    JSList* driver_list = NULL;
    jack_driver_desc_t* desc = NULL;

    const char* driver_dir;
    if ((driver_dir = getenv("JACK_DRIVER_DIR")) == 0) {
        driver_dir = ADDON_DIR;
    }

    /* search through the driver_dir and add get descriptors
    from the .so files in it */
    std::error_code ec;
    const auto entries = ListDirectory(driver_dir, ec);
    if (ec) {
        jack_error("Could not open driver directory %s: %s",
            driver_dir, ec.message().c_str());
        return NULL;
    }

    for (const std::string& name : entries) {

        /* check the filename is of the right format */
        if (!std::string_view(name).starts_with("jack_")) {
            continue;
        }
        if (!HasSharedObjectExtension(name)) {
            continue;
        }

        desc = load_descriptor(drivers, name.c_str(), driver_dir);
        if (desc) {
            driver_list = jack_slist_append(driver_list, desc);
        } else {
            jack_error("Driver file \"%s\" does not give a usable driver descriptor", name.c_str());
        }
    }

    if (!driver_list) {
        jack_error("Could not find any drivers in %s!", driver_dir);
        return NULL;
    }

    return driver_list;
}


jackmini::JackDriverClientInterface* JackDriverInfo::Open(jack_driver_desc_t* driver_desc,
    jackmini::JackLockedEngine* engine,
    jackmini::JackSynchro* synchro,
    const JSList* params)
{
    const char* errstr;

    fHandle = LoadDriverModule(driver_desc->file);

    if (fHandle == NULL) {
        if ((errstr = dlerror()) != 0) {
            jack_error("Can't load \"%s\": %s", driver_desc->file, errstr);

        } else {
            jack_error("Error loading driver shared object %s", driver_desc->file);
        }
        return NULL;
    }


    const auto get_module = reinterpret_cast<jackmini::driver::ModuleFunction>(GetDriverProc(fHandle, jackmini::driver::kModuleSymbol));
    fModule = get_module ? get_module() : NULL;
    if (fModule == NULL) {
        jack_error("No driver module entry point in shared object %s", driver_desc->file);
        return NULL;
    }

    if (fModule->version != jackmini::driver::kModuleVersion) {
        jack_error("Driver %s has interface version %u, this server needs version %u",
            driver_desc->file, fModule->version, jackmini::driver::kModuleVersion);
        return NULL;
    }

    fBackend = fModule->initialize(engine, synchro, params);
    return fBackend;
}

JackDriverInfo::~JackDriverInfo()
{
    delete fBackend;
    if (fHandle) {
        UnloadDriverModule(fHandle);
    }
}

SERVER_EXPORT jack_driver_desc_t* jack_driver_descriptor_construct(
    const char* name,
    jack_driver_type_t type,
    const char* description,
    jack_driver_desc_filler_t* filler_ptr)
{
    size_t name_len;
    size_t description_len;
    jack_driver_desc_t* desc_ptr;

    name_len = strlen(name);
    description_len = strlen(description);

    if (name_len > sizeof(desc_ptr->name) - 1 || description_len > sizeof(desc_ptr->desc) - 1) {
        assert(false);
        return 0;
    }

    desc_ptr = (jack_driver_desc_t*)calloc(1, sizeof(jack_driver_desc_t));
    if (desc_ptr == NULL) {
        jack_error("Error calloc() failed to allocate memory for driver descriptor struct");
        return 0;
    }

    memcpy(desc_ptr->name, name, name_len + 1);
    memcpy(desc_ptr->desc, description, description_len + 1);

    desc_ptr->nparams = 0;
    desc_ptr->type = type;

    if (filler_ptr != NULL) {
        filler_ptr->size = 0;
    }

    return desc_ptr;
}

SERVER_EXPORT int jack_driver_descriptor_add_parameter(
    jack_driver_desc_t* desc_ptr,
    jack_driver_desc_filler_t* filler_ptr,
    const char* name,
    char character,
    jack_driver_param_type_t type,
    const jack_driver_param_value_t* value_ptr,
    jack_driver_param_constraint_desc_t* constraint,
    const char* short_desc,
    const char* long_desc)
{
    size_t name_len;
    size_t short_desc_len;
    size_t long_desc_len;
    jack_driver_param_desc_t* param_ptr;
    size_t newsize;

    name_len = strlen(name);
    short_desc_len = strlen(short_desc);

    if (long_desc != NULL) {
        long_desc_len = strlen(long_desc);
    } else {
        long_desc = short_desc;
        long_desc_len = short_desc_len;
    }

    if (name_len > sizeof(param_ptr->name) - 1 || short_desc_len > sizeof(param_ptr->short_desc) - 1 || long_desc_len > sizeof(param_ptr->long_desc) - 1) {
        assert(false);
        return 0;
    }

    if (desc_ptr->nparams == filler_ptr->size) {
        newsize = filler_ptr->size + 20; // most drivers have less than 20 parameters
        param_ptr = (jack_driver_param_desc_t*)realloc(desc_ptr->params, newsize * sizeof(jack_driver_param_desc_t));
        if (param_ptr == NULL) {
            jack_error("Error realloc() failed for parameter array of %zu elements", newsize);
            return false;
        }
        filler_ptr->size = newsize;
        desc_ptr->params = param_ptr;
    }

    assert(desc_ptr->nparams < filler_ptr->size);
    param_ptr = desc_ptr->params + desc_ptr->nparams;

    memcpy(param_ptr->name, name, name_len + 1);
    param_ptr->character = character;
    param_ptr->type = type;
    param_ptr->value = *value_ptr;
    param_ptr->constraint = constraint;
    memcpy(param_ptr->short_desc, short_desc, short_desc_len + 1);
    memcpy(param_ptr->long_desc, long_desc, long_desc_len + 1);

    desc_ptr->nparams++;
    return true;
}

SERVER_EXPORT
int jack_constraint_add_enum(
    jack_driver_param_constraint_desc_t** constraint_ptr_ptr,
    uint32_t* array_size_ptr,
    jack_driver_param_value_t* value_ptr,
    const char* short_desc)
{
    jack_driver_param_constraint_desc_t* constraint_ptr;
    uint32_t array_size;
    jack_driver_param_value_enum_t* possible_value_ptr;
    size_t len;

    len = strlen(short_desc) + 1;
    if (len > sizeof(possible_value_ptr->short_desc)) {
        assert(false);
        return false;
    }

    constraint_ptr = *constraint_ptr_ptr;
    if (constraint_ptr == NULL) {
        constraint_ptr = (jack_driver_param_constraint_desc_t*)calloc(1, sizeof(jack_driver_param_constraint_desc_t));
        if (constraint_ptr == NULL) {
            jack_error("calloc() failed to allocate memory for param constraint struct");
            return false;
        }

        array_size = 0;
    } else {
        array_size = *array_size_ptr;
    }

    if (constraint_ptr->constraint.enumeration.count == array_size) {
        array_size += 10;
        possible_value_ptr = (jack_driver_param_value_enum_t*)realloc(
            constraint_ptr->constraint.enumeration.possible_values_array,
            sizeof(jack_driver_param_value_enum_t) * array_size);
        if (possible_value_ptr == NULL) {
            jack_error("realloc() failed to (re)allocate memory for possible values array");
            return false;
        }
        constraint_ptr->constraint.enumeration.possible_values_array = possible_value_ptr;
    } else {
        possible_value_ptr = constraint_ptr->constraint.enumeration.possible_values_array;
    }

    possible_value_ptr += constraint_ptr->constraint.enumeration.count;
    constraint_ptr->constraint.enumeration.count++;

    possible_value_ptr->value = *value_ptr;
    memcpy(possible_value_ptr->short_desc, short_desc, len);

    *constraint_ptr_ptr = constraint_ptr;
    *array_size_ptr = array_size;

    return true;
}

SERVER_EXPORT
void jack_constraint_free(
    jack_driver_param_constraint_desc_t* constraint_ptr)
{
    if (constraint_ptr != NULL) {
        if ((constraint_ptr->flags & JACK_CONSTRAINT_FLAG_RANGE) == 0) {
            free(constraint_ptr->constraint.enumeration.possible_values_array);
        }

        free(constraint_ptr);
    }
}

#define JACK_CONSTRAINT_COMPOSE_ENUM_IMPL(type, copy)               \
    JACK_CONSTRAINT_COMPOSE_ENUM(type)                              \
    {                                                               \
        jack_driver_param_constraint_desc_t* constraint_ptr;        \
        uint32_t array_size;                                        \
        jack_driver_param_value_t value;                            \
        struct jack_constraint_enum_##type##_descriptor* descr_ptr; \
                                                                    \
        constraint_ptr = NULL;                                      \
        for (descr_ptr = descr_array_ptr;                           \
            descr_ptr->value;                                       \
            descr_ptr++) {                                          \
            copy;                                                   \
            if (!jack_constraint_add_enum(                          \
                    &constraint_ptr,                                \
                    &array_size,                                    \
                    &value,                                         \
                    descr_ptr->short_desc)) {                       \
                jack_constraint_free(constraint_ptr);               \
                return NULL;                                        \
            }                                                       \
        }                                                           \
                                                                    \
        constraint_ptr->flags = flags;                              \
                                                                    \
        return constraint_ptr;                                      \
    }

JACK_CONSTRAINT_COMPOSE_ENUM_IMPL(uint32, value.c = descr_ptr->value);
JACK_CONSTRAINT_COMPOSE_ENUM_IMPL(sint32, value.c = descr_ptr->value);
JACK_CONSTRAINT_COMPOSE_ENUM_IMPL(char, value.c = descr_ptr->value);
JACK_CONSTRAINT_COMPOSE_ENUM_IMPL(str, strcpy(value.str, descr_ptr->value));
