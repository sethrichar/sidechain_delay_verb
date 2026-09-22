// Phase 3 acceptance (BUILD_PLAN Phase 3, CLAUDE.md §6): the Plate reverb (RT60 tracks
// reverbDecay, no metallic ringing, stereo decorrelation, level sanity) and the ReverbEngine
// wrapper (pre-delay, low/high cut, width, mode clamp, tail) — measured through
// ClearSpaceProcessor via the render library, plus direct unit tests on the DSP classes.

#include "Measure.h"
#include "Parameters.h"
#include "Renderer.h"
#include "Sources.h"
#include "dsp/Biquad.h"
#include "dsp/reverb/PlateReverb.h"
#include "dsp/reverb/ReverbEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

using namespace clearspace;
using namespace clearspace::render;
using namespace clearspace::test;
using Catch::Approx;

namespace
{

using Overrides = std::vector<std::pair<std::string, std::string>>;
using Mode = dsp::ReverbEngine::Mode;

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
                                    const Overrides& overrides, int block = 512, bool tail = false,
                                    const std::vector<AutomationPoint>& automation = {})
{
    RenderSettings s;
    s.sampleRate = sr;
    s.blockSize = block;
    s.parameterOverrides = overrides;
    s.automation = automation;
    s.appendTail = tail;
    auto out = renderThroughProcessor(input, s);
    for (const auto& e : out.errors)
        FAIL("render error: " << e.message);
    REQUIRE(out.ok());
    REQUIRE_FALSE(hasNonFinite(out.buffer));
    return std::move(out.buffer);
}

// Wet-only reverb, no ducking, no delay, no pre-delay, everything else at its default.
const Overrides reverbOnly = {{ParamID::mix, "100"},
                              {ParamID::delayBypass, "on"},
                              {ParamID::delayDuckEnable, "off"},
                              {ParamID::reverbDuckEnable, "off"},
                              {ParamID::reverbPreDelay, "0"}};

/** Pearson correlation between the two channels over [start, end). */
double correlation(const juce::AudioBuffer<float>& b, int start, int end)
{
    const float* l = b.getReadPointer(0);
    const float* r = b.getReadPointer(1);
    double sl = 0, sr2 = 0, sll = 0, srr = 0, slr = 0;
    const int n = end - start;
    for (int i = start; i < end; ++i)
    {
        sl += l[i];
        sr2 += r[i];
        sll += static_cast<double>(l[i]) * l[i];
        srr += static_cast<double>(r[i]) * r[i];
        slr += static_cast<double>(l[i]) * r[i];
    }
    const double cov = slr / n - (sl / n) * (sr2 / n);
    const double vl = sll / n - (sl / n) * (sl / n);
    const double vr = srr / n - (sr2 / n) * (sr2 / n);
    if (vl <= 0.0 || vr <= 0.0)
        return 0.0;
    return cov / std::sqrt(vl * vr);
}

/** Normalised autocorrelation of x[start, end) at one lag (1 = the segment repeats itself
    exactly at that lag). Normalised by the two overlapping energies so a decay does not bias
    it. */
double autocorrelationAt(const float* x, int start, int end, int lag)
{
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = start; i + lag < end; ++i)
    {
        const double a = x[i], b = x[i + lag];
        sxy += a * b;
        sxx += a * a;
        syy += b * b;
    }
    return (sxx > 0.0 && syy > 0.0) ? std::abs(sxy) / std::sqrt(sxx * syy) : 0.0;
}

/** Largest normalised autocorrelation over lags [minLag, maxLag]: a single echo pair. */
double maxAutocorrelation(const float* x, int start, int end, int minLag, int maxLag)
{
    double worst = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        worst = std::max(worst, autocorrelationAt(x, start, end, lag));
    return worst;
}

/** Metallic-ringing measure: a comb / flutter repeats at a period AND its multiples, so this
    is the largest min(r(τ), r(2τ)) over periods τ in [minLag, maxLag]. A lone echo pair (two
    output taps on one delay line, which the Dattorro plate has by design) scores ~0 because
    nothing sits at 2τ; a comb with amplitude ratio a per period scores ≈ a² × its energy share. */
double periodicity(const float* x, int start, int end, int minLag, int maxLag)
{
    double worst = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        worst = std::max(worst, std::min(autocorrelationAt(x, start, end, lag),
                                         autocorrelationAt(x, start, end, 2 * lag)));
    return worst;
}

/** RT60 of the band below `cutoffHz` (4th-order Butterworth low-pass on a copy), i.e. the
    part of the spectrum the tank damping does not reach. */
double lowBandRT60(const juce::AudioBuffer<float>& ir, int channel, double sr, double cutoffHz)
{
    std::vector<float> band(ir.getReadPointer(channel),
                            ir.getReadPointer(channel) + ir.getNumSamples());
    for (int pass = 0; pass < 2; ++pass)
    {
        dsp::Biquad lpf;
        lpf.setLowpass(sr, cutoffHz);
        lpf.process(band.data(), static_cast<int>(band.size()));
    }
    return schroederRT60(band.data(), static_cast<int>(band.size()), sr);
}

juce::AudioBuffer<float> impulseResponse(double sr, double seconds, const Overrides& extra,
                                         int block = 512)
{
    const auto in = source(SourceKind::impulse, seconds, sr, 0.0f);
    return renderWith(in, sr, reverbOnly + extra, block);
}

/** Samples in [start, end) within `windowDb` of the window's own peak. */
int countNearPeak(const float* x, int start, int end, float windowDb)
{
    float peak = 0.0f;
    for (int i = start; i < end; ++i)
        peak = std::max(peak, std::abs(x[i]));
    const float threshold = peak * std::pow(10.0f, windowDb / 20.0f);
    int count = 0;
    for (int i = start; i < end; ++i)
        if (std::abs(x[i]) > threshold)
            ++count;
    return count;
}

} // namespace

// -------------------------------------------------------------------------------------------
// Unit tests on the DSP classes
// -------------------------------------------------------------------------------------------

TEST_CASE("plate: decay mapping, allpass caps, size coupling and tail are consistent",
          "[reverb][unit]")
{
    using P = dsp::PlateReverb;

    SECTION("the loop round trip is the paper's lengths over 29.761 kHz")
    {
        // 672 + 4453 + 1800 + 3720 + 908 + 4217 + 2656 + 3163 = 21589 samples.
        CHECK(P::loopSecondsFor(1.0f) == Approx(21589.0 / 29761.0).epsilon(1e-9));
        CHECK(P::stageSecondsFor(1.0f) == Approx(P::loopSecondsFor(1.0f) / 4.0));
        CHECK(P::loopSecondsFor(0.5f) == Approx(0.5 * P::loopSecondsFor(1.0f)));
    }

    SECTION("decay gain rises with the decay time and never reaches the ceiling")
    {
        float last = 0.0f;
        for (float decay : {0.1f, 0.3f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f})
        {
            const float g = P::decayGainFor(decay, P::stageSecondsFor(P::maxSizeScale));
            CHECK(g > last);
            CHECK(g < P::maxDecayGain);
            last = g;
        }
        // 10^(−3·stage·cal / RT60) at the default size.
        const double stage = P::stageSecondsFor(1.0f);
        CHECK(P::decayGainFor(2.0f, stage) ==
              Approx(std::pow(10.0, -3.0 * stage * P::decayCalibration / 2.0)).epsilon(1e-6));
    }

    SECTION("allpass coefficients keep their nominal value unless their ring-out would outlast "
            "half the decay")
    {
        // 2656 samples at 29.761 kHz = 89 ms; at k = 0.5 it rings ~0.9 s to −60 dB.
        const double length = 2656.0 / 29761.0;
        CHECK(P::allpassCoefficientFor(0.5f, length, 5.0f) == Approx(0.5f));
        CHECK(P::allpassCoefficientFor(0.5f, length, 2.0f) == Approx(0.5f));
        const float capped = P::allpassCoefficientFor(0.5f, length, 0.5f);
        CHECK(capped < 0.5f);
        CHECK(capped == Approx(std::pow(10.0, -3.0 * length / (P::allpassRingoutFraction * 0.5))));
        // Sign is preserved for the negative tank coefficient.
        CHECK(P::allpassCoefficientFor(-0.7f, 672.0 / 29761.0, 0.3f) < 0.0f);
        CHECK(P::allpassCoefficientFor(-0.7f, 672.0 / 29761.0, 10.0f) == Approx(-0.7f));
    }

    SECTION("size maps 0…1 → 0.5×…1.5× and short decays shrink the tank")
    {
        CHECK(P::sizeScaleFor(0.0f) == Approx(0.5f));
        CHECK(P::sizeScaleFor(0.5f) == Approx(1.0f));
        CHECK(P::sizeScaleFor(1.0f) == Approx(1.5f));
        CHECK(P::effectiveSizeScale(2.0f, 1.0f) == Approx(1.5f));
        CHECK(P::effectiveSizeScale(1.0f, 1.0f) == Approx(1.5f));
        CHECK(P::effectiveSizeScale(0.3f, 1.0f) == Approx(0.5f));
        CHECK(P::effectiveSizeScale(0.3f, 0.0f) == Approx(0.5f));
        const float mid = P::effectiveSizeScale(0.5f, 1.0f);
        CHECK(mid > 0.5f);
        CHECK(mid < 1.5f);
        CHECK(P::loopSecondsFor(mid) == Approx(P::maxLoopToDecayRatio * 0.5).epsilon(1e-5));
    }

    SECTION("tail = round trip + 1.25 × decay, and the engine adds the pre-delay")
    {
        CHECK(P::tailSecondsFor(2.0f, 0.5f) == Approx(P::loopSecondsFor(1.0f) + 2.5));
        CHECK(P::tailSecondsFor(0.3f, 1.0f) == Approx(P::loopSecondsFor(0.5f) + 0.375));
        CHECK(dsp::ReverbEngine::tailSecondsFor(Mode::plate, 2.0f, 20.0f, 0.5f) ==
              Approx(0.02 + P::tailSecondsFor(2.0f, 0.5f)));
        CHECK(dsp::ReverbEngine::tailSecondsFor(Mode::plate, 2.0f, 500.0f, 0.5f) ==
              Approx(0.25 + P::tailSecondsFor(2.0f, 0.5f))); // pre-delay clamps at 250 ms
    }

    SECTION("Room clamps the decay to 2.5 s, Plate and Hall do not")
    {
        CHECK(dsp::ReverbEngine::clampDecaySeconds(Mode::room, 10.0f) == Approx(2.5f));
        CHECK(dsp::ReverbEngine::clampDecaySeconds(Mode::room, 1.0f) == Approx(1.0f));
        CHECK(dsp::ReverbEngine::clampDecaySeconds(Mode::plate, 10.0f) == Approx(10.0f));
        CHECK(dsp::ReverbEngine::clampDecaySeconds(Mode::hall, 20.0f) == Approx(20.0f));
        CHECK(dsp::ReverbEngine::clampDecaySeconds(Mode::plate, 0.01f) == Approx(0.1f));
        CHECK(dsp::ReverbEngine::tailSecondsFor(Mode::room, 10.0f, 0.0f, 0.5f) ==
              Approx(P::tailSecondsFor(2.5f, 0.5f)));
    }
}

// -------------------------------------------------------------------------------------------
// Acceptance, through the processor
// -------------------------------------------------------------------------------------------

TEST_CASE("reverb: RT60 tracks reverbDecay within ±15 % across 0.3–10 s and every size",
          "[reverb][rt60]")
{
    const double sr = 48000.0;
    for (float size : {0.0f, 50.0f, 100.0f})
        for (float decay : {0.3f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f})
        {
            INFO("size " << size << " %, decay " << decay << " s");
            const auto ir = impulseResponse(
                sr, 2.0 * decay + 1.5,
                {{ParamID::reverbDecay, num(decay)}, {ParamID::reverbSize, num(size)}});
            for (int ch = 0; ch < 2; ++ch)
            {
                const double rt60 = schroederRT60(ir.getReadPointer(ch), ir.getNumSamples(), sr);
                INFO("channel " << ch << " measured " << rt60 << " s");
                CHECK(rt60 == Approx(decay).epsilon(0.15));
            }
        }
}

TEST_CASE("reverb: RT60 holds at 44.1 / 96 kHz and at the damping extremes", "[reverb][rt60]")
{
    for (double sr : {44100.0, 96000.0})
    {
        INFO(sr << " Hz");
        const auto ir = impulseResponse(sr, 5.5, {{ParamID::reverbDecay, "2"}});
        CHECK(schroederRT60(ir.getReadPointer(0), ir.getNumSamples(), sr) ==
              Approx(2.0).epsilon(0.15));
        CHECK(schroederRT60(ir.getReadPointer(1), ir.getNumSamples(), sr) ==
              Approx(2.0).epsilon(0.15));
    }
    // Damping shortens the highs by design, so the broadband figure reads shorter on a dark
    // plate; what must hold is that the band the damping does not reach (< 300 Hz) decays at
    // the same rate whether the damping is open or closed. (That low band rings ~20 % longer
    // than the broadband RT60 at every setting — the plate's own character, ADR-0005.)
    const double sr = 48000.0;
    const auto bright =
        impulseResponse(sr, 5.5, {{ParamID::reverbDecay, "2"}, {ParamID::reverbDamping, "20000"}});
    const auto dark =
        impulseResponse(sr, 5.5, {{ParamID::reverbDecay, "2"}, {ParamID::reverbDamping, "1000"}});
    const double brightBroadband =
        schroederRT60(bright.getReadPointer(0), bright.getNumSamples(), sr);
    const double darkBroadband = schroederRT60(dark.getReadPointer(0), dark.getNumSamples(), sr);
    const double brightLow = lowBandRT60(bright, 0, sr, 300.0);
    const double darkLow = lowBandRT60(dark, 0, sr, 300.0);
    INFO("broadband: open " << brightBroadband << " s, dark " << darkBroadband
                            << " s; low band: open " << brightLow << " s, dark " << darkLow
                            << " s");
    CHECK(brightBroadband == Approx(2.0).epsilon(0.15));
    CHECK(darkBroadband < brightBroadband);
    CHECK(darkLow == Approx(brightLow).epsilon(0.10));
    CHECK(brightLow == Approx(2.0).epsilon(0.30));
}

TEST_CASE("reverb: no metallic ringing — the late response has no repeating structure",
          "[reverb][ringing]")
{
    const double sr = 48000.0;
    const int from = toSample(0.1, sr);
    const int to = toSample(0.6, sr);
    const int minLag = toSample(0.001, sr);
    const int maxLag = toSample(0.075, sr); // 2τ stays inside the 150 ms the window resolves

    SECTION("the measure catches combs (sanity checks on the test itself)")
    {
        // One decaying impulse train at 20 ms: a textbook metallic ring.
        std::vector<float> comb(static_cast<size_t>(to), 0.0f);
        const int period = toSample(0.02, sr);
        for (int i = 0; i < to; i += period)
            comb[static_cast<size_t>(i)] = std::pow(0.85f, static_cast<float>(i / period));
        CHECK(periodicity(comb.data(), from, to, minLag, maxLag) > 0.5);

        // Four combs at the Phase 1 stand-in's periods, each ringing 2 s: the kind of tail the
        // stand-in produced, which Seth heard as metallic.
        std::vector<float> bank(static_cast<size_t>(to), 0.0f);
        for (double ms : {29.7, 37.1, 41.1, 43.7})
        {
            const int p = toSample(ms * 1.0e-3, sr);
            const float g = static_cast<float>(std::pow(10.0, -3.0 * ms * 1.0e-3 / 2.0));
            for (int i = 0; i < to; i += p)
                bank[static_cast<size_t>(i)] += std::pow(g, static_cast<float>(i / p));
        }
        const double bankScore = periodicity(bank.data(), from, to, minLag, maxLag);
        INFO("comb bank periodicity " << bankScore);
        CHECK(bankScore > 0.1);

        // A lone echo pair (the plate's paired taps) is not a ring: white noise plus a copy of
        // itself 50 ms later scores ~0.
        std::vector<float> pair(static_cast<size_t>(to), 0.0f);
        juce::Random rng(7);
        const int echo = toSample(0.05, sr);
        for (int i = 0; i < to; ++i)
            pair[static_cast<size_t>(i)] = rng.nextFloat() * 2.0f - 1.0f;
        for (int i = to - 1; i >= echo; --i)
            pair[static_cast<size_t>(i)] += pair[static_cast<size_t>(i - echo)];
        CHECK(maxAutocorrelation(pair.data(), from, to, minLag, maxLag) > 0.4);
        CHECK(periodicity(pair.data(), from, to, minLag, maxLag) < 0.1);
    }

    for (float modDepth : {30.0f, 0.0f})
        for (float size : {0.0f, 50.0f, 100.0f})
        {
            INFO("mod depth " << modDepth << " %, size " << size << " %");
            const auto ir = impulseResponse(sr, 0.7,
                                            {{ParamID::reverbDecay, "2"},
                                             {ParamID::reverbSize, num(size)},
                                             {ParamID::reverbModDepth, num(modDepth)}});
            for (int ch = 0; ch < 2; ++ch)
            {
                const double ring = periodicity(ir.getReadPointer(ch), from, to, minLag, maxLag);
                const double pairs =
                    maxAutocorrelation(ir.getReadPointer(ch), from, to, minLag, 2 * maxLag);
                INFO("channel " << ch << ": periodicity " << ring << ", largest single echo pair "
                                << pairs);
                CHECK(ring < 0.1);   // no repeating peak above −20 dB
                CHECK(pairs < 0.35); // the paper's paired taps sit around 0.2
            }
        }
}

TEST_CASE("reverb: stereo width — decorrelated at 100 %, mono at 0 %", "[reverb][width]")
{
    const double sr = 48000.0;
    const int n = toSample(2.0, sr);

    const auto wide = impulseResponse(sr, 2.0, {{ParamID::reverbWidth, "100"}});
    const double cWide = correlation(wide, 0, n);
    INFO("width 100 % correlation " << cWide);
    CHECK(std::abs(cWide) < 0.5);

    const auto mono = impulseResponse(sr, 2.0, {{ParamID::reverbWidth, "0"}});
    float maxDiff = 0.0f;
    for (int i = 0; i < n; ++i)
        maxDiff = std::max(maxDiff, std::abs(mono.getSample(0, i) - mono.getSample(1, i)));
    CHECK(maxDiff < 1.0e-6f);
    CHECK(mono.getMagnitude(0, 0, n) > 0.001f); // still sounding

    const auto half = impulseResponse(sr, 2.0, {{ParamID::reverbWidth, "50"}});
    const double cHalf = correlation(half, 0, n);
    INFO("width 50 % correlation " << cHalf);
    CHECK(cHalf > cWide);
    CHECK(cHalf < 1.0);
}

TEST_CASE("reverb: pre-delay shifts the onset by exactly the set time", "[reverb][predelay]")
{
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        INFO(sr << " Hz");
        const auto in = source(SourceKind::impulse, 0.6, sr, 0.0f);
        const auto base = reverbOnly + Overrides{{ParamID::reverbModDepth, "0"}};
        const auto dry = renderWith(in, sr, base + Overrides{{ParamID::reverbPreDelay, "0"}});
        const int onset0 = firstAbove(dry.getReadPointer(0), 0, dry.getNumSamples(), 1.0e-6f);
        REQUIRE(onset0 >= 0);
        for (double ms : {1.0, 20.0, 100.0, 250.0})
        {
            const auto out =
                renderWith(in, sr, base + Overrides{{ParamID::reverbPreDelay, num(ms)}});
            const int onset = firstAbove(out.getReadPointer(0), 0, out.getNumSamples(), 1.0e-6f);
            INFO(ms << " ms: onset moved by " << onset - onset0 << " samples");
            CHECK(std::abs((onset - onset0) - toSample(ms * 1.0e-3, sr)) <= 1);
        }
    }
}

TEST_CASE("reverb: pre-delay and size changes mid-stream are click-free", "[reverb][clickfree]")
{
    const double sr = 48000.0;
    const auto in = source(SourceKind::sine, 3.0, sr, -6.0f, 200.0);
    const auto base = reverbOnly + Overrides{{ParamID::reverbDecay, "1"}};
    const auto steady = renderWith(in, sr, base);
    const float steadyStep =
        maxSampleStep(steady.getReadPointer(0), toSample(0.5, sr), steady.getNumSamples());
    INFO("steady max step " << steadyStep);

    SECTION("pre-delay 0 → 250 → 0 ms")
    {
        const auto out = renderWith(
            in, sr, base, 512, false,
            {{1.0, ParamID::reverbPreDelay, "250"}, {2.0, ParamID::reverbPreDelay, "0"}});
        const float step =
            maxSampleStep(out.getReadPointer(0), toSample(0.5, sr), out.getNumSamples());
        INFO("max step " << step);
        // A glide pitch-shifts the tone for 100 ms (bigger steps), a click would be a jump.
        CHECK(step < 3.0f * steadyStep + 0.02f);
    }

    SECTION("size 0 → 100 → 0 %")
    {
        const auto out =
            renderWith(in, sr, base + Overrides{{ParamID::reverbSize, "0"}}, 512, false,
                       {{1.0, ParamID::reverbSize, "100"}, {2.0, ParamID::reverbSize, "0"}});
        const float step =
            maxSampleStep(out.getReadPointer(0), toSample(0.5, sr), out.getNumSamples());
        INFO("max step " << step);
        CHECK(step < 3.0f * steadyStep + 0.02f);
    }

    SECTION("mode Plate → Hall → Room keeps running (all Plate until Phase 4)")
    {
        const auto out =
            renderWith(in, sr, base, 512, false,
                       {{1.0, ParamID::reverbMode, "Hall"}, {2.0, ParamID::reverbMode, "Room"}});
        const float step =
            maxSampleStep(out.getReadPointer(0), toSample(0.5, sr), out.getNumSamples());
        CHECK(step < 3.0f * steadyStep + 0.02f);
        CHECK(rmsDbIn(out, 0, 2.5, 3.0, sr) ==
              Approx(rmsDbIn(steady, 0, 2.5, 3.0, sr)).margin(1.0));
    }
}

TEST_CASE("reverb: low cut and high cut shape the return", "[reverb][filters]")
{
    const double sr = 48000.0;
    const auto base = reverbOnly + Overrides{{ParamID::reverbDecay, "1"}};

    SECTION("low cut at 500 Hz drops a 40 Hz tone by ≥ 30 dB against 20 Hz")
    {
        const auto in = source(SourceKind::sine, 3.0, sr, -6.0f, 40.0);
        const auto open = renderWith(in, sr, base + Overrides{{ParamID::reverbLowCut, "20"}});
        const auto cut = renderWith(in, sr, base + Overrides{{ParamID::reverbLowCut, "500"}});
        const float drop = rmsDbIn(open, 0, 2.0, 3.0, sr) - rmsDbIn(cut, 0, 2.0, 3.0, sr);
        INFO("drop " << drop << " dB");
        CHECK(drop >= 30.0f);
    }

    SECTION("high cut at 1 kHz drops a 15 kHz tone by ≥ 30 dB against 20 kHz")
    {
        const auto in = source(SourceKind::sine, 3.0, sr, -6.0f, 15000.0);
        const auto open = renderWith(in, sr, base + Overrides{{ParamID::reverbHighCut, "20000"}});
        const auto cut = renderWith(in, sr, base + Overrides{{ParamID::reverbHighCut, "1000"}});
        const float drop = rmsDbIn(open, 0, 2.0, 3.0, sr) - rmsDbIn(cut, 0, 2.0, 3.0, sr);
        INFO("drop " << drop << " dB");
        CHECK(drop >= 30.0f);
    }

    SECTION("a 1 kHz tone passes the default cuts within 1 dB of the wide-open setting")
    {
        const auto in = source(SourceKind::sine, 3.0, sr, -6.0f, 1000.0);
        const auto open = renderWith(
            in, sr,
            base + Overrides{{ParamID::reverbLowCut, "20"}, {ParamID::reverbHighCut, "20000"}});
        const auto def = renderWith(in, sr, base);
        CHECK(rmsDbIn(def, 0, 2.0, 3.0, sr) == Approx(rmsDbIn(open, 0, 2.0, 3.0, sr)).margin(1.0));
    }
}

TEST_CASE("reverb: damping darkens the tail without touching the low end", "[reverb][damping]")
{
    const double sr = 48000.0;
    // A 1 s burst, then the tail is measured 0.3–0.8 s after it stops.
    const auto base = reverbOnly + Overrides{{ParamID::reverbDecay, "3"},
                                             {ParamID::reverbHighCut, "20000"},
                                             {ParamID::reverbLowCut, "20"}};
    const auto hf = source(SourceKind::burst, 2.0, sr, -6.0f, 8000.0, 1.0, 1.0);
    const auto lf = source(SourceKind::burst, 2.0, sr, -6.0f, 200.0, 1.0, 1.0);

    const auto hfBright = renderWith(hf, sr, base + Overrides{{ParamID::reverbDamping, "20000"}});
    const auto hfDark = renderWith(hf, sr, base + Overrides{{ParamID::reverbDamping, "1000"}});
    const float hfDrop = rmsDbIn(hfBright, 0, 1.3, 1.8, sr) - rmsDbIn(hfDark, 0, 1.3, 1.8, sr);
    INFO("8 kHz tail drop " << hfDrop << " dB");
    CHECK(hfDrop >= 15.0f);

    const auto lfBright = renderWith(lf, sr, base + Overrides{{ParamID::reverbDamping, "20000"}});
    const auto lfDark = renderWith(lf, sr, base + Overrides{{ParamID::reverbDamping, "1000"}});
    const float lfDrop = rmsDbIn(lfBright, 0, 1.3, 1.8, sr) - rmsDbIn(lfDark, 0, 1.3, 1.8, sr);
    INFO("200 Hz tail drop " << lfDrop << " dB");
    CHECK(std::abs(lfDrop) < 3.0f);
}

TEST_CASE("reverb: diffusion densifies the early response", "[reverb][diffusion]")
{
    const double sr = 48000.0;
    const int window = toSample(0.015, sr); // before the first tank tap at any size
    const auto sparse =
        impulseResponse(sr, 0.1, {{ParamID::reverbDiffusion, "0"}, {ParamID::reverbModDepth, "0"}});
    const auto dense = impulseResponse(
        sr, 0.1, {{ParamID::reverbDiffusion, "100"}, {ParamID::reverbModDepth, "0"}});
    const int sparseCount = countNearPeak(sparse.getReadPointer(0), 0, window, -40.0f);
    const int denseCount = countNearPeak(dense.getReadPointer(0), 0, window, -40.0f);
    INFO("samples within 40 dB of the window peak: diffusion 0 % → " << sparseCount << ", 100 % → "
                                                                     << denseCount);
    CHECK(denseCount > 5 * sparseCount);
    CHECK(sparseCount <= 8); // a few discrete taps (plus their Hermite neighbours)
}

TEST_CASE("reverb: modulation moves the tail and the two halves do not beat together",
          "[reverb][modulation]")
{
    const double sr = 48000.0;
    const auto still = impulseResponse(sr, 2.0, {{ParamID::reverbModDepth, "0"}});
    const auto moving =
        impulseResponse(sr, 2.0, {{ParamID::reverbModDepth, "100"}, {ParamID::reverbModRate, "5"}});
    // Same energy, different waveform.
    CHECK(rmsDbIn(moving, 0, 0.5, 2.0, sr) == Approx(rmsDbIn(still, 0, 0.5, 2.0, sr)).margin(1.5));
    juce::AudioBuffer<float> diff(1, still.getNumSamples());
    for (int i = 0; i < still.getNumSamples(); ++i)
        diff.setSample(0, i, moving.getSample(0, i) - still.getSample(0, i));
    const float diffDb = rmsDbIn(diff, 0, 0.5, 2.0, sr) - rmsDbIn(still, 0, 0.5, 2.0, sr);
    INFO("difference " << diffDb << " dB relative to the still tail");
    CHECK(diffDb > -10.0f);

    // The right half's LFO is detuned from the left's, so the modulated halves never lock.
    CHECK(dsp::PlateReverb::rightLfoRatio != Approx(1.0));
}

TEST_CASE("reverb: after the reported tail the output is ≥ 60 dB down", "[reverb][tail]")
{
    const double sr = 48000.0;
    for (float size : {0.0f, 50.0f, 100.0f})
        for (float decay : {0.3f, 2.0f, 10.0f})
        {
            INFO("size " << size << " %, decay " << decay << " s");
            const auto in = source(SourceKind::impulse, 0.05, sr, 0.0f);
            const auto out = renderWith(in, sr,
                                        reverbOnly + Overrides{{ParamID::reverbDecay, num(decay)},
                                                               {ParamID::reverbSize, num(size)},
                                                               {ParamID::reverbPreDelay, "250"}},
                                        512, true);
            const int n = out.getNumSamples();
            const double expected =
                0.05 + dsp::ReverbEngine::tailSecondsFor(Mode::plate, decay, 250.0f, size / 100.0f);
            CHECK(n == Approx(toSample(expected, sr)).margin(1024));
            for (int ch = 0; ch < 2; ++ch)
            {
                const float peakDb = linearToDb(out.getMagnitude(ch, 0, n));
                const float endDb = linearToDb(peakIn(out, ch, n - toSample(0.05, sr), n));
                INFO("channel " << ch << ": peak " << peakDb << " dB, end " << endDb << " dB");
                CHECK(peakDb - endDb >= 60.0f);
            }
        }
}

TEST_CASE("reverb: level sanity", "[reverb][level]")
{
    const double sr = 48000.0;
    const auto burst = source(SourceKind::burst, 4.0, sr, -6.0f, 1000.0, 3.0, 1.0);
    const auto noise = source(SourceKind::noise, 4.0, sr, -6.0f);

    SECTION("at the default decay the wet peak stays within +6 dB of the input peak")
    {
        for (const auto* in : {&burst, &noise})
        {
            const auto out = renderWith(*in, sr, reverbOnly);
            const float inPeak = linearToDb(in->getMagnitude(0, 0, in->getNumSamples()));
            const float outPeak = linearToDb(out.getMagnitude(0, 0, out.getNumSamples()));
            INFO("in " << inPeak << " dBFS, wet " << outPeak << " dBFS");
            CHECK(outPeak <= inPeak + 6.0f);
        }
    }

    SECTION("a sustained tone into a 20 s plate builds up but stays bounded")
    {
        const auto out =
            renderWith(burst, sr, reverbOnly + Overrides{{ParamID::reverbDecay, "20"}});
        const float inPeak = linearToDb(burst.getMagnitude(0, 0, burst.getNumSamples()));
        const float outPeak = linearToDb(out.getMagnitude(0, 0, out.getNumSamples()));
        INFO("in " << inPeak << " dBFS, wet " << outPeak << " dBFS");
        CHECK(outPeak <= inPeak + 12.0f);
    }

    SECTION("a 2 s plate on noise returns at a usable level (−12…0 dB relative RMS)")
    {
        const auto out = renderWith(noise, sr, reverbOnly);
        const float rel = rmsDbIn(out, 0, 2.0, 4.0, sr) - rmsDbIn(noise, 0, 2.0, 4.0, sr);
        INFO("relative RMS " << rel << " dB");
        CHECK(rel > -12.0f);
        CHECK(rel < 0.0f);
    }
}

TEST_CASE("reverb: silence in → silence out, no NaN/Inf at any rate or block size", "[reverb][nan]")
{
    const Overrides extreme = reverbOnly + Overrides{{ParamID::reverbModDepth, "100"},
                                                     {ParamID::reverbModRate, "5"},
                                                     {ParamID::reverbPreDelay, "250"},
                                                     {ParamID::reverbDiffusion, "100"},
                                                     {ParamID::reverbDamping, "20000"}};
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int block : {1, 16, 512, 4096})
            for (const char* decay : {"0.1", "20"})
                for (const char* size : {"0", "100"})
                {
                    INFO(sr << " Hz, block " << block << ", decay " << decay << ", size " << size);
                    const Overrides o = extreme + Overrides{{ParamID::reverbDecay, decay},
                                                            {ParamID::reverbSize, size}};

                    juce::AudioBuffer<float> silence(2, toSample(0.5, sr));
                    silence.clear();
                    auto out = renderWith(silence, sr, o, block, true);
                    CHECK(out.getMagnitude(0, 0, out.getNumSamples()) == Approx(0.0f).margin(0.0f));
                    CHECK(out.getMagnitude(1, 0, out.getNumSamples()) == Approx(0.0f).margin(0.0f));

                    const auto in = source(SourceKind::noise, 1.0, sr, 0.0f);
                    out = renderWith(in, sr, o, block); // renderWith REQUIREs finite output
                    CHECK(out.getMagnitude(0, 0, out.getNumSamples()) < 4.0f);
                }
}

TEST_CASE("cpu: reverb section alone (48 kHz / 512, mod on)", "[cpu][reverb]")
{
    const double sr = 48000.0;
    const double seconds = 20.0;
    const auto in = source(SourceKind::noise, seconds, sr, -6.0f);
    const Overrides o = reverbOnly + Overrides{{ParamID::reverbModDepth, "50"}};

    renderWith(in, sr, o); // warm-up
    const auto t0 = std::chrono::steady_clock::now();
    renderWith(in, sr, o);
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double ratio = elapsed / seconds;
    std::cout << "cpu: reverb section alone (48 kHz / 512): " << seconds << " s rendered in "
              << elapsed << " s → " << ratio * 100.0 << " % of one core\n";
#ifdef NDEBUG
    CHECK(ratio < 0.03);
#else
    WARN("cpu check not enforced in a Debug build (" << ratio * 100.0 << " %)");
#endif
}

// -------------------------------------------------------------------------------------------
// Calibration helpers (hidden): print measured RT60 vs. target and levels for a grid of
// settings. Run with:  ClearSpaceTests "[.calibrate]"
// -------------------------------------------------------------------------------------------
TEST_CASE("reverb calibration table", "[.calibrate]")
{
    const double sr = 48000.0;
    for (float size : {0.0f, 50.0f, 100.0f})
        for (float damping : {6000.0f, 20000.0f})
            for (float decay : {0.1f, 0.3f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f})
            {
                const auto ir = impulseResponse(sr, 2.0 * decay + 1.5,
                                                {{ParamID::reverbDecay, num(decay)},
                                                 {ParamID::reverbSize, num(size)},
                                                 {ParamID::reverbDamping, num(damping)}});
                const double rt = schroederRT60(ir.getReadPointer(0), ir.getNumSamples(), sr);
                const double rtR = schroederRT60(ir.getReadPointer(1), ir.getNumSamples(), sr);
                const int n = ir.getNumSamples();
                const double peakDb = linearToDb(ir.getMagnitude(0, 0, n));
                const int tailAt =
                    toSample(dsp::PlateReverb::tailSecondsFor(decay, size / 100.0f), sr);
                const double atTail =
                    tailAt < n ? linearToDb(peakIn(ir, 0, tailAt, std::min(n, tailAt + 4800)))
                               : -999.0;
                const int rtAt = toSample(decay, sr);
                const double atRt =
                    rtAt < n ? linearToDb(peakIn(ir, 0, rtAt, std::min(n, rtAt + 4800))) : -999.0;
                std::cout << "size " << size << " damp " << damping << " decay " << decay
                          << "  rt60 L " << rt << " R " << rtR << "  ratio " << rt / decay
                          << "  peak " << peakDb << " dB, at RT60 " << atRt - peakDb
                          << " dB, at tail " << atTail - peakDb << " dB\n";
            }
}

TEST_CASE("reverb level table", "[.calibrate]")
{
    const double sr = 48000.0;
    for (float decay : {0.5f, 2.0f, 10.0f, 20.0f})
    {
        const auto in = source(SourceKind::burst, 4.0, sr, -6.0f, 1000.0, 3.0, 1.0);
        auto out = renderWith(in, sr, reverbOnly + Overrides{{ParamID::reverbDecay, num(decay)}});
        const auto noise = source(SourceKind::noise, 4.0, sr, -6.0f);
        auto outN =
            renderWith(noise, sr, reverbOnly + Overrides{{ParamID::reverbDecay, num(decay)}});
        std::cout << "decay " << decay << "  sine: in rms " << rmsDbIn(in, 0, 2.0, 3.0, sr)
                  << " out rms " << rmsDbIn(out, 0, 2.0, 3.0, sr) << " out peak "
                  << peakDbIn(out, 0, 0.0, 4.0, sr) << "  noise: in rms "
                  << rmsDbIn(noise, 0, 2.0, 3.0, sr) << " out rms "
                  << rmsDbIn(outN, 0, 2.0, 4.0, sr) << " out peak "
                  << peakDbIn(outN, 0, 0.0, 4.0, sr) << "\n";
    }
}
