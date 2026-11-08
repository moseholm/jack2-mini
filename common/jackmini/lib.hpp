// The internal client functions the jackmini client uses.  They were the C client API; the C names are kept, the
// functions are in jackmini::lib and not exported.
#ifndef JACKMINI_LIB_HPP
#define JACKMINI_LIB_HPP

#include "jack/types.h"
#include "jack/transport.h"
#include "jack/uuid.h"

namespace jackmini::lib
{

const char* jack_port_name(const jack_port_t* port);

const char* jack_port_type(const jack_port_t* port);

jack_nframes_t jack_get_buffer_size(jack_client_t* ext_client);

void* jack_port_get_buffer(jack_port_t* port, jack_nframes_t frames);

jack_port_t* jack_port_by_name(jack_client_t* ext_client, const char* portname);

char* jack_get_client_name(jack_client_t* ext_client);

void jack_free(void* ptr);

jack_client_t* jack_client_open(const char* ext_client_name, jack_options_t options, jack_status_t* status, ...);

int jack_uuid_parse(const char* b, jack_uuid_t* u);

void jack_transport_stop(jack_client_t* ext_client);

void jack_transport_start(jack_client_t* ext_client);

jack_transport_state_t jack_transport_query(const jack_client_t* ext_client, jack_position_t* pos);

int jack_transport_locate(jack_client_t* ext_client, jack_nframes_t frame);

int jack_set_xrun_callback(jack_client_t* ext_client, JackXRunCallback xrun_callback, void* arg);

int jack_set_sample_rate_callback(jack_client_t* ext_client, JackSampleRateCallback srate_callback, void* arg);

int jack_set_process_callback(jack_client_t* ext_client, JackProcessCallback callback, void* arg);

int jack_set_latency_callback(jack_client_t* ext_client, JackLatencyCallback latency_callback, void *arg);

int jack_set_buffer_size_callback(jack_client_t* ext_client, JackBufferSizeCallback bufsize_callback, void* arg);

int jack_recompute_total_latencies(jack_client_t* ext_client);

int jack_port_unregister(jack_client_t* ext_client, jack_port_t* port);

void jack_port_set_latency_range(jack_port_t *port, jack_latency_callback_mode_t mode, jack_latency_range_t *range);

void jack_port_get_latency_range(jack_port_t *port, jack_latency_callback_mode_t mode, jack_latency_range_t *range);

jack_port_t* jack_port_register(jack_client_t* ext_client, const char* port_name, const char* port_type, unsigned long flags, unsigned long buffer_size);

int jack_port_flags(const jack_port_t* port);

int jack_port_connected_to(const jack_port_t* port, const char* port_name);

void jack_on_shutdown(jack_client_t* ext_client, JackShutdownCallback callback, void* arg);

int jack_get_cycle_times(const jack_client_t *client, jack_nframes_t *current_frames, jack_time_t *current_usecs, jack_time_t *next_usecs, float *period_usecs);

jack_nframes_t jack_get_sample_rate(jack_client_t* ext_client);

float jack_cpu_load(jack_client_t* ext_client);

int jack_connect(jack_client_t* ext_client, const char* src, const char* dst);

int jack_disconnect(jack_client_t* ext_client, const char* src, const char* dst);

int jack_activate(jack_client_t* ext_client);

int jack_deactivate(jack_client_t* ext_client);

int jack_client_close(jack_client_t* ext_client);

char* jack_get_uuid_for_client_name(jack_client_t* ext_client, const char* client_name);

const char** jack_port_get_all_connections(const jack_client_t* ext_client, const jack_port_t* port);

const char** jack_get_ports(jack_client_t* ext_client, const char* port_name_pattern, const char* type_name_pattern, unsigned long flags);

jack_nframes_t jack_port_get_total_latency(jack_client_t* ext_client, jack_port_t* port);

jack_nframes_t jack_port_get_latency(jack_port_t* port);

const char* jack_port_short_name(const jack_port_t* port);

int jack_port_is_mine(const jack_client_t* ext_client, const jack_port_t* port);

int jack_port_connected(const jack_port_t* port);

} // namespace jackmini::lib

#endif
