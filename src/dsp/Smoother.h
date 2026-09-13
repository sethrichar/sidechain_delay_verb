#pragma once

// Linear parameter smoother: ramps to a new target over a fixed number of samples.
// Pure DSP, no allocation. Used for every gain that touches audio (CLAUDE.md §3).

#include "Util.h"

#include <cmath>

namespace clearspace::dsp
{

class LinearSmoother
{
public:
    void prepare(double sampleRate, float rampMs)
    {
        rampSamples = static_cast<int>(std::lround(std::fmax(0.0, rampMs * 1.0e-3 * sampleRate)));
        if (rampSamples < 1)
            rampSamples = 1;
        snap(target);
    }

    /** Sets the target and starts a ramp from the current value. */
    void setTarget(float newTarget)
    {
        if (exactlyEqual(newTarget, target))
            return;
        target = newTarget;
        remaining = rampSamples;
        step = (target - current) / static_cast<float>(rampSamples);
    }

    /** Jumps to `value` with no ramp. */
    void snap(float value)
    {
        target = value;
        current = value;
        remaining = 0;
        step = 0.0f;
    }

    /** Jumps to the current target with no ramp. */
    void snapToTarget() { snap(target); }

    float getNext()
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0)
                current = target; // land exactly, so unity stays bit-exact
        }
        return current;
    }

    float getCurrent() const { return current; }
    float getTarget() const { return target; }
    bool isSmoothing() const { return remaining > 0; }

private:
    float target = 0.0f;
    float current = 0.0f;
    float step = 0.0f;
    int remaining = 0;
    int rampSamples = 1;
};

} // namespace clearspace::dsp
