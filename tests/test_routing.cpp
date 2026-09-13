// Phase 1 acceptance: SPEC §2 routing measured end to end through the processor with the
// render library (mix law, trims, serial/parallel, click-free bypass, tail, external sidechain,
// mono-in aliasing guard, silence/NaN sweeps, CPU budget).

#include "Parameters.h"
#include "Renderer.h"
#include "Sources.h"
#include "Stats.h"
#include "dsp/Routing.h"
#include "dsp/Util.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace clearspace;
using namespace clearspace::render;
using Catch::Approx;

namespace
{

using Overrides = std::vector<std::pair<std::string, std::string>>;

std::string num(double v)
{
    return juce::String(v, 4).toStdString();
}

juce::AudioBuffer<float> sine(double sr, double seconds, float levelDb = -6.0f,
                              double freqHz = 1000.0)
{
    SourceSpec spec;
    spec.kind = SourceKind::sine;
    spec.seconds = seconds;
    spec.levelDb = levelDb;
    spec.freqHz = freqHz;
    return makeSource(spec, sr);
}

juce::AudioBuffer<float> impulse(double sr, double seconds, float levelDb = -6.0f)
{
    SourceSpec spec;
    spec.kind = SourceKind::impulse;
    spec.seconds = seconds;
    spec.levelDb = levelDb;
    return makeSource(spec, sr);
}

juce::AudioBuffer<float> noise(double sr, double seconds, float levelDb = -6.0f)
{
    SourceSpec spec;
    spec.kind = SourceKind::noise;
    spec.seconds = seconds;
    spec.levelDb = levelDb;
    return makeSource(spec, sr);
}

juce::AudioBuffer<float> silence(double sr, double seconds)
{
    juce::AudioBuffer<float> b(2, static_cast<int>(std::lround(seconds * sr)));
    b.clear();
    return b;
}

int at(double seconds, double sr)
{
    return static_cast<int>(std::lround(seconds * sr));
}

float peakIn(const juce::AudioBuffer<float>& b, int ch, double fromSec, double toSec, double sr)
{
    const auto a = std::clamp(at(fromSec, sr), 0, b.getNumSamples());
    const auto z = std::clamp(at(toSec, sr), 0, b.getNumSamples());
    return z > a ? b.getMagnitude(ch, a, z - a) : 0.0f;
}

float rmsDbIn(const juce::AudioBuffer<float>& b, int ch, double fromSec, double toSec, double sr)
{
    const auto a = std::clamp(at(fromSec, sr), 0, b.getNumSamples());
    const auto z = std::clamp(at(toSec, sr), 0, b.getNumSamples());
    if (z <= a)
        return -200.0f;
    return juce::Decibels::gainToDecibels(b.getRMSLevel(ch, a, z - a), -200.0f);
}

/** Sample-exact equality (values, so −0.0 == +0.0; NaN never equal). */
bool bitExact(const juce::AudioBuffer<float>& a, int chA, const juce::AudioBuffer<float>& b,
              int chB)
{
    if (a.getNumSamples() != b.getNumSamples())
        return false;
    const auto* x = a.getReadPointer(chA);
    const auto* y = b.getReadPointer(chB);
    for (int i = 0; i < a.getNumSamples(); ++i)
        if (!dsp::exactlyEqual(x[i], y[i]))
            return false;
    return true;
}

/** Delay wet only, no feedback, no ducking, reverb off, 100 % wet: the output is the delayed
    input. */
Overrides wetDelayOnly(double delayMs)
{
    return {{ParamID::mix, "100"},
            {ParamID::reverbBypass, "on"},
            {ParamID::delayTime, num(delayMs)},
            {ParamID::delayFeedback, "0"},
            {ParamID::delayDuckEnable, "off"},
            {ParamID::reverbDuckEnable, "off"}};
}

} // namespace

TEST_CASE("routing: equal-power mix law", "[routing]")
{
    for (int i = 0; i <= 20; ++i)
    {
        const float m = static_cast<float>(i) / 20.0f;
        float d = 0.0f, w = 0.0f;
        dsp::Routing::equalPowerGains(m, d, w);
        INFO("mix = " << m);
        CHECK(d * d + w * w == Approx(1.0f).margin(1.0e-5f));
    }
    float d = 0.0f, w = 0.0f;
    dsp::Routing::equalPowerGains(0.5f, d, w);
    CHECK(d == Approx(0.70710678f).margin(1.0e-5f));
    CHECK(w == Approx(0.70710678f).margin(1.0e-5f));
    dsp::Routing::equalPowerGains(0.0f, d, w);
    CHECK((dsp::exactlyEqual(d, 1.0f) && dsp::exactlyEqual(w, 0.0f)));
    dsp::Routing::equalPowerGains(1.0f, d, w);
    CHECK((dsp::exactlyEqual(d, 0.0f) && dsp::exactlyEqual(w, 1.0f)));
}

TEST_CASE("routing: tail length is the longer of delay tail and reverb decay", "[routing]")
{
    CHECK(dsp::Routing::delayTailSeconds(500.0, 0.0) == Approx(0.5));
    CHECK(dsp::Routing::delayTailSeconds(500.0, 50.0) == Approx(5.0)); // 9 more echoes ≥ −60 dB
    CHECK(dsp::Routing::delayTailSeconds(2000.0, 100.0) == Approx(dsp::Routing::maxTailSeconds));
    CHECK(dsp::Routing::tailSeconds(500.0, 0.0, false, 0.1, false) == Approx(0.5));
    CHECK(dsp::Routing::tailSeconds(500.0, 0.0, false, 2.0, false) == Approx(2.0));
    CHECK(dsp::Routing::tailSeconds(500.0, 0.0, true, 0.1, false) == Approx(0.1));
    CHECK(dsp::Routing::tailSeconds(500.0, 0.0, false, 2.0, true) == Approx(0.5));

    const double sr = 48000.0;
    RenderSettings s;
    s.sampleRate = sr;
    s.parameterOverrides = {
        {ParamID::delayTime, "500"}, {ParamID::delayFeedback, "0"}, {ParamID::reverbDecay, "0.1"}};
    auto out = renderThroughProcessor(silence(sr, 0.05), s);
    REQUIRE(out.ok());
    CHECK(out.tailSeconds == Approx(0.5));

    s.parameterOverrides = {
        {ParamID::delayTime, "500"}, {ParamID::delayFeedback, "0"}, {ParamID::reverbDecay, "2"}};
    out = renderThroughProcessor(silence(sr, 0.05), s);
    REQUIRE(out.ok());
    CHECK(out.tailSeconds == Approx(2.0));
}

TEST_CASE("routing: mix 100 with both sections bypassed is silent", "[routing]")
{
    const double sr = 48000.0;
    RenderSettings s;
    s.sampleRate = sr;
    s.parameterOverrides = {
        {ParamID::mix, "100"}, {ParamID::delayBypass, "on"}, {ParamID::reverbBypass, "on"}};
    auto out = renderThroughProcessor(sine(sr, 0.5), s);
    REQUIRE(out.ok());
    CHECK(dsp::exactlyEqual(out.buffer.getMagnitude(0, out.buffer.getNumSamples()), 0.0f));
}

TEST_CASE("routing: input and output trims", "[routing]")
{
    const double sr = 48000.0;
    RenderSettings s;
    s.sampleRate = sr;
    s.parameterOverrides = {
        {ParamID::mix, "0"}, {ParamID::inputTrim, "-6"}, {ParamID::outputTrim, "-6"}};
    auto out = renderThroughProcessor(sine(sr, 1.0, -6.0f), s);
    REQUIRE(out.ok());
    // −6 dBFS sine = −9.01 dB RMS, minus 12 dB of trim.
    CHECK(rmsDbIn(out.buffer, 0, 0.1, 1.0, sr) == Approx(-21.01f).margin(0.1f));
    CHECK(rmsDbIn(out.buffer, 1, 0.1, 1.0, sr) == Approx(-21.01f).margin(0.1f));
}

TEST_CASE("routing: stand-in delay echoes at the requested time", "[routing][standin]")
{
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        RenderSettings s;
        s.sampleRate = sr;
        s.parameterOverrides = wetDelayOnly(100.0);
        auto out = renderThroughProcessor(impulse(sr, 0.3), s);
        REQUIRE(out.ok());
        const auto stats = computeStats(out.buffer, sr);
        CHECK(stats.channels[0].firstNonZero == at(0.1, sr));
        CHECK(stats.channels[1].firstNonZero == at(0.1, sr));
    }
}

TEST_CASE("routing: serial feeds the reverb with the delay return, parallel does not", "[routing]")
{
    const double sr = 48000.0;
    RenderSettings s;
    s.sampleRate = sr;
    Overrides common = {{ParamID::mix, "100"},
                        {ParamID::delayTime, "500"},
                        {ParamID::delayFeedback, "0"},
                        {ParamID::reverbDecay, "0.3"},
                        {ParamID::delayDuckEnable, "off"},
                        {ParamID::reverbDuckEnable, "off"}};

    s.parameterOverrides = common;
    s.parameterOverrides.emplace_back(ParamID::routing, "Parallel");
    auto parallel = renderThroughProcessor(impulse(sr, 1.0), s);
    REQUIRE(parallel.ok());

    s.parameterOverrides = common;
    s.parameterOverrides.emplace_back(ParamID::routing, "Serial");
    auto serial = renderThroughProcessor(impulse(sr, 1.0), s);
    REQUIRE(serial.ok());

    // The echo lands at 500 ms. In Parallel the reverb only ever saw the dry impulse, whose
    // 0.3 s tail is long gone; in Serial the echo re-excites it.
    CHECK(peakIn(parallel.buffer, 0, 0.55, 0.9, sr) < 1.0e-4f);
    CHECK(peakIn(serial.buffer, 0, 0.55, 0.9, sr) > 1.0e-3f);
}

TEST_CASE("routing: section bypass is click-free", "[routing]")
{
    const double sr = 48000.0;
    const float amp = 0.5f; // −6 dBFS
    RenderSettings s;
    s.sampleRate = sr;
    s.blockSize = 64;
    s.parameterOverrides = wetDelayOnly(100.0);
    s.perBlockHook = [&](ClearSpaceProcessor& p, int, int startSample)
    {
        if (startSample >= at(0.5, sr) && startSample < at(0.5, sr) + s.blockSize)
            p.getAPVTS().getParameter(ParamID::delayBypass)->setValueNotifyingHost(1.0f);
    };
    auto out = renderThroughProcessor(sine(sr, 1.0, -6.0f, 1000.0), s);
    REQUIRE(out.ok());

    // Steady wet sine before the switch; silence well after the 20 ms fade.
    CHECK(peakIn(out.buffer, 0, 0.3, 0.5, sr) == Approx(amp).margin(0.01f));
    CHECK(dsp::exactlyEqual(peakIn(out.buffer, 0, 0.6, 1.0, sr), 0.0f));

    // Largest sample-to-sample step must not exceed the sine's own slope by more than a hair.
    const float ownStep = amp * static_cast<float>(2.0 * 3.14159265 * 1000.0 / sr);
    float maxStep = 0.0f;
    const auto* x = out.buffer.getReadPointer(0);
    for (int i = at(0.15, sr); i < out.buffer.getNumSamples(); ++i)
        maxStep = std::max(maxStep, std::abs(x[i] - x[i - 1]));
    CHECK(maxStep <= ownStep + 0.01f);
}

TEST_CASE("routing: external sidechain keys the ducker while the main input is silent",
          "[routing][sidechain]")
{
    const double sr = 48000.0;
    RenderSettings s;
    s.sampleRate = sr;
    s.sidechain = sine(sr, 0.5, -6.0f);

    SECTION("External + connected bus → ducks from the sidechain")
    {
        s.parameterOverrides = {{ParamID::duckSource, "External"}};
        auto out = renderThroughProcessor(silence(sr, 0.5), s);
        REQUIRE(out.ok());
        CHECK(out.usedExternalKey);
        CHECK(out.maxDelayGrDb >= 11.0f);
        CHECK(out.maxReverbGrDb >= 11.0f); // duckLink on: reverb ducker mirrors the delay's
    }

    SECTION("Internal ignores the sidechain bus")
    {
        s.parameterOverrides = {{ParamID::duckSource, "Internal"}};
        auto out = renderThroughProcessor(silence(sr, 0.5), s);
        REQUIRE(out.ok());
        CHECK_FALSE(out.usedExternalKey);
        CHECK(dsp::exactlyEqual(out.maxDelayGrDb, 0.0f));
    }

    SECTION("External without a connected bus falls back to the input")
    {
        s.sidechain = {};
        s.parameterOverrides = {{ParamID::duckSource, "External"}};
        auto out = renderThroughProcessor(sine(sr, 0.5, -6.0f), s);
        REQUIRE(out.ok());
        CHECK_FALSE(out.usedExternalKey);
        CHECK(out.maxDelayGrDb >= 11.0f);
    }

    SECTION("mono sidechain bus works too")
    {
        juce::AudioBuffer<float> monoKey(1, s.sidechain.getNumSamples());
        monoKey.copyFrom(0, 0, s.sidechain, 0, 0, s.sidechain.getNumSamples());
        s.sidechain = monoKey;
        s.parameterOverrides = {{ParamID::duckSource, "External"}};
        auto out = renderThroughProcessor(silence(sr, 0.5), s);
        REQUIRE(out.ok());
        CHECK(out.usedExternalKey);
        CHECK(out.maxDelayGrDb >= 11.0f);
    }
}

TEST_CASE("routing: duck link and enable", "[routing][sidechain]")
{
    const double sr = 48000.0;
    RenderSettings s;
    s.sampleRate = sr;
    const auto input = sine(sr, 0.5, -6.0f);

    s.parameterOverrides = {{ParamID::duckLink, "on"},
                            {ParamID::delayDuckDepth, "20"},
                            {ParamID::reverbDuckDepth, "5"}};
    auto linked = renderThroughProcessor(input, s);
    REQUIRE(linked.ok());
    CHECK(linked.maxDelayGrDb == Approx(20.0f).margin(0.5f));
    CHECK(linked.maxReverbGrDb == Approx(20.0f).margin(0.5f));

    s.parameterOverrides = {{ParamID::duckLink, "off"},
                            {ParamID::delayDuckDepth, "20"},
                            {ParamID::reverbDuckDepth, "5"}};
    auto unlinked = renderThroughProcessor(input, s);
    REQUIRE(unlinked.ok());
    CHECK(unlinked.maxDelayGrDb == Approx(20.0f).margin(0.5f));
    CHECK(unlinked.maxReverbGrDb == Approx(5.0f).margin(0.5f));

    s.parameterOverrides = {{ParamID::duckLink, "off"},
                            {ParamID::delayDuckEnable, "off"},
                            {ParamID::reverbDuckEnable, "off"}};
    auto disabled = renderThroughProcessor(input, s);
    REQUIRE(disabled.ok());
    CHECK(dsp::exactlyEqual(disabled.maxDelayGrDb, 0.0f));
    CHECK(dsp::exactlyEqual(disabled.maxReverbGrDb, 0.0f));
}

TEST_CASE("routing: mono-in layout with a sidechain never leaks the key into output 1",
          "[routing][sidechain]")
{
    const double sr = 48000.0;
    SourceSpec spec;
    spec.kind = SourceKind::sine;
    spec.seconds = 0.5;
    spec.numChannels = 1;
    const auto mono = makeSource(spec, sr);

    RenderSettings s;
    s.sampleRate = sr;
    s.monoInput = true;
    s.sidechain = noise(sr, 0.5, -6.0f);

    SECTION("mix 0 is a bit-exact mono→stereo pass-through")
    {
        s.parameterOverrides = {{ParamID::mix, "0"}, {ParamID::duckSource, "External"}};
        auto out = renderThroughProcessor(mono, s);
        REQUIRE(out.ok());
        CHECK(out.usedExternalKey);
        CHECK(bitExact(out.buffer, 0, mono, 0));
        CHECK(bitExact(out.buffer, 1, mono, 0));
    }

    SECTION("bypassed and 100 % wet is silent on both channels")
    {
        s.parameterOverrides = {{ParamID::mix, "100"},
                                {ParamID::delayBypass, "on"},
                                {ParamID::reverbBypass, "on"},
                                {ParamID::duckSource, "External"}};
        auto out = renderThroughProcessor(mono, s);
        REQUIRE(out.ok());
        CHECK(dsp::exactlyEqual(out.buffer.getMagnitude(0, out.buffer.getNumSamples()), 0.0f));
        CHECK(dsp::exactlyEqual(out.buffer.getMagnitude(1, out.buffer.getNumSamples()), 0.0f));
    }
}

TEST_CASE("routing: silence in → silence out after the tail", "[routing]")
{
    const double sr = 48000.0;
    RenderSettings s;
    s.sampleRate = sr;
    s.appendTail = true;
    s.parameterOverrides = {{ParamID::mix, "100"}}; // defaults otherwise: fb 35 %, decay 2 s
    auto out = renderThroughProcessor(impulse(sr, 0.2, -6.0f), s);
    REQUIRE(out.ok());
    const double end = static_cast<double>(out.buffer.getNumSamples()) / sr;
    CHECK(peakIn(out.buffer, 0, end - 0.01, end, sr) < 0.5f * 1.0e-3f); // ≥ 60 dB down

    // Three seconds beyond the reported tail there is nothing left at all.
    auto longer = impulse(sr, 0.2 + out.tailSeconds + 3.0, -6.0f);
    s.appendTail = false;
    out = renderThroughProcessor(longer, s);
    REQUIRE(out.ok());
    const double end2 = static_cast<double>(out.buffer.getNumSamples()) / sr;
    CHECK(peakIn(out.buffer, 0, end2 - 0.01, end2, sr) < 1.0e-5f);
    CHECK(computeStats(out.buffer, sr).isClean());
}

TEST_CASE("routing: no NaN/Inf at parameter extremes, all rates and block sizes", "[routing]")
{
    const Overrides maxed = {{ParamID::inputTrim, "24"},
                             {ParamID::outputTrim, "24"},
                             {ParamID::mix, "100"},
                             {ParamID::delayTime, "2000"},
                             {ParamID::delayFeedback, "100"},
                             {ParamID::delayLevel, "6"},
                             {ParamID::reverbDecay, "20"},
                             {ParamID::reverbLevel, "6"},
                             {ParamID::delayDuckDepth, "40"},
                             {ParamID::delayDuckThreshold, "-60"},
                             {ParamID::delayDuckAttack, "0.1"},
                             {ParamID::delayDuckHold, "500"},
                             {ParamID::delayDuckRelease, "20"},
                             {ParamID::delayDuckKeyHPF, "1000"}};
    const Overrides minimal = {{ParamID::inputTrim, "-24"},
                               {ParamID::outputTrim, "-24"},
                               {ParamID::mix, "0"},
                               {ParamID::delayTime, "1"},
                               {ParamID::delayFeedback, "0"},
                               {ParamID::delayLevel, "-60"},
                               {ParamID::reverbDecay, "0.1"},
                               {ParamID::reverbLevel, "-60"},
                               {ParamID::duckLink, "off"},
                               {ParamID::delayDuckDepth, "0"},
                               {ParamID::delayDuckThreshold, "0"},
                               {ParamID::delayDuckAttack, "200"},
                               {ParamID::delayDuckHold, "0"},
                               {ParamID::delayDuckRelease, "3000"},
                               {ParamID::delayDuckKeyHPF, "20"},
                               {ParamID::routing, "Parallel"}};

    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int block : {1, 4096})
            for (const auto* overrides : {&maxed, &minimal})
            {
                INFO("sr = " << sr << " block = " << block);
                RenderSettings s;
                s.sampleRate = sr;
                s.blockSize = block;
                s.parameterOverrides = *overrides;
                auto out = renderThroughProcessor(noise(sr, 0.3, -6.0f), s);
                REQUIRE(out.ok());
                const auto stats = computeStats(out.buffer, sr);
                CHECK(stats.isClean());
                CHECK(stats.channels[0].peak < 100.0f); // bounded even at +48 dB of trim
            }
}

TEST_CASE("routing: CPU budget for 60 s of stereo at 48 kHz / 512", "[routing][cpu]")
{
    const double sr = 48000.0;
    const double seconds = 60.0;
    const auto input = noise(sr, seconds, -6.0f);

    RenderSettings s;
    s.sampleRate = sr;
    s.blockSize = 512;
    s.parameterOverrides = {{ParamID::duckSource, "Internal"}};

    const auto start = std::chrono::steady_clock::now();
    auto out = renderThroughProcessor(input, s);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
    REQUIRE(out.ok());

    const double ratio = elapsed.count() / seconds;
    double budget = 0.03;
    if (const char* env = std::getenv("CLEARSPACE_CPU_BUDGET"))
        budget = std::atof(env);
    std::cout << "cpu: realtime ratio = " << ratio << " (budget " << budget << ")\n";
    CHECK(ratio < budget);
}
