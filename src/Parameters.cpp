#include "Parameters.h"

#include <cmath>

namespace clearspace::params
{

namespace
{

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using Group = juce::AudioProcessorParameterGroup;
using FloatAttr = juce::AudioParameterFloatAttributes;

juce::ParameterID id(const char* paramId)
{
    return {paramId, parameterVersionHint};
}

// ---- ranges ---------------------------------------------------------------------------

/** True logarithmic mapping: the knob's midpoint is the geometric mean of the ends. */
juce::NormalisableRange<float> logRange(float lo, float hi)
{
    jassert(lo > 0.0f && hi > lo);
    return {lo, hi, [](float start, float end, float normalised)
            { return start * std::pow(end / start, normalised); },
            [](float start, float end, float value)
            { return std::log(value / start) / std::log(end / start); },
            [](float start, float end, float value) { return juce::jlimit(start, end, value); }};
}

juce::NormalisableRange<float> linRange(float lo, float hi, float step = 0.0f)
{
    return {lo, hi, step};
}

// ---- value <-> text -------------------------------------------------------------------

juce::String fmt(float value, int decimals)
{
    return juce::String(value, decimals);
}

juce::String msToText(float ms, int)
{
    if (ms < 10.0f)
        return fmt(ms, 2) + " ms";
    if (ms < 100.0f)
        return fmt(ms, 1) + " ms";
    return fmt(std::round(ms), 0) + " ms";
}

juce::String secondsToText(float s, int)
{
    return fmt(s, 2) + " s";
}

juce::String hzToText(float hz, int)
{
    if (hz >= 1000.0f)
        return fmt(hz / 1000.0f, 2) + " kHz";
    if (hz < 10.0f)
        return fmt(hz, 2) + " Hz";
    return fmt(hz, 1) + " Hz";
}

juce::String dbToText(float db, int)
{
    return fmt(db, 1) + " dB";
}

juce::String dbfsToText(float db, int)
{
    return fmt(db, 1) + " dBFS";
}

juce::String percentToText(float pc, int)
{
    return fmt(std::round(pc), 0) + " %";
}

/** Parses the leading number of a string; "k" (as in "2.5 kHz") multiplies by 1000. */
float textToValue(const juce::String& text)
{
    auto trimmed = text.trim();
    auto value = trimmed.getFloatValue();
    if (trimmed.containsIgnoreCase("k"))
        value *= 1000.0f;
    return value;
}

// ---- builders -------------------------------------------------------------------------

using TextFn = juce::String (*)(float, int);

std::unique_ptr<juce::AudioParameterFloat> floatParam(const char* paramId, const char* name,
                                                      juce::NormalisableRange<float> range,
                                                      float defaultValue, const char* label,
                                                      TextFn toText)
{
    auto attrs = FloatAttr()
                     .withLabel(label)
                     .withStringFromValueFunction(toText)
                     .withValueFromStringFunction(textToValue);
    return std::make_unique<juce::AudioParameterFloat>(id(paramId), name, range, defaultValue,
                                                       attrs);
}

std::unique_ptr<juce::AudioParameterChoice> choiceParam(const char* paramId, const char* name,
                                                        const juce::StringArray& choices,
                                                        int defaultIndex)
{
    return std::make_unique<juce::AudioParameterChoice>(id(paramId), name, choices, defaultIndex);
}

std::unique_ptr<juce::AudioParameterBool> boolParam(const char* paramId, const char* name,
                                                    bool defaultValue)
{
    return std::make_unique<juce::AudioParameterBool>(id(paramId), name, defaultValue);
}

/** The seven ducker parameters, shared by the delay and reverb sections (SPEC §3 defaults). */
struct DuckIDs
{
    const char* enable;
    const char* depth;
    const char* threshold;
    const char* attack;
    const char* hold;
    const char* release;
    const char* keyHPF;
};

std::unique_ptr<Group> duckGroup(const char* groupId, const char* groupName, const DuckIDs& ids)
{
    return std::make_unique<Group>(
        groupId, groupName, "|", boolParam(ids.enable, "Duck Enable", true),
        floatParam(ids.depth, "Duck Depth", linRange(0.0f, 40.0f), 12.0f, "dB", dbToText),
        floatParam(ids.threshold, "Duck Threshold", linRange(-60.0f, 0.0f), -30.0f, "dBFS",
                   dbfsToText),
        floatParam(ids.attack, "Duck Attack", logRange(0.1f, 200.0f), 5.0f, "ms", msToText),
        floatParam(ids.hold, "Duck Hold", linRange(0.0f, 500.0f), 60.0f, "ms", msToText),
        floatParam(ids.release, "Duck Release", logRange(20.0f, 3000.0f), 400.0f, "ms", msToText),
        floatParam(ids.keyHPF, "Duck Key HPF", logRange(20.0f, 1000.0f), 120.0f, "Hz", hzToText));
}

} // namespace

// ---- choice lists ---------------------------------------------------------------------

const juce::StringArray& routingChoices()
{
    static const juce::StringArray choices{"Serial", "Parallel"};
    return choices;
}

const juce::StringArray& duckSourceChoices()
{
    static const juce::StringArray choices{"Internal", "External"};
    return choices;
}

const juce::StringArray& delayModeChoices()
{
    static const juce::StringArray choices{"Digital", "BBD", "Tape"};
    return choices;
}

const juce::StringArray& delayNoteChoices()
{
    static const juce::StringArray choices{"1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"};
    return choices;
}

const juce::StringArray& delayNoteModChoices()
{
    static const juce::StringArray choices{"Straight", "Dotted", "Triplet"};
    return choices;
}

const juce::StringArray& delayStereoModeChoices()
{
    static const juce::StringArray choices{"Stereo", "PingPong"};
    return choices;
}

const juce::StringArray& reverbModeChoices()
{
    static const juce::StringArray choices{"Plate", "Hall", "Room"};
    return choices;
}

// ---- layout ---------------------------------------------------------------------------

Layout createLayout()
{
    using namespace ParamID;
    Layout layout;

    layout.add(std::make_unique<Group>(
        "global", "Global", "|",
        floatParam(inputTrim, "Input Trim", linRange(-24.0f, 24.0f), 0.0f, "dB", dbToText),
        floatParam(outputTrim, "Output Trim", linRange(-24.0f, 24.0f), 0.0f, "dB", dbToText),
        floatParam(mix, "Mix", linRange(0.0f, 100.0f), 50.0f, "%", percentToText),
        choiceParam(routing, "Routing", routingChoices(), 0),
        choiceParam(duckSource, "Duck Source", duckSourceChoices(), 0),
        boolParam(duckLink, "Duck Link", true)));

    layout.add(std::make_unique<Group>(
        "delay", "Delay", "|", boolParam(delayBypass, "Delay Bypass", false),
        choiceParam(delayMode, "Delay Mode", delayModeChoices(), 0),
        floatParam(delayTime, "Delay Time", logRange(1.0f, 2000.0f), 375.0f, "ms", msToText),
        boolParam(delaySync, "Delay Sync", false),
        choiceParam(delayNote, "Delay Note", delayNoteChoices(), 3),
        choiceParam(delayNoteMod, "Delay Note Mod", delayNoteModChoices(), 0),
        floatParam(delayFeedback, "Delay Feedback", linRange(0.0f, 100.0f), 35.0f, "%",
                   percentToText),
        floatParam(delayLowCut, "Delay Low Cut", logRange(20.0f, 2000.0f), 150.0f, "Hz", hzToText),
        floatParam(delayHighCut, "Delay High Cut", logRange(1000.0f, 20000.0f), 8000.0f, "Hz",
                   hzToText),
        floatParam(delayDrive, "Delay Drive", linRange(0.0f, 100.0f), 20.0f, "%", percentToText),
        floatParam(delayAge, "Delay Age", linRange(0.0f, 100.0f), 30.0f, "%", percentToText),
        floatParam(delayMod, "Delay Mod", linRange(0.0f, 100.0f), 10.0f, "%", percentToText),
        floatParam(delayModRate, "Delay Mod Rate", logRange(0.1f, 10.0f), 0.8f, "Hz", hzToText),
        choiceParam(delayStereoMode, "Delay Stereo Mode", delayStereoModeChoices(), 0),
        floatParam(delayLevel, "Delay Level", linRange(-60.0f, 6.0f), 0.0f, "dB", dbToText)));

    layout.add(duckGroup("delayDuck", "Delay Duck",
                         {delayDuckEnable, delayDuckDepth, delayDuckThreshold, delayDuckAttack,
                          delayDuckHold, delayDuckRelease, delayDuckKeyHPF}));

    layout.add(std::make_unique<Group>(
        "reverb", "Reverb", "|", boolParam(reverbBypass, "Reverb Bypass", false),
        choiceParam(reverbMode, "Reverb Mode", reverbModeChoices(), 0),
        floatParam(reverbPreDelay, "Reverb Pre-Delay", linRange(0.0f, 250.0f), 20.0f, "ms",
                   msToText),
        floatParam(reverbDecay, "Reverb Decay", logRange(0.1f, 20.0f), 2.0f, "s", secondsToText),
        floatParam(reverbSize, "Reverb Size", linRange(0.0f, 100.0f), 50.0f, "%", percentToText),
        floatParam(reverbDamping, "Reverb Damping", logRange(1000.0f, 20000.0f), 6000.0f, "Hz",
                   hzToText),
        floatParam(reverbLowCut, "Reverb Low Cut", logRange(20.0f, 500.0f), 100.0f, "Hz", hzToText),
        floatParam(reverbHighCut, "Reverb High Cut", logRange(1000.0f, 20000.0f), 12000.0f, "Hz",
                   hzToText),
        floatParam(reverbDiffusion, "Reverb Diffusion", linRange(0.0f, 100.0f), 80.0f, "%",
                   percentToText),
        floatParam(reverbModRate, "Reverb Mod Rate", logRange(0.1f, 5.0f), 1.0f, "Hz", hzToText),
        floatParam(reverbModDepth, "Reverb Mod Depth", linRange(0.0f, 100.0f), 30.0f, "%",
                   percentToText),
        floatParam(reverbWidth, "Reverb Width", linRange(0.0f, 100.0f), 100.0f, "%", percentToText),
        floatParam(reverbLevel, "Reverb Level", linRange(-60.0f, 6.0f), 0.0f, "dB", dbToText)));

    layout.add(duckGroup("reverbDuck", "Reverb Duck",
                         {reverbDuckEnable, reverbDuckDepth, reverbDuckThreshold, reverbDuckAttack,
                          reverbDuckHold, reverbDuckRelease, reverbDuckKeyHPF}));

    return layout;
}

} // namespace clearspace::params
