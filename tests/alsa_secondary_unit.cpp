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

// Unit tests for the secondary ALSA card building blocks.  They need no ALSA
// hardware: the card's control, stream and thread code is covered by the live
// checks described in docs/secondary-alsa-cards.md.

#include "JackAlsaCardDiff.h"
#include "JackAlsaDriftController.h"
#include "JackAlsaRateConverter.h"
#include "JackAlsaRingBuffer.h"
#include "JackAlsaSecondaryConfig.h"
#include "JackAlsaSlotTable.h"
#include "resample/JackResampler.h"
#include "resample/pipewire_resample_bridge.h"

#include <array>
#include <cstdint>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace jackmini;

static std::int32_t gFailures = 0;
static std::int32_t gChecks = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        gChecks++;                                                    \
        if (!(cond)) {                                                 \
            gFailures++;                                              \
            printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                              \
    } while (0)

// A test-only resampler that passes samples through unchanged.  It shows that
// the converter works with any JackResampler, not only the PipeWire one.
class PassThroughResampler : public Resampler {
public:
    PassThroughResampler()
        : m_rate(1.0)
    {
    }
    virtual void reset() { }
    virtual void setRate(double rate) { m_rate = rate; }
    virtual std::uint32_t inputLength(std::uint32_t outLen) { return outLen; }
    virtual std::uint32_t outputLength(std::uint32_t inLen) { return inLen; }
    virtual void process(const float* const* src, std::uint32_t* inLen,
        float* const* dst, std::uint32_t* outLen)
    {
        std::uint32_t n = (*inLen < *outLen) ? *inLen : *outLen;
        // Only the first two channels are used by these tests.
        for (std::int32_t c = 0; c < 2; c++) {
            memcpy(dst[c], src[c], sizeof(float) * n);
        }
        *inLen = n;
        *outLen = n;
    }
    virtual std::uint32_t delay() { return 0; }
    double m_rate;
};

static void testConfigDefaults()
{
    printf("config: defaults and a full specification\n");
    AlsaSecondaryConfig c;
    std::string err;
    CHECK(alsaSecondaryConfigParse("name=usb1;device=hw:1;mode=capture;channels=4;rate=44100", &c, &err));
    CHECK(c.m_name == "usb1");
    CHECK(c.m_mode == kSecondaryModeCapture);
    CHECK(c.m_captureChannels == 4);
    CHECK(c.m_rate == 44100);
    CHECK(c.m_clock == kSecondaryClockAsync);

    CHECK(alsaSecondaryConfigParse("device=hw:1", &c, &err));
    CHECK(c.m_mode == kSecondaryModeDuplex);
    CHECK(c.m_clock == kSecondaryClockAsync);
    CHECK(c.m_name == "hw1");

    CHECK(alsaSecondaryConfigParse("hw:2,0", &c, &err));
    CHECK(c.m_name == "hw2");

    // strtol() accepted leading white space and '+'; keep accepting them.
    CHECK(alsaSecondaryConfigParse("device=hw:1;channels=+4", &c, &err));
    CHECK(c.m_captureChannels == 4);
    CHECK(alsaSecondaryConfigParse("device=hw:1;channels=\"  3\"", &c, &err));
    CHECK(c.m_captureChannels == 3);
}

static void testConfigErrors()
{
    printf("config: rejected specifications\n");
    AlsaSecondaryConfig c;
    std::string err;
    CHECK(!alsaSecondaryConfigParse("device=hw:1;mode=bogus", &c, &err));
    CHECK(!alsaSecondaryConfigParse("device=hw:1;clock=fast", &c, &err));
    CHECK(!alsaSecondaryConfigParse("device=hw:1;rate=1", &c, &err));
    CHECK(!alsaSecondaryConfigParse("device=hw:1;wat=1", &c, &err));
    CHECK(!alsaSecondaryConfigParse("name=x", &c, &err)); // no device
    CHECK(!alsaSecondaryConfigParse("", &c, &err));
    CHECK(!alsaSecondaryConfigParse("device=hw:1;channels=2x", &c, &err));
    CHECK(!alsaSecondaryConfigParse("device=hw:1;channels=+-2", &c, &err));
    CHECK(!alsaSecondaryConfigParse("device=hw:1;rate=99999999999999999999", &c, &err));
    CHECK(!alsaSecondaryConfigParse("device=hw:1;channels=\"\"", &c, &err));
}

static void testConfigClock()
{
    printf("config: clock=async and clock=sync\n");
    AlsaSecondaryConfig c;
    std::string err;
    CHECK(alsaSecondaryConfigParse("device=hw:1;clock=sync", &c, &err));
    CHECK(c.m_clock == kSecondaryClockSync);
    CHECK(alsaSecondaryConfigParse("device=hw:1;clock=async", &c, &err));
    CHECK(c.m_clock == kSecondaryClockAsync);
}

static void testConfigFormat()
{
    printf("config: format=float|s32|s16|auto\n");
    AlsaSecondaryConfig c;
    std::string err;
    CHECK(alsaSecondaryConfigParse("device=hw:1", &c, &err));
    CHECK(c.m_sampleFormat == kSampleFormatAuto);
    CHECK(alsaSecondaryConfigParse("device=hw:1;format=s32", &c, &err));
    CHECK(c.m_sampleFormat == kSampleFormatS32);
    CHECK(alsaSecondaryConfigParse("device=hw:1;format=s16", &c, &err));
    CHECK(c.m_sampleFormat == kSampleFormatS16);
    CHECK(!alsaSecondaryConfigParse("device=hw:1;format=u8", &c, &err));
}

static void testConfigResampler()
{
    printf("config: resampler=<name>\n");
    AlsaSecondaryConfig c;
    std::string err;
    CHECK(alsaSecondaryConfigParse("device=hw:1", &c, &err));
    CHECK(c.m_resampler == "pipewire");
    CHECK(alsaSecondaryConfigParse("device=hw:1;resampler=other", &c, &err));
    CHECK(c.m_resampler == "other");
    CHECK(!alsaSecondaryConfigParse("device=hw:1;resampler=", &c, &err));
}

static void testConfigFile()
{
    printf("config: file with comments and an error line\n");
    const char* good = "/tmp/jack_alsa_unit_good.conf";
    const char* bad = "/tmp/jack_alsa_unit_bad.conf";
    FILE* f = fopen(good, "w");
    fprintf(f, "# two cards\n\nname=a;device=hw:1;mode=capture\n  name=b;device=hw:2  # trailing\n");
    fclose(f);
    std::vector<AlsaSecondaryConfig> cards;
    std::string err;
    CHECK(alsaSecondaryConfigLoadFile(good, &cards, &err));
    CHECK(cards.size() == 2);

    f = fopen(bad, "w");
    fprintf(f, "name=a;device=hw:1\nname=b;device=hw:2;mode=nonsense\n");
    fclose(f);
    cards.clear();
    CHECK(!alsaSecondaryConfigLoadFile(bad, &cards, &err));
    CHECK(err.find(":2:") != std::string::npos); // names the line
    remove(good);
    remove(bad);
}

static void testRing()
{
    printf("ring: wrap-around, overflow and underflow\n");
    AlsaRingBuffer ring(2, 8);
    std::array<std::array<float, 5>, 2> a;
    std::array<std::array<float, 6>, 2> b;
    std::array<float*, 2> out = { b[0].data(), b[1].data() };
    std::array<const float*, 2> in = { a[0].data(), a[1].data() };
    for (std::int32_t i = 0; i < 5; i++) {
        a[0][i] = (float)i;
        a[1][i] = 100.0f + i;
    }

    CHECK(ring.write(in.data(), 5) == 5);
    CHECK(ring.write(in.data(), 5) == 3); // only 3 slots free
    CHECK(ring.read(out.data(), 6) == 8); // 8 buffered
    CHECK(b[0][4] == 4.0f && b[1][0] == 100.0f);
    CHECK(ring.write(in.data(), 4) == 4); // wraps past the end
    std::array<std::array<float, 6>, 2> c;
    std::array<float*, 2> cp = { c[0].data(), c[1].data() };
    CHECK(ring.read(cp.data(), 6) == 6); // the 2 left over plus the 4 new
    CHECK(c[0][0] == 1.0f && c[0][2] == 0.0f && c[0][5] == 3.0f);
    std::array<std::array<float, 4>, 2> z;
    std::array<float*, 2> zp = { z[0].data(), z[1].data() };
    CHECK(ring.read(zp.data(), 4) == 0);
    CHECK(z[0][0] == 0.0f && z[1][3] == 0.0f);

    // A non-power-of-two request is rounded up so the RT path can use a mask.
    AlsaRingBuffer odd(1, 5);
    CHECK(odd.capacity() == 8);
    std::array<float, 7> s = { 0, 1, 2, 3, 4, 5, 6 };
    std::array<float, 8> d = { 0 };
    std::array<const float*, 1> sin = { s.data() };
    std::array<float*, 1> dout = { d.data() };
    CHECK(odd.write(sin.data(), 7) == 7);
    CHECK(odd.write(sin.data(), 7) == 1);
    CHECK(odd.read(dout.data(), 8) == 8);
    CHECK(d[0] == 0.0f && d[6] == 6.0f && d[7] == 0.0f);
}

static void testConverterDI()
{
    printf("converter: injected resampler is used\n");
    PassThroughResampler* fake = new PassThroughResampler();
    AlsaRateConverter conv(fake, 2, 1024);
    CHECK(conv.isValid());
    std::array<float, 4> in0 = { 1, 2, 3, 4 };
    std::array<float, 4> in1 = { 5, 6, 7, 8 };
    std::array<const float*, 2> in = { in0.data(), in1.data() };
    std::array<float, 4> o0 = { 0 };
    std::array<float, 4> o1 = { 0 };
    std::array<float*, 2> out = { o0.data(), o1.data() };
    CHECK(conv.needInput(4) == 4);
    CHECK(conv.convert(in.data(), 4, out.data(), 4) == 4);
    CHECK(o0[0] == 1 && o0[3] == 4 && o1[2] == 7);
    conv.setRate(1.25);
    CHECK(fabs(fake->m_rate - 1.25) < 1e-12);
}

static void testResamplerFactories()
{
    printf("resampler: only pipewire is built in\n");
    CHECK(ResamplerFactory::find("pipewire") != nullptr);
    CHECK(ResamplerFactory::defaultFactory() == ResamplerFactory::find("pipewire"));
    CHECK(ResamplerFactory::find("linear") == nullptr);
    CHECK(ResamplerFactory::find("speex") == nullptr);

    ResamplerParams p = { 2, 44100, 48000, -1 };
    Resampler* r = ResamplerFactory::defaultFactory()->create(p);
    CHECK(r != nullptr);
    delete r;

    // The bridge picks the widest kernel the running CPU supports.  This is
    // informational on a given machine, but it must always be a known name.
    struct jack_pw_resampler* pw = jack_pw_resampler_new(2, 44100, 48000, -1);
    CHECK(pw != nullptr);
    if (pw != nullptr) {
        const char* kernel = jack_pw_resampler_kernel_name(pw);
        printf("resampler: selected '%s' kernel\n", kernel);
        bool known = strcmp(kernel, "full_c") == 0 || strcmp(kernel, "inter_c") == 0 || strcmp(kernel, "full_sse") == 0 || strcmp(kernel, "inter_sse") == 0 || strcmp(kernel, "full_avx2") == 0 || strcmp(kernel, "inter_avx2") == 0 || strcmp(kernel, "full_neon") == 0 || strcmp(kernel, "inter_neon") == 0;
        CHECK(known);
        jack_pw_resampler_free(pw);
    }
}

static void testSlotTable()
{
    printf("slots: fixed table, full and free, RT-readable\n");
    typedef AlsaSlotTable<std::int32_t, 3> Table;
    Table table;
    std::int32_t a = 1, b = 2, c = 3, d = 4;
    CHECK(Table::capacity() == 3);
    CHECK(table.count() == 0);
    CHECK(table.assign(&a) == 0);
    CHECK(table.assign(&b) == 1);
    CHECK(table.assign(&c) == 2);
    CHECK(table.assign(&d) == -1); // full
    CHECK(table.count() == 3);
    CHECK(table.get(1) == &b);
    CHECK(table.remove(&b) == 1);
    CHECK(table.get(1) == nullptr);
    CHECK(table.count() == 2);
    CHECK(table.remove(&b) == 0); // already gone
    CHECK(table.assign(&d) == 1); // the freed slot is reused
    CHECK(table.remove(&a) == 1);
    CHECK(table.remove(&c) == 1);
    CHECK(table.remove(&d) == 1);
    CHECK(table.count() == 0);
}

static void testPcmDeviceNumber()
{
    printf("naming: device numbers from PCM names\n");
    CHECK(alsaPcmDeviceNumber("hw:2,3") == 3);
    CHECK(alsaPcmDeviceNumber("hw:1,7") == 7);
    CHECK(alsaPcmDeviceNumber("hw:CARD=NVidia,DEV=8") == 8);
    CHECK(alsaPcmDeviceNumber("hw:0") == 0);
    CHECK(alsaPcmDeviceNumber("hw:CARD=Loopback") == 0);
}

static void testCardDiff()
{
    printf("reconcile: which cards to retire and add\n");
    std::vector<std::string> cur, want;
    AlsaCardChanges c = alsaDiffCards(cur, want);
    CHECK(c.m_retire.empty() && c.m_add.empty());

    cur.push_back("nvidia_d3|p");
    cur.push_back("loop_d0|cp");
    want.push_back("loop_d0|cp");
    want.push_back("nvidia_d3|p");
    want.push_back("nvidia_d7|p");
    c = alsaDiffCards(cur, want);
    CHECK(c.m_retire.empty());
    CHECK(c.m_add.size() == 1 && c.m_add[0] == 2);

    cur.clear();
    cur.push_back("hdmi_d3|p");
    want.clear();
    want.push_back("nvidia_d7|p");
    c = alsaDiffCards(cur, want);
    CHECK(c.m_retire.size() == 1 && c.m_retire[0] == 0);
    CHECK(c.m_add.size() == 1 && c.m_add[0] == 0);

    cur.clear();
    cur.push_back("loop_d1|c");
    want.clear();
    want.push_back("loop_d1|cp");
    c = alsaDiffCards(cur, want);
    CHECK(c.m_retire.size() == 1 && c.m_add.size() == 1);
}

static void testCardExcluded()
{
    printf("exclude-card: ids are matched without case\n");
    std::vector<std::string> none;
    std::vector<std::string> one;
    one.push_back("Loopback");
    CHECK(!alsaCardExcluded("Loopback", none));
    CHECK(alsaCardExcluded("Loopback", one));
    CHECK(alsaCardExcluded("loopback", one));
    CHECK(!alsaCardExcluded("NVidia", one));
    CHECK(!alsaCardExcluded("Loop", one));
    CHECK(!alsaCardExcluded("LoopbackX", one));
}

static void testPrimaryById()
{
    printf("primary: named by ALSA id, not by number\n");
    CHECK(alsaPcmNameIsById("hw:CARD=PCH"));
    CHECK(alsaPcmNameIsById("hw:CARD=PCH,DEV=0"));
    CHECK(alsaPcmNameIsById("hw:PCH,0"));
    CHECK(alsaPcmNameIsById("plughw:CARD=Loopback,DEV=1"));
    CHECK(!alsaPcmNameIsById("hw:0"));
    CHECK(!alsaPcmNameIsById("hw:2,0"));
    CHECK(!alsaPcmNameIsById("hw:CARD=0"));
    CHECK(!alsaPcmNameIsById("hw:"));
    CHECK(!alsaPcmNameIsById("hw:CARD="));
    CHECK(!alsaPcmNameIsById(""));
    CHECK(!alsaPcmNameIsById("default"));
}

static void testConverterRates()
{
    printf("converter: 44.1 kHz capture and 48 kHz playback keep exact counts\n");
    const std::uint32_t period = 512;
    ResamplerParams p = { 2, 44100, 48000, -1 };
    AlsaRateConverter cap(ResamplerFactory::defaultFactory()->create(p), 2, 8192);
    CHECK(cap.isValid());

    std::vector<float> ring(1 << 20);
    std::uint64_t cardN = 0, ringR = 0;
    std::vector<float> src0(8192), src1(8192), out0(period), out1(period);
    std::array<float*, 2> src = { src0.data(), src1.data() };
    std::array<float*, 2> out = { out0.data(), out1.data() };
    double acc = 0;
    bool exact = true;
    for (std::uint32_t c = 0; c < 400; c++) {
        acc += period * 44100.0 / 48000.0;
        std::uint32_t w = (std::uint32_t)acc;
        acc -= w;
        for (std::uint32_t i = 0; i < w; i++, cardN++) {
            ring[cardN] = sinf(2 * (float)M_PI * 1000.0f * cardN / 44100.0f);
        }
        std::uint32_t need = cap.needInput(period);
        for (std::uint32_t i = 0; i < need; i++) {
            std::uint64_t idx = ringR + i;
            src[0][i] = src[1][i] = idx < cardN ? ring[idx] : 0.0f;
        }
        ringR += need;
        std::uint32_t got = cap.convert(src.data(), need, out.data(), period);
        if (got != period) {
            exact = false;
        }
    }
    CHECK(exact);

    ResamplerParams q = { 2, 48000, 44100, -1 };
    AlsaRateConverter play(ResamplerFactory::defaultFactory()->create(q), 2, 8192);
    std::vector<float> in0(period), in1(period), pout0(8192), pout1(8192);
    std::array<float*, 2> in = { in0.data(), in1.data() };
    std::array<float*, 2> pout = { pout0.data(), pout1.data() };
    std::uint64_t inTotal = 0, outTotal = 0;
    for (std::uint32_t c = 0; c < 400; c++) {
        std::uint32_t produced = play.convert(in.data(), period, pout.data(), 8192);
        if (c > 20) {
            outTotal += produced;
            inTotal += period;
        }
    }
    double ratio = (double)outTotal / (double)inTotal;
    CHECK(fabs(ratio - 44100.0 / 48000.0) < 1e-3);
}

// Drives a capture ring with a card running `drift` faster than the primary,
// through the converter and the drift controller, as the card does at run time.
static void simulateCapture(double drift, double* finalCorrection, double* meanFillError)
{
    const std::uint32_t primary = 512;
    const std::uint32_t cardPeriod = 470;
    const std::uint32_t target = 3 * cardPeriod;

    ResamplerParams p = { 2, 44100, 48000, -1 };
    AlsaRateConverter conv(ResamplerFactory::defaultFactory()->create(p), 2, 8192);
    AlsaRingBuffer ring(2, cardPeriod * 8);
    AlsaDriftController driftCtl;

    std::array<float*, 2> prime = { new float[target](), new float[target]() };
    std::array<const float*, 2> primeC = { prime[0], prime[1] };
    ring.write(primeC.data(), target);

    std::array<float*, 2> needBuf = { new float[8192](), new float[8192]() };
    std::array<float*, 2> out = { new float[primary](), new float[primary]() };
    double acc = 0, errorSum = 0;
    std::uint64_t samples = 0;
    for (std::uint32_t cycle = 0; cycle < 4000; cycle++) {
        acc += primary * 44100.0 / 48000.0 * (1.0 + drift);
        std::uint32_t w = (std::uint32_t)acc;
        acc -= w;
        std::array<float*, 2> wr = { new float[w + 1](), new float[w + 1]() };
        std::array<const float*, 2> wc = { wr[0], wr[1] };
        ring.write(wc.data(), w);
        delete[] wr[0];
        delete[] wr[1];

        double fill = ring.availableRead() + conv.backlog();
        conv.setRate(driftCtl.update(fill, target, cardPeriod));
        std::uint32_t need = conv.needInput(primary);
        ring.read(needBuf.data(), need);
        conv.convert(needBuf.data(), need, out.data(), primary);

        if (cycle >= 2000) {
            errorSum += fabs(fill - target);
            samples++;
        }
    }
    *finalCorrection = driftCtl.correction();
    *meanFillError = errorSum / samples;
    for (std::int32_t c = 0; c < 2; c++) {
        delete[] prime[c];
        delete[] needBuf[c];
        delete[] out[c];
    }
}

static void testDriftController()
{
    printf("drift: the correction follows the card's clock\n");
    std::array<const double, 3> drifts = { 0.0, 300e-6, -300e-6 };
    for (std::int32_t i = 0; i < 3; i++) {
        double correction = 0, error = 0;
        simulateCapture(drifts[i], &correction, &error);
        // The correction must cancel the drift, and the buffer must stay on target.
        CHECK(fabs(correction + drifts[i]) < 20e-6);
        CHECK(error < 5.0);
    }
}

static void testDriftLimits()
{
    printf("drift: the correction is clamped and slew limited\n");
    AlsaDriftController c;
    double rate = c.update(100000.0, 0.0, 470.0); // huge error
    CHECK(fabs(rate - 1.0) <= 1000e-6 + 1e-12);
    CHECK(fabs(c.correction()) <= 20e-6 + 1e-12); // one slew step at most
    c.reset();
    CHECK(c.correction() == 0.0);
}

int main()
{
    testConfigDefaults();
    testConfigErrors();
    testConfigClock();
    testConfigFormat();
    testConfigResampler();
    testConfigFile();
    testRing();
    testConverterDI();
    testResamplerFactories();
    testSlotTable();
    testPcmDeviceNumber();
    testCardDiff();
    testCardExcluded();
    testPrimaryById();
    testConverterRates();
    testDriftController();
    testDriftLimits();

    printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
