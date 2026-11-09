/*
  Copyright (C) 2011 David Robillard
  Copyright (C) 2013 Paul Davis

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

#ifndef __JackMetadata__
#define __JackMetadata__

#include <stdint.h>
#include <jack/uuid.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* key;
    const char* data;
    const char* type;
} jack_property_t;

typedef struct {
    jack_uuid_t      subject;
    uint32_t         property_cnt;
    jack_property_t* properties;
    uint32_t         property_size;
} jack_description_t;

typedef enum {
    PropertyCreated,
    PropertyChanged,
    PropertyDeleted
} jack_property_change_t;

typedef void (*JackPropertyChangeCallback)(jack_uuid_t            subject,
                                           const char*            key,
                                           jack_property_change_t change,
                                           void*                  arg);

#ifdef __cplusplus
}
#endif


namespace jackmini
{

/*!
\brief Metadata base.
*/

class JackMetadata
{
    public:

        JackMetadata();
        ~JackMetadata();

        int GetProperty(jack_uuid_t subject, const char* key, char** value, char** type);
        int GetProperties(jack_uuid_t subject, jack_description_t* desc);
        int GetAllProperties(jack_description_t** descriptions);

        int GetDescription(jack_uuid_t subject, jack_description_t* desc);
        int GetAllDescriptions(jack_description_t** descs);
        void FreeDescription(jack_description_t* desc, int free_actual_description_too);

        int SetProperty(jack_uuid_t subject, const char* key, const char* value, const char* type);

        int RemoveProperty(jack_uuid_t subject, const char* key);
        int RemoveProperties(jack_uuid_t subject);
        int RemoveAllProperties();

};

} // end of namespace

#endif
