/* Measures the process callback: start-to-start interval and callback duration.
 * Samples are stored in a preallocated buffer; the callback does not allocate. */
#include <jack/jack.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>

#define MAXS 400000
static long long starts[MAXS], ends[MAXS];
static atomic_long nsamples, nxruns;
static jack_port_t *in, *out;

static long long now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
static int process(jack_nframes_t nframes, void *arg) {
    (void)arg; (void)nframes;
    long i = atomic_fetch_add(&nsamples, 1);
    long long s = now_ns();
    jack_default_audio_sample_t *o = jack_port_get_buffer(out, nframes);
    jack_default_audio_sample_t *x = jack_port_get_buffer(in, nframes);
    memcpy(o, x, sizeof(jack_default_audio_sample_t) * nframes);
    long long e = now_ns();
    if (i < MAXS) { starts[i] = s; ends[i] = e; }
    return 0;
}
static int xrun(void *arg) { (void)arg; atomic_fetch_add(&nxruns, 1); return 0; }
static int cmp(const void *a, const void *b) { long long x = *(long long*)a, y = *(long long*)b; return (x > y) - (x < y); }
static void pct(const char *label, long long *v, long n) {
    qsort(v, n, sizeof(long long), cmp);
    printf("%-22s p50 %8.1f us  p99 %8.1f us  p99.9 %8.1f us  max %8.1f us\n", label,
           v[n/2]/1e3, v[(long)(n*0.99)]/1e3, v[(long)(n*0.999)]/1e3, v[n-1]/1e3);
}
int main(int argc, char **argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 30;
    jack_status_t st;
    jack_client_t *c = jack_client_open("rtbench", JackNoStartServer, &st);
    if (!c) { fprintf(stderr, "open failed 0x%x\n", st); return 1; }
    jack_set_process_callback(c, process, NULL);
    jack_set_xrun_callback(c, xrun, NULL);
    in = jack_port_register(c, "in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    out = jack_port_register(c, "out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    jack_activate(c);
    sleep(seconds);
    jack_deactivate(c);
    long n = atomic_load(&nsamples); if (n > MAXS) n = MAXS;
    long long *iv = malloc(sizeof(long long) * n), *du = malloc(sizeof(long long) * n);
    for (long i = 1; i < n; i++) iv[i-1] = starts[i] - starts[i-1];
    for (long i = 0; i < n; i++) du[i] = ends[i] - starts[i];
    printf("cycles %ld  xruns %ld  period %u  rate %u\n", atomic_load(&nsamples), atomic_load(&nxruns),
           jack_get_buffer_size(c), jack_get_sample_rate(c));
    pct("interval (start-start)", iv, n-1);
    pct("callback duration", du, n);
    jack_client_close(c);
    return 0;
}
