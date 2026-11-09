/*
  Copyright (C) 2011 David Robillard
  Copyright (C) 2013 Paul Davis
  Copyright (C) 2019 Filipe Coelho

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or (at
  your option) any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
  License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, write to the Free Software Foundation,
  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "JackMetadata.h"
#include "JackCompilerDeps.h"


#include <stdlib.h>

#define JACK_METADATA_PREFIX "http://jackaudio.org/metadata/"
LIB_EXPORT const char* JACK_METADATA_CONNECTED   = JACK_METADATA_PREFIX "connected";
LIB_EXPORT const char* JACK_METADATA_EVENT_TYPES = JACK_METADATA_PREFIX "event-types";
LIB_EXPORT const char* JACK_METADATA_HARDWARE    = JACK_METADATA_PREFIX "hardware";
LIB_EXPORT const char* JACK_METADATA_ICON_LARGE  = JACK_METADATA_PREFIX "icon-large";
LIB_EXPORT const char* JACK_METADATA_ICON_NAME   = JACK_METADATA_PREFIX "icon-name";
LIB_EXPORT const char* JACK_METADATA_ICON_SMALL  = JACK_METADATA_PREFIX "icon-small";
LIB_EXPORT const char* JACK_METADATA_ORDER       = JACK_METADATA_PREFIX "order";
LIB_EXPORT const char* JACK_METADATA_PORT_GROUP  = JACK_METADATA_PREFIX "port-group";
LIB_EXPORT const char* JACK_METADATA_PRETTY_NAME = JACK_METADATA_PREFIX "pretty-name";
LIB_EXPORT const char* JACK_METADATA_SIGNAL_TYPE = JACK_METADATA_PREFIX "signal-type";
#undef JACK_METADATA_PREFIX

namespace jackmini
{

JackMetadata::JackMetadata()
{}

JackMetadata::~JackMetadata()
{}

int JackMetadata::GetProperty(jack_uuid_t, const char*, char**, char**)
{
    return -1;
}

int JackMetadata::GetProperties(jack_uuid_t, jack_description_t*)
{
    return -1;
}

int JackMetadata::GetAllProperties(jack_description_t**)
{
    return -1;
}

int JackMetadata::GetDescription(jack_uuid_t, jack_description_t*)
{
    return 0;
}

int JackMetadata::GetAllDescriptions(jack_description_t**)
{
    return 0;
}

void JackMetadata::FreeDescription(jack_description_t* desc, int free_actual_description_too)
{
    for (uint32_t n = 0; n < desc->property_cnt; ++n) {
        free((char*)desc->properties[n].key);
        free((char*)desc->properties[n].data);
        if (desc->properties[n].type) {
            free((char*)desc->properties[n].type);
        }
    }

    free(desc->properties);

    if (free_actual_description_too) {
        free(desc);
    }
}

int JackMetadata::SetProperty(jack_uuid_t, const char*, const char*, const char*)
{
    return -1;
}

int JackMetadata::RemoveProperty(jack_uuid_t, const char*)
{
    return -1;
}

int JackMetadata::RemoveProperties(jack_uuid_t)
{
    return -1;
}

int JackMetadata::RemoveAllProperties()
{
    return -1;
}

} // end of namespace
