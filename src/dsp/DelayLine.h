#pragma once

// Fractional delay line (SPEC §4): a power-of-two ring buffer with 4-point, 3rd-order
// Hermite (Catmull-Rom) interpolation on read. Header-only, no JUCE dependency, no
// allocation outside prepare().
//
// Usage per sample: read(...) first (delays are measured from the *next* write position, so
// a delay of d samples returns the input from d samples ago), then write(x). This order lets
// the value being written include feedback derived from the read.
//
// Reference for the interpolator: Olli Niemitalo, "Polynomial Interpolators for
// High-Quality Resampling of Oversampled Audio" (2001), 4-point 3rd-order Hermite.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace clearspace::dsp
{

template <typename T = float>
class DelayLine
{
public:
    /** Smallest delay the Hermite read can serve: the interpolator needs one sample *after*
        the read point, and that sample is the most recent write. */
    static constexpr int minHermiteDelay = 2;

    /** Allocates room for `maxDelaySamples` plus the interpolator's margin. Not realtime-safe. */
    void prepare(int maxDelaySamples)
    {
        maxDelay = std::max(maxDelaySamples, minHermiteDelay);
        size_t size = 16;
        while (size < static_cast<size_t>(maxDelay) + 4)
            size <<= 1;
        buffer.assign(size, T(0));
        mask = size - 1;
        writeIndex = 0;
    }

    void clear() noexcept
    {
        std::fill(buffer.begin(), buffer.end(), T(0));
        writeIndex = 0;
    }

    int getMaxDelay() const noexcept { return maxDelay; }

    /** Pushes one sample and advances. */
    void write(T x) noexcept
    {
        buffer[writeIndex] = x;
        writeIndex = (writeIndex + 1) & mask;
    }

    /** Integer delay, no interpolation. `delay` is clamped to [1, maxDelay]. */
    T read(int delay) const noexcept
    {
        delay = std::clamp(delay, 1, maxDelay);
        return buffer[(writeIndex - static_cast<size_t>(delay)) & mask];
    }

    /** Fractional delay with Hermite interpolation. `delay` is clamped to
        [minHermiteDelay, maxDelay]. */
    T readHermite(T delay) const noexcept
    {
        // A non-finite delay would sail through std::clamp and index out of range.
        if (!std::isfinite(delay))
            delay = static_cast<T>(minHermiteDelay);
        delay = std::clamp(delay, static_cast<T>(minHermiteDelay), static_cast<T>(maxDelay));
        const T integerPart = std::floor(delay);
        const T frac = delay - integerPart;
        const size_t i = static_cast<size_t>(integerPart);

        // Points around the read position: xm1 is one sample newer than x0, x1 and x2 older.
        const T xm1 = buffer[(writeIndex - i + 1) & mask];
        const T x0 = buffer[(writeIndex - i) & mask];
        const T x1 = buffer[(writeIndex - i - 1) & mask];
        const T x2 = buffer[(writeIndex - i - 2) & mask];
        return hermite(xm1, x0, x1, x2, frac);
    }

    /** 4-point, 3rd-order Hermite between x0 (t = 0) and x1 (t = 1). Exposed for tests. */
    static T hermite(T xm1, T x0, T x1, T x2, T t) noexcept
    {
        const T c1 = T(0.5) * (x1 - xm1);
        const T c2 = xm1 - T(2.5) * x0 + T(2) * x1 - T(0.5) * x2;
        const T c3 = T(0.5) * (x2 - xm1) + T(1.5) * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

private:
    std::vector<T> buffer;
    size_t mask = 0;
    size_t writeIndex = 0;
    int maxDelay = minHermiteDelay;
};

} // namespace clearspace::dsp
