// The internal client functions of the jackmini client: the bodies of the former C client API (JackAPI.cpp,
// JackLibAPI.cpp, JackMidiAPI.cpp), not exported.  Generated from those files; see lib.hpp.

#include "lib.hpp"

#include <array>
#include <inttypes.h>
#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <new>
#include <assert.h>
#include "JackClient.h"
#include "JackError.h"
#include "JackGraphManager.h"
#include "JackEngineControl.h"
#include "JackClientControl.h"
#include "JackGlobals.h"
#include "JackTime.h"
#include "JackCString.h"
#include "JackPortType.h"
#include "JackMetadata.h"
#include "JackMidiPort.h"
#include "JackLibClient.h"
#include "JackChannel.h"
#include "JackLibGlobals.h"
#include "JackTools.h"
#include "JackSystemDeps.h"
#include "JackServerLaunch.h"
#include "JackMidiPort.h"

namespace jackmini::lib
{
using namespace Jack;

static inline bool CheckPort(jack_port_id_t port_index)
{
    return (port_index > 0 && port_index < PORT_NUM_MAX);
}

static inline void WaitGraphChange()
{
    /*
    TLS key that is set only in RT thread, so never waits for pending
    graph change in RT context (just read the current graph state).
    */

    if (jack_tls_get(JackGlobals::fRealTimeThread) == NULL) {
        JackGraphManager* manager = GetGraphManager();
        JackEngineControl* control = GetEngineControl();
        assert(manager);
        assert(control);
        if (manager->IsPendingChange()) {
            jack_log("WaitGraphChange...");
            JackSleep(int(control->fPeriodUsecs * 1.1f));
        }
    }
}

static jack_client_t* jack_client_open_aux(const char* client_name, jack_options_t options, jack_status_t* status, va_list ap)
{
    jack_varargs_t va;		/* variable arguments */
    jack_status_t my_status;
    JackClient* client;

    if (client_name == NULL) {
        jack_error("jack_client_open called with a NULL client_name");
        return NULL;
    }

    jack_log("jack_client_open %s", client_name);

    if (status == NULL)			/* no status from caller? */
        status = &my_status;	/* use local status word */
    *status = (jack_status_t)0;

    /* validate parameters */
    if ((options & ~JackOpenOptions)) {
        int my_status1 = *status | (JackFailure | JackInvalidOption);
        *status = (jack_status_t)my_status1;
        return NULL;
    }

    /* parse variable arguments */
    jack_varargs_parse(options, ap, &va);

    JackLibGlobals::Init(); // jack library initialisation

    if (try_start_server(&va, options, status)) {
        jack_error("jack server is not running or cannot be started");
        JackLibGlobals::Destroy(); // jack library destruction
        return 0;
    }

    client = new JackLibClient(GetSynchroTable());

    int res = client->Open(va.server_name, client_name, va.session_id, options, status);
    if (res < 0) {
        delete client;
        JackLibGlobals::Destroy(); // jack library destruction
        // Keep the reason Open reported (for example JackNameNotUnique); add the failure bits to it.
        int my_status1 = (*status | JackFailure | JackServerError);
        *status = (jack_status_t)my_status1;
        return NULL;
    } else {
        return (jack_client_t*)client;
    }
}

const char* jack_port_name(const jack_port_t* port)
{
    JackGlobals::CheckContext("jack_port_name");

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_name called with an incorrect port %ld", myport);
        return NULL;
    } else {
        JackGraphManager* manager = GetGraphManager();
        return (manager ? manager->GetPort(myport)->GetName() : NULL);
    }
}

const char* jack_port_type(const jack_port_t* port)
{
    JackGlobals::CheckContext("jack_port_type");

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_flags called an incorrect port %ld", myport);
        return NULL;
    } else {
        JackGraphManager* manager = GetGraphManager();
        return (manager ? manager->GetPort(myport)->GetType() : NULL);
    }
}

jack_nframes_t jack_get_buffer_size(jack_client_t* ext_client)
{
    JackGlobals::CheckContext("jack_get_buffer_size");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_get_buffer_size called with a NULL client");
        return 0;
    } else {
        JackEngineControl* control = GetEngineControl();
        return (control ? control->fBufferSize : 0);
    }
}

void* jack_port_get_buffer(jack_port_t* port, jack_nframes_t frames)
{
    JackGlobals::CheckContext("jack_port_get_buffer");

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_get_buffer called with an incorrect port %ld", myport);
        return NULL;
    } else {
        JackGraphManager* manager = GetGraphManager();
        return (manager ? manager->GetBuffer(myport, frames) : NULL);
    }
}

jack_port_t* jack_port_by_name(jack_client_t* ext_client, const char* portname)
{
    JackGlobals::CheckContext("jack_port_by_name");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_port_by_name called with a NULL client");
        return NULL;
    }

    if (portname == NULL) {
        jack_error("jack_port_by_name called with a NULL port name");
        return NULL;
    }
    
    JackGraphManager* manager = GetGraphManager();
    if (manager) {
        int res = manager->GetPort(portname); // returns a port index at least > 1
        return (res == NO_PORT) ? NULL : (jack_port_t*)((uintptr_t)res);
    } else {
        return NULL;
    }
}

char* jack_get_client_name(jack_client_t* ext_client)
{
    JackGlobals::CheckContext("jack_get_client_name");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_get_client_name called with a NULL client");
        return NULL;
    } else {
        return client->GetClientControl()->fName;
    }
}

void jack_free(void* ptr)
{
    JackGlobals::CheckContext("jack_free");

    if (ptr) {
        free(ptr);
    }
}

jack_client_t* jack_client_open(const char* ext_client_name, jack_options_t options, jack_status_t* status, ...)
{
    JackGlobals::CheckContext("jack_client_open");

    try {
        assert(JackGlobals::fOpenMutex);
        JackGlobals::fOpenMutex->Lock();
        va_list ap;
        va_start(ap, status);
        jack_client_t* res = jack_client_open_aux(ext_client_name, options, status, ap);
        va_end(ap);
        JackGlobals::fOpenMutex->Unlock();
        return res;
    } catch (std::bad_alloc& e) {
        jack_error("Memory allocation error...");
        return NULL;
    } catch (...) {
        jack_error("Unknown error...");
        return NULL;
    }
}

int jack_uuid_parse(const char* b, jack_uuid_t* u)
{
    const std::string_view text = SkipNumericPrefix(b);
    uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc()) {
        return -1;
    }

    *u = value;
    if (*u < (0x1LL << 32)) {
        /* has not type bits set - not legal */
        return -1;
    }

    return 0;
}

void jack_transport_stop(jack_client_t* ext_client)
{
    JackGlobals::CheckContext("jack_transport_stop");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_transport_stop called with a NULL client");
    } else {
        client->TransportStop();
    }
}

void jack_transport_start(jack_client_t* ext_client)
{
    JackGlobals::CheckContext("jack_transport_start");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_transport_start called with a NULL client");
    } else {
        client->TransportStart();
    }
}

jack_transport_state_t jack_transport_query(const jack_client_t* ext_client, jack_position_t* pos)
{
    JackGlobals::CheckContext("jack_transport_query");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_transport_query called with a NULL client");
        return JackTransportStopped;
    } else {
        return client->TransportQuery(pos);
    }
}

int jack_transport_locate(jack_client_t* ext_client, jack_nframes_t frame)
{
    JackGlobals::CheckContext("jack_transport_locate");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_transport_locate called with a NULL client");
        return -1;
    } else {
        client->TransportLocate(frame);
        return 0;
    }
}

int jack_set_xrun_callback(jack_client_t* ext_client, JackXRunCallback xrun_callback, void* arg)
{
    JackGlobals::CheckContext("jack_set_xrun_callback");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_set_xrun_callback called with a NULL client");
        return -1;
    } else {
        return client->SetXRunCallback(xrun_callback, arg);
    }
}

int jack_set_sample_rate_callback(jack_client_t* ext_client, JackSampleRateCallback srate_callback, void* arg)
{
    JackGlobals::CheckContext("jack_set_sample_rate_callback");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_set_sample_rate_callback called with a NULL client");
        return -1;
    } else {
        return client->SetSampleRateCallback(srate_callback, arg);
    }
}

int jack_set_process_callback(jack_client_t* ext_client, JackProcessCallback callback, void* arg)
{
    JackGlobals::CheckContext("jack_set_process_callback");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_set_process_callback called with a NULL client");
        return -1;
    } else {
        return client->SetProcessCallback(callback, arg);
    }
}

int jack_set_latency_callback(jack_client_t* ext_client, JackLatencyCallback latency_callback, void *arg)
{
    JackGlobals::CheckContext("jack_set_latency_callback");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_set_latency_callback called with a NULL client");
        return -1;
    } else {
        return client->SetLatencyCallback(latency_callback, arg);
    }
}

int jack_set_buffer_size_callback(jack_client_t* ext_client, JackBufferSizeCallback bufsize_callback, void* arg)
{
    JackGlobals::CheckContext("jack_set_buffer_size_callback");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_set_buffer_size_callback called with a NULL client");
        return -1;
    } else {
        return client->SetBufferSizeCallback(bufsize_callback, arg);
    }
}

int jack_recompute_total_latencies(jack_client_t* ext_client)
{
    JackGlobals::CheckContext("jack_recompute_total_latencies");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_recompute_total_latencies called with a NULL client");
        return -1;
    } else {
        return client->ComputeTotalLatencies();
    }
}

int jack_port_unregister(jack_client_t* ext_client, jack_port_t* port)
{
    JackGlobals::CheckContext("jack_port_unregister");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_port_unregister called with a NULL client");
        return -1;
    }
    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_unregister called with an incorrect port %ld", myport);
        return -1;
    }
    return client->PortUnRegister(myport);
}

void jack_port_set_latency_range(jack_port_t *port, jack_latency_callback_mode_t mode, jack_latency_range_t *range)
{
    JackGlobals::CheckContext("jack_port_set_latency_range");

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_set_latency_range called with an incorrect port %ld", myport);
    } else {
        WaitGraphChange();
        JackGraphManager* manager = GetGraphManager();
        if (manager)
            manager->GetPort(myport)->SetLatencyRange(mode, range);
    }
}

void jack_port_get_latency_range(jack_port_t *port, jack_latency_callback_mode_t mode, jack_latency_range_t *range)
{
    JackGlobals::CheckContext("jack_port_get_latency_range");

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_get_latency_range called with an incorrect port %ld", myport);
    } else {
        WaitGraphChange();
        JackGraphManager* manager = GetGraphManager();
        if (manager)
            manager->GetPort(myport)->GetLatencyRange(mode, range);
    }
}

jack_port_t* jack_port_register(jack_client_t* ext_client, const char* port_name, const char* port_type, unsigned long flags, unsigned long buffer_size)
{
    JackGlobals::CheckContext("jack_port_register");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_port_register called with a NULL client");
        return NULL;
    } else if ((port_name == NULL) || (port_type == NULL)) {
        jack_error("jack_port_register called with a NULL port name or a NULL port_type");
        return NULL;
    } else {
        return (jack_port_t *)((uintptr_t)client->PortRegister(port_name, port_type, flags, buffer_size));
    }
}

int jack_port_flags(const jack_port_t* port)
{
    JackGlobals::CheckContext("jack_port_flags");

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_flags called with an incorrect port %ld", myport);
        return -1;
    } else {
        JackGraphManager* manager = GetGraphManager();
        return (manager ? manager->GetPort(myport)->GetFlags() : -1);
    }
}

int jack_port_connected_to(const jack_port_t* port, const char* port_name)
{
    JackGlobals::CheckContext("jack_port_connected_to");

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t src = (jack_port_id_t)port_aux;
    if (!CheckPort(src)) {
        jack_error("jack_port_connected_to called with an incorrect port %ld", src);
        return -1;
    } else if (port_name == NULL) {
        jack_error("jack_port_connected_to called with a NULL port name");
        return -1;
    } else {
        WaitGraphChange();
        JackGraphManager* manager = GetGraphManager();
        jack_port_id_t dst = (manager ? manager->GetPort(port_name) : NO_PORT);
        if (dst == NO_PORT) {
            jack_error("Unknown destination port port_name = %s", port_name);
            return 0;
        } else {
            return manager->IsConnected(src, dst);
        }
    }
}

void jack_on_shutdown(jack_client_t* ext_client, JackShutdownCallback callback, void* arg)
{
    JackGlobals::CheckContext("jack_on_shutdown");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_on_shutdown called with a NULL client");
    } else {
        client->OnShutdown(callback, arg);
    }
}

int jack_get_cycle_times(const jack_client_t *client,
                                    jack_nframes_t *current_frames,
                                    jack_time_t    *current_usecs,
                                    jack_time_t    *next_usecs,
                                    float          *period_usecs)
{
    JackGlobals::CheckContext("jack_get_cycle_times");

    JackEngineControl* control = GetEngineControl();
    if (control) {
        JackTimer timer;
        control->ReadFrameTime(&timer);
        return timer.GetCycleTimes(current_frames, current_usecs, next_usecs, period_usecs);
    } else {
        return -1;
    }
}

jack_nframes_t jack_get_sample_rate(jack_client_t* ext_client)
{
    JackGlobals::CheckContext("jack_get_sample_rate");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_get_sample_rate called with a NULL client");
        return 0;
    } else {
        JackEngineControl* control = GetEngineControl();
        return (control ? control->fSampleRate : 0);
    }
}

float jack_cpu_load(jack_client_t* ext_client)
{
    JackGlobals::CheckContext("jack_cpu_load");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_cpu_load called with a NULL client");
        return 0.0f;
    } else {
        JackEngineControl* control = GetEngineControl();
        return (control ? control->fCPULoad : 0.0f);
    }
}

int jack_connect(jack_client_t* ext_client, const char* src, const char* dst)
{
    JackGlobals::CheckContext("jack_connect");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_connect called with a NULL client");
        return -1;
    } else if ((src == NULL) || (dst == NULL)) {
        jack_error("jack_connect called with a NULL port name");
        return -1;
    } else {
        return client->PortConnect(src, dst);
    }
}

int jack_disconnect(jack_client_t* ext_client, const char* src, const char* dst)
{
    JackGlobals::CheckContext("jack_disconnect");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_disconnect called with a NULL client");
        return -1;
    } else if ((src == NULL) || (dst == NULL)) {
        jack_error("jack_disconnect called with a NULL port name");
        return -1;
    } else {
        return client->PortDisconnect(src, dst);
    }
}

int jack_activate(jack_client_t* ext_client)
{
    JackGlobals::CheckContext("jack_activate");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_activate called with a NULL client");
        return -1;
    } else {
        return client->Activate();
    }
}

int jack_deactivate(jack_client_t* ext_client)
{
    JackGlobals::CheckContext("jack_deactivate");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_deactivate called with a NULL client");
        return -1;
    } else {
        return client->Deactivate();
    }
}

int jack_client_close(jack_client_t* ext_client)
{
    JackGlobals::CheckContext("jack_client_close");

    assert(JackGlobals::fOpenMutex);
    JackGlobals::fOpenMutex->Lock();
    int res = -1;
    jack_log("jack_client_close");
    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_client_close called with a NULL client");
    } else {
        res = client->Close();
        delete client;
        JackLibGlobals::Destroy(); // jack library destruction
        jack_log("jack_client_close res = %d", res);
    }
    JackGlobals::fOpenMutex->Unlock();
    return res;
}

char* jack_get_uuid_for_client_name(jack_client_t* ext_client, const char* client_name)
{
    JackGlobals::CheckContext("jack_get_uuid_for_client_name");

    JackClient* client = (JackClient*)ext_client;
    jack_log("jack_get_uuid_for_client_name ext_client %x client %x ", ext_client, client);
    if (client == NULL) {
        jack_error("jack_get_uuid_for_client_name called with a NULL client");
        return NULL;
    } else {
        return client->GetUUIDForClientName(client_name);
    }
}

const char** jack_port_get_all_connections(const jack_client_t* ext_client, const jack_port_t* port)
{
    JackGlobals::CheckContext("jack_port_get_all_connections");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_port_get_all_connections called with a NULL client");
        return NULL;
    }

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_get_all_connections called with an incorrect port %ld", myport);
        return NULL;
    } else {
        WaitGraphChange();
        JackGraphManager* manager = GetGraphManager();
        return (manager ? manager->GetConnections(myport) : NULL);
    }
}

const char** jack_get_ports(jack_client_t* ext_client, const char* port_name_pattern, const char* type_name_pattern, unsigned long flags)
{
    JackGlobals::CheckContext("jack_get_ports");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_get_ports called with a NULL client");
        return NULL;
    }
    JackGraphManager* manager = GetGraphManager();
    return (manager ? manager->GetPorts(port_name_pattern, type_name_pattern, flags) : NULL);
}

jack_nframes_t jack_port_get_total_latency(jack_client_t* ext_client, jack_port_t* port)
{
    JackGlobals::CheckContext("jack_port_get_total_latency");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_port_get_total_latency called with a NULL client");
        return 0;
    }

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_get_total_latency called with an incorrect port %ld", myport);
        return 0;
    } else {
        WaitGraphChange();
        JackGraphManager* manager = GetGraphManager();
        if (manager) {
            manager->ComputeTotalLatency(myport);
            return manager->GetPort(myport)->GetTotalLatency();
        } else {
            return 0;
        }
    }
}

jack_nframes_t jack_port_get_latency(jack_port_t* port)
{
    JackGlobals::CheckContext("jack_port_get_latency");

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_get_latency called with an incorrect port %ld", myport);
        return 0;
    } else {
        WaitGraphChange();
        JackGraphManager* manager = GetGraphManager();
        return (manager ? manager->GetPort(myport)->GetLatency() : 0);
    }
}

const char* jack_port_short_name(const jack_port_t* port)
{
    JackGlobals::CheckContext("jack_port_short_name");

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_short_name called with an incorrect port %ld", myport);
        return NULL;
    } else {
        JackGraphManager* manager = GetGraphManager();
        return (manager ? manager->GetPort(myport)->GetShortName() : NULL);
    }
}

int jack_port_is_mine(const jack_client_t* ext_client, const jack_port_t* port)
{
    JackGlobals::CheckContext("jack_port_is_mine");

    JackClient* client = (JackClient*)ext_client;
    if (client == NULL) {
        jack_error("jack_port_is_mine called with a NULL client");
        return -1;
    }
    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_is_mine called with an incorrect port %ld", myport);
        return -1;
    }
    return client->PortIsMine(myport);
}

int jack_port_connected(const jack_port_t* port)
{
    JackGlobals::CheckContext("jack_port_connected");

    uintptr_t port_aux = (uintptr_t)port;
    jack_port_id_t myport = (jack_port_id_t)port_aux;
    if (!CheckPort(myport)) {
        jack_error("jack_port_connected called with an incorrect port %ld", myport);
        return -1;
    } else {
        WaitGraphChange();
        JackGraphManager* manager = GetGraphManager();
        return (manager ? manager->GetConnectionsNum(myport) : -1);
    }
}

uint32_t jack_midi_get_event_count(void* port_buffer)
{
    JackMidiBuffer *buf = (JackMidiBuffer*)port_buffer;
    if (!buf || !buf->IsValid()) {
        return 0;
    }
    return buf->event_count;
}

int jack_midi_event_get(jack_midi_event_t *event, void* port_buffer, uint32_t event_index)
{
    JackMidiBuffer *buf = (JackMidiBuffer*)port_buffer;
    if (!buf || !buf->IsValid()) {
        return -EINVAL;
    }
    if (event_index >= buf->event_count) {
        return -ENOBUFS;
    }
    JackMidiEvent* ev = &buf->events[event_index];
    event->time = ev->time;
    event->size = ev->size;
    event->buffer = ev->GetData(buf);
    return 0;
}

void jack_midi_clear_buffer(void* port_buffer)
{
    JackMidiBuffer *buf = (JackMidiBuffer*)port_buffer;
    if (buf && buf->IsValid()) {
        buf->Reset(buf->nframes);
    }
}

int jack_midi_event_write(void* port_buffer,
                          jack_nframes_t time, const jack_midi_data_t* data, size_t data_size)
{
    JackMidiBuffer *buf = (JackMidiBuffer*)port_buffer;
    if (!buf || !buf->IsValid()) {
        return -EINVAL;
    }
    if (time >= buf->nframes || (buf->event_count && buf->events[buf->event_count - 1].time > time)) {
        return -EINVAL;
    }
    jack_midi_data_t* dest = buf->ReserveEvent(time, data_size);
    if (!dest) {
        return -ENOBUFS;
    }
    memcpy(dest, data, data_size);
    return 0;
}

jack_midi_data_t* jack_midi_event_reserve(void* port_buffer, jack_nframes_t time, size_t data_size)
{
    JackMidiBuffer *buf = (JackMidiBuffer*)port_buffer;
    if (! buf) {
        jack_error("jack_midi_event_reserve: port buffer is set to NULL");
        return 0;
    }
    if (! buf->IsValid()) {
        jack_error("jack_midi_event_reserve: port buffer is invalid");
        return 0;
    }
    if (time >= buf->nframes) {
        jack_error("jack_midi_event_reserve: time parameter is out of range "
                   "(%lu >= %lu)", time, buf->nframes);
        return 0;
    }
    if (buf->event_count && (buf->events[buf->event_count - 1].time > time)) {
        jack_error("jack_midi_event_reserve: time parameter is earlier than "
                   "last reserved event");
        return 0;
    }
    return buf->ReserveEvent(time, data_size);
}

size_t jack_midi_max_event_size(void* port_buffer)
{
    JackMidiBuffer *buf = (JackMidiBuffer*)port_buffer;
    if (buf && buf->IsValid()) {
        return buf->MaxEventSize();
    }
    return 0;
}

void jack_midi_reset_buffer(void* port_buffer)
{
    MidiBufferInit(port_buffer, BUFFER_SIZE_MAX, BUFFER_SIZE_MAX);
}

uint32_t jack_midi_get_lost_event_count(void* port_buffer)
{
    JackMidiBuffer *buf = (JackMidiBuffer*)port_buffer;
    if (buf && buf->IsValid()) {
        return buf->lost_events;
    }
    return 0;
}

} // namespace jackmini::lib
