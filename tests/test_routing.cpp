// Phase 1 acceptance, end to end through ClearSpaceProcessor via the render library:
// routing (mix law, trims, serial/parallel, click-free bypass, tails), the effects' levels
// and tails, ducking on the returns, duck link, and the external sidechain key.
// The delay is pinned to 500 ms with modulation off wherever timing matters; the delay
// engine itself is covered in test_delay.cpp.

#include "Measure.h"
#include "Parameters.h"
#include "Renderer.h"
#include "Sources.h"
#include "dsp/Routing.h"
#include "dsp/reverb/ReverbEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace clearspace;
using namespace clearspace::render;
using namespace clearspace::test;
using Catch::Approx;

namespace
{

using Overrides = std::vector<std::pair<std::string, std::string>>;

juce::AudioBuffer<float> source(SourceKind kind, double seconds, double sr, float levelDb = -6.0f,
                                double onSeconds = 0.5, double offSeconds = 0.5)
{
    SourceSpec spec;
    spec.kind = kind;
    spec.seconds = seconds;
    spec.levelDb = levelDb;
    spec.burstOnSeconds = onSeconds;
    spec.burstOffSeconds = offSeconds;
    return makeSource(spec, sr);
}

/** Sine at `freqHz` on the window [from, to), silence elsewhere, stereo. */
juce::AudioBuffer<float> gatedSine(double sr, double seconds, double from, double to,
                                   float peakDb = -6.0f, double freqHz = 1000.0)
{
    juce::AudioBuffer<float> b(2, toSample(seconds, sr));
    b.clear();
    const float amp = std::pow(10.0f, peakDb / 20.0f);
    const double inc = 2.0 * std::numbers::pi * freqHz / sr;
    for (int i = toSample(from, sr); i < std::min(b.getNumSamples(), toSample(to, sr)); ++i)
    {
        const float v = amp * static_cast<float>(std::sin(inc * i));
        b.setSample(0, i, v);
        b.setSample(1, i, v);
    }
    return b;
}

juce::AudioBuffer<float> renderWith(const juce::AudioBuffer<float>& input, double sr,
                                    const Overrides& overrides, int block = 512, bool tail = false,
                                    const std::vector<AutomationPoint>& automation = {},
                                    const juce::AudioBuffer<float>* sidechain = nullptr)
{
    RenderSettings s;
    s.sampleRate = sr;
    s.blockSize = block;
    s.parameterOverrides = overrides;
    s.automation = automation;
    s.appendTail = tail;
    if (sidechain != nullptr)
        s.sidechain = *sidechain;
    auto out = renderThroughProcessor(input, s);
    for (const auto& e : out.errors)
        FAIL("render error: " << e.message);
    REQUIRE(out.ok());
    REQUIRE_FALSE(hasNonFinite(out.buffer));
    return std::move(out.buffer);
}

// Wet-only, no ducking, one section at a time.
const Overrides delayOnly = {{ParamID::mix, "100"},
                             {ParamID::reverbBypass, "on"},
                             {ParamID::delayDuckEnable, "off"},
                             {ParamID::reverbDuckEnable, "off"},
                             {ParamID::delayTime, "500"},
                             {ParamID::delayMod, "0"}};
const Overrides reverbOnly = {{ParamID::mix, "100"},
                              {ParamID::delayBypass, "on"},
                              {ParamID::delayDuckEnable, "off"},
                              {ParamID::reverbDuckEnable, "off"}};
const Overrides dryOnly = {{ParamID::delayBypass, "on"}, {ParamID::reverbBypass, "on"}};

Overrides operator+(Overrides a, const Overrides& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

} // namespace

TEST_CASE("routing: equal-power mix law", "[routing][mix]")
{
    float dry = 0.0f, wet = 0.0f;
    dsp::Routing::mixGains(0.0f, dry, wet);
    CHECK(dry == Approx(1.0f).margin(0.0f));
    CHECK(wet == Approx(0.0f).margin(0.0f));
    dsp::Routing::mixGains(100.0f, dry, wet);
    CHECK(dry == Approx(0.0f).margin(0.0f));
    CHECK(wet == Approx(1.0f).margin(0.0f));
    dsp::Routing::mixGains(50.0f, dry, wet);
    CHECK(dry == Approx(0.70710678f).margin(1e-5));
    CHECK(wet == Approx(0.70710678f).margin(1e-5));
    for (float m : {10.0f, 25.0f, 50.0f, 75.0f, 90.0f})
    {
        dsp::Routing::mixGains(m, dry, wet);
        CHECK(dry * dry + wet * wet == Approx(1.0f).margin(1e-5));
    }

    const double sr = 48000.0;
    const auto in = source(SourceKind::sine, 0.5, sr);
    const float inPeak = in.getMagnitude(0, 0, in.getNumSamples());

    SECTION("mix 50 with no wet signal is −3 dB dry")
    {
        auto out = renderWith(in, sr, dryOnly + Overrides{{ParamID::mix, "50"}});
        CHECK(peakIn(out, 0, toSample(0.1, sr), out.getNumSamples()) ==
              Approx(inPeak * 0.70710678f).epsilon(0.01));
    }
    SECTION("mix 100 with both sections bypassed is silence")
    {
        auto out = renderWith(in, sr, dryOnly + Overrides{{ParamID::mix, "100"}});
        CHECK(out.getMagnitude(0, 0, out.getNumSamples()) == Approx(0.0f).margin(0.0f));
    }
    SECTION("mix 0 is the dry input, whatever the effects do")
    {
        auto out = renderWith(in, sr, {{ParamID::mix, "0"}, {ParamID::delayFeedback, "90"}});
        float maxDiff = 0.0f;
        for (int i = 0; i < in.getNumSamples(); ++i)
            maxDiff = std::max(maxDiff, std::abs(out.getSample(0, i) - in.getSample(0, i)));
        CHECK(maxDiff == Approx(0.0f).margin(0.0f));
    }
}

TEST_CASE("routing: input and output trims", "[routing][trim]")
{
    const double sr = 48000.0;
    const auto in = source(SourceKind::sine, 0.3, sr);
    const float inPeak = in.getMagnitude(0, 0, in.getNumSamples());
    const int from = toSample(0.1, sr);

    auto out =
        renderWith(in, sr, dryOnly + Overrides{{ParamID::mix, "0"}, {ParamID::inputTrim, "6"}});
    CHECK(peakIn(out, 0, from, out.getNumSamples()) == Approx(inPeak * 1.99526f).epsilon(0.005));

    out =
        renderWith(in, sr, dryOnly + Overrides{{ParamID::mix, "0"}, {ParamID::outputTrim, "-12"}});
    CHECK(peakIn(out, 0, from, out.getNumSamples()) == Approx(inPeak * 0.251189f).epsilon(0.005));

    out = renderWith(in, sr,
                     dryOnly + Overrides{{ParamID::mix, "0"},
                                         {ParamID::inputTrim, "6"},
                                         {ParamID::outputTrim, "-6"}});
    CHECK(peakIn(out, 0, from, out.getNumSamples()) == Approx(inPeak).epsilon(0.005));
}

TEST_CASE("routing: the delay return echoes at the set time and level", "[routing][delay]")
{
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        INFO("sr " << sr);
        const auto in = source(SourceKind::impulse, 1.1, sr);
        auto out = renderWith(in, sr, delayOnly + Overrides{{ParamID::delayFeedback, "50"}});
        const float* L = out.getReadPointer(0);
        const int first = firstAbove(L, 0, out.getNumSamples(), 1.0e-6f);
        CHECK(std::abs(first - toSample(0.5, sr)) <= 1);
        // The impulse comes straight back at unity …
        CHECK(peakIn(out, 0, toSample(0.49, sr), toSample(0.51, sr)) ==
              Approx(in.getMagnitude(0, 0, 1)).epsilon(0.01));
        // … with nothing between it and the first repeat.
        CHECK(peakIn(out, 0, toSample(0.51, sr), toSample(0.99, sr)) == Approx(0.0f).margin(0.0f));
        // Both channels identical for an identical input.
        CHECK(peakIn(out, 1, toSample(0.49, sr), toSample(0.51, sr)) ==
              Approx(in.getMagnitude(0, 0, 1)).epsilon(0.01));
    }

    SECTION("delayLevel scales the return, and its minimum is off")
    {
        const double sr = 48000.0;
        const auto in = source(SourceKind::impulse, 0.6, sr);
        auto out = renderWith(in, sr, delayOnly + Overrides{{ParamID::delayLevel, "-6"}});
        CHECK(peakDbIn(out, 0, 0.49, 0.51, sr) == Approx(-12.0f).margin(0.1));
        out = renderWith(in, sr, delayOnly + Overrides{{ParamID::delayLevel, "-60"}});
        CHECK(out.getMagnitude(0, 0, out.getNumSamples()) == Approx(0.0f).margin(0.0f));
    }
}

TEST_CASE("routing: the reverb section's RT60 tracks reverbDecay within ±15 %", "[routing][reverb]")
{
    const double sr = 48000.0;
    for (float decay : {0.5f, 2.0f, 5.0f})
    {
        INFO("decay " << decay);
        const auto in = source(SourceKind::impulse, 0.05, sr);
        auto out = renderWith(
            in, sr,
            reverbOnly + Overrides{{ParamID::reverbDecay, juce::String(decay).toStdString()}}, 512,
            true);
        REQUIRE(out.getNumSamples() >= toSample(decay, sr));
        const double rt60 = schroederRT60(out.getReadPointer(0), out.getNumSamples(), sr);
        CHECK(rt60 == Approx(decay).epsilon(0.15));
        const double rt60R = schroederRT60(out.getReadPointer(1), out.getNumSamples(), sr);
        CHECK(rt60R == Approx(decay).epsilon(0.15));
    }
}

TEST_CASE("routing: serial feeds the reverb with the delay return, parallel does not",
          "[routing][serial]")
{
    const double sr = 48000.0;
    const auto in = source(SourceKind::impulse, 1.2, sr);
    const Overrides base = {{ParamID::mix, "100"},
                            {ParamID::delayDuckEnable, "off"},
                            {ParamID::reverbDuckEnable, "off"},
                            {ParamID::delayTime, "500"},
                            {ParamID::delayMod, "0"},
                            {ParamID::delayFeedback, "0"},
                            {ParamID::reverbDecay, "0.3"}};
    auto serial = renderWith(in, sr, base + Overrides{{ParamID::routing, "Serial"}});
    auto parallel = renderWith(in, sr, base + Overrides{{ParamID::routing, "Parallel"}});

    // The reverb response to the 500 ms echo exists only in serial.
    const float serialDb = rmsDbIn(serial, 0, 0.52, 0.8, sr);
    const float parallelDb = rmsDbIn(parallel, 0, 0.52, 0.8, sr);
    INFO("serial " << serialDb << " dBFS, parallel " << parallelDb << " dBFS");
    CHECK(serialDb - parallelDb > 30.0f);
    // Before the echo both are the same reverb of the dry impulse.
    CHECK(rmsDbIn(serial, 0, 0.0, 0.4, sr) ==
          Approx(rmsDbIn(parallel, 0, 0.0, 0.4, sr)).margin(0.01));
}

TEST_CASE("routing: section bypass is click-free and restarts clean", "[routing][bypass]")
{
    const double sr = 48000.0;
    const auto in = source(SourceKind::sine, 2.0, sr);
    const float* x = in.getReadPointer(0);
    const float ownStep = maxSampleStep(x, 1, in.getNumSamples());

    std::vector<AutomationPoint> automation = {{1.0, ParamID::delayBypass, "on"},
                                               {1.2, ParamID::delayBypass, "off"}};
    auto out = renderWith(in, sr, delayOnly + Overrides{{ParamID::delayFeedback, "0"}}, 256, false,
                          automation);
    const float* y = out.getReadPointer(0);

    // Delayed sine is present before the bypass …
    CHECK(peakDbIn(out, 0, 0.8, 0.99, sr) == Approx(-6.0f).margin(0.1));
    // … fades out without a step larger than the signal's own …
    CHECK(maxSampleStep(y, toSample(0.99, sr), toSample(1.1, sr)) <= ownStep * 1.05f);
    // … is gone within 30 ms of the switch (block quantised) …
    CHECK(peakIn(out, 0, toSample(1.04, sr), toSample(1.2, sr)) == Approx(0.0f).margin(0.0f));
    // … and after re-enabling the line restarts empty: nothing until 500 ms later.
    CHECK(peakIn(out, 0, toSample(1.21, sr), toSample(1.69, sr)) == Approx(0.0f).margin(0.0f));
    CHECK(peakDbIn(out, 0, 1.75, 2.0, sr) == Approx(-6.0f).margin(0.1));
    CHECK(maxSampleStep(y, toSample(1.69, sr), toSample(1.8, sr)) <= ownStep * 1.05f);
}

TEST_CASE("routing: mix and level changes are smoothed", "[routing][smoothing]")
{
    const double sr = 48000.0;
    const auto in = source(SourceKind::sine, 1.0, sr);
    const float ownStep = maxSampleStep(in.getReadPointer(0), 1, in.getNumSamples());

    auto out = renderWith(in, sr, dryOnly + Overrides{{ParamID::mix, "100"}}, 64, false,
                          {{0.5, ParamID::mix, "0"}});
    const float* y = out.getReadPointer(0);
    CHECK(peakIn(out, 0, 0, toSample(0.49, sr)) == Approx(0.0f).margin(0.0f));
    CHECK(maxSampleStep(y, toSample(0.49, sr), toSample(0.6, sr)) <= ownStep * 1.05f);
    CHECK(peakDbIn(out, 0, 0.6, 1.0, sr) == Approx(-6.0f).margin(0.1));

    out = renderWith(in, sr, dryOnly + Overrides{{ParamID::mix, "0"}, {ParamID::outputTrim, "-24"}},
                     64, false, {{0.5, ParamID::outputTrim, "0"}});
    y = out.getReadPointer(0);
    CHECK(maxSampleStep(y, toSample(0.49, sr), toSample(0.6, sr)) <= ownStep * 1.05f);
}

TEST_CASE("routing: tail reporting and silence", "[routing][tail]")
{
    const double sr = 48000.0;

    SECTION("silence in → exactly silence out, every routing, with tail")
    {
        juce::AudioBuffer<float> silence(2, toSample(0.5, sr));
        silence.clear();
        for (const char* mode : {"Serial", "Parallel"})
        {
            auto out = renderWith(silence, sr, {{ParamID::routing, mode}}, 512, true);
            CHECK(out.getNumSamples() > silence.getNumSamples()); // tail was appended
            CHECK(out.getMagnitude(0, 0, out.getNumSamples()) == Approx(0.0f).margin(0.0f));
            CHECK(out.getMagnitude(1, 0, out.getNumSamples()) == Approx(0.0f).margin(0.0f));
        }
    }

    SECTION("after the reported tail the output has decayed by ≥ 55 dB")
    {
        const auto in = source(SourceKind::burst, 0.3, sr, -6.0f, 0.3, 10.0);
        for (const char* mode : {"Serial", "Parallel"})
        {
            INFO(mode);
            auto out =
                renderWith(in, sr, {{ParamID::routing, mode}, {ParamID::mix, "100"}}, 512, true);
            const float peakDb = linearToDb(out.getMagnitude(0, 0, out.getNumSamples()));
            const int n = out.getNumSamples();
            const float endDb = linearToDb(peakIn(out, 0, n - toSample(0.05, sr), n));
            CHECK(peakDb - endDb >= 55.0f);
        }
    }

    SECTION("tail follows bypass and routing")
    {
        ClearSpaceProcessor p;
        auto& apvts = p.getAPVTS();
        applyParameterOverride(apvts, ParamID::delayTime, "500");
        applyParameterOverride(apvts, ParamID::delayFeedback, "0"); // one repeat: 0.5 s
        applyParameterOverride(apvts, ParamID::reverbDecay, "3");
        // The delay tail carries a 2 ms allowance for read-position modulation. The time is
        // taken from the last processBlock (or the constructor), so run one block first.
        // The reverb tail is pre-delay + the plate's round trip + 1.25 × decay (ADR-0005).
        const double reverbTail =
            dsp::ReverbEngine::tailSecondsFor(dsp::ReverbEngine::Mode::plate, 3.0f, 20.0f, 0.5f);
        p.setRateAndBufferSizeDetails(sr, 64);
        p.prepareToPlay(sr, 64);
        juce::AudioBuffer<float> block(2, 64);
        block.clear();
        juce::MidiBuffer midi;
        p.processBlock(block, midi);
        CHECK(p.getTailLengthSeconds() == Approx(0.502 + reverbTail).margin(1e-3));
        applyParameterOverride(apvts, ParamID::routing, "Parallel");
        CHECK(p.getTailLengthSeconds() == Approx(reverbTail).margin(1e-3));
        applyParameterOverride(apvts, ParamID::reverbBypass, "on");
        CHECK(p.getTailLengthSeconds() == Approx(0.502).margin(1e-3));
        applyParameterOverride(apvts, ParamID::delayBypass, "on");
        CHECK(p.getTailLengthSeconds() == Approx(0.0).margin(1e-6));
        p.releaseResources();
    }
}

TEST_CASE("ducking: the wet return ducks by Depth under the internal key and swells back",
          "[ducking]")
{
    const double sr = 48000.0;
    // Bursts 0–0.5 on, 0.5–1.0 off, 1.0–1.5 on. Reverb only (5 s decay so there is plenty
    // of tail), parallel, 100 % wet; the reverb ducker is driven directly (link off).
    const auto in = source(SourceKind::burst, 2.0, sr);
    const Overrides base = {{ParamID::mix, "100"},        {ParamID::routing, "Parallel"},
                            {ParamID::delayBypass, "on"}, {ParamID::reverbDecay, "5"},
                            {ParamID::duckLink, "off"},   {ParamID::delayDuckEnable, "off"}};
    auto ducked = renderWith(in, sr, base + Overrides{{ParamID::reverbDuckEnable, "on"}});
    auto open = renderWith(in, sr, base + Overrides{{ParamID::reverbDuckEnable, "off"}});

    // Fully engaged during the second burst.
    const float during = rmsDbIn(ducked, 0, 1.2, 1.45, sr) - rmsDbIn(open, 0, 1.2, 1.45, sr);
    INFO("during burst: " << during << " dB");
    CHECK(during == Approx(-12.0f).margin(1.0));
    // Swelled back (release 400 ms + hold 60 ms after the burst ends at 0.5 s).
    const float after = rmsDbIn(ducked, 0, 0.93, 0.99, sr) - rmsDbIn(open, 0, 0.93, 0.99, sr);
    INFO("after release: " << after << " dB");
    CHECK(after > -1.0f);
    // Mid-release the gain is between the two.
    const float mid = rmsDbIn(ducked, 0, 0.65, 0.7, sr) - rmsDbIn(open, 0, 0.65, 0.7, sr);
    CHECK(mid < -2.0f);
    CHECK(mid > -11.0f);

    SECTION("depth follows the parameter")
    {
        auto deep = renderWith(in, sr,
                               base + Overrides{{ParamID::reverbDuckEnable, "on"},
                                                {ParamID::reverbDuckDepth, "20"},
                                                {ParamID::reverbDuckThreshold, "-40"}});
        CHECK(rmsDbIn(deep, 0, 1.2, 1.45, sr) - rmsDbIn(open, 0, 1.2, 1.45, sr) ==
              Approx(-20.0f).margin(1.0));
    }

    SECTION("the delay return is ducked the same way")
    {
        const Overrides delayBase = {{ParamID::mix, "100"},
                                     {ParamID::routing, "Parallel"},
                                     {ParamID::reverbBypass, "on"},
                                     {ParamID::delayFeedback, "80"},
                                     {ParamID::duckLink, "off"}};
        auto d = renderWith(in, sr, delayBase + Overrides{{ParamID::delayDuckEnable, "on"}});
        auto o = renderWith(in, sr, delayBase + Overrides{{ParamID::delayDuckEnable, "off"}});
        CHECK(rmsDbIn(d, 0, 1.2, 1.45, sr) - rmsDbIn(o, 0, 1.2, 1.45, sr) ==
              Approx(-12.0f).margin(1.0));
    }
}

TEST_CASE("ducking: duckLink makes the reverb ducker mirror the delay ducker", "[ducking][link]")
{
    const double sr = 48000.0;
    const auto in = source(SourceKind::burst, 1.6, sr);
    // Reverb duck disabled with depth 0; delay duck enabled at depth 12. Linked, the reverb
    // must follow the delay's settings; unlinked, its own (nothing).
    const Overrides base = {{ParamID::mix, "100"},
                            {ParamID::routing, "Parallel"},
                            {ParamID::delayBypass, "on"},
                            {ParamID::reverbDecay, "5"},
                            {ParamID::delayDuckEnable, "on"},
                            {ParamID::delayDuckDepth, "12"},
                            {ParamID::reverbDuckEnable, "off"},
                            {ParamID::reverbDuckDepth, "0"}};
    auto linked = renderWith(in, sr, base + Overrides{{ParamID::duckLink, "on"}});
    auto unlinked = renderWith(in, sr, base + Overrides{{ParamID::duckLink, "off"}});
    const float diff = rmsDbIn(linked, 0, 1.2, 1.45, sr) - rmsDbIn(unlinked, 0, 1.2, 1.45, sr);
    INFO("linked − unlinked: " << diff << " dB");
    CHECK(diff == Approx(-12.0f).margin(1.0));
}

TEST_CASE("ducking: external sidechain key ducks the wet while the main input is silent",
          "[ducking][sidechain]")
{
    const double sr = 48000.0;
    // Main: a single 0.2 s burst, then silence; the reverb tail rings for seconds.
    const auto in = source(SourceKind::burst, 2.0, sr, -6.0f, 0.2, 10.0);
    // Key on the sidechain bus: 1 kHz during 1.0–1.5 s, when the main input is silent.
    const auto key = gatedSine(sr, 2.0, 1.0, 1.5);
    const Overrides base = {{ParamID::mix, "100"},        {ParamID::routing, "Parallel"},
                            {ParamID::delayBypass, "on"}, {ParamID::reverbDecay, "5"},
                            {ParamID::duckLink, "off"},   {ParamID::delayDuckEnable, "off"}};

    auto open = renderWith(in, sr, base + Overrides{{ParamID::reverbDuckEnable, "off"}});
    auto external = renderWith(
        in, sr,
        base + Overrides{{ParamID::reverbDuckEnable, "on"}, {ParamID::duckSource, "External"}}, 512,
        false, {}, &key);
    auto internal = renderWith(
        in, sr,
        base + Overrides{{ParamID::reverbDuckEnable, "on"}, {ParamID::duckSource, "Internal"}}, 512,
        false, {}, &key);
    auto fallback = renderWith(in, sr,
                               base + Overrides{{ParamID::reverbDuckEnable, "on"},
                                                {ParamID::duckSource, "External"}}); // bus off

    const float refDb = rmsDbIn(open, 0, 1.1, 1.45, sr);
    INFO("external " << rmsDbIn(external, 0, 1.1, 1.45, sr) - refDb << " dB, internal "
                     << rmsDbIn(internal, 0, 1.1, 1.45, sr) - refDb << " dB, fallback "
                     << rmsDbIn(fallback, 0, 1.1, 1.45, sr) - refDb << " dB");
    // External key ducks the tail by Depth while bus 1 is silent.
    CHECK(rmsDbIn(external, 0, 1.1, 1.45, sr) - refDb == Approx(-12.0f).margin(1.0));
    // Internal source ignores the sidechain bus (main is silent → no ducking).
    CHECK(rmsDbIn(internal, 0, 1.1, 1.45, sr) - refDb == Approx(0.0f).margin(0.2));
    // External selected but not connected falls back to internal: no ducking here …
    CHECK(rmsDbIn(fallback, 0, 1.1, 1.45, sr) - refDb == Approx(0.0f).margin(0.2));
    // … but the main burst still ducks during 0.05–0.2 s.
    CHECK(rmsDbIn(fallback, 0, 0.08, 0.2, sr) - rmsDbIn(open, 0, 0.08, 0.2, sr) ==
          Approx(-12.0f).margin(1.0));
    // The sidechain key itself never reaches the output.
    CHECK(rmsDbIn(external, 0, 1.6, 2.0, sr) < rmsDbIn(open, 0, 1.6, 2.0, sr) + 0.5f);

    SECTION("mono sidechain works too")
    {
        juce::AudioBuffer<float> monoKey(1, key.getNumSamples());
        monoKey.copyFrom(0, 0, key, 0, 0, key.getNumSamples());
        auto ext = renderWith(
            in, sr,
            base + Overrides{{ParamID::reverbDuckEnable, "on"}, {ParamID::duckSource, "External"}},
            512, false, {}, &monoKey);
        CHECK(rmsDbIn(ext, 0, 1.1, 1.45, sr) - refDb == Approx(-12.0f).margin(1.0));
    }
}

TEST_CASE("processor: mono main input with a sidechain (aliased output channel) is safe",
          "[processor][layout]")
{
    // Mono in + mono sidechain + stereo out: the process buffer has two channels and output
    // R aliases the sidechain input. The dry path must still duplicate the input cleanly.
    ClearSpaceProcessor p;
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(ClearSpaceProcessor::mainInputBus, juce::AudioChannelSet::mono());
    layout.inputBuses.set(ClearSpaceProcessor::sidechainBus, juce::AudioChannelSet::mono());
    layout.outputBuses.set(ClearSpaceProcessor::mainOutputBus, juce::AudioChannelSet::stereo());
    REQUIRE(p.setBusesLayout(layout));
    REQUIRE(p.isSidechainConnected());
    REQUIRE(p.getTotalNumInputChannels() == 2);
    REQUIRE(p.getTotalNumOutputChannels() == 2);

    applyParameterOverride(p.getAPVTS(), ParamID::mix, "0");
    applyParameterOverride(p.getAPVTS(), ParamID::duckSource, "External");

    const double sr = 48000.0;
    const int block = 256;
    p.setRateAndBufferSizeDetails(sr, block);
    p.prepareToPlay(sr, block);

    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const int n = b * block + i;
            const float t = static_cast<float>(n);
            buffer.setSample(0, i, 0.25f * std::sin(0.05f * t)); // main (mono)
            buffer.setSample(1, i, 0.5f * std::sin(0.3f * t));   // sidechain key (hot)
        }
        std::vector<float> expected(buffer.getReadPointer(0), buffer.getReadPointer(0) + block);
        p.processBlock(buffer, midi);
        for (int i = 0; i < block; ++i)
        {
            REQUIRE(std::isfinite(buffer.getSample(0, i)));
            REQUIRE(buffer.getSample(0, i) ==
                    Approx(expected[static_cast<size_t>(i)]).margin(0.0f));
            REQUIRE(buffer.getSample(1, i) ==
                    Approx(expected[static_cast<size_t>(i)]).margin(0.0f));
        }
    }
    CHECK(p.isUsingExternalKey());
    CHECK(p.getDelayGainReductionDb() == Approx(12.0f).margin(0.5)); // the key was hot
    p.releaseResources();
}

TEST_CASE("processor: no NaN/Inf/denormals for every rate, block size, routing and duck state",
          "[processor][nan]")
{
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int block : {1, 7, 64, 512, 4096})
            for (const char* mode : {"Serial", "Parallel"})
            {
                INFO("sr " << sr << " block " << block << " " << mode);
                const auto in = source(SourceKind::noise, 0.3, sr, -3.0f);
                auto out = renderWith(in, sr,
                                      {{ParamID::routing, mode},
                                       {ParamID::delayFeedback, "100"},
                                       {ParamID::reverbDecay, "20"},
                                       {ParamID::mix, "100"}},
                                      block, false);
                CHECK_FALSE(hasNonFinite(out));
                // Feedback at max is clamped inside the delay, so the loop stays bounded.
                CHECK(out.getMagnitude(0, 0, out.getNumSamples()) < 10.0f);
            }
}

TEST_CASE("processor: block sizes larger than prepared are chunked, not overrun",
          "[processor][layout]")
{
    ClearSpaceProcessor p;
    const double sr = 48000.0;
    p.setRateAndBufferSizeDetails(sr, 64);
    p.prepareToPlay(sr, 64);
    juce::AudioBuffer<float> buffer(2, 1000);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 1000; ++i)
            buffer.setSample(ch, i, 0.5f * std::sin(0.1f * static_cast<float>(i)));
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);
    REQUIRE_FALSE(hasNonFinite(buffer));
    CHECK(buffer.getMagnitude(0, 0, 1000) > 0.0f);
}

TEST_CASE("cpu: realtime ratio of the whole chain (defaults, 48 kHz / 512)", "[cpu]")
{
    const double sr = 48000.0;
    const double seconds = 20.0;
    const auto in = source(SourceKind::speechlike, seconds, sr);
    const auto t0 = std::chrono::steady_clock::now();
    auto out = renderWith(in, sr, {{ParamID::mix, "50"}});
    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    const double ratio = elapsed / seconds;
    std::cout << "cpu: rendered " << seconds << " s in " << elapsed << " s → " << ratio * 100.0
              << " % of one core\n";
#ifdef NDEBUG
    CHECK(ratio < 0.03); // CLAUDE.md §6: a mode may not cost more than 3 % of a core
#else
    WARN("cpu check not enforced in a Debug build (" << ratio * 100.0 << " %)");
#endif
}
