/* Measures the process callback: start-to-start interval and callback duration.
 * Samples are stored in a preallocated buffer; the callback does not allocate.
 *
 * usage: jack_rtbench [seconds]
 * The run is seconds of audio: it counts cycles, so it holds in freewheel mode as well.  The client waits for the server
 * to come up. */
#include "jackmini/jackmini.hpp"
#include "../jackmini_wait.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <optional>
#include <vector>

using namespace jackmini;

#define MAXS 400000
static long long starts[MAXS], ends[MAXS];
static std::atomic<unsigned long> nsamples { 0 }, nxruns { 0 };
static Port in, out;
static Client* jack_handle;

static long long now_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static bool process(const Cycle& cycle)
{
    const FrameCount nframes = cycle.frames;
    unsigned long i = nsamples.fetch_add(1);
    long long s = now_ns();
    auto o = jack_handle->audio_buffer(out);
    auto x = jack_handle->audio_buffer(in);
    std::memcpy(o.data(), x.data(), sizeof(float) * nframes);
    long long e = now_ns();
    if (i < MAXS) {
        starts[i] = s;
        ends[i] = e;
    }
    return true;
}

static int cmp(const void* a, const void* b)
{
    long long x = *(const long long*)a, y = *(const long long*)b;
    return (x > y) - (x < y);
}

static void pct(const char* label, long long* v, long n)
{
    std::qsort(v, n, sizeof(long long), cmp);
    std::printf("%-22s p50 %8.1f us  p99 %8.1f us  p99.9 %8.1f us  max %8.1f us\n", label,
        v[n / 2] / 1e3, v[(long)(n * 0.99)] / 1e3, v[(long)(n * 0.999)] / 1e3, v[n - 1] / 1e3);
}

int main(int argc, char** argv)
{
    int seconds = argc > 1 ? std::atoi(argv[1]) : 30;

    OpenOptions options;
    options.start_server = false;

    // The server may still be starting: retry the open for a while, instead of a fixed delay.
    std::optional<Client> client;
    WaitUntil([&] {
        auto opened = Client::open("rtbench", options);
        if (opened) {
            client.emplace(std::move(*opened));
        }
        return client.has_value();
    });
    if (!client) {
        std::fprintf(stderr, "open failed: no server\n");
        return 1;
    }
    jack_handle = &*client;

    client->on_process(process);
    client->on_xrun([] { nxruns.fetch_add(1); });
    auto pin = client->register_port("in", PortType::Audio, PortFlags::Input);
    auto pout = client->register_port("out", PortType::Audio, PortFlags::Output);
    if (!pin || !pout) {
        std::fprintf(stderr, "cannot register the ports\n");
        return 1;
    }
    in = *pin;
    out = *pout;
    client->activate();

    const unsigned long cycles = static_cast<unsigned long>(seconds) * client->sample_rate() / client->buffer_size();
    WaitUntil([&] { return nsamples.load() >= cycles; });
    client->deactivate();

    unsigned long n = nsamples.load();
    if (n > MAXS) {
        n = MAXS;
    }
    std::vector<long long> iv(n > 0 ? n - 1 : 0), du(n);
    for (unsigned long i = 1; i < n; i++) {
        iv[i - 1] = starts[i] - starts[i - 1];
    }
    for (unsigned long i = 0; i < n; i++) {
        du[i] = ends[i] - starts[i];
    }
    std::printf("cycles %lu  xruns %lu  period %u  rate %u\n", nsamples.load(), nxruns.load(),
        client->buffer_size(), client->sample_rate());
    if (n > 1) {
        pct("interval (start-start)", iv.data(), (long)iv.size());
    }
    if (n > 0) {
        pct("callback duration", du.data(), (long)du.size());
    }
    client->close();
    return 0;
}
