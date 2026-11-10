// Copyright (C) 2026 JACK contributors
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

#include "JackAlsaSecondaryConfig.h"
#include "JackCString.h"
#include <cstdint>
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <fstream>
#include <ranges>
#include <string_view>

namespace jackmini {

AlsaSecondaryConfig::AlsaSecondaryConfig()
    : m_mode(kSecondaryModeDuplex)
    , m_captureChannels(2)
    , m_playbackChannels(2)
    , m_rate(0)
    , m_nperiods(0)
    , m_optional(false)
    , m_followHwRate(false)
    , m_clock(kSecondaryClockAsync)
    , m_sampleFormat(kSampleFormatAuto)
    , m_resampler("pipewire")
    , m_automatic(false)
{
}

static std::string_view trim(std::string_view text)
{
    constexpr std::string_view kWhitespace = " \t\r\n";
    const auto begin = text.find_first_not_of(kWhitespace);
    if (begin == std::string_view::npos) {
        return { };
    }
    return text.substr(begin, text.find_last_not_of(kWhitespace) - begin + 1);
}

static std::string_view unquote(std::string_view text)
{
    if (text.size() >= 2) {
        const char first = text.front();
        const char last = text.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return text.substr(1, text.size() - 2);
        }
    }
    return text;
}

static std::string lowercase(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return result;
}

static bool parseBool(std::string_view value, bool* result)
{
    constexpr std::array kTruthy = { "1", "true", "yes", "on" };
    constexpr std::array kFalsy = { "0", "false", "no", "off" };
    const std::string lower = lowercase(value);
    if (std::ranges::contains(kTruthy, lower)) {
        *result = true;
        return true;
    }
    if (std::ranges::contains(kFalsy, lower)) {
        *result = false;
        return true;
    }
    return false;
}

static bool parseInt(std::string_view value, std::int32_t* result)
{
    // Accepts what strtol() accepted (leading white space and '+'), but stays strict after the digits.
    value = skipNumericPrefix(value);
    if (value.empty()) {
        return false;
    }
    std::int32_t parsed = 0;
    const char* end = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(value.data(), end, parsed);
    if (ec != std::errc() || ptr != end) {
        return false;
    }
    *result = parsed;
    return true;
}

std::int32_t alsaPcmDeviceNumber(std::string_view pcm)
{
    if (const auto dev = pcm.find("DEV="); dev != std::string_view::npos) {
        return parseLeadingInteger<std::int32_t>(pcm.substr(dev + 4));
    }
    if (const auto comma = pcm.rfind(','); comma != std::string_view::npos) {
        return parseLeadingInteger<std::int32_t>(pcm.substr(comma + 1));
    }
    return 0;
}

bool alsaPcmNameIsById(std::string_view pcm)
{
    const auto colon = pcm.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    const auto type = pcm.substr(0, colon);
    if (type != "hw" && type != "plughw") {
        return false;
    }
    auto rest = pcm.substr(colon + 1);
    if (rest.starts_with("CARD=")) {
        rest.remove_prefix(5);
    }
    const auto card = rest.substr(0, rest.find(','));
    if (card.empty()) {
        return false;
    }
    return card.find_first_not_of("0123456789") != std::string_view::npos;
}

bool alsaCardExcluded(std::string_view cardId, const std::vector<std::string>& excluded)
{
    return std::ranges::any_of(excluded, [cardId](const std::string& id) {
        return std::ranges::equal(id, cardId, [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
    });
}

std::string alsaSecondarySanitizeName(std::string_view name)
{
    std::string result;
    for (const char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c == '_') {
            result += ch;
        } else if (!result.empty() && result.back() != '_') {
            result += '_';
        }
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result;
}

static std::string defaultNameForDevice(std::string_view device)
{
    // Prefer a compact name derived from the first number in the device
    // string, so "hw:1,0" becomes "hw1" rather than "hw_1_0".
    constexpr std::string_view kDigits = "0123456789";
    const auto start = device.find_first_of(kDigits);
    if (start != std::string_view::npos) {
        auto number = device.substr(start);
        number = number.substr(0, number.find_first_not_of(kDigits));
        return std::format("hw{}", number);
    }
    std::string sanitized = alsaSecondarySanitizeName(device);
    return sanitized.empty() ? "card" : sanitized;
}

static bool parseClock(std::string_view value, AlsaSecondaryClock* clock)
{
    if (value == "async") {
        *clock = kSecondaryClockAsync;
    } else if (value == "sync") {
        *clock = kSecondaryClockSync;
    } else {
        return false;
    }
    return true;
}

static bool parseFormat(std::string_view value, AlsaSampleFormat* format)
{
    if (value == "auto") {
        *format = kSampleFormatAuto;
    } else if (value == "float") {
        *format = kSampleFormatFloat;
    } else if (value == "s32") {
        *format = kSampleFormatS32;
    } else if (value == "s16") {
        *format = kSampleFormatS16;
    } else {
        return false;
    }
    return true;
}

static bool parseMode(std::string_view value, AlsaSecondaryMode* mode)
{
    if (value == "duplex" || value == "both") {
        *mode = kSecondaryModeDuplex;
    } else if (value == "capture" || value == "capture-only" || value == "in") {
        *mode = kSecondaryModeCapture;
    } else if (value == "playback" || value == "playback-only" || value == "out") {
        *mode = kSecondaryModePlayback;
    } else {
        return false;
    }
    return true;
}

static bool applyField(AlsaSecondaryConfig* config,
    std::string_view key,
    std::string_view value,
    std::string* error)
{
    if (key == "name") {
        config->m_name = value;
    } else if (key == "device") {
        config->m_captureDevice = value;
        config->m_playbackDevice = value;
    } else if (key == "capture_device") {
        config->m_captureDevice = value;
    } else if (key == "playback_device") {
        config->m_playbackDevice = value;
    } else if (key == "mode") {
        if (!parseMode(value, &config->m_mode)) {
            *error = std::format("invalid mode '{}'", value);
            return false;
        }
    } else if (key == "channels") {
        if (!parseInt(value, &config->m_captureChannels) || config->m_captureChannels <= 0) {
            *error = std::format("invalid channels '{}'", value);
            return false;
        }
        config->m_playbackChannels = config->m_captureChannels;
    } else if (key == "capture_channels") {
        if (!parseInt(value, &config->m_captureChannels) || config->m_captureChannels <= 0) {
            *error = std::format("invalid capture_channels '{}'", value);
            return false;
        }
    } else if (key == "playback_channels") {
        if (!parseInt(value, &config->m_playbackChannels) || config->m_playbackChannels <= 0) {
            *error = std::format("invalid playback_channels '{}'", value);
            return false;
        }
    } else if (key == "rate") {
        if (!parseInt(value, &config->m_rate) || config->m_rate <= 0) {
            *error = std::format("invalid rate '{}'", value);
            return false;
        }
    } else if (key == "nperiods") {
        if (!parseInt(value, &config->m_nperiods) || config->m_nperiods < 2) {
            *error = std::format("invalid nperiods '{}'", value);
            return false;
        }
    } else if (key == "optional") {
        if (!parseBool(value, &config->m_optional)) {
            *error = std::format("invalid optional value '{}'", value);
            return false;
        }
    } else if (key == "follow_hw_rate") {
        if (!parseBool(value, &config->m_followHwRate)) {
            *error = std::format("invalid follow_hw_rate value '{}'", value);
            return false;
        }
    } else if (key == "resampler") {
        if (value.empty()) {
            *error = "resampler name is empty";
            return false;
        }
        config->m_resampler = value;
    } else if (key == "format") {
        if (!parseFormat(value, &config->m_sampleFormat)) {
            *error = std::format("invalid format '{}' (use auto, float, s32 or s16)", value);
            return false;
        }
    } else if (key == "clock") {
        if (!parseClock(value, &config->m_clock)) {
            *error = std::format("invalid clock '{}' (use async or sync)", value);
            return false;
        }
    } else if (key == "capture_rate_ctl") {
        config->m_captureRateCtl = value;
    } else if (key == "playback_rate_ctl") {
        config->m_playbackRateCtl = value;
    } else if (key == "rate_map") {
        config->m_rateMap = value;
    } else {
        *error = std::format("unknown key '{}'", key);
        return false;
    }
    return true;
}

static bool validate(AlsaSecondaryConfig* config, std::string* error)
{
    if (config->m_captureDevice.empty() && config->m_playbackDevice.empty()) {
        *error = "no ALSA device specified";
        return false;
    }

    if (config->m_mode == kSecondaryModeDuplex) {
        if (config->m_captureDevice.empty()) {
            config->m_captureDevice = config->m_playbackDevice;
        }
        if (config->m_playbackDevice.empty()) {
            config->m_playbackDevice = config->m_captureDevice;
        }
    } else if (config->m_mode == kSecondaryModeCapture) {
        if (config->m_captureDevice.empty()) {
            config->m_captureDevice = config->m_playbackDevice;
        }
        if (config->m_captureDevice.empty()) {
            *error = "capture mode requires a capture device";
            return false;
        }
    } else if (config->m_mode == kSecondaryModePlayback) {
        if (config->m_playbackDevice.empty()) {
            config->m_playbackDevice = config->m_captureDevice;
        }
        if (config->m_playbackDevice.empty()) {
            *error = "playback mode requires a playback device";
            return false;
        }
    }

    if (config->m_rate != 0 && (config->m_rate < 8000 || config->m_rate > 768000)) {
        *error = std::format("rate {} is outside the supported range", config->m_rate);
        return false;
    }

    if (config->m_name.empty()) {
        const std::string& device = config->m_captureDevice.empty()
            ? config->m_playbackDevice
            : config->m_captureDevice;
        config->m_name = defaultNameForDevice(device);
    }
    config->m_name = alsaSecondarySanitizeName(config->m_name);
    if (config->m_name.empty()) {
        *error = "card name is empty after sanitizing";
        return false;
    }

    return true;
}

bool alsaSecondaryConfigParse(std::string_view spec,
    AlsaSecondaryConfig* config,
    std::string* error)
{
    AlsaSecondaryConfig parsed;
    const std::string_view text = trim(spec);

    if (text.empty()) {
        *error = "empty secondary card specification";
        return false;
    }

    // Bare device name shorthand.
    if (text.find('=') == std::string_view::npos) {
        parsed.m_captureDevice = text;
        parsed.m_playbackDevice = text;
    } else {
        for (const auto segment : text | std::views::split(';')) {
            const std::string_view field = trim(std::string_view(std::ranges::data(segment),
                std::ranges::size(segment)));
            if (field.empty()) {
                continue;
            }
            const auto eq = field.find('=');
            if (eq == std::string_view::npos) {
                *error = std::format("expected key=value in '{}'", field);
                return false;
            }
            const std::string key = lowercase(trim(field.substr(0, eq)));
            const std::string_view value = unquote(trim(field.substr(eq + 1)));
            if (key.empty()) {
                *error = std::format("empty key in '{}'", field);
                return false;
            }
            if (!applyField(&parsed, key, value, error)) {
                return false;
            }
        }
    }

    if (!validate(&parsed, error)) {
        return false;
    }

    *config = parsed;
    return true;
}

bool alsaSecondaryConfigLoadFile(const std::string& path,
    std::vector<AlsaSecondaryConfig>* configs,
    std::string* error)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        *error = std::format("cannot open secondary config file '{}'", path);
        return false;
    }

    std::string line;
    std::int32_t lineNumber = 0;
    while (std::getline(file, line)) {
        lineNumber++;
        const std::string_view content(line);
        const std::string_view text = trim(content.substr(0, content.find('#')));
        if (text.empty()) {
            continue;
        }

        AlsaSecondaryConfig config;
        std::string parseError;
        if (!alsaSecondaryConfigParse(text, &config, &parseError)) {
            *error = std::format("{}:{}: {}", path, lineNumber, parseError);
            return false;
        }
        configs->push_back(std::move(config));
    }

    return true;
}

} // namespace jackmini
