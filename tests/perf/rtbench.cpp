// Measures the process callback: start-to-start interval and callback duration.
// Samples are stored in a preallocated buffer; the callback does not allocate.
//
// usage: jack_rtbench [seconds]
// The run is seconds of audio: it counts cycles, so it holds in freewheel mode as well.  The client waits for the server
// to come up.
#include "jackmini/jackmini.hpp"
#include "../jackmini_wait.hpp"

#include <array>
#include <cstdint>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <optional>
#include <vector>

using namespace jackmini;

static constexpr std::int32_t kMaxs = 400000;
static std::array<std::int64_t, kMaxs> gStarts;
static std::array<std::int64_t, kMaxs> gEnds;
static std::atomic<std::uint64_t> gNsamples { 0 }, gNxruns { 0 };
static Port gIn, gOut;
static Client* gJackHandle;

static std::int64_t nowNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static bool process(const Cycle& cycle)
{
    const FrameCount nframes = cycle.m_frames;
    std::uint64_t i = gNsamples.fetch_add(1);
    std::int64_t s = nowNs();
    auto o = gJackHandle->audioBuffer(gOut);
    auto x = gJackHandle->audioBuffer(gIn);
    std::memcpy(o.data(), x.data(), sizeof(float) * nframes);
    std::int64_t e = nowNs();
    if (i < kMaxs) {
        gStarts[i] = s;
        gEnds[i] = e;
    }
    return true;
}

static std::int32_t cmp(const void* a, const void* b)
{
    std::int64_t x = *(const std::int64_t*)a, y = *(const std::int64_t*)b;
    return (x > y) - (x < y);
}

static void pct(const char* label, std::int64_t* v, std::int64_t n)
{
    std::qsort(v, n, sizeof(std::int64_t), cmp);
    std::printf("%-22s p50 %8.1f us  p99 %8.1f us  p99.9 %8.1f us  max %8.1f us\n", label,
        v[n / 2] / 1e3, v[(std::int64_t)(n * 0.99)] / 1e3, v[(std::int64_t)(n * 0.999)] / 1e3, v[n - 1] / 1e3);
}

int main(std::int32_t argc, char** argv)
{
    std::int32_t seconds = argc > 1 ? std::atoi(argv[1]) : 30;

    OpenOptions options;
    options.m_startServer = false;

    // The server may still be starting: retry the open for a while, instead of a fixed delay.
    std::optional<Client> client;
    waitUntil([&] {
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
    gJackHandle = &*client;

    client->onProcess(process);
    client->onXrun([] { gNxruns.fetch_add(1); });
    auto pin = client->registerPort("in", PortType::kAudio, PortFlags::kInput);
    auto pout = client->registerPort("out", PortType::kAudio, PortFlags::kOutput);
    if (!pin || !pout) {
        std::fprintf(stderr, "cannot register the ports\n");
        return 1;
    }
    gIn = *pin;
    gOut = *pout;
    client->activate();

    const std::uint64_t cycles = static_cast<std::uint64_t>(seconds) * client->sampleRate() / client->bufferSize();
    waitUntil([&] { return gNsamples.load() >= cycles; });
    client->deactivate();

    std::uint64_t n = gNsamples.load();
    if (n > kMaxs) {
        n = kMaxs;
    }
    std::vector<std::int64_t> iv(n > 0 ? n - 1 : 0), du(n);
    for (std::uint64_t i = 1; i < n; i++) {
        iv[i - 1] = gStarts[i] - gStarts[i - 1];
    }
    for (std::uint64_t i = 0; i < n; i++) {
        du[i] = gEnds[i] - gStarts[i];
    }
    std::printf("cycles %lu  xruns %lu  period %u  rate %u\n", gNsamples.load(), gNxruns.load(),
        client->bufferSize(), client->sampleRate());
    if (n > 1) {
        pct("interval (start-start)", iv.data(), (std::int64_t)iv.size());
    }
    if (n > 0) {
        pct("callback duration", du.data(), (std::int64_t)du.size());
    }
    client->close();
    return 0;
}
