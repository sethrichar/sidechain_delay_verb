#pragma once

// Phase 1 stand-in delay: a plain 500 ms integer-sample stereo delay with feedback, so the
// ducker and routing can be tested end to end before the real DelayEngine (Phase 2).
// Output is 100 % wet. Deliberately has no filters, modulation or interpolation.
// Superseded in Phase 2 — move to archive/, never delete (CLAUDE.md §5).

#include "../Effect.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace clearspace::dsp::standin
{

class StandInDelay final : public Effect
{
public:
    static constexpr double delaySeconds = 0.5;
    /** Feedback is clamped below unity so the tail always ends. */
    static constexpr float maxFeedback = 0.98f;
    static constexpr float feedbackSmoothMs = 20.0f;

    void prepare(double sampleRate, int) override
    {
        fs = sampleRate;
        delaySamples = std::max(1, static_cast<int>(std::lround(delaySeconds * sampleRate)));
        for (auto& line : lines)
            line.assign(static_cast<size_t>(delaySamples), 0.0f);
        smoothCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (feedbackSmoothMs * 1.0e-3 * fs)));
        reset();
    }

    void reset() noexcept override
    {
        for (auto& line : lines)
            std::fill(line.begin(), line.end(), 0.0f);
        writeIndex = 0;
        feedbackSmoothed = feedbackTarget;
    }

    /** Feedback as a fraction 0…1 (the `delayFeedback` percentage / 100). */
    void setFeedback(float feedback) noexcept
    {
        feedbackTarget = std::clamp(feedback, 0.0f, maxFeedback);
        setTailSeconds(tailSecondsFor(feedbackTarget));
    }

    /** Repeats until the loop has decayed by 60 dB, times the delay time. */
    static double tailSecondsFor(float feedback) noexcept
    {
        const auto fb = std::clamp(feedback, 0.0f, maxFeedback);
        if (fb <= 0.0f)
            return delaySeconds;
        const double repeats = std::ceil(std::log(1.0e-3) / std::log(static_cast<double>(fb)));
        return delaySeconds * (1.0 + std::max(0.0, repeats));
    }

    void process(float* left, float* right, int numSamples) noexcept override
    {
        if (delaySamples <= 0)
            return;
        float* channels[2] = {left, right};
        for (int i = 0; i < numSamples; ++i)
        {
            feedbackSmoothed += smoothCoeff * (feedbackTarget - feedbackSmoothed);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& line = lines[static_cast<size_t>(ch)];
                const float delayed = line[static_cast<size_t>(writeIndex)];
                line[static_cast<size_t>(writeIndex)] =
                    channels[ch][i] + delayed * feedbackSmoothed;
                channels[ch][i] = delayed;
            }
            if (++writeIndex >= delaySamples)
                writeIndex = 0;
        }
    }

private:
    double fs = 48000.0;
    int delaySamples = 0;
    int writeIndex = 0;
    std::vector<float> lines[2];
    float feedbackTarget = 0.35f;
    float feedbackSmoothed = 0.35f;
    float smoothCoeff = 1.0f;
};

} // namespace clearspace::dsp::standin
