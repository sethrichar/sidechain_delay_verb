// Guards the APVTS layout against drift from SPEC §6. Presets and automation depend on
// every ID, default, range, and choice order staying exactly as specified.

#include "Parameters.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using namespace clearspace;
using Catch::Approx;

namespace
{

struct FloatSpec
{
    const char* id;
    float lo;
    float hi;
    float def;
};

struct ChoiceSpec
{
    const char* id;
    juce::StringArray choices;
    int def;
};

struct BoolSpec
{
    const char* id;
    bool def;
};

// SPEC §6, transcribed. Keep this table in sync with the spec, not with Parameters.cpp.
const FloatSpec floatSpecs[] = {
    {ParamID::inputTrim, -24.0f, 24.0f, 0.0f},
    {ParamID::outputTrim, -24.0f, 24.0f, 0.0f},
    {ParamID::mix, 0.0f, 100.0f, 50.0f},
    {ParamID::delayTime, 1.0f, 2000.0f, 375.0f},
    {ParamID::delayFeedback, 0.0f, 100.0f, 35.0f},
    {ParamID::delayLowCut, 20.0f, 2000.0f, 150.0f},
    {ParamID::delayHighCut, 1000.0f, 20000.0f, 8000.0f},
    {ParamID::delayDrive, 0.0f, 100.0f, 20.0f},
    {ParamID::delayAge, 0.0f, 100.0f, 30.0f},
    {ParamID::delayMod, 0.0f, 100.0f, 10.0f},
    {ParamID::delayModRate, 0.1f, 10.0f, 0.8f},
    {ParamID::delayLevel, -60.0f, 6.0f, 0.0f},
    {ParamID::delayDuckDepth, 0.0f, 40.0f, 12.0f},
    {ParamID::delayDuckThreshold, -60.0f, 0.0f, -30.0f},
    {ParamID::delayDuckAttack, 0.1f, 200.0f, 5.0f},
    {ParamID::delayDuckHold, 0.0f, 500.0f, 60.0f},
    {ParamID::delayDuckRelease, 20.0f, 3000.0f, 400.0f},
    {ParamID::delayDuckKeyHPF, 20.0f, 1000.0f, 120.0f},
    {ParamID::reverbPreDelay, 0.0f, 250.0f, 20.0f},
    {ParamID::reverbDecay, 0.1f, 20.0f, 2.0f},
    {ParamID::reverbSize, 0.0f, 100.0f, 50.0f},
    {ParamID::reverbDamping, 1000.0f, 20000.0f, 6000.0f},
    {ParamID::reverbLowCut, 20.0f, 500.0f, 100.0f},
    {ParamID::reverbHighCut, 1000.0f, 20000.0f, 12000.0f},
    {ParamID::reverbDiffusion, 0.0f, 100.0f, 80.0f},
    {ParamID::reverbModRate, 0.1f, 5.0f, 1.0f},
    {ParamID::reverbModDepth, 0.0f, 100.0f, 30.0f},
    {ParamID::reverbWidth, 0.0f, 100.0f, 100.0f},
    {ParamID::reverbLevel, -60.0f, 6.0f, 0.0f},
    {ParamID::reverbDuckDepth, 0.0f, 40.0f, 12.0f},
    {ParamID::reverbDuckThreshold, -60.0f, 0.0f, -30.0f},
    {ParamID::reverbDuckAttack, 0.1f, 200.0f, 5.0f},
    {ParamID::reverbDuckHold, 0.0f, 500.0f, 60.0f},
    {ParamID::reverbDuckRelease, 20.0f, 3000.0f, 400.0f},
    {ParamID::reverbDuckKeyHPF, 20.0f, 1000.0f, 120.0f},
};

const ChoiceSpec choiceSpecs[] = {
    {ParamID::routing, {"Serial", "Parallel"}, 0},
    {ParamID::duckSource, {"Internal", "External"}, 0},
    {ParamID::delayMode, {"Digital", "BBD", "Tape"}, 0},
    {ParamID::delayNote, {"1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"}, 3},
    {ParamID::delayNoteMod, {"Straight", "Dotted", "Triplet"}, 0},
    {ParamID::delayStereoMode, {"Stereo", "PingPong"}, 0},
    {ParamID::reverbMode, {"Plate", "Hall", "Room"}, 0},
};

const BoolSpec boolSpecs[] = {
    {ParamID::duckLink, true},      {ParamID::delayBypass, false},
    {ParamID::delaySync, false},    {ParamID::delayDuckEnable, true},
    {ParamID::reverbBypass, false}, {ParamID::reverbDuckEnable, true},
};

constexpr size_t expectedCount =
    std::size(floatSpecs) + std::size(choiceSpecs) + std::size(boolSpecs);

} // namespace

TEST_CASE("parameters: every SPEC §6 parameter exists exactly once", "[parameters]")
{
    ClearSpaceProcessor processor;
    auto& apvts = processor.getAPVTS();

    std::set<std::string> seen;
    for (const auto* id : ParamID::all)
    {
        INFO("id = " << id);
        CHECK(apvts.getParameter(id) != nullptr);
        CHECK(seen.insert(id).second); // no duplicates in ParamID::all
    }

    CHECK(ParamID::all.size() == expectedCount);
    CHECK(static_cast<size_t>(processor.getParameters().size()) == expectedCount);
}

TEST_CASE("parameters: float ranges and defaults match SPEC §6", "[parameters]")
{
    ClearSpaceProcessor processor;
    auto& apvts = processor.getAPVTS();

    for (const auto& spec : floatSpecs)
    {
        INFO("id = " << spec.id);
        auto* param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(spec.id));
        REQUIRE(param != nullptr);
        const auto& range = param->getNormalisableRange();
        CHECK(range.start == Approx(spec.lo));
        CHECK(range.end == Approx(spec.hi));
        CHECK(param->get() == Approx(spec.def));

        // Round trip through normalisation must land back on the default.
        const auto norm = range.convertTo0to1(spec.def);
        CHECK(range.convertFrom0to1(norm) == Approx(spec.def).epsilon(1e-4));

        // Ends of the range must normalise to exactly 0 and 1.
        CHECK(range.convertTo0to1(spec.lo) == Approx(0.0f).margin(1e-6));
        CHECK(range.convertTo0to1(spec.hi) == Approx(1.0f).margin(1e-6));
    }
}

TEST_CASE("parameters: choice lists and defaults match SPEC §6", "[parameters]")
{
    ClearSpaceProcessor processor;
    auto& apvts = processor.getAPVTS();

    for (const auto& spec : choiceSpecs)
    {
        INFO("id = " << spec.id);
        auto* param = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(spec.id));
        REQUIRE(param != nullptr);
        CHECK(param->choices == spec.choices);
        CHECK(param->getIndex() == spec.def);
    }
}

TEST_CASE("parameters: bool defaults match SPEC §6", "[parameters]")
{
    ClearSpaceProcessor processor;
    auto& apvts = processor.getAPVTS();

    for (const auto& spec : boolSpecs)
    {
        INFO("id = " << spec.id);
        auto* param = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(spec.id));
        REQUIRE(param != nullptr);
        CHECK(param->get() == spec.def);
    }
}

TEST_CASE("parameters: log ranges put the geometric mean at the knob midpoint", "[parameters]")
{
    ClearSpaceProcessor processor;
    auto& range = processor.getAPVTS().getParameter(ParamID::delayTime)->getNormalisableRange();
    // sqrt(1 * 2000) ≈ 44.72 ms
    CHECK(range.convertFrom0to1(0.5f) == Approx(44.72f).epsilon(1e-3));
}

TEST_CASE("parameters: state round-trips through get/setStateInformation", "[parameters][state]")
{
    ClearSpaceProcessor a;
    auto* time = a.getAPVTS().getParameter(ParamID::delayTime);
    auto* mode = a.getAPVTS().getParameter(ParamID::reverbMode);
    time->setValueNotifyingHost(time->getNormalisableRange().convertTo0to1(750.0f));
    mode->setValueNotifyingHost(mode->getNormalisableRange().convertTo0to1(2.0f)); // Room

    juce::MemoryBlock blob;
    a.getStateInformation(blob);
    REQUIRE(blob.getSize() > 0);

    ClearSpaceProcessor b;
    b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));

    auto* bTime =
        dynamic_cast<juce::AudioParameterFloat*>(b.getAPVTS().getParameter(ParamID::delayTime));
    auto* bMode =
        dynamic_cast<juce::AudioParameterChoice*>(b.getAPVTS().getParameter(ParamID::reverbMode));
    REQUIRE(bTime != nullptr);
    REQUIRE(bMode != nullptr);
    CHECK(bTime->get() == Approx(750.0f).epsilon(1e-3));
    CHECK(bMode->getIndex() == 2);
    CHECK(static_cast<int>(b.getAPVTS().state.getProperty("stateVersion")) == params::stateVersion);
}
