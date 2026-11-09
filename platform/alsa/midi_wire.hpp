/*
Copyright (C) 2026 JACK contributors

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

#ifndef JACKMINI_ALSA_MIDI_WIRE_HPP
#define JACKMINI_ALSA_MIDI_WIRE_HPP

#include <cstddef>
#include <cstdint>
#include <span>

namespace jackmini::alsa::midi
{

// The bytes of one message, as they are sent to or received from a device.
using Bytes = std::span<const std::uint8_t>;

// Running status on output: a voice message whose status byte repeats the last voice status is sent without it.
class Packer
{
public:
    void reset() { running_status_ = 0; }

    // The bytes to send for one event.  The event must not be empty.
    Bytes pack(Bytes event)
    {
        const std::uint8_t status = event[0];
        if (status >= 0x80 && status < 0xF0) {
            if (status == running_status_) {
                return event.subspan(1);
            }
            running_status_ = status;
        } else if (status < 0xF8) {
            // Data bytes and system common messages end the running status; system realtime does not.
            running_status_ = 0;
        }
        return event;
    }

private:
    std::uint8_t running_status_ = 0;
};

// Parses the bytes of a device into complete messages.  A message in progress survives a read, and a system realtime
// byte is passed on at once, between the bytes of a message.  A system exclusive message longer than the buffer is
// dropped.
class Unpacker
{
public:
    static constexpr std::size_t kMaxMessage = 1024;

    Unpacker() { reset(); }

    void reset()
    {
        pos_ = 0;
        need_ = kMaxMessage;
    }

    // Calls emit(bytes) for each complete message, in order.  Returns the number of bytes consumed, which is all of them.
    template <class Emit>
    std::size_t feed(Bytes data, Emit&& emit)
    {
        for (std::size_t i = 0; i < data.size(); ++i) {
            const std::uint8_t byte = data[i];
            if (byte >= 0xF8) {
                emit(data.subspan(i, 1));
                continue;
            }
            if (byte < 0x80) {
                append(byte);
            } else if (byte < 0xF0) {
                buf_[0] = byte;
                pos_ = 1;
                need_ = kVoiceLength[(byte - 0x80) >> 4];
            } else if (byte == 0xF7) {
                // An end byte without a start in progress is dropped.
                if (pos_ == 0 || buf_[0] != 0xF0) {
                    continue;
                }
                append(byte);
                need_ = pos_;
            } else {
                buf_[0] = byte;
                pos_ = 1;
                need_ = kSystemLength[byte - 0xF0];
                if (need_ == 0) {
                    need_ = kMaxMessage;
                }
            }
            if (pos_ == need_) {
                complete(emit);
            }
        }
        return data.size();
    }

private:
    static constexpr std::uint8_t kVoiceLength[] = {
        3,  // 0x80 Note Off
        3,  // 0x90 Note On
        3,  // 0xA0 Aftertouch
        3,  // 0xB0 Control Change
        2,  // 0xC0 Program Change
        2,  // 0xD0 Channel Pressure
        3,  // 0xE0 Pitch Wheel
        1,  // 0xF0 System
    };

    static constexpr std::uint8_t kSystemLength[] = {
        0,  // 0xF0 System Exclusive Start: runs to the end byte
        2,  // 0xF1 MTC Quarter Frame
        3,  // 0xF2 Song Position
        2,  // 0xF3 Song Select
        0,  // 0xF4 undefined
        0,  // 0xF5 undefined
        1,  // 0xF6 Tune Request
        1,  // 0xF7 System Exclusive End
    };

    void append(std::uint8_t byte)
    {
        if (pos_ < kMaxMessage) {
            buf_[pos_++] = byte;
        }
    }

    template <class Emit>
    void complete(Emit& emit)
    {
        // A status message is complete; a system exclusive one only when it ends with its end byte.
        if (buf_[0] >= 0x80 && (buf_[0] != 0xF0 || buf_[pos_ - 1] == 0xF7)) {
            if ((buf_[0] & 0xF0) == 0x90 && pos_ == 3 && buf_[2] == 0) {
                // A Note On with velocity 0 is a Note Off.
                const std::uint8_t off[3] = {static_cast<std::uint8_t>(0x80 | (buf_[0] & 0x0F)), buf_[1], 0x40};
                emit(Bytes(off, 3));
            } else {
                emit(Bytes(buf_, pos_));
            }
        }
        // A voice status keeps its running status for the next message.
        if (buf_[0] >= 0x80 && buf_[0] < 0xF0) {
            pos_ = 1;
        } else {
            pos_ = 0;
            need_ = kMaxMessage;
        }
    }

    std::uint8_t buf_[kMaxMessage];
    std::size_t pos_ = 0;
    std::size_t need_ = kMaxMessage;
};

} // namespace jackmini::alsa::midi

#endif
