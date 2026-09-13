#pragma once

// Second-order IIR section (RBJ "Audio EQ Cookbook" designs), transposed direct form II.
// Pure DSP: no JUCE, no allocation. One instance per channel.

#include <cmath>

namespace clearspace::dsp
{

class Biquad
{
public:
    enum class Type
    {
        highPass,
        lowPass
    };

    /** Designs the coefficients. Q = 1/sqrt(2) gives a Butterworth response. */
    void set(Type type, double sampleRate, double cutoffHz, double q = 0.70710678118654752)
    {
        const double fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        // Keep the cutoff strictly inside (0, Nyquist) so the design stays stable.
        const double fc = std::fmin(std::fmax(cutoffHz, 1.0), 0.49 * fs);
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * q);

        double nb0 = 0.0, nb1 = 0.0, nb2 = 0.0;
        const double na0 = 1.0 + alpha;
        const double na1 = -2.0 * cosw0;
        const double na2 = 1.0 - alpha;

        switch (type)
        {
        case Type::highPass:
            nb0 = (1.0 + cosw0) * 0.5;
            nb1 = -(1.0 + cosw0);
            nb2 = (1.0 + cosw0) * 0.5;
            break;
        case Type::lowPass:
            nb0 = (1.0 - cosw0) * 0.5;
            nb1 = 1.0 - cosw0;
            nb2 = (1.0 - cosw0) * 0.5;
            break;
        }

        b0 = static_cast<float>(nb0 / na0);
        b1 = static_cast<float>(nb1 / na0);
        b2 = static_cast<float>(nb2 / na0);
        a1 = static_cast<float>(na1 / na0);
        a2 = static_cast<float>(na2 / na0);
    }

    void reset()
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

} // namespace clearspace::dsp
