#pragma once

// Common interface for the delay and reverb engines (and the Phase 1 stand-ins).
// Pure DSP: no allocation in process(), all sizing in prepare().

#include <atomic>

namespace clearspace::dsp
{

class Effect
{
public:
    virtual ~Effect() = default;

    /** Allocates everything for `maxBlockSize` samples at `sampleRate`. Not realtime-safe. */
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    /** Clears all internal state (delay lines, filters). Realtime-safe. */
    virtual void reset() noexcept = 0;

    /** In-place: replaces the stereo input with the 100 % wet output. Realtime-safe. */
    virtual void process(float* left, float* right, int numSamples) noexcept = 0;

    /** Worst-case ring-out after the input stops, for AudioProcessor::getTailLengthSeconds.
        Called from any thread, so implementations keep it in an atomic. */
    double getTailSeconds() const noexcept { return tailSeconds.load(std::memory_order_relaxed); }

protected:
    void setTailSeconds(double seconds) noexcept
    {
        tailSeconds.store(seconds, std::memory_order_relaxed);
    }

private:
    std::atomic<double> tailSeconds{0.0};
};

} // namespace clearspace::dsp
