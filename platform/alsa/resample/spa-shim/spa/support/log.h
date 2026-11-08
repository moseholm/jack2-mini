/*
 * Minimal subset of PipeWire's spa/support/log.h.  Logging calls in the
 * vendored resampler compile to nothing; JACK logging is not wired in here.
 */

#ifndef JACK_SPA_SHIM_SUPPORT_LOG_H
#define JACK_SPA_SHIM_SUPPORT_LOG_H

#include <spa/utils/defs.h>

struct spa_log;

struct spa_log_topic {
	const char *topic;
};

#define SPA_LOG_TOPIC_DEFINE(name,str) struct spa_log_topic name = { str }
#define SPA_LOG_TOPIC_DEFAULT NULL

#define spa_log_error(l,...) ((void)(l))
#define spa_log_warn(l,...) ((void)(l))
#define spa_log_info(l,...) ((void)(l))
#define spa_log_debug(l,...) ((void)(l))
#define spa_log_trace(l,...) ((void)(l))
#define spa_log_trace_fp(l,...) ((void)(l))

#endif
