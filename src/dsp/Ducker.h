#pragma once

// Sidechain ducker (SPEC §3): a feed-forward compressor whose detector reads the key and
// whose gain is applied to an effect return. Log-domain topology after Giannoulis,
// Massberg & Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis",
// JAES 60(6), 2012 (feed-forward, smoothed gain-reduction, Figure 7 style), plus a hold
// stage between attack and release.
//
// Timing definitions (ADR-0003): Attack and Release are the time for the gain reduction
// to complete 95 % of its move (three one-pole time constants). Hold starts the moment the
// computed gain reduction drops below the current one.

#include "Biquad.h"

namespace clearspace::dsp
{

class Ducker
{
public:
    struct Params
    {
        bool enabled = true;
        float depthDb = 12.0f;      // maximum attenuation, 0…40
        float thresholdDb = -30.0f; // key RMS level (dBFS) where ducking begins
        float attackMs = 5.0f;
        float holdMs = 60.0f;
        float releaseMs = 400.0f;
        float keyHpfHz = 120.0f;
    };

    static constexpr float ratio = 20.0f;      // internal compressor ratio
    static constexpr float kneeDb = 6.0f;      // soft-knee width
    static constexpr float rmsWindowMs = 8.0f; // detector integration time
    /** Time constant used when the ducker is disabled and has to let go of the gain. */
    static constexpr float disableFadeMs = 20.0f;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Sets targets for the next process() call. Realtime-safe; call once per block. */
    void setParams(const Params& params) noexcept;

    /** Computes one linear gain per sample from the mono key. `gainOut` may not alias `key`. */
    void process(const float* key, float* gainOut, int numSamples) noexcept;

    /** Gain reduction (dB, ≥ 0) at the end of the last block. */
    float getGainReductionDb() const noexcept { return envDb; }
    /** Largest gain reduction (dB) seen during the last process() call — for the meter. */
    float getBlockPeakGainReductionDb() const noexcept { return blockPeakDb; }

    /** Static gain computer: soft knee, ratio 20:1, clamped to depth. Returns dB ≥ 0.
        Exposed for tests. */
    static float computeGainReductionDb(float levelDb, float thresholdDb, float depthDb) noexcept;

    /** One-pole coefficient for a "95 % complete in `ms`" move. Exposed for tests. */
    static float coefficientFor95PercentIn(float ms, double sampleRate) noexcept;

private:
    double fs = 48000.0;
    Params current;

    Biquad keyHpf;
    float lastHpfHz = -1.0f;

    float rmsCoeff = 0.0f;
    float meanSquare = 0.0f;

    float attackCoeff = 1.0f;
    float releaseCoeff = 1.0f;
    float disableCoeff = 1.0f;
    int holdSamples = 0;
    int holdCounter = 0;

    float envDb = 0.0f; // smoothed gain reduction, dB
    float blockPeakDb = 0.0f;
};

} // namespace clearspace::dsp
