#pragma once

// Single source of truth for the APVTS layout (SPEC §6).
// Every parameter ID is declared here once; UI and DSP include this header and never
// spell an ID string anywhere else (CLAUDE.md §3).

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace clearspace
{

namespace ParamID
{
// Global
inline constexpr const char* inputTrim = "inputTrim";
inline constexpr const char* outputTrim = "outputTrim";
inline constexpr const char* mix = "mix";
inline constexpr const char* routing = "routing";
inline constexpr const char* duckSource = "duckSource";
inline constexpr const char* duckLink = "duckLink";

// Delay
inline constexpr const char* delayBypass = "delayBypass";
inline constexpr const char* delayMode = "delayMode";
inline constexpr const char* delayTime = "delayTime";
inline constexpr const char* delaySync = "delaySync";
inline constexpr const char* delayNote = "delayNote";
inline constexpr const char* delayNoteMod = "delayNoteMod";
inline constexpr const char* delayFeedback = "delayFeedback";
inline constexpr const char* delayLowCut = "delayLowCut";
inline constexpr const char* delayHighCut = "delayHighCut";
inline constexpr const char* delayDrive = "delayDrive";
inline constexpr const char* delayAge = "delayAge";
inline constexpr const char* delayMod = "delayMod";
inline constexpr const char* delayModRate = "delayModRate";
inline constexpr const char* delayStereoMode = "delayStereoMode";
inline constexpr const char* delayLevel = "delayLevel";
inline constexpr const char* delayDuckEnable = "delayDuckEnable";
inline constexpr const char* delayDuckDepth = "delayDuckDepth";
inline constexpr const char* delayDuckThreshold = "delayDuckThreshold";
inline constexpr const char* delayDuckAttack = "delayDuckAttack";
inline constexpr const char* delayDuckHold = "delayDuckHold";
inline constexpr const char* delayDuckRelease = "delayDuckRelease";
inline constexpr const char* delayDuckKeyHPF = "delayDuckKeyHPF";

// Reverb
inline constexpr const char* reverbBypass = "reverbBypass";
inline constexpr const char* reverbMode = "reverbMode";
inline constexpr const char* reverbPreDelay = "reverbPreDelay";
inline constexpr const char* reverbDecay = "reverbDecay";
inline constexpr const char* reverbSize = "reverbSize";
inline constexpr const char* reverbDamping = "reverbDamping";
inline constexpr const char* reverbLowCut = "reverbLowCut";
inline constexpr const char* reverbHighCut = "reverbHighCut";
inline constexpr const char* reverbDiffusion = "reverbDiffusion";
inline constexpr const char* reverbModRate = "reverbModRate";
inline constexpr const char* reverbModDepth = "reverbModDepth";
inline constexpr const char* reverbWidth = "reverbWidth";
inline constexpr const char* reverbLevel = "reverbLevel";
inline constexpr const char* reverbDuckEnable = "reverbDuckEnable";
inline constexpr const char* reverbDuckDepth = "reverbDuckDepth";
inline constexpr const char* reverbDuckThreshold = "reverbDuckThreshold";
inline constexpr const char* reverbDuckAttack = "reverbDuckAttack";
inline constexpr const char* reverbDuckHold = "reverbDuckHold";
inline constexpr const char* reverbDuckRelease = "reverbDuckRelease";
inline constexpr const char* reverbDuckKeyHPF = "reverbDuckKeyHPF";

/** Every ID above, in layout order. Used by tests and by the Phase 6 UI walk. */
inline constexpr std::array all = {
    inputTrim,
    outputTrim,
    mix,
    routing,
    duckSource,
    duckLink,
    delayBypass,
    delayMode,
    delayTime,
    delaySync,
    delayNote,
    delayNoteMod,
    delayFeedback,
    delayLowCut,
    delayHighCut,
    delayDrive,
    delayAge,
    delayMod,
    delayModRate,
    delayStereoMode,
    delayLevel,
    delayDuckEnable,
    delayDuckDepth,
    delayDuckThreshold,
    delayDuckAttack,
    delayDuckHold,
    delayDuckRelease,
    delayDuckKeyHPF,
    reverbBypass,
    reverbMode,
    reverbPreDelay,
    reverbDecay,
    reverbSize,
    reverbDamping,
    reverbLowCut,
    reverbHighCut,
    reverbDiffusion,
    reverbModRate,
    reverbModDepth,
    reverbWidth,
    reverbLevel,
    reverbDuckEnable,
    reverbDuckDepth,
    reverbDuckThreshold,
    reverbDuckAttack,
    reverbDuckHold,
    reverbDuckRelease,
    reverbDuckKeyHPF,
};
} // namespace ParamID

namespace params
{
/** Version stored in the APVTS state root as the "stateVersion" property. Bump it when the
    layout changes in a way that needs migration (hook lands in Phase 7). */
inline constexpr int stateVersion = 1;

/** juce::ParameterID version hint. AU hosts key automation on this; never lower it. */
inline constexpr int parameterVersionHint = 1;

/** Choice lists — index order is part of the saved state, so never reorder; append only. */
const juce::StringArray& routingChoices();         // Serial, Parallel
const juce::StringArray& duckSourceChoices();      // Internal, External
const juce::StringArray& delayModeChoices();       // Digital, BBD, Tape
const juce::StringArray& delayNoteChoices();       // 1/64 … 1/1
const juce::StringArray& delayNoteModChoices();    // Straight, Dotted, Triplet
const juce::StringArray& delayStereoModeChoices(); // Stereo, PingPong
const juce::StringArray& reverbModeChoices();      // Plate, Hall, Room

/** The full SPEC §6 layout. */
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace params

} // namespace clearspace
