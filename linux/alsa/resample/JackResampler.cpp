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

#include "JackResampler.h"
#include "JackResamplerPlugin.h"
#include "pipewire_resample_bridge.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>

#ifndef RESAMPLE_DEFAULT_QUALITY
#define RESAMPLE_DEFAULT_QUALITY 4
#endif

namespace Jack
{

class JackPipeWireResampler : public JackResampler
{

    private:

        jack_pw_resampler* fHandle;

    public:

        explicit JackPipeWireResampler(jack_pw_resampler* handle) : fHandle(handle)
        {}

        virtual ~JackPipeWireResampler()
        {
            jack_pw_resampler_free(fHandle);
        }

        void Reset()
        {
            jack_pw_resampler_reset(fHandle);
        }

        void SetRate(double rate)
        {
            jack_pw_resampler_set_rate(fHandle, rate);
        }

        unsigned int InputLength(unsigned int out_len)
        {
            return jack_pw_resampler_in_len(fHandle, out_len);
        }

        unsigned int OutputLength(unsigned int in_len)
        {
            return jack_pw_resampler_out_len(fHandle, in_len);
        }

        void Process(const float* const* src, unsigned int* in_len,
                     float* const* dst, unsigned int* out_len)
        {
            jack_pw_resampler_process(fHandle, src, in_len, dst, out_len);
        }

        unsigned int Delay()
        {
            return jack_pw_resampler_delay(fHandle);
        }
};

class JackPipeWireResamplerFactory : public JackResamplerFactory
{

    public:

        const char* Name() const
        {
            return "pipewire";
        }

        JackResampler* Create(const JackResamplerParams& params)
        {
            int quality = (params.quality < 0) ? RESAMPLE_DEFAULT_QUALITY : params.quality;
            jack_pw_resampler* handle = jack_pw_resampler_new(params.channels, params.in_rate,
                                                              params.out_rate, quality);
            if (handle == NULL) {
                return NULL;
            }
            return new JackPipeWireResampler(handle);
        }
};

// A resampler instance that forwards to a plugin's function table.
class JackPluginResampler : public JackResampler
{

    private:

        const jack_resampler_plugin_t* fPlugin;
        void* fInstance;

    public:

        JackPluginResampler(const jack_resampler_plugin_t* plugin, void* instance)
            : fPlugin(plugin), fInstance(instance)
        {}

        virtual ~JackPluginResampler()
        {
            fPlugin->destroy(fInstance);
        }

        void Reset()
        {
            fPlugin->reset(fInstance);
        }

        void SetRate(double rate)
        {
            fPlugin->set_rate(fInstance, rate);
        }

        unsigned int InputLength(unsigned int out_len)
        {
            return fPlugin->in_len(fInstance, out_len);
        }

        unsigned int OutputLength(unsigned int in_len)
        {
            return fPlugin->out_len(fInstance, in_len);
        }

        void Process(const float* const* src, unsigned int* in_len,
                     float* const* dst, unsigned int* out_len)
        {
            fPlugin->process(fInstance, src, in_len, dst, out_len);
        }

        unsigned int Delay()
        {
            return fPlugin->delay(fInstance);
        }
};

// A factory backed by a loaded plugin.  The shared object stays loaded for the
// life of the process; plugins are not unloaded while resamplers may use them.
class JackPluginResamplerFactory : public JackResamplerFactory
{

    private:

        std::string fName;
        const jack_resampler_plugin_t* fPlugin;

    public:

        JackPluginResamplerFactory(const std::string& name, const jack_resampler_plugin_t* plugin)
            : fName(name), fPlugin(plugin)
        {}

        const char* Name() const
        {
            return fName.c_str();
        }

        JackResampler* Create(const JackResamplerParams& params)
        {
            int quality = (params.quality < 0) ? RESAMPLE_DEFAULT_QUALITY : params.quality;
            void* instance = fPlugin->create(params.channels, params.in_rate, params.out_rate, quality);
            if (instance == NULL) {
                return NULL;
            }
            return new JackPluginResampler(fPlugin, instance);
        }
};

// Loads <dir>/<name>.so from JACK_RESAMPLER_PATH.  Failures are not cached, so a
// fixed plugin can be picked up by a later Find().
static JackResamplerFactory* LoadPlugin(const char* name)
{
    static std::map<std::string, JackResamplerFactory*> loaded;
    std::map<std::string, JackResamplerFactory*>::iterator it = loaded.find(name);
    if (it != loaded.end()) {
        return it->second;
    }

    const char* path = getenv("JACK_RESAMPLER_PATH");
    if (path == NULL || *path == '\0') {
        return NULL;
    }

    std::string dirs(path);
    size_t start = 0;
    while (start <= dirs.size()) {
        size_t end = dirs.find(':', start);
        if (end == std::string::npos) {
            end = dirs.size();
        }
        std::string dir = dirs.substr(start, end - start);
        start = end + 1;
        if (dir.empty()) {
            continue;
        }

        std::string file = dir + "/" + name + ".so";
        void* handle = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == NULL) {
            continue;
        }

        jack_resampler_plugin_entry_t entry =
            (jack_resampler_plugin_entry_t)dlsym(handle, JACK_RESAMPLER_PLUGIN_ENTRY);
        const jack_resampler_plugin_t* plugin = entry ? entry() : NULL;
        if (plugin == NULL ||
            plugin->abi_version != JACK_RESAMPLER_PLUGIN_ABI_VERSION ||
            plugin->struct_size != sizeof(jack_resampler_plugin_t) ||
            plugin->name == NULL || strcmp(plugin->name, name) != 0 ||
            plugin->create == NULL || plugin->destroy == NULL || plugin->reset == NULL ||
            plugin->set_rate == NULL || plugin->in_len == NULL || plugin->out_len == NULL ||
            plugin->process == NULL || plugin->delay == NULL) {
            dlclose(handle);
            return NULL;
        }

        JackResamplerFactory* factory = new JackPluginResamplerFactory(name, plugin);
        loaded[name] = factory;
        return factory;
    }
    return NULL;
}

JackResamplerFactory* JackResamplerFactory::Find(const char* name)
{
    static JackPipeWireResamplerFactory pipewire;
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(name, pipewire.Name()) == 0) {
        return &pipewire;
    }
    return LoadPlugin(name);
}

JackResamplerFactory* JackResamplerFactory::Default()
{
    return Find("pipewire");
}

} // end of namespace
