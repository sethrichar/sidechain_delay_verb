#pragma once

// Phase 1 stand-in reverb: a bank of parallel feedback comb filters per channel whose
// loop gains are set from the requested decay time (g = 10^(-3·L/(T60·fs)), so every comb
// decays together and the measured RT60 tracks `reverbDecay`). Not musical — it exists so
// the ducker and routing can be tested end to end before the real reverbs (Phase 3+).
// Output is 100 % wet. Superseded in Phase 3 — move to archive/, never delete.

#include "../Effect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace clearspace::dsp::standin
{

class StandInReverb final : public Effect
{
public:
    static constexpr int numCombs = 4;
    static constexpr float minDecaySeconds = 0.1f;
    static constexpr float maxDecaySeconds = 20.0f;
    static constexpr float gainSmoothMs = 20.0f;

    void prepare(double sampleRate, int) override
    {
        fs = sampleRate;
        // Comb lengths in ms (mutually prime at 48 kHz), the right channel slightly longer
        // for decorrelation.
        constexpr std::array<double, numCombs> leftMs = {29.7, 37.1, 41.1, 43.7};
        constexpr std::array<double, numCombs> rightMs = {30.3, 36.5, 41.9, 44.3};
        for (int c = 0; c < numCombs; ++c)
        {
            combs[0][static_cast<size_t>(c)].setLength(leftMs[static_cast<size_t>(c)], fs);
            combs[1][static_cast<size_t>(c)].setLength(rightMs[static_cast<size_t>(c)], fs);
        }
        smoothCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (gainSmoothMs * 1.0e-3 * fs)));
        setDecaySeconds(decayTarget);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& bank : combs)
            for (auto& comb : bank)
            {
                comb.clear();
                comb.gain = comb.gainTarget;
            }
    }

    /** RT60 in seconds (the `reverbDecay` parameter). */
    void setDecaySeconds(float seconds) noexcept
    {
        decayTarget = std::clamp(seconds, minDecaySeconds, maxDecaySeconds);
        for (auto& bank : combs)
            for (auto& comb : bank)
                comb.gainTarget = static_cast<float>(
                    std::pow(10.0, -3.0 * comb.lengthSamples / (decayTarget * fs)));
        setTailSeconds(decayTarget);
    }

    void process(float* left, float* right, int numSamples) noexcept override
    {
        float* channels[2] = {left, right};
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& bank = combs[static_cast<size_t>(ch)];
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = channels[ch][i] * inputScale;
                float y = 0.0f;
                for (auto& comb : bank)
                    y += comb.tick(x, smoothCoeff);
                channels[ch][i] = y;
            }
        }
    }

private:
    struct Comb
    {
        std::vector<float> buffer;
        int lengthSamples = 1;
        int index = 0;
        float gain = 0.0f;
        float gainTarget = 0.0f;

        void setLength(double ms, double sampleRate)
        {
            lengthSamples = std::max(1, static_cast<int>(std::lround(ms * 1.0e-3 * sampleRate)));
            buffer.assign(static_cast<size_t>(lengthSamples), 0.0f);
            index = 0;
        }

        void clear() noexcept
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            index = 0;
        }

        float tick(float x, float coeff) noexcept
        {
            gain += coeff * (gainTarget - gain);
            const float y = buffer[static_cast<size_t>(index)];
            buffer[static_cast<size_t>(index)] = x + y * gain;
            if (++index >= lengthSamples)
                index = 0;
            return y;
        }
    };

    static constexpr float inputScale = 0.25f; // four combs summed → roughly unity

    double fs = 48000.0;
    float decayTarget = 2.0f;
    float smoothCoeff = 1.0f;
    std::array<std::array<Comb, numCombs>, 2> combs;
};

} // namespace clearspace::dsp::standin
