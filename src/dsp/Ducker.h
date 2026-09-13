#pragma once

// Sidechain ducker (SPEC §3): a feed-forward compressor whose detector reads the key and
// whose gain is applied to an effect return. Log-domain topology after Giannoulis, Massberg
// & Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", JAES 2012.
//
//   key → 2nd-order HPF → RMS (8 ms) → dB → soft-knee gain computer (6 dB knee, 20:1,
//   clamped to −Depth) → attack / hold / release ballistics (dB) → linear gain → × wet
//
// Conventions (docs/decisions/ADR-0002): the detector is calibrated so a sine reads its peak
// level in dBFS; Attack/Release are the time to complete 95 % of a move (one-pole τ = T/3);
// Hold freezes the gain after the key drops and re-arms whenever the key asks for ≥ the
// current reduction. Pure DSP: no JUCE, no allocation after prepare().

#include "Biquad.h"

#include <vector>

namespace clearspace::dsp
{

struct DuckerParams
{
    bool enabled = true;
    float depthDb = 12.0f;      // maximum attenuation, 0…40
    float thresholdDb = -30.0f; // dBFS
    float attackMs = 5.0f;
    float holdMs = 60.0f;
    float releaseMs = 400.0f;
    float keyHpfHz = 120.0f;
};

class Ducker
{
public:
    static constexpr float kneeWidthDb = 6.0f;
    static constexpr float ratio = 20.0f;
    static constexpr float detectorWindowMs = 8.0f;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    /** Cheap; call once per block before process(). Recomputes coefficients only on change. */
    void setParams(const DuckerParams& params);

    /** Computes per-sample gains from `key` (mono, n samples) into lastGains() and multiplies
        every wet channel by them. n must be ≤ the maxBlockSize given to prepare(). */
    void process(const float* key, float* const* wet, int numChannels, int n);

    /** Computes the gains only (into lastGains()), without applying them. */
    void computeGains(const float* key, int n);

    /** Gain reduction at the end of the last block, in positive dB (0 = unity). */
    float gainReductionDb() const { return currentGrDb; }

    /** Linear gains computed by the last process()/computeGains() call. */
    const float* lastGains() const { return gains.data(); }

    /** Static gain computer, exposed for tests: input level (dBFS) → gain reduction (dB ≥ 0). */
    static float computeGainReductionDb(float levelDb, float thresholdDb, float depthDb);

private:
    void updateCoefficients();

    DuckerParams params;
    double sampleRate = 48000.0;
    float detectorCoef = 0.0f;
    float attackCoef = 1.0f;
    float releaseCoef = 1.0f;
    int holdSamples = 0;
    float lastKeyHpfHz = -1.0f;
    double lastSampleRate = -1.0;

    Biquad keyHpf;
    float meanSquare = 0.0f;
    float currentGrDb = 0.0f;
    int holdCounter = 0;
    std::vector<float> gains;
};

} // namespace clearspace::dsp
