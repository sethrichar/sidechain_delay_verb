// Phase 2 acceptance (BUILD_PLAN Phase 2, CLAUDE.md §6): the fractional delay line, the
// Digital delay (echo position, feedback level, feedback filters, click-free time changes,
// modulation, stereo / ping-pong) and tempo sync against a mocked playhead — measured through
// ClearSpaceProcessor via the render library, plus direct unit tests on the DSP classes.

#include "Measure.h"
#include "Parameters.h"
#include "Renderer.h"
#include "Sources.h"
#include "dsp/DelayLine.h"
#include "dsp/delay/DelayEngine.h"
#include "dsp/delay/DigitalDelay.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace clearspace;
using namespace clearspace::render;
using namespace clearspace::test;
using Catch::Approx;

namespace
{

using Overrides = std::vector<std::pair<std::string, std::string>>;

Overrides operator+(Overrides a, const Overrides& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

std::string num(double v)
{
    return juce::String(v, 4).toStdString();
}

juce::AudioBuffer<float> source(SourceKind kind, double seconds, double sr, float levelDb = -6.0f,
                                double freqHz = 1000.0, double onSeconds = 0.5,
                                double offSeconds = 0.5)
{
    SourceSpec spec;
    spec.kind = kind;
    spec.seconds = seconds;
    spec.levelDb = levelDb;
    spec.freqHz = freqHz;
    spec.burstOnSeconds = onSeconds;
    spec.burstOffSeconds = offSeconds;
    return makeSource(spec, sr);
}

juce::AudioBuffer<float> renderWith(const juce::AudioBuffer<float>& input, double sr,
                                    const Overrides& overrides, int block = 512,
                                    const std::vector<AutomationPoint>& automation = {},
                                    std::optional<double> bpm = std::nullopt, bool tail = false)
{
    RenderSettings s;
    s.sampleRate = sr;
    s.blockSize = block;
    s.parameterOverrides = overrides;
    s.automation = automation;
    s.bpm = bpm;
    s.appendTail = tail;
    auto out = renderThroughProcessor(input, s);
    for (const auto& e : out.errors)
        FAIL("render error: " << e.message);
    REQUIRE(out.ok());
    REQUIRE_FALSE(hasNonFinite(out.buffer));
    return std::move(out.buffer);
}

// Delay only, 100 % wet, no ducking, modulation off, feedback filters wide open.
const Overrides delayClean = {{ParamID::mix, "100"},
                              {ParamID::reverbBypass, "on"},
                              {ParamID::delayDuckEnable, "off"},
                              {ParamID::reverbDuckEnable, "off"},
                              {ParamID::delayMod, "0"},
                              {ParamID::delayLowCut, "20"},
                              {ParamID::delayHighCut, "20000"}};

/** Arrival time of an isolated echo, in samples: the |y|-weighted centroid over [from, to).
    A fractional delay spreads an impulse over ≤ 4 samples around the true position (the Hermite
    kernel is symmetric, so the centroid is that position); "first nonzero sample" would be up to
    two samples early because of the kernel's small pre-ring. */
double echoPositionSamples(const juce::AudioBuffer<float>& b, int channel, int from, int to)
{
    from = std::max(0, from);
    to = std::min(b.getNumSamples(), to);
    const float* y = b.getReadPointer(channel);
    double num = 0.0, den = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double w = std::abs(static_cast<double>(y[i]));
        num += w * i;
        den += w;
    }
    return den > 0.0 ? num / den : -1.0;
}

/** Periods (seconds) between successive rising zero crossings in [start, end), with linear
    interpolation of the crossing instant. */
std::vector<double> risingZeroCrossingPeriods(const float* x, int start, int end, double sr)
{
    std::vector<double> periods;
    double last = -1.0;
    for (int i = std::max(1, start); i < end; ++i)
    {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double frac = static_cast<double>(-x[i - 1]) / (x[i] - x[i - 1]);
            const double t = (i - 1 + frac) / sr;
            if (last >= 0.0)
                periods.push_back(t - last);
            last = t;
        }
    }
    return periods;
}

/** Largest relative deviation of the instantaneous frequency from `freqHz`. */
double maxPitchDeviation(const juce::AudioBuffer<float>& b, int channel, double from, double to,
                         double sr, double freqHz)
{
    const auto periods = risingZeroCrossingPeriods(b.getReadPointer(channel), toSample(from, sr),
                                                   toSample(to, sr), sr);
    REQUIRE(periods.size() > 10);
    double worst = 0.0;
    for (double p : periods)
        worst = std::max(worst, std::abs(1.0 / (p * freqHz) - 1.0));
    return worst;
}

} // namespace

// ---- DelayLine ------------------------------------------------------------------------

TEST_CASE("delayline: integer reads are exact and Hermite reproduces polynomials", "[delayline]")
{
    dsp::DelayLine<float> line;
    line.prepare(100);
    CHECK(line.getMaxDelay() == 100);

    // Quadratic ramp: Catmull-Rom tangents are exact for quadratics, so fractional reads must
    // land on the curve. Reads happen before the write of sample n (the line's contract), so a
    // delay of d returns sample n − d.
    auto f = [](double n) { return 0.001 * n * n - 0.3 * n + 2.0; };
    for (int n = 0; n < 400; ++n)
    {
        if (n >= 110)
        {
            for (int d : {1, 2, 17, 100})
                CHECK(line.read(d) == Approx(f(n - d)).margin(1e-4));
            for (float d : {2.0f, 2.25f, 7.5f, 33.999f, 99.0f})
                CHECK(line.readHermite(d) == Approx(f(n - static_cast<double>(d))).margin(2e-3));
        }
        line.write(static_cast<float>(f(n)));
    }

    SECTION("a fractionally delayed sine is accurate to better than −60 dB")
    {
        const double sr = 48000.0, freq = 1000.0;
        line.clear();
        double worst = 0.0;
        for (int n = 0; n < 2000; ++n)
        {
            const double x = std::sin(2.0 * std::numbers::pi * freq * n / sr);
            line.write(static_cast<float>(x));
            if (n >= 100)
            {
                const double d = 10.37;
                // Read after the write here, so the delay is relative to sample n + 1.
                const double expected = std::sin(2.0 * std::numbers::pi * freq * (n + 1 - d) / sr);
                worst =
                    std::max(worst, std::abs(line.readHermite(static_cast<float>(d)) - expected));
            }
        }
        CHECK(20.0 * std::log10(worst) < -60.0);
    }

    SECTION("reads are clamped to the usable range, never out of bounds")
    {
        line.clear();
        for (int n = 0; n < 10; ++n)
            line.write(1.0f);
        CHECK(line.read(0) == Approx(1.0f));
        CHECK(line.read(1000) == Approx(0.0f).margin(0.0f)); // beyond max → clamped, cleared
        CHECK(line.readHermite(-5.0f) == Approx(1.0f));
        CHECK(line.readHermite(1.0e9f) == Approx(0.0f).margin(0.0f));
        CHECK(std::isfinite(line.readHermite(std::numeric_limits<float>::quiet_NaN())));
    }
}

// ---- Echo position and level ---------------------------------------------------------

TEST_CASE("delay: first echo lands within ±1 sample at 44.1 / 48 / 96 kHz", "[delay][time]")
{
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (double ms : {1.0, 23.7, 375.0, 1000.0, 2000.0})
        {
            INFO("sr " << sr << " time " << ms << " ms");
            const auto in = source(SourceKind::impulse, ms * 1.0e-3 + 0.2, sr);
            auto out = renderWith(in, sr,
                                  delayClean + Overrides{{ParamID::delayTime, num(ms)},
                                                         {ParamID::delayFeedback, "0"}},
                                  256);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* y = out.getReadPointer(ch);
                const double exact = ms * 1.0e-3 * sr;
                const int expected = toSample(ms * 1.0e-3, sr);
                const double arrival = echoPositionSamples(out, ch, expected - 8, expected + 8);
                INFO("ch " << ch << " arrival " << arrival << " expected " << exact);
                CHECK(std::abs(arrival - exact) <= 1.0);
                // The main lobe (≥ ¼ of the impulse) also starts within ±1 sample.
                const int first =
                    firstAbove(y, 0, out.getNumSamples(), 0.25f * in.getMagnitude(0, 0, 1));
                CHECK(std::abs(first - expected) <= 1);
                // Unity: a fractional read spreads the impulse over ≤ 4 samples, but the
                // interpolator's DC gain is exactly 1, so the echo sums to the input.
                double sum = 0.0;
                for (int k = std::max(0, expected - 3);
                     k < std::min(out.getNumSamples(), expected + 4); ++k)
                    sum += y[k];
                CHECK(sum == Approx(in.getMagnitude(0, 0, 1)).epsilon(0.01));
                // Nothing else: feedback is 0.
                CHECK(peakIn(out, ch, expected + 4, out.getNumSamples()) ==
                      Approx(0.0f).margin(1e-6f));
            }
        }
}

TEST_CASE("delay: repeats follow the feedback amount and stay bounded at 100 %",
          "[delay][feedback]")
{
    const double sr = 48000.0;
    // 0.1 s of 1 kHz at −6 dBFS, then silence; delay 300 ms.
    const auto in = source(SourceKind::burst, 1.3, sr, -6.0f, 1000.0, 0.1, 10.0);
    const float inDb = rmsDbIn(in, 0, 0.02, 0.08, sr);

    SECTION("50 %: each repeat is 6 dB down")
    {
        auto out = renderWith(
            in, sr,
            delayClean + Overrides{{ParamID::delayTime, "300"}, {ParamID::delayFeedback, "50"}});
        CHECK(rmsDbIn(out, 0, 0.32, 0.38, sr) == Approx(inDb).margin(0.3));
        CHECK(rmsDbIn(out, 0, 0.62, 0.68, sr) == Approx(inDb - 6.02f).margin(0.3));
        CHECK(rmsDbIn(out, 0, 0.92, 0.98, sr) == Approx(inDb - 12.04f).margin(0.4));
        CHECK(rmsDbIn(out, 0, 1.22, 1.28, sr) == Approx(inDb - 18.06f).margin(0.5));
    }
    SECTION("0 %: a single echo")
    {
        auto out = renderWith(
            in, sr,
            delayClean + Overrides{{ParamID::delayTime, "300"}, {ParamID::delayFeedback, "0"}});
        CHECK(rmsDbIn(out, 0, 0.32, 0.38, sr) == Approx(inDb).margin(0.3));
        CHECK(peakIn(out, 0, toSample(0.42, sr), out.getNumSamples()) ==
              Approx(0.0f).margin(1e-6f));
    }
    SECTION("100 % is clamped: a steady tone through the loop never grows without bound")
    {
        const auto tone = source(SourceKind::sine, 6.0, sr, -6.0f);
        auto out = renderWith(
            tone, sr,
            delayClean + Overrides{{ParamID::delayTime, "50"}, {ParamID::delayFeedback, "100"}});
        const float inPeak = tone.getMagnitude(0, 0, tone.getNumSamples());
        const float outPeak = out.getMagnitude(0, 0, out.getNumSamples());
        // Geometric series with the clamped coefficient: ≤ 1 / (1 − maxFeedback).
        const float bound = inPeak / (1.0f - dsp::DigitalDelay::maxFeedback);
        INFO("out/in peak = " << outPeak / inPeak << " (bound " << bound / inPeak << ")");
        CHECK(outPeak <= bound * 1.01f);
        // And still decays once the input stops: the tail reported is honoured (≥ 55 dB down).
        auto burst = source(SourceKind::burst, 0.5, sr, -6.0f, 1000.0, 0.5, 10.0);
        auto withTail = renderWith(
            burst, sr,
            delayClean + Overrides{{ParamID::delayTime, "50"}, {ParamID::delayFeedback, "100"}},
            512, {}, std::nullopt, true);
        const int n = withTail.getNumSamples();
        CHECK(withTail.getMagnitude(0, n - toSample(0.05, sr), toSample(0.05, sr)) <
              withTail.getMagnitude(0, 0, n) * std::pow(10.0f, -55.0f / 20.0f));
    }
}

TEST_CASE("delay: feedback filters shape the repeats but not the first echo", "[delay][filters]")
{
    const double sr = 48000.0;
    const Overrides base = {{ParamID::mix, "100"},
                            {ParamID::reverbBypass, "on"},
                            {ParamID::delayDuckEnable, "off"},
                            {ParamID::reverbDuckEnable, "off"},
                            {ParamID::delayMod, "0"},
                            {ParamID::delayTime, "200"},
                            {ParamID::delayFeedback, "70"}};

    SECTION("low cut: a 100 Hz repeat drops by ≥ 30 dB with the HPF at 1 kHz")
    {
        const auto in = source(SourceKind::burst, 0.7, sr, -6.0f, 100.0, 0.1, 10.0);
        auto open = renderWith(
            in, sr,
            base + Overrides{{ParamID::delayLowCut, "20"}, {ParamID::delayHighCut, "20000"}});
        auto cut = renderWith(
            in, sr,
            base + Overrides{{ParamID::delayLowCut, "1000"}, {ParamID::delayHighCut, "20000"}});
        // First echo (0.2–0.3 s) is unfiltered in both …
        CHECK(rmsDbIn(cut, 0, 0.22, 0.28, sr) ==
              Approx(rmsDbIn(open, 0, 0.22, 0.28, sr)).margin(0.1));
        // … the second (0.4–0.5 s) has been through the loop filters once.
        const float drop = rmsDbIn(open, 0, 0.42, 0.48, sr) - rmsDbIn(cut, 0, 0.42, 0.48, sr);
        INFO("drop " << drop << " dB");
        CHECK(drop >= 30.0f);
    }
    SECTION("high cut: a 10 kHz repeat drops by ≥ 30 dB with the LPF at 1 kHz")
    {
        const auto in = source(SourceKind::burst, 0.7, sr, -6.0f, 10000.0, 0.1, 10.0);
        auto open = renderWith(
            in, sr,
            base + Overrides{{ParamID::delayLowCut, "20"}, {ParamID::delayHighCut, "20000"}});
        auto cut = renderWith(
            in, sr,
            base + Overrides{{ParamID::delayLowCut, "20"}, {ParamID::delayHighCut, "1000"}});
        CHECK(rmsDbIn(cut, 0, 0.22, 0.28, sr) ==
              Approx(rmsDbIn(open, 0, 0.22, 0.28, sr)).margin(0.1));
        const float drop = rmsDbIn(open, 0, 0.42, 0.48, sr) - rmsDbIn(cut, 0, 0.42, 0.48, sr);
        INFO("drop " << drop << " dB");
        CHECK(drop >= 30.0f);
    }
    SECTION("defaults pass 1 kHz through the loop within 0.5 dB per repeat")
    {
        const auto in = source(SourceKind::burst, 0.7, sr, -6.0f, 1000.0, 0.1, 10.0);
        auto out = renderWith(in, sr, base);
        const float first = rmsDbIn(out, 0, 0.22, 0.28, sr);
        const float second = rmsDbIn(out, 0, 0.42, 0.48, sr);
        CHECK(first - second == Approx(-20.0f * std::log10(0.7f)).margin(0.5));
    }
}

// ---- Time changes ----------------------------------------------------------------------

TEST_CASE("delay: time changes are click-free (crossfade, no pitch artefact)", "[delay][crossfade]")
{
    const double sr = 48000.0;
    const auto in = source(SourceKind::sine, 3.0, sr);
    const float ownStep = maxSampleStep(in.getReadPointer(0), 1, in.getNumSamples());
    const float inPeak = in.getMagnitude(0, 0, in.getNumSamples());

    SECTION("a single jump 100 → 1000 ms")
    {
        auto out = renderWith(
            in, sr,
            delayClean + Overrides{{ParamID::delayTime, "100"}, {ParamID::delayFeedback, "0"}}, 64,
            {{1.0, ParamID::delayTime, "1000"}});
        const float* y = out.getReadPointer(0);
        const float step = maxSampleStep(y, toSample(0.99, sr), toSample(2.5, sr));
        INFO("max step " << step << " vs signal's own " << ownStep);
        CHECK(step < ownStep + 0.1f);
        // The old echo is gone and the new one present.
        CHECK(peakDbIn(out, 0, 2.0, 3.0, sr) == Approx(-6.0f).margin(0.2));
    }
    SECTION("a continuous sweep 100 → 1000 ms over 1.5 s, feedback 0 and 50 %")
    {
        std::vector<AutomationPoint> sweep;
        const int steps = 75; // a new value every 20 ms
        for (int i = 0; i <= steps; ++i)
        {
            const double t = 0.5 + 1.5 * i / steps;
            const double ms = 100.0 * std::pow(10.0, static_cast<double>(i) / steps);
            sweep.push_back({t, ParamID::delayTime, num(ms)});
        }
        for (const char* fb : {"0", "50"})
        {
            INFO("feedback " << fb);
            auto out = renderWith(
                in, sr,
                delayClean + Overrides{{ParamID::delayTime, "100"}, {ParamID::delayFeedback, fb}},
                64, sweep);
            const float* y = out.getReadPointer(0);
            const float outPeak = out.getMagnitude(0, 0, out.getNumSamples());
            const float step = maxSampleStep(y, toSample(0.4, sr), out.getNumSamples());
            // The signal's own step scales with its level (feedback sums copies of the sine).
            const float allowed = ownStep * std::max(1.0f, outPeak / inPeak) + 0.1f;
            INFO("max step " << step << " allowed " << allowed);
            CHECK(step < allowed);
        }
    }
    SECTION("a naive jump would click: the crossfade is what keeps it clean")
    {
        // Sanity check on the measure itself: splice two differently delayed sines by hand.
        std::vector<float> spliced(static_cast<size_t>(toSample(1.0, sr)));
        const float* x = in.getReadPointer(0);
        for (int i = 0; i < static_cast<int>(spliced.size()); ++i)
            spliced[static_cast<size_t>(i)] = i < toSample(0.5, sr) ? x[i + 100] : x[i + 137];
        CHECK(maxSampleStep(spliced.data(), 1, static_cast<int>(spliced.size())) > ownStep + 0.1f);
    }
}

TEST_CASE("delay: the crossfade takes 30 ms and later changes wait for it", "[delay][crossfade]")
{
    const double sr = 48000.0;
    dsp::DigitalDelay d;
    d.prepare(sr, 64);
    dsp::DigitalDelay::Params p;
    p.timeMs = 100.0f;
    p.feedback = 0.0f;
    p.modDepth = 0.0f;
    d.setParams(p);
    d.reset();
    std::vector<float> l(64, 0.0f), r(64, 0.0f);
    d.process(l.data(), r.data(), 64);
    CHECK_FALSE(d.isCrossfading());

    p.timeMs = 500.0f;
    d.setParams(p);
    d.process(l.data(), r.data(), 64);
    CHECK(d.isCrossfading());
    int blocks = 1;
    while (d.isCrossfading() && blocks < 100)
    {
        d.process(l.data(), r.data(), 64);
        ++blocks;
    }
    const double ms = blocks * 64 * 1000.0 / sr;
    INFO("crossfade completed after " << ms << " ms");
    CHECK(ms == Approx(dsp::DigitalDelay::crossfadeMs).margin(1.5));
}

// ---- Modulation ------------------------------------------------------------------------

TEST_CASE("delay: modulation wobbles the read position by the expected pitch deviation",
          "[delay][mod]")
{
    const double sr = 48000.0, freq = 1000.0;
    const auto in = source(SourceKind::sine, 3.0, sr, -6.0f, freq);
    const Overrides base = {{ParamID::mix, "100"},
                            {ParamID::reverbBypass, "on"},
                            {ParamID::delayDuckEnable, "off"},
                            {ParamID::reverbDuckEnable, "off"},
                            {ParamID::delayTime, "100"},
                            {ParamID::delayFeedback, "0"},
                            {ParamID::delayModRate, "2"}};

    // Peak deviation = 2π · rate · depth (depth in seconds): 2π · 2 Hz · 2 ms = 2.51 %.
    const double full = 2.0 * std::numbers::pi * 2.0 * dsp::DigitalDelay::maxModMs * 1.0e-3;

    auto off = renderWith(in, sr, base + Overrides{{ParamID::delayMod, "0"}});
    auto half = renderWith(in, sr, base + Overrides{{ParamID::delayMod, "50"}});
    auto on = renderWith(in, sr, base + Overrides{{ParamID::delayMod, "100"}});

    const double devOff = maxPitchDeviation(off, 0, 1.0, 3.0, sr, freq);
    const double devHalf = maxPitchDeviation(half, 0, 1.0, 3.0, sr, freq);
    const double devOn = maxPitchDeviation(on, 0, 1.0, 3.0, sr, freq);
    INFO("deviation off " << devOff * 100 << " %, half " << devHalf * 100 << " %, full "
                          << devOn * 100 << " % (expected " << full * 100 << " %)");
    CHECK(devOff < 0.001);
    CHECK(devOn == Approx(full).epsilon(0.15));
    CHECK(devHalf == Approx(0.5 * full).epsilon(0.2));

    // Right channel is modulated in quadrature: same depth, different phase.
    const double devOnR = maxPitchDeviation(on, 1, 1.0, 3.0, sr, freq);
    CHECK(devOnR == Approx(full).epsilon(0.15));
    float maxDiff = 0.0f;
    for (int i = toSample(1.0, sr); i < toSample(1.1, sr); ++i)
        maxDiff = std::max(maxDiff, std::abs(on.getSample(0, i) - on.getSample(1, i)));
    CHECK(maxDiff > 0.05f);
}

// ---- Stereo / ping-pong ----------------------------------------------------------------

TEST_CASE("delay: ping-pong alternates channels on successive echoes, stereo does not",
          "[delay][pingpong]")
{
    const double sr = 48000.0;
    const auto in = source(SourceKind::impulse, 0.75, sr);
    const Overrides base =
        delayClean + Overrides{{ParamID::delayTime, "200"}, {ParamID::delayFeedback, "50"}};
    auto peakAt = [&](const juce::AudioBuffer<float>& b, int ch, double seconds)
    { return peakDbIn(b, ch, seconds - 0.005, seconds + 0.02, sr); };

    SECTION("PingPong")
    {
        auto out = renderWith(in, sr, base + Overrides{{ParamID::delayStereoMode, "PingPong"}});
        // Echo 1: left only. Echo 2: right only. Echo 3: left only. (Repeats have been
        // through the loop filters, which spread the impulse, so their peaks sit a few dB
        // below the nominal −12 / −18 dB.)
        CHECK(peakAt(out, 0, 0.2) == Approx(-6.0f).margin(0.1));
        CHECK(peakAt(out, 1, 0.2) < -80.0f);
        CHECK(peakAt(out, 1, 0.4) > -20.0f);
        CHECK(peakAt(out, 0, 0.4) < -80.0f);
        CHECK(peakAt(out, 0, 0.6) > -28.0f);
        CHECK(peakAt(out, 1, 0.6) < -80.0f);
    }
    SECTION("Stereo")
    {
        auto out = renderWith(in, sr, base + Overrides{{ParamID::delayStereoMode, "Stereo"}});
        for (double t : {0.2, 0.4, 0.6})
        {
            CHECK(peakAt(out, 0, t) > -28.0f);
            CHECK(peakAt(out, 0, t) == Approx(peakAt(out, 1, t)).margin(0.01));
        }
    }
    SECTION("a mono-only left input in Stereo mode stays on the left")
    {
        juce::AudioBuffer<float> leftOnly(in);
        leftOnly.clear(1, 0, leftOnly.getNumSamples());
        auto out = renderWith(leftOnly, sr, base + Overrides{{ParamID::delayStereoMode, "Stereo"}});
        CHECK(peakAt(out, 0, 0.2) == Approx(-6.0f).margin(0.1));
        CHECK(out.getMagnitude(1, 0, out.getNumSamples()) == Approx(0.0f).margin(0.0f));
    }
    SECTION("switching modes mid-stream is smooth")
    {
        const auto tone = source(SourceKind::sine, 2.0, sr);
        const float ownStep = maxSampleStep(tone.getReadPointer(0), 1, tone.getNumSamples());
        auto out = renderWith(tone, sr, base + Overrides{{ParamID::delayStereoMode, "Stereo"}}, 64,
                              {{1.0, ParamID::delayStereoMode, "PingPong"}});
        const float gain = out.getMagnitude(0, 0, out.getNumSamples()) /
                           tone.getMagnitude(0, 0, tone.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            CHECK(maxSampleStep(out.getReadPointer(ch), toSample(0.9, sr), toSample(1.5, sr)) <
                  ownStep * std::max(1.0f, gain) + 0.05f);
    }
}

// ---- Tempo sync ------------------------------------------------------------------------

TEST_CASE("delay: note lengths", "[delay][sync]")
{
    using E = dsp::DelayEngine;
    // Index order: 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1/1; Straight, Dotted, Triplet.
    CHECK(E::noteLengthSeconds(120.0, 4, 0) == Approx(0.5));
    CHECK(E::noteLengthSeconds(120.0, 3, 0) == Approx(0.25));
    CHECK(E::noteLengthSeconds(120.0, 3, 1) == Approx(0.375));
    CHECK(E::noteLengthSeconds(120.0, 4, 2) == Approx(1.0 / 3.0));
    CHECK(E::noteLengthSeconds(120.0, 0, 0) == Approx(0.03125));
    CHECK(E::noteLengthSeconds(120.0, 6, 0) == Approx(2.0));
    CHECK(E::noteLengthSeconds(90.0, 4, 0) == Approx(60.0 / 90.0));
    CHECK(E::noteLengthSeconds(0.0, 4, 0) == Approx(60.0 / E::fallbackBpm)); // no tempo
    CHECK(E::noteLengthSeconds(120.0, 99, 7) == Approx(2.0 * 2.0 / 3.0));    // clamped indices

    CHECK(E::clampTimeMs(E::Mode::digital, 0.1f) == Approx(1.0f));
    CHECK(E::clampTimeMs(E::Mode::digital, 4000.0f) == Approx(2000.0f));
    CHECK(E::clampTimeMs(E::Mode::bbd, 5.0f) == Approx(20.0f));
    CHECK(E::clampTimeMs(E::Mode::bbd, 1500.0f) == Approx(1000.0f));
    CHECK(E::clampTimeMs(E::Mode::tape, 1500.0f) == Approx(1500.0f));
}

TEST_CASE("delay: sync follows the host playhead", "[delay][sync]")
{
    const double sr = 48000.0;
    const auto in = source(SourceKind::impulse, 1.2, sr);
    const Overrides synced = delayClean + Overrides{{ParamID::delayFeedback, "0"},
                                                    {ParamID::delaySync, "on"},
                                                    {ParamID::delayTime, "37"}}; // must be ignored

    struct Case
    {
        std::optional<double> bpm;
        const char* note;
        const char* mod;
        double expectedSeconds;
    };
    const Case cases[] = {
        {120.0, "1/8", "Straight", 0.25},
        {120.0, "1/8", "Dotted", 0.375},
        {120.0, "1/4", "Triplet", 1.0 / 3.0},
        {90.0, "1/4", "Straight", 60.0 / 90.0},
        {140.0, "1/16", "Straight", 60.0 / 140.0 / 4.0},
        {std::nullopt, "1/8", "Straight", 0.25}, // no playhead → 120 BPM fallback
    };
    for (const auto& c : cases)
    {
        INFO((c.bpm ? std::to_string(*c.bpm) : "no") << " BPM " << c.note << " " << c.mod);
        auto out = renderWith(
            in, sr,
            synced + Overrides{{ParamID::delayNote, c.note}, {ParamID::delayNoteMod, c.mod}}, 512,
            {}, c.bpm);
        const int expected = toSample(c.expectedSeconds, sr);
        const double arrival = echoPositionSamples(out, 0, expected - 8, expected + 8);
        CHECK(std::abs(arrival - c.expectedSeconds * sr) <= 1.0);
    }

    SECTION("sync off uses delayTime even with a playhead")
    {
        auto out = renderWith(
            in, sr,
            delayClean + Overrides{{ParamID::delayFeedback, "0"}, {ParamID::delayTime, "37"}}, 512,
            {}, 120.0);
        const int first = firstAbove(out.getReadPointer(0), 0, out.getNumSamples(), 1.0e-4f);
        CHECK(std::abs(first - toSample(0.037, sr)) <= 1);
    }
    SECTION("a whole note at slow tempo clamps to the 2 s maximum")
    {
        const auto longIn = source(SourceKind::impulse, 2.3, sr);
        auto out =
            renderWith(longIn, sr, synced + Overrides{{ParamID::delayNote, "1/1"}}, 512, {}, 60.0);
        const int first = firstAbove(out.getReadPointer(0), 0, out.getNumSamples(), 1.0e-4f);
        CHECK(std::abs(first - toSample(2.0, sr)) <= 1);
    }
    SECTION("the processor reports the effective time and tempo")
    {
        RenderSettings s;
        s.sampleRate = sr;
        s.parameterOverrides =
            synced + Overrides{{ParamID::delayNote, "1/8"}, {ParamID::delayNoteMod, "Dotted"}};
        s.bpm = 100.0;
        ClearSpaceProcessor p;
        for (const auto& [id, value] : s.parameterOverrides)
            REQUIRE(applyParameterOverride(p.getAPVTS(), id, value).empty());
        // Before any block: fallback tempo.
        CHECK(p.getHostBpm() == Approx(dsp::DelayEngine::fallbackBpm));
        auto out = renderThroughProcessor(in, s); // its own processor; just proves the path
        REQUIRE(out.ok());
        CHECK(firstAbove(out.buffer.getReadPointer(0), 0, out.buffer.getNumSamples(), 1.0e-4f) ==
              Approx(toSample(0.375 * 120.0 / 100.0, sr)).margin(1));
    }
}

// ---- Safety and cost -------------------------------------------------------------------

TEST_CASE("delay: silence in → silence out, no NaN/Inf at any rate or block size", "[delay][nan]")
{
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int block : {1, 7, 64, 512, 4096})
            for (const char* time : {"1", "2000"})
                for (const char* mode : {"Stereo", "PingPong"})
                {
                    INFO("sr " << sr << " block " << block << " time " << time << " " << mode);
                    const Overrides hot = {{ParamID::mix, "100"},
                                           {ParamID::reverbBypass, "on"},
                                           {ParamID::delayTime, time},
                                           {ParamID::delayFeedback, "100"},
                                           {ParamID::delayMod, "100"},
                                           {ParamID::delayModRate, "10"},
                                           {ParamID::delayStereoMode, mode},
                                           {ParamID::delayLowCut, "2000"},
                                           {ParamID::delayHighCut, "1000"}};
                    const auto in = source(SourceKind::noise, 0.25, sr, -3.0f);
                    auto out = renderWith(in, sr, hot, block);
                    CHECK_FALSE(hasNonFinite(out));
                    CHECK(out.getMagnitude(0, 0, out.getNumSamples()) < 60.0f);

                    juce::AudioBuffer<float> silence(2, toSample(0.25, sr));
                    silence.clear();
                    auto quiet = renderWith(silence, sr, hot, block);
                    CHECK(quiet.getMagnitude(0, 0, quiet.getNumSamples()) ==
                          Approx(0.0f).margin(0.0f));
                    CHECK(quiet.getMagnitude(1, 0, quiet.getNumSamples()) ==
                          Approx(0.0f).margin(0.0f));
                }
}

TEST_CASE("cpu: delay section alone (48 kHz / 512, mod on, ping-pong)", "[cpu][delay]")
{
    const double sr = 48000.0;
    const double seconds = 20.0;
    const auto in = source(SourceKind::speechlike, seconds, sr);
    const Overrides cfg = {{ParamID::mix, "100"},
                           {ParamID::reverbBypass, "on"},
                           {ParamID::delayMod, "50"},
                           {ParamID::delayStereoMode, "PingPong"}};
    const auto t0 = std::chrono::steady_clock::now();
    auto out = renderWith(in, sr, cfg);
    const auto t1 = std::chrono::steady_clock::now();
    const double ratio = std::chrono::duration<double>(t1 - t0).count() / seconds;
    std::cout << "cpu: delay + ducker, " << seconds << " s rendered → " << ratio * 100.0
              << " % of one core\n";
#ifdef NDEBUG
    CHECK(ratio < 0.03);
#else
    WARN("cpu check not enforced in a Debug build (" << ratio * 100.0 << " %)");
#endif
}
