// Copyright (C) 2001 Paul Davis
// Copyright (C) 2004 Jack O'Quin
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

#include "common/CompilerDeps.h"

#include <pthread.h>
#include <array>
#include <cstdint>

typedef std::uint64_t UuidC;

typedef std::int32_t ShmsizeC;

// Type used to represent sample frame counts.
typedef std::uint32_t NframesC;

// Type used to represent the value of free running
// monotonic clock with units of microseconds.
typedef std::uint64_t TimeC;

//  Ports have unique ids. A port registration callback is the only
//  place you ever need to know their value.
typedef std::uint32_t PortIdC;

typedef std::uint32_t PortTypeIdC;

//  @ref jack_options_t bits
enum OptionsC {

    // Null value to use when no option bits are needed.
    kNullOption = 0x00,

    // Reserved: clients never start a server.
    kNoStartServer = 0x01,

    // Use the exact client name requested.  Otherwise, JACK
    // automatically generates a unique one, if needed.
    kUseExactName = 0x02,

    // Open with optional <em>(char *) server_name</em> parameter.
    kServerName = 0x04,

    // Load internal client from optional <em>(char *)
    // load_name</em>.  Otherwise use the @a client_name.
    kLoadName = 0x08,

    // Pass optional <em>(char *) load_init</em> string to the
    // jack_initialize() entry point of an internal client.
    kLoadInit = 0x10,

    // pass a SessionID Token this allows the sessionmanager to identify the client again.
    kSessionID = 0x20
};

//  Options for several JACK operations, formed by OR-ing together the
//  relevant @ref JackOptions bits.

//  @ref jack_status_t bits
enum StatusC {

    // Overall operation failed.
    kFailure = 0x01,

    // The operation contained an invalid or unsupported option.
    kInvalidOption = 0x02,

    // The desired client name was not unique.  With the @ref
    // JackUseExactName option this situation is fatal.  Otherwise,
    // the name was modified by appending a dash and a two-digit
    // number in the range "-01" to "-99".  The
    // jack_get_client_name() function will return the exact string
    // that was used.  If the specified @a client_name plus these
    // extra characters would be too long, the open fails instead.
    kNameNotUnique = 0x04,

    // Reserved: a client never starts a server, so these are never reported.
    kServerStarted = 0x08,
    kServerFailed = 0x10,

    // Communication error with the JACK server.
    kServerError = 0x20,

    // Requested client does not exist.
    kNoSuchClient = 0x40,

    // Unable to load internal client
    kLoadFailure = 0x80,

    // Unable to initialize client
    kInitFailure = 0x100,

    // Unable to access shared memory
    kShmFailure = 0x200,

    // Client's protocol version does not match
    kVersionError = 0x400,

    // Backend error
    kBackendError = 0x800,

    // Client zombified failure
    kClientZombie = 0x1000
};

//  Status word returned from several JACK operations, formed by
//  OR-ing together the relevant @ref JackStatus bits.

//  @ref jack_latency_callback_mode_t
enum LatencyCallbackModeC {

    // Latency Callback for Capture Latency.
    // Input Ports have their latency value setup.
    // In the Callback the client needs to set the latency of the output ports
    kCaptureLatency,

    // Latency Callback for Playback Latency.
    // Output Ports have their latency value setup.
    // In the Callback the client needs to set the latency of the input ports
    kPlaybackLatency

};

// The minimum and maximum latency of a port, in frames.
struct LatencyRangeTag {
    NframesC m_min;
    NframesC m_max;
};

using LatencyRangeC = LatencyRangeTag;

// Called once after the creation of the thread in which the other callbacks run.
using ThreadInitCallbackC = void (*)(void* arg);

// Direction bits passed to a JackSecondarySampleRateCallback.  A card whose
// capture and playback share one clock reports both bits in one call.
enum SecondaryRateDirectionC {
    kSecondaryRateCapture = 0x1,
    kSecondaryRatePlayback = 0x2
};

// Used for the type argument of jack_port_register() for default
// audio ports and midi ports.
inline constexpr const char* kDefaultAudioType = "32 bit float mono audio";
inline constexpr const char* kDefaultMidiType = "8 bit raw midi";

// For convenience, use this typedef if you want to be able to change
// between float and double. You may want to typedef sample_t to
// jack_default_audio_sample_t in your application.
typedef float DefaultAudioSampleC;

//  A port has a set of flags that are formed by AND-ing together the
//  desired values from the list below. The flags "JackPortIsInput" and
//  "JackPortIsOutput" are mutually exclusive and it is an error to use
//  them both.
enum PortFlagsC {

    // if JackPortIsInput is set, then the port can receive
    // data.
    kPortIsInput = 0x1,

    // if JackPortIsOutput is set, then data can be read from
    // the port.
    kPortIsOutput = 0x2,

    // if JackPortIsPhysical is set, then the port corresponds
    // to some kind of physical I/O connector.
    kPortIsPhysical = 0x4,

    // if JackPortCanMonitor is set, then a call to
    // jack_port_request_monitor() makes sense.
    //
    // Precisely what this means is dependent on the client. A typical
    // result of it being called with TRUE as the second argument is
    // that data that would be available from an output port (with
    // JackPortIsPhysical set) is sent to a physical output connector
    // as well, so that it can be heard/seen/whatever.
    //
    // Clients that do not control physical interfaces
    // should never create ports with this bit set.
    kPortCanMonitor = 0x8,

    // JackPortIsTerminal means:
    //
    //  for an input port: the data received by the port
    //                    will not be passed on or made
    //                     available at any other port
    //
    // for an output port: the data available at the port
    //                    does not originate from any other port
    //
    // Audio synthesizers, I/O hardware interface clients, HDR
    // systems are examples of clients that would set this flag for
    // their ports.
    kPortIsTerminal = 0x10,

};

// Transport states.
typedef enum {

    // the order matters for binary compatibility
    kTransportStopped = 0, // < Transport halted
    kTransportRolling = 1, // < Transport playing
    kTransportLooping = 2, // < For OLD_TRANSPORT, now ignored
    kTransportStarting = 3, // < Waiting for sync ready
    kTransportNetStarting = 4, // < Waiting for sync ready on the network

} TransportStateC;

typedef std::uint64_t UniqueC; // < Unique ID (opaque)

// Optional struct jack_position_t fields.
typedef enum {

    kPositionBBT = 0x10, // < Bar, Beat, Tick
    kPositionTimecode = 0x20, // < External timecode
    kBBTFrameOffset = 0x40, // < Frame offset of BBT information
    kAudioVideoRatio = 0x80, // < audio frames per video frame
    kVideoFrameOffset = 0x100, // < frame offset of first video frame
    kTickDouble = 0x200, // < double-resolution tick

} PositionBitsC;

// all valid position bits
inline constexpr std::int32_t kPositionMask = (kPositionBBT | kPositionTimecode);
struct JackPositionC {

    // these four cannot be set from clients: the server sets them
    UniqueC m_unique1; // < unique ID
    TimeC m_usecs; // < monotonic, free-rolling
    NframesC m_frameRate; // < current frame rate (per second)
    NframesC m_frame; // < frame number, always present

    PositionBitsC m_valid; // < which other fields are valid

    // JackPositionBBT fields:
    std::int32_t m_bar; // < current bar
    std::int32_t m_beat; // < current beat-within-bar
    std::int32_t m_tick; // < current tick-within-beat
    double m_barStartTick;

    float m_beatsPerBar; // < time signature "numerator"
    float m_beatType; // < time signature "denominator"
    double m_ticksPerBeat;
    double m_beatsPerMinute;

    // JackPositionTimecode fields:     (EXPERIMENTAL: could change)
    double m_frameTime; // < current time in seconds
    double m_nextTime; // < next sequential frame_time
    // (unless repositioned)

    // JackBBTFrameOffset fields:
    NframesC m_bbtOffset; // < frame offset for the BBT fields
    // (the given bar, beat, and tick
    // values actually refer to a time
    // frame_offset frames before the
    // start of the cycle), should
    // be assumed to be 0 if
    // JackBBTFrameOffset is not
    // set. If JackBBTFrameOffset is
    // set and this value is zero, the BBT
    // time refers to the first frame of this
    // cycle. If the value is positive,
    // the BBT time refers to a frame that
    // many frames before the start of the
    // cycle.

    // JACK video positional data (experimental)

    float m_audioFramesPerVideoFrame; // < number of audio frames
    // per video frame. Should be assumed
    // zero if JackAudioVideoRatio is not
    // set. If JackAudioVideoRatio is set
    // and the value is zero, no video
    // data exists within the JACK graph

    NframesC m_videoOffset; // < audio frame at which the first video
    // frame in this cycle occurs. Should
    // be assumed to be 0 if JackVideoFrameOffset
    // is not set. If JackVideoFrameOffset is
    // set, but the value is zero, there is
    // no video frame within this cycle.

    // JACK extra transport fields

    double m_tickDouble; // < current tick-within-beat in double resolution.
    // Should be assumed zero if JackTickDouble is not set.
    // Since older versions of JACK do not expose this variable,
    // the macro JACK_TICK_DOUBLE is provided,
    // which can be used as build-time detection.

    std::array<std::int32_t, 5> m_padding;

    // When (unique_1 == unique_2) the contents are consistent.
    UniqueC m_unique2; // < unique ID
};

typedef struct JackPositionC PositionC;

typedef std::uint16_t UInt16;
typedef std::uint32_t UInt32;
typedef std::int32_t SInt32;

typedef std::uint64_t UInt64;
typedef pthread_key_t TlsKey;

typedef std::int32_t (*ThreadCreator)(pthread_t*, const pthread_attr_t*, void* (*function)(void*), void* arg);

typedef std::uint16_t Int; // Internal type for ports and refnum

typedef enum {
    kTimerSystemClock,
    kTimerHpet,
} TimerType;

typedef enum {
    kNotTriggered,
    kTriggered,
    kRunning,
    kFinished,
} ClientState;
