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

#include "JackAlsaSecondaryConfig.h"
#include "JackCString.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <fstream>
#include <ranges>
#include <string_view>

namespace jackmini {

JackAlsaSecondaryConfig::JackAlsaSecondaryConfig()
    : mode(SecondaryModeDuplex)
    , capture_channels(2)
    , playback_channels(2)
    , rate(0)
    , nperiods(0)
    , optional(false)
    , follow_hw_rate(false)
    , clock(SecondaryClockAsync)
    , sample_format(SampleFormatAuto)
    , resampler("pipewire")
    , automatic(false)
{
}

static std::string_view Trim(std::string_view text)
{
    constexpr std::string_view whitespace = " \t\r\n";
    const auto begin = text.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) {
        return { };
    }
    return text.substr(begin, text.find_last_not_of(whitespace) - begin + 1);
}

static std::string_view Unquote(std::string_view text)
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

static std::string Lowercase(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return result;
}

static bool ParseBool(std::string_view value, bool* result)
{
    constexpr std::array truthy = { "1", "true", "yes", "on" };
    constexpr std::array falsy = { "0", "false", "no", "off" };
    const std::string lower = Lowercase(value);
    if (std::ranges::contains(truthy, lower)) {
        *result = true;
        return true;
    }
    if (std::ranges::contains(falsy, lower)) {
        *result = false;
        return true;
    }
    return false;
}

static bool ParseInt(std::string_view value, int* result)
{
    // Accepts what strtol() accepted (leading white space and '+'), but stays strict after the digits.
    value = SkipNumericPrefix(value);
    if (value.empty()) {
        return false;
    }
    int parsed = 0;
    const char* end = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(value.data(), end, parsed);
    if (ec != std::errc() || ptr != end) {
        return false;
    }
    *result = parsed;
    return true;
}

int JackAlsaPcmDeviceNumber(std::string_view pcm)
{
    if (const auto dev = pcm.find("DEV="); dev != std::string_view::npos) {
        return ParseLeadingInteger<int>(pcm.substr(dev + 4));
    }
    if (const auto comma = pcm.rfind(','); comma != std::string_view::npos) {
        return ParseLeadingInteger<int>(pcm.substr(comma + 1));
    }
    return 0;
}

bool JackAlsaPcmNameIsById(std::string_view pcm)
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

bool JackAlsaCardExcluded(std::string_view card_id, const std::vector<std::string>& excluded)
{
    return std::ranges::any_of(excluded, [card_id](const std::string& id) {
        return std::ranges::equal(id, card_id, [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
    });
}

std::string JackAlsaSecondarySanitizeName(std::string_view name)
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

static std::string DefaultNameForDevice(std::string_view device)
{
    // Prefer a compact name derived from the first number in the device
    // string, so "hw:1,0" becomes "hw1" rather than "hw_1_0".
    constexpr std::string_view digits = "0123456789";
    const auto start = device.find_first_of(digits);
    if (start != std::string_view::npos) {
        auto number = device.substr(start);
        number = number.substr(0, number.find_first_not_of(digits));
        return std::format("hw{}", number);
    }
    std::string sanitized = JackAlsaSecondarySanitizeName(device);
    return sanitized.empty() ? "card" : sanitized;
}

static bool ParseClock(std::string_view value, JackAlsaSecondaryClock* clock)
{
    if (value == "async") {
        *clock = SecondaryClockAsync;
    } else if (value == "sync") {
        *clock = SecondaryClockSync;
    } else {
        return false;
    }
    return true;
}

static bool ParseFormat(std::string_view value, JackAlsaSampleFormat* format)
{
    if (value == "auto") {
        *format = SampleFormatAuto;
    } else if (value == "float") {
        *format = SampleFormatFloat;
    } else if (value == "s32") {
        *format = SampleFormatS32;
    } else if (value == "s16") {
        *format = SampleFormatS16;
    } else {
        return false;
    }
    return true;
}

static bool ParseMode(std::string_view value, JackAlsaSecondaryMode* mode)
{
    if (value == "duplex" || value == "both") {
        *mode = SecondaryModeDuplex;
    } else if (value == "capture" || value == "capture-only" || value == "in") {
        *mode = SecondaryModeCapture;
    } else if (value == "playback" || value == "playback-only" || value == "out") {
        *mode = SecondaryModePlayback;
    } else {
        return false;
    }
    return true;
}

static bool ApplyField(JackAlsaSecondaryConfig* config,
    std::string_view key,
    std::string_view value,
    std::string* error)
{
    if (key == "name") {
        config->name = value;
    } else if (key == "device") {
        config->capture_device = value;
        config->playback_device = value;
    } else if (key == "capture_device") {
        config->capture_device = value;
    } else if (key == "playback_device") {
        config->playback_device = value;
    } else if (key == "mode") {
        if (!ParseMode(value, &config->mode)) {
            *error = std::format("invalid mode '{}'", value);
            return false;
        }
    } else if (key == "channels") {
        if (!ParseInt(value, &config->capture_channels) || config->capture_channels <= 0) {
            *error = std::format("invalid channels '{}'", value);
            return false;
        }
        config->playback_channels = config->capture_channels;
    } else if (key == "capture_channels") {
        if (!ParseInt(value, &config->capture_channels) || config->capture_channels <= 0) {
            *error = std::format("invalid capture_channels '{}'", value);
            return false;
        }
    } else if (key == "playback_channels") {
        if (!ParseInt(value, &config->playback_channels) || config->playback_channels <= 0) {
            *error = std::format("invalid playback_channels '{}'", value);
            return false;
        }
    } else if (key == "rate") {
        if (!ParseInt(value, &config->rate) || config->rate <= 0) {
            *error = std::format("invalid rate '{}'", value);
            return false;
        }
    } else if (key == "nperiods") {
        if (!ParseInt(value, &config->nperiods) || config->nperiods < 2) {
            *error = std::format("invalid nperiods '{}'", value);
            return false;
        }
    } else if (key == "optional") {
        if (!ParseBool(value, &config->optional)) {
            *error = std::format("invalid optional value '{}'", value);
            return false;
        }
    } else if (key == "follow_hw_rate") {
        if (!ParseBool(value, &config->follow_hw_rate)) {
            *error = std::format("invalid follow_hw_rate value '{}'", value);
            return false;
        }
    } else if (key == "resampler") {
        if (value.empty()) {
            *error = "resampler name is empty";
            return false;
        }
        config->resampler = value;
    } else if (key == "format") {
        if (!ParseFormat(value, &config->sample_format)) {
            *error = std::format("invalid format '{}' (use auto, float, s32 or s16)", value);
            return false;
        }
    } else if (key == "clock") {
        if (!ParseClock(value, &config->clock)) {
            *error = std::format("invalid clock '{}' (use async or sync)", value);
            return false;
        }
    } else if (key == "capture_rate_ctl") {
        config->capture_rate_ctl = value;
    } else if (key == "playback_rate_ctl") {
        config->playback_rate_ctl = value;
    } else if (key == "rate_map") {
        config->rate_map = value;
    } else {
        *error = std::format("unknown key '{}'", key);
        return false;
    }
    return true;
}

static bool Validate(JackAlsaSecondaryConfig* config, std::string* error)
{
    if (config->capture_device.empty() && config->playback_device.empty()) {
        *error = "no ALSA device specified";
        return false;
    }

    if (config->mode == SecondaryModeDuplex) {
        if (config->capture_device.empty()) {
            config->capture_device = config->playback_device;
        }
        if (config->playback_device.empty()) {
            config->playback_device = config->capture_device;
        }
    } else if (config->mode == SecondaryModeCapture) {
        if (config->capture_device.empty()) {
            config->capture_device = config->playback_device;
        }
        if (config->capture_device.empty()) {
            *error = "capture mode requires a capture device";
            return false;
        }
    } else if (config->mode == SecondaryModePlayback) {
        if (config->playback_device.empty()) {
            config->playback_device = config->capture_device;
        }
        if (config->playback_device.empty()) {
            *error = "playback mode requires a playback device";
            return false;
        }
    }

    if (config->rate != 0 && (config->rate < 8000 || config->rate > 768000)) {
        *error = std::format("rate {} is outside the supported range", config->rate);
        return false;
    }

    if (config->name.empty()) {
        const std::string& device = config->capture_device.empty()
            ? config->playback_device
            : config->capture_device;
        config->name = DefaultNameForDevice(device);
    }
    config->name = JackAlsaSecondarySanitizeName(config->name);
    if (config->name.empty()) {
        *error = "card name is empty after sanitizing";
        return false;
    }

    return true;
}

bool JackAlsaSecondaryConfigParse(std::string_view spec,
    JackAlsaSecondaryConfig* config,
    std::string* error)
{
    JackAlsaSecondaryConfig parsed;
    const std::string_view text = Trim(spec);

    if (text.empty()) {
        *error = "empty secondary card specification";
        return false;
    }

    // Bare device name shorthand.
    if (text.find('=') == std::string_view::npos) {
        parsed.capture_device = text;
        parsed.playback_device = text;
    } else {
        for (const auto segment : text | std::views::split(';')) {
            const std::string_view field = Trim(std::string_view(std::ranges::data(segment),
                std::ranges::size(segment)));
            if (field.empty()) {
                continue;
            }
            const auto eq = field.find('=');
            if (eq == std::string_view::npos) {
                *error = std::format("expected key=value in '{}'", field);
                return false;
            }
            const std::string key = Lowercase(Trim(field.substr(0, eq)));
            const std::string_view value = Unquote(Trim(field.substr(eq + 1)));
            if (key.empty()) {
                *error = std::format("empty key in '{}'", field);
                return false;
            }
            if (!ApplyField(&parsed, key, value, error)) {
                return false;
            }
        }
    }

    if (!Validate(&parsed, error)) {
        return false;
    }

    *config = parsed;
    return true;
}

bool JackAlsaSecondaryConfigLoadFile(const std::string& path,
    std::vector<JackAlsaSecondaryConfig>* configs,
    std::string* error)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        *error = std::format("cannot open secondary config file '{}'", path);
        return false;
    }

    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        line_number++;
        const std::string_view content(line);
        const std::string_view text = Trim(content.substr(0, content.find('#')));
        if (text.empty()) {
            continue;
        }

        JackAlsaSecondaryConfig config;
        std::string parse_error;
        if (!JackAlsaSecondaryConfigParse(text, &config, &parse_error)) {
            *error = std::format("{}:{}: {}", path, line_number, parse_error);
            return false;
        }
        configs->push_back(std::move(config));
    }

    return true;
}

} // namespace jackmini
