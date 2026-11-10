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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace jackmini;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        g_checks++;                                                    \
        if (!(cond)) {                                                 \
            g_failures++;                                              \
            printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                              \
    } while (0)

// A test-only resampler that passes samples through unchanged.  It shows that
// the converter works with any JackResampler, not only the PipeWire one.
class PassThroughResampler : public JackResampler {
public:
    PassThroughResampler()
        : fRate(1.0)
    {
    }
    virtual void Reset() { }
    virtual void SetRate(double rate) { fRate = rate; }
    virtual unsigned int InputLength(unsigned int out_len) { return out_len; }
    virtual unsigned int OutputLength(unsigned int in_len) { return in_len; }
    virtual void Process(const float* const* src, unsigned int* in_len,
        float* const* dst, unsigned int* out_len)
    {
        unsigned int n = (*in_len < *out_len) ? *in_len : *out_len;
        // Only the first two channels are used by these tests.
        for (int c = 0; c < 2; c++) {
            memcpy(dst[c], src[c], sizeof(float) * n);
        }
        *in_len = n;
        *out_len = n;
    }
    virtual unsigned int Delay() { return 0; }
    double fRate;
};

static void TestConfigDefaults()
{
    printf("config: defaults and a full specification\n");
    JackAlsaSecondaryConfig c;
    std::string err;
    CHECK(JackAlsaSecondaryConfigParse("name=usb1;device=hw:1;mode=capture;channels=4;rate=44100", &c, &err));
    CHECK(c.name == "usb1");
    CHECK(c.mode == SecondaryModeCapture);
    CHECK(c.capture_channels == 4);
    CHECK(c.rate == 44100);
    CHECK(c.clock == SecondaryClockAsync);

    CHECK(JackAlsaSecondaryConfigParse("device=hw:1", &c, &err));
    CHECK(c.mode == SecondaryModeDuplex);
    CHECK(c.clock == SecondaryClockAsync);
    CHECK(c.name == "hw1");

    CHECK(JackAlsaSecondaryConfigParse("hw:2,0", &c, &err));
    CHECK(c.name == "hw2");

    // strtol() accepted leading white space and '+'; keep accepting them.
    CHECK(JackAlsaSecondaryConfigParse("device=hw:1;channels=+4", &c, &err));
    CHECK(c.capture_channels == 4);
    CHECK(JackAlsaSecondaryConfigParse("device=hw:1;channels=\"  3\"", &c, &err));
    CHECK(c.capture_channels == 3);
}

static void TestConfigErrors()
{
    printf("config: rejected specifications\n");
    JackAlsaSecondaryConfig c;
    std::string err;
    CHECK(!JackAlsaSecondaryConfigParse("device=hw:1;mode=bogus", &c, &err));
    CHECK(!JackAlsaSecondaryConfigParse("device=hw:1;clock=fast", &c, &err));
    CHECK(!JackAlsaSecondaryConfigParse("device=hw:1;rate=1", &c, &err));
    CHECK(!JackAlsaSecondaryConfigParse("device=hw:1;wat=1", &c, &err));
    CHECK(!JackAlsaSecondaryConfigParse("name=x", &c, &err)); // no device
    CHECK(!JackAlsaSecondaryConfigParse("", &c, &err));
    CHECK(!JackAlsaSecondaryConfigParse("device=hw:1;channels=2x", &c, &err));
    CHECK(!JackAlsaSecondaryConfigParse("device=hw:1;channels=+-2", &c, &err));
    CHECK(!JackAlsaSecondaryConfigParse("device=hw:1;rate=99999999999999999999", &c, &err));
    CHECK(!JackAlsaSecondaryConfigParse("device=hw:1;channels=\"\"", &c, &err));
}

static void TestConfigClock()
{
    printf("config: clock=async and clock=sync\n");
    JackAlsaSecondaryConfig c;
    std::string err;
    CHECK(JackAlsaSecondaryConfigParse("device=hw:1;clock=sync", &c, &err));
    CHECK(c.clock == SecondaryClockSync);
    CHECK(JackAlsaSecondaryConfigParse("device=hw:1;clock=async", &c, &err));
    CHECK(c.clock == SecondaryClockAsync);
}

static void TestConfigFormat()
{
    printf("config: format=float|s32|s16|auto\n");
    JackAlsaSecondaryConfig c;
    std::string err;
    CHECK(JackAlsaSecondaryConfigParse("device=hw:1", &c, &err));
    CHECK(c.sample_format == SampleFormatAuto);
    CHECK(JackAlsaSecondaryConfigParse("device=hw:1;format=s32", &c, &err));
    CHECK(c.sample_format == SampleFormatS32);
    CHECK(JackAlsaSecondaryConfigParse("device=hw:1;format=s16", &c, &err));
    CHECK(c.sample_format == SampleFormatS16);
    CHECK(!JackAlsaSecondaryConfigParse("device=hw:1;format=u8", &c, &err));
}

static void TestConfigResampler()
{
    printf("config: resampler=<name>\n");
    JackAlsaSecondaryConfig c;
    std::string err;
    CHECK(JackAlsaSecondaryConfigParse("device=hw:1", &c, &err));
    CHECK(c.resampler == "pipewire");
    CHECK(JackAlsaSecondaryConfigParse("device=hw:1;resampler=other", &c, &err));
    CHECK(c.resampler == "other");
    CHECK(!JackAlsaSecondaryConfigParse("device=hw:1;resampler=", &c, &err));
}

static void TestConfigFile()
{
    printf("config: file with comments and an error line\n");
    const char* good = "/tmp/jack_alsa_unit_good.conf";
    const char* bad = "/tmp/jack_alsa_unit_bad.conf";
    FILE* f = fopen(good, "w");
    fprintf(f, "# two cards\n\nname=a;device=hw:1;mode=capture\n  name=b;device=hw:2  # trailing\n");
    fclose(f);
    std::vector<JackAlsaSecondaryConfig> cards;
    std::string err;
    CHECK(JackAlsaSecondaryConfigLoadFile(good, &cards, &err));
    CHECK(cards.size() == 2);

    f = fopen(bad, "w");
    fprintf(f, "name=a;device=hw:1\nname=b;device=hw:2;mode=nonsense\n");
    fclose(f);
    cards.clear();
    CHECK(!JackAlsaSecondaryConfigLoadFile(bad, &cards, &err));
    CHECK(err.find(":2:") != std::string::npos); // names the line
    remove(good);
    remove(bad);
}

static void TestRing()
{
    printf("ring: wrap-around, overflow and underflow\n");
    JackAlsaRingBuffer ring(2, 8);
    float a[2][5], b[2][6];
    float* out[2] = { b[0], b[1] };
    const float* in[2] = { a[0], a[1] };
    for (int i = 0; i < 5; i++) {
        a[0][i] = (float)i;
        a[1][i] = 100.0f + i;
    }

    CHECK(ring.Write(in, 5) == 5);
    CHECK(ring.Write(in, 5) == 3); // only 3 slots free
    CHECK(ring.Read(out, 6) == 8); // 8 buffered
    CHECK(b[0][4] == 4.0f && b[1][0] == 100.0f);
    CHECK(ring.Write(in, 4) == 4); // wraps past the end
    float c[2][6];
    float* cp[2] = { c[0], c[1] };
    CHECK(ring.Read(cp, 6) == 6); // the 2 left over plus the 4 new
    CHECK(c[0][0] == 1.0f && c[0][2] == 0.0f && c[0][5] == 3.0f);
    float z[2][4];
    float* zp[2] = { z[0], z[1] };
    CHECK(ring.Read(zp, 4) == 0);
    CHECK(z[0][0] == 0.0f && z[1][3] == 0.0f);

    // A non-power-of-two request is rounded up so the RT path can use a mask.
    JackAlsaRingBuffer odd(1, 5);
    CHECK(odd.Capacity() == 8);
    float s[7] = { 0, 1, 2, 3, 4, 5, 6 };
    float d[8] = { 0 };
    const float* sin[1] = { s };
    float* dout[1] = { d };
    CHECK(odd.Write(sin, 7) == 7);
    CHECK(odd.Write(sin, 7) == 1);
    CHECK(odd.Read(dout, 8) == 8);
    CHECK(d[0] == 0.0f && d[6] == 6.0f && d[7] == 0.0f);
}

static void TestConverterDI()
{
    printf("converter: injected resampler is used\n");
    PassThroughResampler* fake = new PassThroughResampler();
    JackAlsaRateConverter conv(fake, 2, 1024);
    CHECK(conv.IsValid());
    float in0[4] = { 1, 2, 3, 4 }, in1[4] = { 5, 6, 7, 8 };
    const float* in[2] = { in0, in1 };
    float o0[4] = { 0 }, o1[4] = { 0 };
    float* out[2] = { o0, o1 };
    CHECK(conv.NeedInput(4) == 4);
    CHECK(conv.Convert(in, 4, out, 4) == 4);
    CHECK(o0[0] == 1 && o0[3] == 4 && o1[2] == 7);
    conv.SetRate(1.25);
    CHECK(fabs(fake->fRate - 1.25) < 1e-12);
}

static void TestResamplerFactories()
{
    printf("resampler: only pipewire is built in\n");
    CHECK(JackResamplerFactory::Find("pipewire") != NULL);
    CHECK(JackResamplerFactory::Default() == JackResamplerFactory::Find("pipewire"));
    CHECK(JackResamplerFactory::Find("linear") == NULL);
    CHECK(JackResamplerFactory::Find("speex") == NULL);

    JackResamplerParams p = { 2, 44100, 48000, -1 };
    JackResampler* r = JackResamplerFactory::Default()->Create(p);
    CHECK(r != NULL);
    delete r;

    // The bridge picks the widest kernel the running CPU supports.  This is
    // informational on a given machine, but it must always be a known name.
    struct jack_pw_resampler* pw = jack_pw_resampler_new(2, 44100, 48000, -1);
    CHECK(pw != NULL);
    if (pw != NULL) {
        const char* kernel = jack_pw_resampler_kernel_name(pw);
        printf("resampler: selected '%s' kernel\n", kernel);
        bool known = strcmp(kernel, "full_c") == 0 || strcmp(kernel, "inter_c") == 0 || strcmp(kernel, "full_sse") == 0 || strcmp(kernel, "inter_sse") == 0 || strcmp(kernel, "full_avx2") == 0 || strcmp(kernel, "inter_avx2") == 0 || strcmp(kernel, "full_neon") == 0 || strcmp(kernel, "inter_neon") == 0;
        CHECK(known);
        jack_pw_resampler_free(pw);
    }
}

static void TestSlotTable()
{
    printf("slots: fixed table, full and free, RT-readable\n");
    typedef JackAlsaSlotTable<int, 3> Table;
    Table table;
    int a = 1, b = 2, c = 3, d = 4;
    CHECK(Table::Capacity() == 3);
    CHECK(table.Count() == 0);
    CHECK(table.Assign(&a) == 0);
    CHECK(table.Assign(&b) == 1);
    CHECK(table.Assign(&c) == 2);
    CHECK(table.Assign(&d) == -1); // full
    CHECK(table.Count() == 3);
    CHECK(table.Get(1) == &b);
    CHECK(table.Remove(&b) == 1);
    CHECK(table.Get(1) == NULL);
    CHECK(table.Count() == 2);
    CHECK(table.Remove(&b) == 0); // already gone
    CHECK(table.Assign(&d) == 1); // the freed slot is reused
    CHECK(table.Remove(&a) == 1);
    CHECK(table.Remove(&c) == 1);
    CHECK(table.Remove(&d) == 1);
    CHECK(table.Count() == 0);
}

static void TestPcmDeviceNumber()
{
    printf("naming: device numbers from PCM names\n");
    CHECK(JackAlsaPcmDeviceNumber("hw:2,3") == 3);
    CHECK(JackAlsaPcmDeviceNumber("hw:1,7") == 7);
    CHECK(JackAlsaPcmDeviceNumber("hw:CARD=NVidia,DEV=8") == 8);
    CHECK(JackAlsaPcmDeviceNumber("hw:0") == 0);
    CHECK(JackAlsaPcmDeviceNumber("hw:CARD=Loopback") == 0);
}

static void TestCardDiff()
{
    printf("reconcile: which cards to retire and add\n");
    std::vector<std::string> cur, want;
    JackAlsaCardChanges c = JackAlsaDiffCards(cur, want);
    CHECK(c.retire.empty() && c.add.empty());

    cur.push_back("nvidia_d3|p");
    cur.push_back("loop_d0|cp");
    want.push_back("loop_d0|cp");
    want.push_back("nvidia_d3|p");
    want.push_back("nvidia_d7|p");
    c = JackAlsaDiffCards(cur, want);
    CHECK(c.retire.empty());
    CHECK(c.add.size() == 1 && c.add[0] == 2);

    cur.clear();
    cur.push_back("hdmi_d3|p");
    want.clear();
    want.push_back("nvidia_d7|p");
    c = JackAlsaDiffCards(cur, want);
    CHECK(c.retire.size() == 1 && c.retire[0] == 0);
    CHECK(c.add.size() == 1 && c.add[0] == 0);

    cur.clear();
    cur.push_back("loop_d1|c");
    want.clear();
    want.push_back("loop_d1|cp");
    c = JackAlsaDiffCards(cur, want);
    CHECK(c.retire.size() == 1 && c.add.size() == 1);
}

static void TestCardExcluded()
{
    printf("exclude-card: ids are matched without case\n");
    std::vector<std::string> none;
    std::vector<std::string> one;
    one.push_back("Loopback");
    CHECK(!JackAlsaCardExcluded("Loopback", none));
    CHECK(JackAlsaCardExcluded("Loopback", one));
    CHECK(JackAlsaCardExcluded("loopback", one));
    CHECK(!JackAlsaCardExcluded("NVidia", one));
    CHECK(!JackAlsaCardExcluded("Loop", one));
    CHECK(!JackAlsaCardExcluded("LoopbackX", one));
}

static void TestPrimaryById()
{
    printf("primary: named by ALSA id, not by number\n");
    CHECK(JackAlsaPcmNameIsById("hw:CARD=PCH"));
    CHECK(JackAlsaPcmNameIsById("hw:CARD=PCH,DEV=0"));
    CHECK(JackAlsaPcmNameIsById("hw:PCH,0"));
    CHECK(JackAlsaPcmNameIsById("plughw:CARD=Loopback,DEV=1"));
    CHECK(!JackAlsaPcmNameIsById("hw:0"));
    CHECK(!JackAlsaPcmNameIsById("hw:2,0"));
    CHECK(!JackAlsaPcmNameIsById("hw:CARD=0"));
    CHECK(!JackAlsaPcmNameIsById("hw:"));
    CHECK(!JackAlsaPcmNameIsById("hw:CARD="));
    CHECK(!JackAlsaPcmNameIsById(""));
    CHECK(!JackAlsaPcmNameIsById("default"));
}

static void TestConverterRates()
{
    printf("converter: 44.1 kHz capture and 48 kHz playback keep exact counts\n");
    const unsigned int period = 512;
    JackResamplerParams p = { 2, 44100, 48000, -1 };
    JackAlsaRateConverter cap(JackResamplerFactory::Default()->Create(p), 2, 8192);
    CHECK(cap.IsValid());

    std::vector<float> ring(1 << 20);
    unsigned long card_n = 0, ring_r = 0;
    std::vector<float> src0(8192), src1(8192), out0(period), out1(period);
    float* src[2] = { src0.data(), src1.data() };
    float* out[2] = { out0.data(), out1.data() };
    double acc = 0;
    bool exact = true;
    for (unsigned int c = 0; c < 400; c++) {
        acc += period * 44100.0 / 48000.0;
        unsigned int w = (unsigned int)acc;
        acc -= w;
        for (unsigned int i = 0; i < w; i++, card_n++) {
            ring[card_n] = sinf(2 * (float)M_PI * 1000.0f * card_n / 44100.0f);
        }
        unsigned int need = cap.NeedInput(period);
        for (unsigned int i = 0; i < need; i++) {
            unsigned long idx = ring_r + i;
            src[0][i] = src[1][i] = idx < card_n ? ring[idx] : 0.0f;
        }
        ring_r += need;
        unsigned int got = cap.Convert(src, need, out, period);
        if (got != period) {
            exact = false;
        }
    }
    CHECK(exact);

    JackResamplerParams q = { 2, 48000, 44100, -1 };
    JackAlsaRateConverter play(JackResamplerFactory::Default()->Create(q), 2, 8192);
    std::vector<float> in0(period), in1(period), pout0(8192), pout1(8192);
    float* in[2] = { in0.data(), in1.data() };
    float* pout[2] = { pout0.data(), pout1.data() };
    unsigned long in_total = 0, out_total = 0;
    for (unsigned int c = 0; c < 400; c++) {
        unsigned int produced = play.Convert(in, period, pout, 8192);
        if (c > 20) {
            out_total += produced;
            in_total += period;
        }
    }
    double ratio = (double)out_total / (double)in_total;
    CHECK(fabs(ratio - 44100.0 / 48000.0) < 1e-3);
}

// Drives a capture ring with a card running `drift` faster than the primary,
// through the converter and the drift controller, as the card does at run time.
static void SimulateCapture(double drift, double* final_correction, double* mean_fill_error)
{
    const unsigned int primary = 512;
    const unsigned int card_period = 470;
    const unsigned int target = 3 * card_period;

    JackResamplerParams p = { 2, 44100, 48000, -1 };
    JackAlsaRateConverter conv(JackResamplerFactory::Default()->Create(p), 2, 8192);
    JackAlsaRingBuffer ring(2, card_period * 8);
    JackAlsaDriftController drift_ctl;

    float* prime[2] = { new float[target](), new float[target]() };
    const float* prime_c[2] = { prime[0], prime[1] };
    ring.Write(prime_c, target);

    float* need_buf[2] = { new float[8192](), new float[8192]() };
    float* out[2] = { new float[primary](), new float[primary]() };
    double acc = 0, error_sum = 0;
    unsigned long samples = 0;
    for (unsigned int cycle = 0; cycle < 4000; cycle++) {
        acc += primary * 44100.0 / 48000.0 * (1.0 + drift);
        unsigned int w = (unsigned int)acc;
        acc -= w;
        float* wr[2] = { new float[w + 1](), new float[w + 1]() };
        const float* wc[2] = { wr[0], wr[1] };
        ring.Write(wc, w);
        delete[] wr[0];
        delete[] wr[1];

        double fill = ring.AvailableRead() + conv.Backlog();
        conv.SetRate(drift_ctl.Update(fill, target, card_period));
        unsigned int need = conv.NeedInput(primary);
        ring.Read(need_buf, need);
        conv.Convert(need_buf, need, out, primary);

        if (cycle >= 2000) {
            error_sum += fabs(fill - target);
            samples++;
        }
    }
    *final_correction = drift_ctl.Correction();
    *mean_fill_error = error_sum / samples;
    for (int c = 0; c < 2; c++) {
        delete[] prime[c];
        delete[] need_buf[c];
        delete[] out[c];
    }
}

static void TestDriftController()
{
    printf("drift: the correction follows the card's clock\n");
    const double drifts[] = { 0.0, 300e-6, -300e-6 };
    for (int i = 0; i < 3; i++) {
        double correction = 0, error = 0;
        SimulateCapture(drifts[i], &correction, &error);
        // The correction must cancel the drift, and the buffer must stay on target.
        CHECK(fabs(correction + drifts[i]) < 20e-6);
        CHECK(error < 5.0);
    }
}

static void TestDriftLimits()
{
    printf("drift: the correction is clamped and slew limited\n");
    JackAlsaDriftController c;
    double rate = c.Update(100000.0, 0.0, 470.0); // huge error
    CHECK(fabs(rate - 1.0) <= 1000e-6 + 1e-12);
    CHECK(fabs(c.Correction()) <= 20e-6 + 1e-12); // one slew step at most
    c.Reset();
    CHECK(c.Correction() == 0.0);
}

int main()
{
    TestConfigDefaults();
    TestConfigErrors();
    TestConfigClock();
    TestConfigFormat();
    TestConfigResampler();
    TestConfigFile();
    TestRing();
    TestConverterDI();
    TestResamplerFactories();
    TestSlotTable();
    TestPcmDeviceNumber();
    TestCardDiff();
    TestCardExcluded();
    TestPrimaryById();
    TestConverterRates();
    TestDriftController();
    TestDriftLimits();

    printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
