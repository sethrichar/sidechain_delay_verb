#pragma once

// PHASE 1 STAND-IN — four parallel damped feedback combs per channel (Schroeder lengths), gain
// set from the Decay parameter so RT60 ≈ reverbDecay. It is a test fixture for the routing,
// ducker and tail logic, not a musical reverb: Phase 3 replaces it with the Dattorro plate and
// this file moves to archive/ (never deleted, CLAUDE.md §5).

#include <algorithm>
#include <cmath>
#include <vector>

namespace clearspace::dsp::standin
{

class StandInReverb
{
public:
    static constexpr int numChannels = 2;
    static constexpr int numCombs = 4;

    void prepare(double sampleRate, int /*maxBlockSize*/)
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int c = 0; c < numCombs; ++c)
            {
                auto& comb = combs[ch][c];
                // Right channel lengths are stretched 3 % so the two sides decorrelate.
                const double lengthMs = baseLengthsMs[c] * (ch == 0 ? 1.0 : 1.03);
                comb.length = std::max(1, static_cast<int>(std::lround(lengthMs * 1.0e-3 * fs)));
                comb.buffer.assign(static_cast<size_t>(comb.length), 0.0f);
            }
        setDecay(decaySeconds);
        reset();
    }

    void reset()
    {
        for (auto& channel : combs)
            for (auto& comb : channel)
            {
                std::fill(comb.buffer.begin(), comb.buffer.end(), 0.0f);
                comb.index = 0;
                comb.lowpassState = 0.0f;
            }
    }

    void setDecay(float seconds)
    {
        decaySeconds = std::max(0.05f, seconds);
        for (auto& channel : combs)
            for (auto& comb : channel)
            {
                // g = 10^(−3·L/(T60·fs)) → each comb decays 60 dB in T60 seconds.
                const double exponent = -3.0 * comb.length / (decaySeconds * fs);
                comb.gain = static_cast<float>(std::pow(10.0, exponent));
            }
    }

    /** in/out are numChannels wide; out may not alias in. */
    void process(const float* const* in, float* const* out, int n)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            for (int i = 0; i < n; ++i)
            {
                float sum = 0.0f;
                const float x = in[ch][i];
                for (auto& comb : combs[ch])
                {
                    const float delayed = comb.buffer[static_cast<size_t>(comb.index)];
                    comb.lowpassState += damping * (delayed - comb.lowpassState);
                    comb.buffer[static_cast<size_t>(comb.index)] =
                        x + comb.gain * comb.lowpassState;
                    if (++comb.index >= comb.length)
                        comb.index = 0;
                    sum += delayed;
                }
                out[ch][i] = sum * outputScale;
            }
        }
    }

private:
    struct Comb
    {
        std::vector<float> buffer;
        int length = 1;
        int index = 0;
        float gain = 0.0f;
        float lowpassState = 0.0f;
    };

    static constexpr double baseLengthsMs[numCombs] = {29.7, 37.1, 41.1, 43.7};
    static constexpr float damping = 0.4f;
    static constexpr float outputScale = 0.25f;

    double fs = 48000.0;
    float decaySeconds = 2.0f;
    Comb combs[numChannels][numCombs];
};

} // namespace clearspace::dsp::standin
