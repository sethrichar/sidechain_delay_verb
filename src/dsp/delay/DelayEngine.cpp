#include "DelayEngine.h"

#include <algorithm>
#include <array>

namespace clearspace::dsp
{

float DelayEngine::clampTimeMs(Mode mode, float ms) noexcept
{
    switch (mode)
    {
    case Mode::bbd:
        return std::clamp(ms, 20.0f, 1000.0f);
    case Mode::tape:
        return std::clamp(ms, 20.0f, 2000.0f);
    case Mode::digital:
    default:
        return std::clamp(ms, DigitalDelay::minTimeMs, DigitalDelay::maxTimeMs);
    }
}

double DelayEngine::noteLengthSeconds(double bpm, int noteIndex, int noteModIndex) noexcept
{
    // params::delayNoteChoices(): 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1/1 → quarter-note beats.
    constexpr std::array<double, 7> beats = {0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0};
    // params::delayNoteModChoices(): Straight, Dotted, Triplet.
    constexpr std::array<double, 3> modifier = {1.0, 1.5, 2.0 / 3.0};

    const auto n =
        static_cast<size_t>(std::clamp(noteIndex, 0, static_cast<int>(beats.size()) - 1));
    const auto m =
        static_cast<size_t>(std::clamp(noteModIndex, 0, static_cast<int>(modifier.size()) - 1));
    const double tempo = bpm > 0.0 ? bpm : fallbackBpm;
    return beats[n] * modifier[m] * 60.0 / tempo;
}

double DelayEngine::tailSecondsFor(Mode mode, float timeMs, float feedback) noexcept
{
    // Phase 2: every mode is the Digital delay.
    return DigitalDelay::tailSecondsFor(clampTimeMs(mode, timeMs), feedback);
}

void DelayEngine::prepare(double sampleRate, int maxBlockSize)
{
    digital.prepare(sampleRate, maxBlockSize);
    setParams(current);
}

void DelayEngine::reset() noexcept
{
    digital.reset();
}

void DelayEngine::setParams(const Params& params) noexcept
{
    current = params;

    DigitalDelay::Params d;
    d.timeMs = clampTimeMs(params.mode, params.timeMs);
    d.feedback = params.feedback;
    d.lowCutHz = params.lowCutHz;
    d.highCutHz = params.highCutHz;
    d.modDepth = params.modDepth;
    d.modRateHz = params.modRateHz;
    d.pingPong = params.pingPong;
    digital.setParams(d);

    setTailSeconds(digital.getTailSeconds());
}

void DelayEngine::process(float* left, float* right, int numSamples) noexcept
{
    digital.process(left, right, numSamples);
}

} // namespace clearspace::dsp
