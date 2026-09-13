#pragma once

// PHASE 1 STAND-IN — a plain integer-sample stereo delay with feedback, so the routing and
// ducker can be tested end to end before the real DelayEngine exists (Phase 2). Deliberately
// naive: time changes are NOT click-free (no crossfade), no filters, no modulation. When
// Phase 2 lands this file moves to archive/ (never deleted, CLAUDE.md §5).

#include <algorithm>
#include <cmath>
#include <vector>

namespace clearspace::dsp::standin
{

class StandInDelay
{
public:
    static constexpr int numChannels = 2;
    static constexpr double maxDelaySeconds = 2.0;
    static constexpr float maxFeedback = 0.95f; // hard runaway guard (CLAUDE.md §3)

    void prepare(double sampleRate, int /*maxBlockSize*/)
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto needed = static_cast<int>(std::ceil(maxDelaySeconds * fs)) + 2;
        int size = 1;
        while (size < needed)
            size <<= 1;
        mask = size - 1;
        for (auto& line : lines)
            line.assign(static_cast<size_t>(size), 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& line : lines)
            std::fill(line.begin(), line.end(), 0.0f);
        writeIndex = 0;
    }

    void setParams(float delayMs, float feedbackPercent)
    {
        delaySamples = std::clamp(static_cast<int>(std::lround(delayMs * 1.0e-3 * fs)), 1, mask);
        feedback = std::clamp(feedbackPercent * 0.01f, 0.0f, maxFeedback);
    }

    /** in/out are numChannels wide; out may not alias in. */
    void process(const float* const* in, float* const* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const int readIndex = (writeIndex - delaySamples) & mask;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float delayed =
                    lines[static_cast<size_t>(ch)][static_cast<size_t>(readIndex)];
                out[ch][i] = delayed;
                lines[static_cast<size_t>(ch)][static_cast<size_t>(writeIndex)] =
                    in[ch][i] + feedback * delayed;
            }
            writeIndex = (writeIndex + 1) & mask;
        }
    }

private:
    double fs = 48000.0;
    int mask = 0;
    int writeIndex = 0;
    int delaySamples = 1;
    float feedback = 0.0f;
    std::vector<float> lines[numChannels];
};

} // namespace clearspace::dsp::standin
