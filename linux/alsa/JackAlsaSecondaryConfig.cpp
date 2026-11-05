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
#include <ctype.h>
#include <errno.h>
#include <fstream>
#include <limits.h>
#include <sstream>
#include <stdlib.h>

namespace Jack
{

JackAlsaSecondaryConfig::JackAlsaSecondaryConfig()
    : mode(SecondaryModeDuplex),
      capture_channels(2),
      playback_channels(2),
      rate(0),
      nperiods(0),
      optional(false),
      follow_hw_rate(false),
      clock(SecondaryClockAsync),
      sample_format(SampleFormatAuto)
{}

static std::string Trim(const std::string& text)
{
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return std::string();
    }
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

static std::string Unquote(const std::string& text)
{
    if (text.size() >= 2) {
        char first = text[0];
        char last = text[text.size() - 1];
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return text.substr(1, text.size() - 2);
        }
    }
    return text;
}

static bool ParseBool(const std::string& value, bool* result)
{
    std::string lower;
    for (size_t i = 0; i < value.size(); i++) {
        lower += (char)tolower((unsigned char)value[i]);
    }
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        *result = true;
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        *result = false;
        return true;
    }
    return false;
}

static bool ParseInt(const std::string& value, int* result)
{
    if (value.empty()) {
        return false;
    }
    errno = 0;
    char* end = NULL;
    long v = strtol(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0' || v < INT_MIN || v > INT_MAX) {
        return false;
    }
    *result = (int)v;
    return true;
}

std::string JackAlsaSecondarySanitizeName(const std::string& name)
{
    std::string result;
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = (unsigned char)name[i];
        if (isalnum(c) || c == '_') {
            result += (char)c;
        } else if (!result.empty() && result[result.size() - 1] != '_') {
            result += '_';
        }
    }
    while (!result.empty() && result[result.size() - 1] == '_') {
        result.erase(result.size() - 1);
    }
    return result;
}

static std::string DefaultNameForDevice(const std::string& device)
{
    // Prefer a compact name derived from the first number in the device
    // string, so "hw:1,0" becomes "hw1" rather than "hw_1_0".
    std::string digits;
    for (size_t i = 0; i < device.size(); i++) {
        if (isdigit((unsigned char)device[i])) {
            digits += device[i];
        } else if (!digits.empty()) {
            break;
        }
    }
    if (!digits.empty()) {
        return "hw" + digits;
    }
    std::string sanitized = JackAlsaSecondarySanitizeName(device);
    return sanitized.empty() ? "card" : sanitized;
}

static bool ParseClock(const std::string& value, JackAlsaSecondaryClock* clock)
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

static bool ParseFormat(const std::string& value, JackAlsaSampleFormat* format)
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

static bool ParseMode(const std::string& value, JackAlsaSecondaryMode* mode)
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
                       const std::string& key,
                       const std::string& value,
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
            *error = "invalid mode '" + value + "'";
            return false;
        }
    } else if (key == "channels") {
        if (!ParseInt(value, &config->capture_channels) || config->capture_channels <= 0) {
            *error = "invalid channels '" + value + "'";
            return false;
        }
        config->playback_channels = config->capture_channels;
    } else if (key == "capture_channels") {
        if (!ParseInt(value, &config->capture_channels) || config->capture_channels <= 0) {
            *error = "invalid capture_channels '" + value + "'";
            return false;
        }
    } else if (key == "playback_channels") {
        if (!ParseInt(value, &config->playback_channels) || config->playback_channels <= 0) {
            *error = "invalid playback_channels '" + value + "'";
            return false;
        }
    } else if (key == "rate") {
        if (!ParseInt(value, &config->rate) || config->rate <= 0) {
            *error = "invalid rate '" + value + "'";
            return false;
        }
    } else if (key == "nperiods") {
        if (!ParseInt(value, &config->nperiods) || config->nperiods < 2) {
            *error = "invalid nperiods '" + value + "'";
            return false;
        }
    } else if (key == "optional") {
        if (!ParseBool(value, &config->optional)) {
            *error = "invalid optional value '" + value + "'";
            return false;
        }
    } else if (key == "follow_hw_rate") {
        if (!ParseBool(value, &config->follow_hw_rate)) {
            *error = "invalid follow_hw_rate value '" + value + "'";
            return false;
        }
    } else if (key == "format") {
        if (!ParseFormat(value, &config->sample_format)) {
            *error = "invalid format '" + value + "' (use auto, float, s32 or s16)";
            return false;
        }
    } else if (key == "clock") {
        if (!ParseClock(value, &config->clock)) {
            *error = "invalid clock '" + value + "' (use async or sync)";
            return false;
        }
    } else if (key == "capture_rate_ctl") {
        config->capture_rate_ctl = value;
    } else if (key == "playback_rate_ctl") {
        config->playback_rate_ctl = value;
    } else if (key == "rate_map") {
        config->rate_map = value;
    } else {
        *error = "unknown key '" + key + "'";
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
        std::ostringstream msg;
        msg << "rate " << config->rate << " is outside the supported range";
        *error = msg.str();
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

bool JackAlsaSecondaryConfigParse(const std::string& spec,
                                  JackAlsaSecondaryConfig* config,
                                  std::string* error)
{
    JackAlsaSecondaryConfig parsed;
    std::string text = Trim(spec);

    if (text.empty()) {
        *error = "empty secondary card specification";
        return false;
    }

    // Bare device name shorthand.
    if (text.find('=') == std::string::npos) {
        parsed.capture_device = text;
        parsed.playback_device = text;
    } else {
        std::string field;
        for (size_t i = 0; i <= text.size(); i++) {
            if (i == text.size() || text[i] == ';') {
                field = Trim(field);
                if (!field.empty()) {
                    size_t eq = field.find('=');
                    if (eq == std::string::npos) {
                        *error = "expected key=value in '" + field + "'";
                        return false;
                    }
                    std::string key = Trim(field.substr(0, eq));
                    std::string value = Unquote(Trim(field.substr(eq + 1)));
                    for (size_t k = 0; k < key.size(); k++) {
                        key[k] = (char)tolower((unsigned char)key[k]);
                    }
                    if (key.empty()) {
                        *error = "empty key in '" + field + "'";
                        return false;
                    }
                    if (!ApplyField(&parsed, key, value, error)) {
                        return false;
                    }
                }
                field.clear();
            } else {
                field += text[i];
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
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        *error = "cannot open secondary config file '" + path + "'";
        return false;
    }

    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        line_number++;
        size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        JackAlsaSecondaryConfig config;
        std::string parse_error;
        if (!JackAlsaSecondaryConfigParse(line, &config, &parse_error)) {
            std::ostringstream msg;
            msg << path << ":" << line_number << ": " << parse_error;
            *error = msg.str();
            return false;
        }
        configs->push_back(config);
    }

    return true;
}

} // end of namespace
