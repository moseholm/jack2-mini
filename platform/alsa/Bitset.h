// bitset.h -- some simple bit vector set operations.
//
// This is useful for sets of small non-negative integers.  There are
// some obvious set operations that are not implemented because I
// don't need them right now.
//
// These functions represent sets as arrays of unsigned 32-bit
// integers allocated on the heap.  The first entry contains the set
// cardinality (number of elements allowed), followed by one or more
// words containing bit vectors.
//
//  $Id: bitset.h,v 1.2 2005/11/23 11:24:29 letz Exp $

//  Copyright (C) 2005 Jack O'Quin
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License as
//  published by the Free Software Foundation; either version 2 of the
//  License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#pragma once

#include <assert.h> // `#define NDEBUG' to disable
#include <inttypes.h> // POSIX standard fixed-size types
#include <cstdint>

// On some 64-bit machines, this implementation may be slightly
// inefficient, depending on how compilers allocate space for
// uint32_t.  For the set sizes I currently need, this is acceptable.
// It should not be hard to pack the bits better, if that becomes
// worthwhile.
typedef std::uint32_t _bitset_word_t;
typedef _bitset_word_t* bitset_t;

constexpr std::uint32_t wordSize(std::uint32_t cardinality)
{
    return 1 + (cardinality + 31) / 32;
}

constexpr std::size_t byteSize(std::uint32_t cardinality)
{
    return wordSize(cardinality) * sizeof(_bitset_word_t);
}

constexpr std::uint32_t wordIndex(std::uint32_t element)
{
    return 1 + element / 32;
}

constexpr std::uint32_t bitIndex(std::uint32_t element)
{
    return element & 037;
}

static inline void
bitset_add(bitset_t set, std::uint32_t element)
{
    assert(element < set
               [0]);
    set
        [wordIndex(element)] |= (1 << bitIndex(element));
}

static inline void
bitset_copy(bitset_t to_set, bitset_t from_set)
{
    assert(to_set[0] == from_set[0]);
    memcpy(to_set, from_set, byteSize(to_set[0]));
}

static inline void
bitset_create(bitset_t* set, std::uint32_t cardinality)
{
    *set
        = (bitset_t)calloc(wordSize(cardinality),
            sizeof(_bitset_word_t));
    assert(*set);
    *set
        [0] = cardinality;
}

static inline void
bitset_destroy(bitset_t* set)
{
    if (*set) {
        free(*set);
        *set
            = (bitset_t)0;
    }
}

static inline std::int32_t
bitset_empty(bitset_t set)
{
    std::int32_t i;
    _bitset_word_t result = 0;
    std::int32_t nwords = wordSize(set
            [0]);
    for (i = 1; i < nwords; i++) {
        result |= set
            [i];
    }
    return (result == 0);
}

static inline std::int32_t
bitset_contains(bitset_t set, std::uint32_t element)
{
    assert(element < set
               [0]);
    return (0 != (set[wordIndex(element)] & (1 << bitIndex(element))));
}

static inline void
bitset_remove(bitset_t set, std::uint32_t element)
{
    assert(element < set
               [0]);
    set
        [wordIndex(element)] &= ~(1 << bitIndex(element));
}
