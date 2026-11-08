/* This module provides the POSIX shared memory interface used by JACK.
 *
 * The code is divided into two sections:
 *
 *	- common (interface-independent) code
 *	- POSIX implementation
 */

/*
 Copyright (C) 2001-2003 Paul Davis
 Copyright (C) 2005-2012 Grame

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation; either version 2.1 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

 */

#ifndef __jack_shm_h__
#define __jack_shm_h__

#include <limits.h>
#include <sys/types.h>
#include "types.h"
#include "JackCompilerDeps.h"
#include "JackConstants.h"

#define TRUE 1
#define FALSE 0

#ifdef __cplusplus
extern "C"
{
#endif

#define MAX_SHM_ID 256              /* generally about 16 per server */
#define JACK_SHM_MAGIC 0x4a41434b	/* shm magic number: "JACK" */
#define JACK_SHM_NULL_INDEX -1		/* NULL SHM index */
#define JACK_SHM_REGISTRY_INDEX -2	/* pseudo SHM index for registry */


    /* On POSIX systems, SHM_NAME_MAX is the maximum length of a shared
     * memory segment name (instead of NAME_MAX or PATH_MAX as defined by
     * the standard).
     */
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#ifndef SHM_NAME_MAX
#define SHM_NAME_MAX NAME_MAX
#endif
    typedef char shm_name_t[SHM_NAME_MAX];
    typedef shm_name_t jack_shm_id_t;

    /* shared memory type */
    typedef enum {
        shm_POSIX = 1 			/* POSIX shared memory */
    } jack_shmtype_t;

    typedef int16_t jack_shm_registry_index_t;

    /**
     * A structure holding information about shared memory allocated by
     * JACK. this persists across invocations of JACK, and can be used by
     * multiple JACK servers.  It contains no pointers and is valid across
     * address spaces.
     *
     * The registry consists of two parts: a header, followed by an array of
     * segment registry entries.
     */
    typedef struct _jack_shm_header {
        uint32_t	magic;	/* magic number */
        uint16_t	protocol;	/* JACK protocol version */
        jack_shmtype_t	type;	/* shm type */
        jack_shmsize_t	size;	/* total registry segment size */
        jack_shmsize_t	hdr_len;	/* size of header */
        jack_shmsize_t	entry_len; /* size of registry entry */
    }
    jack_shm_header_t;

    typedef struct _jack_shm_registry {
        jack_shm_registry_index_t index;     /* offset into the registry */
        pid_t allocator; /* PID that created shm segment */
        jack_shmsize_t size;      /* for POSIX unattach */
        jack_shm_id_t id;        /* POSIX shm name, see above */
    }
    jack_shm_registry_t;

#define JACK_SHM_REGISTRY_SIZE (sizeof (jack_shm_header_t) \
				+ sizeof (jack_shm_registry_t) * MAX_SHM_ID)

    /**
     * a structure holding information about shared memory
     * allocated by JACK. this version is valid only
     * for a given address space. It contains a pointer
     * indicating where the shared memory has been
     * attached to the address space.
     */

    PRE_PACKED_STRUCTURE
    struct _jack_shm_info {
        jack_shm_registry_index_t index;       /* offset into the registry */
        uint32_t size;
        union {
            void *attached_at;  /* address where attached */
            char ptr_size[8];
        } ptr;  /* a "pointer" that has the same 8 bytes size when compiling in 32 or 64 bits */
    } POST_PACKED_STRUCTURE;

    typedef struct _jack_shm_info jack_shm_info_t;

    /* utility functions used only within JACK */

    void jack_shm_copy_from_registry (jack_shm_info_t*,
                jack_shm_registry_index_t);
    void jack_shm_copy_to_registry (jack_shm_info_t*,
                                               jack_shm_registry_index_t*);
    int jack_release_shm_info (jack_shm_registry_index_t);
    char* jack_shm_addr (jack_shm_info_t* si);

    /* here begin the API */
    int jack_initialize_shm (const char *server_name);
    int jack_initialize_shm_server (int new_registry);
    int jack_initialize_shm_client (void);
    int jack_cleanup_shm (void);

    int jack_shmalloc (const char *shm_name, jack_shmsize_t size,
                                  jack_shm_info_t* result);
    void jack_release_shm (jack_shm_info_t*);
    void jack_release_lib_shm (jack_shm_info_t*);
    void jack_destroy_shm (jack_shm_info_t*);
    int jack_attach_shm (jack_shm_info_t*);
    int jack_attach_lib_shm (jack_shm_info_t*);
    int jack_attach_shm_read (jack_shm_info_t*);
    int jack_attach_lib_shm_read (jack_shm_info_t*);
    int jack_resize_shm (jack_shm_info_t*, jack_shmsize_t size);

#ifdef __cplusplus
}
#endif

#endif /* __jack_shm_h__ */
