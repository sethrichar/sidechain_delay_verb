#pragma once

// Second-order IIR section (transposed direct form II) with RBJ "Audio EQ Cookbook"
// coefficient recipes. Header-only, no JUCE dependency, no allocation.
//
// Reference: R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter
// coefficients" (public-domain formulae).

#include <cmath>
#include <numbers>

namespace clearspace::dsp
{

class Biquad
{
public:
    /** Butterworth-style Q for a maximally flat 2nd-order response. */
    static constexpr float butterworthQ = 0.70710678f;

    void reset() noexcept
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }

    /** Identity (unity gain, no state change on coefficients). */
    void setBypass() noexcept
    {
        b0 = 1.0f;
        b1 = b2 = a1 = a2 = 0.0f;
    }

    void setHighpass(double sampleRate, double cutoffHz, double q = butterworthQ) noexcept
    {
        const auto o = omega(sampleRate, cutoffHz, q);
        const double a0 = 1.0 + o.alpha;
        set((1.0 + o.cosw) * 0.5 / a0, -(1.0 + o.cosw) / a0, (1.0 + o.cosw) * 0.5 / a0,
            -2.0 * o.cosw / a0, (1.0 - o.alpha) / a0);
    }

    void setLowpass(double sampleRate, double cutoffHz, double q = butterworthQ) noexcept
    {
        const auto o = omega(sampleRate, cutoffHz, q);
        const double a0 = 1.0 + o.alpha;
        set((1.0 - o.cosw) * 0.5 / a0, (1.0 - o.cosw) / a0, (1.0 - o.cosw) * 0.5 / a0,
            -2.0 * o.cosw / a0, (1.0 - o.alpha) / a0);
    }

    float process(float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void process(float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = process(data[i]);
    }

private:
    struct Omega
    {
        double cosw, alpha;
    };

    static Omega omega(double sampleRate, double cutoffHz, double q) noexcept
    {
        // Keep the cutoff strictly inside (0, Nyquist) so the recipes stay finite.
        const double nyquist = 0.5 * sampleRate;
        const double f =
            cutoffHz < 1.0 ? 1.0 : (cutoffHz > nyquist * 0.99 ? nyquist * 0.99 : cutoffHz);
        const double w = 2.0 * std::numbers::pi * f / sampleRate;
        return {std::cos(w), std::sin(w) / (2.0 * (q > 1.0e-3 ? q : 1.0e-3))};
    }

    void set(double nb0, double nb1, double nb2, double na1, double na2) noexcept
    {
        b0 = static_cast<float>(nb0);
        b1 = static_cast<float>(nb1);
        b2 = static_cast<float>(nb2);
        a1 = static_cast<float>(na1);
        a2 = static_cast<float>(na2);
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

} // namespace clearspace::dsp
