// Copyright (C) 2026 JACK contributors
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

// Clang's RealtimeSanitizer (-fsanitize=realtime) only tracks calls made from
// functions marked [[clang::nonblocking]]. Keep the attributes out of normal
// builds: GCC and older Clang do not understand them, and they are only useful
// when the sanitizer runtime is linked in.
#if defined(JACK_REALTIME_SANITIZER) && defined(__clang__) && defined(__has_attribute)
#if __has_attribute(nonblocking)
#define JACK_REALTIME_FN [[clang::nonblocking]]
#else
#define JACK_REALTIME_FN
#endif
#if __has_attribute(blocking)
#define JACK_BLOCKING_FN [[clang::blocking]]
#else
#define JACK_BLOCKING_FN
#endif
#else
#define JACK_REALTIME_FN
#define JACK_BLOCKING_FN
#endif

#if defined(JACK_REALTIME_SANITIZER) && defined(__clang__)
#include <sanitizer/rtsan_interface.h>
// Use these only for a deliberate, documented exception on a realtime path.
#define JACK_RTSAN_DISABLE() __rtsan_disable()
#define JACK_RTSAN_ENABLE() __rtsan_enable()
#define JACK_RTSAN_SCOPED_DISABLE() \
    JACK_RT_UNCHECKED("rtsan runtime disabler", [[maybe_unused]] __rtsan::ScopedDisabler jackRtsanDisabler {})

// A documented exemption from static function-effect checks. The reason is a
// compile-time string, not evaluated; keep it short. Follow Clang's own
// NONBLOCKING_UNSAFE pattern from the FunctionEffectAnalysis documentation.
#define JACK_RT_UNCHECKED(reason, ...)                              \
    _Pragma("clang diagnostic push")                                \
    _Pragma("clang diagnostic ignored \"-Wunknown-warning-option\"") \
    _Pragma("clang diagnostic ignored \"-Wfunction-effects\"")       \
    __VA_ARGS__                                                      \
    _Pragma("clang diagnostic pop")
#else
#define JACK_RTSAN_DISABLE() ((void)0)
#define JACK_RTSAN_ENABLE() ((void)0)
#define JACK_RTSAN_SCOPED_DISABLE() ((void)0)
#define JACK_RT_UNCHECKED(reason, ...) __VA_ARGS__
#endif
