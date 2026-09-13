// Phase 0 acceptance: the render tool round-trips a WAV bit-exactly through the processor
// with mix = 0 (a bit-exact pass-through by design, see dsp/Routing), and the stats/sources
// behave as documented.

#include "Parameters.h"
#include "Renderer.h"
#include "Sources.h"
#include "Stats.h"
#include "WavIO.h"
#include "dsp/Util.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace clearspace;
using namespace clearspace::render;
using Catch::Approx;

namespace
{

juce::File scratchFile(const char* name)
{
    return juce::File(CLEARSPACE_TEST_SCRATCH_DIR).getChildFile(name);
}

/** Sample-exact equality. Compares values, not bit patterns, so −0.0 == +0.0 (a −0.0 source
    sample comes back as +0.0 after "dry·1 + wet·0") while NaN never compares equal. */
bool bitExact(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
    {
        const auto* x = a.getReadPointer(ch);
        const auto* y = b.getReadPointer(ch);
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (!clearspace::dsp::exactlyEqual(x[i], y[i]))
                return false;
    }
    return true;
}

} // namespace

TEST_CASE("render: WAV round trip at mix 0 is bit-exact", "[render][wav]")
{
    const double sr = 48000.0;
    SourceSpec spec;
    spec.kind = SourceKind::speechlike;
    spec.seconds = 1.0;
    const auto original = makeSource(spec, sr);

    const auto inFile = scratchFile("roundtrip_in.wav");
    const auto outFile = scratchFile("roundtrip_out.wav");
    REQUIRE(writeWav(inFile, original, sr));

    auto readBack = readWav(inFile);
    REQUIRE(readBack.has_value());
    CHECK(readBack->sampleRate == Approx(sr));
    REQUIRE(bitExact(readBack->buffer, original)); // float32 WAV is lossless

    RenderSettings settings;
    settings.sampleRate = sr;
    settings.blockSize = 397; // deliberately awkward block size
    settings.parameterOverrides = {{ParamID::mix, "0"}};
    auto out = renderThroughProcessor(readBack->buffer, settings);
    REQUIRE(out.ok());
    REQUIRE(bitExact(out.buffer, original));

    REQUIRE(writeWav(outFile, out.buffer, sr));
    auto outBack = readWav(outFile);
    REQUIRE(outBack.has_value());
    CHECK(bitExact(outBack->buffer, original));
}

TEST_CASE("render: mix 0 is bit-exact for every block size and sample rate", "[render]")
{
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int block : {1, 7, 64, 512, 4096})
        {
            INFO("sr = " << sr << " block = " << block);
            SourceSpec spec;
            spec.kind = SourceKind::noise;
            spec.seconds = 0.25;
            const auto input = makeSource(spec, sr);

            RenderSettings settings;
            settings.sampleRate = sr;
            settings.blockSize = block;
            settings.parameterOverrides = {{ParamID::mix, "0"}};
            auto out = renderThroughProcessor(input, settings);
            REQUIRE(out.ok());
            CHECK(bitExact(out.buffer, input));
            CHECK(computeStats(out.buffer, sr).isClean());
        }
}

TEST_CASE("render: mono input is duplicated to both output channels", "[render]")
{
    SourceSpec spec;
    spec.kind = SourceKind::sine;
    spec.seconds = 0.1;
    spec.numChannels = 1;
    const auto mono = makeSource(spec, 48000.0);

    RenderSettings settings;
    settings.parameterOverrides = {{ParamID::mix, "0"}};
    auto out = renderThroughProcessor(mono, settings);
    REQUIRE(out.ok());
    REQUIRE(out.buffer.getNumChannels() == 2);
    CHECK(std::memcmp(out.buffer.getReadPointer(0), mono.getReadPointer(0),
                      sizeof(float) * static_cast<size_t>(mono.getNumSamples())) == 0);
    CHECK(std::memcmp(out.buffer.getReadPointer(1), mono.getReadPointer(0),
                      sizeof(float) * static_cast<size_t>(mono.getNumSamples())) == 0);
}

TEST_CASE("render: stats measure peak, RMS, first-nonzero, NaN and Inf", "[render][stats]")
{
    const double sr = 48000.0;

    SECTION("impulse")
    {
        SourceSpec spec;
        spec.kind = SourceKind::impulse;
        spec.levelDb = -6.0f;
        spec.seconds = 0.01;
        auto stats = computeStats(makeSource(spec, sr), sr);
        REQUIRE(stats.channels.size() == 2);
        CHECK(stats.channels[0].peakDb == Approx(-6.0f).margin(0.01));
        CHECK(stats.channels[0].firstNonZero == 0);
        CHECK(stats.isClean());
    }

    SECTION("sine level and RMS")
    {
        SourceSpec spec;
        spec.kind = SourceKind::sine;
        spec.levelDb = -6.0f;
        spec.seconds = 1.0;
        auto stats = computeStats(makeSource(spec, sr), sr);
        CHECK(stats.channels[0].peakDb == Approx(-6.0f).margin(0.02));
        CHECK(stats.channels[0].rmsDb == Approx(-6.0f - 3.01f).margin(0.05)); // sine RMS = peak/√2
    }

    SECTION("silence")
    {
        juce::AudioBuffer<float> silence(2, 100);
        silence.clear();
        auto stats = computeStats(silence, sr);
        CHECK(stats.channels[0].peak == Approx(0.0f).margin(0.0f));
        CHECK(stats.channels[0].firstNonZero == -1);
        CHECK(stats.channels[0].peakDb <= -200.0f);
    }

    SECTION("NaN and Inf are counted, not propagated into peak/RMS")
    {
        juce::AudioBuffer<float> bad(1, 4);
        bad.clear();
        bad.setSample(0, 1, std::numeric_limits<float>::quiet_NaN());
        bad.setSample(0, 2, std::numeric_limits<float>::infinity());
        bad.setSample(0, 3, 0.5f);
        auto stats = computeStats(bad, sr);
        CHECK(stats.channels[0].nanCount == 1);
        CHECK(stats.channels[0].infCount == 1);
        CHECK_FALSE(stats.isClean());
        CHECK(stats.channels[0].peak == Approx(0.5f));
        CHECK(stats.channels[0].firstNonZero == 3);
    }

    SECTION("formatStats is key=value per line")
    {
        juce::AudioBuffer<float> b(1, 1);
        b.clear();
        const auto text = formatStats(computeStats(b, sr));
        CHECK(text.find("sampleRate=48000") != std::string::npos);
        CHECK(text.find("channels=1\n") != std::string::npos);
        CHECK(text.find("ch0.firstNonZero=-1\n") != std::string::npos);
    }
}

TEST_CASE("render: --set overrides parameters and rejects bad input", "[render][params]")
{
    ClearSpaceProcessor processor;
    auto& apvts = processor.getAPVTS();

    CHECK(applyParameterOverride(apvts, ParamID::delayTime, "500").empty());
    CHECK(dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(ParamID::delayTime))->get() ==
          Approx(500.0f).epsilon(1e-4));

    CHECK(applyParameterOverride(apvts, ParamID::delayMode, "tape").empty());
    CHECK(dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(ParamID::delayMode))
              ->getIndex() == 2);

    CHECK(applyParameterOverride(apvts, ParamID::reverbMode, "1").empty());
    CHECK(dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(ParamID::reverbMode))
              ->getIndex() == 1);

    CHECK(applyParameterOverride(apvts, ParamID::delayBypass, "on").empty());
    CHECK(dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(ParamID::delayBypass))->get());

    CHECK_FALSE(applyParameterOverride(apvts, "noSuchParam", "1").empty());
    CHECK_FALSE(applyParameterOverride(apvts, ParamID::delayTime, "9999").empty()); // out of range
    CHECK_FALSE(applyParameterOverride(apvts, ParamID::delayMode, "Analog").empty());
    CHECK_FALSE(applyParameterOverride(apvts, ParamID::delayBypass, "maybe").empty());

    RenderSettings settings;
    settings.parameterOverrides = {{"noSuchParam", "1"}};
    auto out = renderThroughProcessor(juce::AudioBuffer<float>(2, 16), settings);
    CHECK_FALSE(out.ok());
}

TEST_CASE("render: synthetic sources are deterministic and at the requested level",
          "[render][sources]")
{
    const double sr = 48000.0;
    for (auto kind : {SourceKind::impulse, SourceKind::sine, SourceKind::burst, SourceKind::noise,
                      SourceKind::speechlike})
    {
        SourceSpec spec;
        spec.kind = kind;
        spec.seconds = 0.5;
        spec.levelDb = -12.0f;
        const auto a = makeSource(spec, sr);
        const auto b = makeSource(spec, sr);
        CHECK(bitExact(a, b));
        auto stats = computeStats(a, sr);
        CHECK(stats.isClean());
        CHECK(stats.channels[0].peakDb == Approx(-12.0f).margin(0.05));
        CHECK(stats.channels[0].firstNonZero >= 0);
    }
}
