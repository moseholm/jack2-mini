// Based on gslist.c from glib-1.2.9 (LGPL).
//
// Adaption to JACK, Copyright (C) 2002 Kai Vehmanen.
// - replaced use of gtypes with normal ANSI C types
// - glib's memory allocation routines replaced with
// malloc/free calls
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

#include <jack/systemdeps.h>
#include <stdlib.h>
#include <cstdint>

typedef struct _JSListC JSListC;

typedef std::int32_t (*JCompareFuncC)(void* a, void* b);
struct _JSListC {
    void* m_data;
    JSListC* m_next;
};

static __inline__ JSListC*
jack_slist_alloc(void)
{
    JSListC* newList;

    newList = (JSListC*)malloc(sizeof(JSListC));
    if (newList) {
        newList->m_data = nullptr;
        newList->m_next = nullptr;
    }

    return newList;
}

static __inline__ JSListC*
jack_slist_prepend(JSListC* list, void* data)
{
    JSListC* newList;

    newList = (JSListC*)malloc(sizeof(JSListC));
    if (newList) {
        newList->m_data = data;
        newList->m_next = list;
    }

    return newList;
}

inline JSListC* jack_slist_next(const JSListC* slist)
{
    return slist ? slist->m_next : nullptr;
}
static __inline__ JSListC*
jack_slist_last(JSListC* list)
{
    if (list) {
        while (list->m_next)
            list = list->m_next;
    }

    return list;
}

static __inline__ JSListC*
jack_slist_remove_link(JSListC* list,
    JSListC* link)
{
    JSListC* tmp;
    JSListC* prev;

    prev = nullptr;
    tmp = list;

    while (tmp) {
        if (tmp == link) {
            if (prev)
                prev->m_next = tmp->m_next;
            if (list == tmp)
                list = list->m_next;

            tmp->m_next = nullptr;
            break;
        }

        prev = tmp;
        tmp = tmp->m_next;
    }

    return list;
}

static __inline__ void
jack_slist_free(JSListC* list)
{
    while (list) {
        JSListC* next = list->m_next;
        free(list);
        list = next;
    }
}

static __inline__ void
jack_slist_free_1(JSListC* list)
{
    if (list) {
        free(list);
    }
}

static __inline__ JSListC*
jack_slist_remove(JSListC* list,
    void* data)
{
    JSListC* tmp;
    JSListC* prev;

    prev = nullptr;
    tmp = list;

    while (tmp) {
        if (tmp->m_data == data) {
            if (prev)
                prev->m_next = tmp->m_next;
            if (list == tmp)
                list = list->m_next;

            tmp->m_next = nullptr;
            jack_slist_free(tmp);

            break;
        }

        prev = tmp;
        tmp = tmp->m_next;
    }

    return list;
}

static __inline__ std::uint32_t
jack_slist_length(JSListC* list)
{
    std::uint32_t length;

    length = 0;
    while (list) {
        length++;
        list = list->m_next;
    }

    return length;
}

static __inline__ JSListC*
jack_slist_find(JSListC* list,
    void* data)
{
    while (list) {
        if (list->m_data == data)
            break;
        list = list->m_next;
    }

    return list;
}

static __inline__ JSListC*
jack_slist_copy(JSListC* list)
{
    JSListC* newList = nullptr;

    if (list) {
        JSListC* last;

        newList = jack_slist_alloc();
        newList->m_data = list->m_data;
        last = newList;
        list = list->m_next;
        while (list) {
            last->m_next = jack_slist_alloc();
            last = last->m_next;
            last->m_data = list->m_data;
            list = list->m_next;
        }
    }

    return newList;
}

static __inline__ JSListC*
jack_slist_append(JSListC* list,
    void* data)
{
    JSListC* newList;
    JSListC* last;

    newList = jack_slist_alloc();
    newList->m_data = data;

    if (list) {
        last = jack_slist_last(list);
        last->m_next = newList;

        return list;
    } else
        return newList;
}

static __inline__ JSListC*
jack_slist_sort_merge(JSListC* l1,
    JSListC* l2,
    JCompareFuncC compareFunc)
{
    JSListC list, *l;

    l = &list;

    while (l1 && l2) {
        if (compareFunc(l1->m_data, l2->m_data) < 0) {
            l = l->m_next = l1;
            l1 = l1->m_next;
        } else {
            l = l->m_next = l2;
            l2 = l2->m_next;
        }
    }
    l->m_next = l1 ? l1 : l2;

    return list.m_next;
}

static __inline__ JSListC*
jack_slist_sort(JSListC* list,
    JCompareFuncC compareFunc)
{
    JSListC *l1, *l2;

    if (!list)
        return nullptr;
    if (!list->m_next)
        return list;

    l1 = list;
    l2 = list->m_next;

    while ((l2 = l2->m_next) != nullptr) {
        if ((l2 = l2->m_next) == nullptr)
            break;
        l1 = l1->m_next;
    }
    l2 = l1->m_next;
    l1->m_next = nullptr;

    return jack_slist_sort_merge(jack_slist_sort(list, compareFunc),
        jack_slist_sort(l2, compareFunc),
        compareFunc);
}
