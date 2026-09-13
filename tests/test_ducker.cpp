// Phase 1 acceptance (CLAUDE.md §6): the ducker's depth, attack, hold, release and key HPF are
// measured directly on dsp::Ducker with a sine key. Timing conventions are in ADR-0002:
// Attack/Release = time to complete 95 % of the move; the detector is an 8 ms RMS one-pole
// calibrated so a sine reads its peak level.

#include "dsp/Ducker.h"
#include "dsp/Util.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace clearspace::dsp;
using Catch::Approx;

namespace
{

constexpr double pi = 3.14159265358979323846;

/** A sine key that is on between [onFrom, onTo) seconds and silent elsewhere. */
std::vector<float> sineKey(double sr, double seconds, double freqHz, float peakDb, double onFrom,
                           double onTo)
{
    const auto n = static_cast<size_t>(std::lround(seconds * sr));
    std::vector<float> key(n, 0.0f);
    const auto amp = std::pow(10.0f, peakDb / 20.0f);
    const auto from = static_cast<size_t>(std::lround(onFrom * sr));
    const auto to = std::min(n, static_cast<size_t>(std::lround(onTo * sr)));
    for (size_t i = from; i < to; ++i)
        key[i] =
            amp * static_cast<float>(std::sin(2.0 * pi * freqHz * static_cast<double>(i) / sr));
    return key;
}

/** Runs the ducker over `key` in blocks and returns the gain reduction per sample (dB ≥ 0). */
std::vector<float> runDucker(const DuckerParams& params, const std::vector<float>& key, double sr,
                             int blockSize)
{
    Ducker ducker;
    ducker.prepare(sr, blockSize);
    ducker.setParams(params);

    std::vector<float> grDb(key.size(), 0.0f);
    for (size_t pos = 0; pos < key.size(); pos += static_cast<size_t>(blockSize))
    {
        const auto n = static_cast<int>(std::min(static_cast<size_t>(blockSize), key.size() - pos));
        ducker.computeGains(key.data() + pos, n);
        const float* gains = ducker.lastGains();
        for (int i = 0; i < n; ++i)
            grDb[pos + static_cast<size_t>(i)] = -20.0f * std::log10(std::max(1.0e-9f, gains[i]));
    }
    return grDb;
}

size_t at(double seconds, double sr)
{
    return static_cast<size_t>(std::lround(seconds * sr));
}

float maxIn(const std::vector<float>& v, double fromSec, double toSec, double sr)
{
    const auto a = std::min(v.size(), at(fromSec, sr));
    const auto b = std::min(v.size(), at(toSec, sr));
    float m = 0.0f;
    for (size_t i = a; i < b; ++i)
        m = std::max(m, v[i]);
    return m;
}

float minIn(const std::vector<float>& v, double fromSec, double toSec, double sr)
{
    const auto a = std::min(v.size(), at(fromSec, sr));
    const auto b = std::min(v.size(), at(toSec, sr));
    float m = 1.0e9f;
    for (size_t i = a; i < b; ++i)
        m = std::min(m, v[i]);
    return m;
}

DuckerParams defaults()
{
    return DuckerParams{}; // depth 12, thr −30, attack 5, hold 60, release 400, HPF 120
}

} // namespace

TEST_CASE("ducker: static gain computer (soft knee, 20:1, clamped to depth)", "[ducker]")
{
    CHECK(Ducker::computeGainReductionDb(-40.0f, -30.0f, 12.0f) == Approx(0.0f));
    CHECK(Ducker::computeGainReductionDb(-30.0f, -30.0f, 12.0f) == Approx(0.7125f).margin(0.01f));
    CHECK(Ducker::computeGainReductionDb(-20.0f, -30.0f, 12.0f) == Approx(9.5f).margin(0.01f));
    CHECK(Ducker::computeGainReductionDb(-6.0f, -30.0f, 12.0f) == Approx(12.0f));
    CHECK(Ducker::computeGainReductionDb(0.0f, -30.0f, 40.0f) == Approx(28.5f).margin(0.01f));
    CHECK(Ducker::computeGainReductionDb(0.0f, -30.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("ducker: depth is reached within Attack and held at steady state", "[ducker]")
{
    const double sr = 48000.0;
    auto p = defaults();
    const auto key = sineKey(sr, 0.5, 1000.0, -6.0f, 0.0, 0.5);

    SECTION("attack 5 ms")
    {
        const auto gr = runDucker(p, key, sr, 512);
        CHECK(gr[at(0.005, sr)] >= 11.0f);
        CHECK(minIn(gr, 0.1, 0.5, sr) == Approx(12.0f).margin(0.5f));
        CHECK(maxIn(gr, 0.1, 0.5, sr) == Approx(12.0f).margin(0.5f));
    }

    SECTION("attack 50 ms scales the ramp")
    {
        p.attackMs = 50.0f;
        const auto gr = runDucker(p, key, sr, 512);
        CHECK(gr[at(0.025, sr)] < 11.0f);
        CHECK(gr[at(0.025, sr)] > 5.0f);
        CHECK(gr[at(0.050, sr)] >= 11.0f);
    }

    SECTION("depth 40 dB with a hot key")
    {
        p.depthDb = 40.0f;
        const auto gr = runDucker(p, sineKey(sr, 0.5, 1000.0, 0.0f, 0.0, 0.5), sr, 512);
        CHECK(minIn(gr, 0.1, 0.5, sr) == Approx(28.5f).margin(0.5f)); // 30 dB over × 0.95
    }
}

TEST_CASE("ducker: hold keeps the gain down after the key stops", "[ducker]")
{
    const double sr = 48000.0;
    const double off = 0.2;
    const auto key = sineKey(sr, 1.0, 1000.0, -6.0f, 0.0, off);

    auto p = defaults(); // hold 60 ms
    const auto held = runDucker(p, key, sr, 512);
    CHECK(held[at(off + 0.060, sr)] >= 11.0f);

    p.holdMs = 200.0f;
    const auto longHold = runDucker(p, key, sr, 512);
    CHECK(longHold[at(off + 0.150, sr)] >= 11.0f);

    p.holdMs = 0.0f;
    const auto noHold = runDucker(p, key, sr, 512);
    CHECK(noHold[at(off + 0.150, sr)] < 11.0f);
}

TEST_CASE("ducker: release recovers to within 1 dB of unity after hold + release", "[ducker]")
{
    const double sr = 48000.0;
    const double off = 0.2;
    auto p = defaults(); // hold 60 ms, release 400 ms
    const auto key = sineKey(sr, 1.2, 1000.0, -6.0f, 0.0, off);
    const auto gr = runDucker(p, key, sr, 512);

    const double holdEnd = off + 0.060;
    CHECK(gr[at(holdEnd + 0.100, sr)] > 1.0f);          // release/4 in: still ducking
    CHECK(gr[at(holdEnd + 0.400 + 0.030, sr)] <= 1.0f); // release + detector margin: back
    CHECK(gr[at(1.19, sr)] < 0.05f);

    p.releaseMs = 100.0f;
    const auto fast = runDucker(p, key, sr, 512);
    CHECK(fast[at(holdEnd + 0.100 + 0.030, sr)] <= 1.0f);
}

TEST_CASE("ducker: key HPF stops a 40 Hz key from triggering", "[ducker]")
{
    const double sr = 48000.0;
    auto p = defaults();
    const float level = p.thresholdDb + 10.0f; // −20 dBFS: 10 dB over threshold

    p.keyHpfHz = 120.0f;
    const auto lowKeyFiltered = runDucker(p, sineKey(sr, 0.6, 40.0, level, 0.0, 0.6), sr, 512);
    CHECK(maxIn(lowKeyFiltered, 0.3, 0.6, sr) <= 0.5f);

    p.keyHpfHz = 20.0f;
    const auto lowKeyOpen = runDucker(p, sineKey(sr, 0.6, 40.0, level, 0.0, 0.6), sr, 512);
    CHECK(maxIn(lowKeyOpen, 0.3, 0.6, sr) >= 8.0f);

    p.keyHpfHz = 120.0f;
    const auto vocalKey = runDucker(p, sineKey(sr, 0.6, 1000.0, level, 0.0, 0.6), sr, 512);
    CHECK(minIn(vocalKey, 0.3, 0.6, sr) >= 8.0f);
}

TEST_CASE("ducker: re-trigger during release re-enters attack cleanly", "[ducker]")
{
    const double sr = 48000.0;
    auto key = sineKey(sr, 0.6, 1000.0, -6.0f, 0.0, 0.2);
    const auto second = sineKey(sr, 0.6, 1000.0, -6.0f, 0.35, 0.6);
    for (size_t i = 0; i < key.size(); ++i)
        key[i] += second[i];

    const auto gr = runDucker(defaults(), key, sr, 64);
    CHECK(gr[at(0.34, sr)] < 11.0f); // releasing when the second phrase starts
    CHECK(gr[at(0.36, sr)] >= 11.0f);
    // Monotonic non-decreasing through the re-attack (no dip, no reset to zero). The first
    // ~0.2 ms still release while the new key rises above the current reduction, so start 1 ms in.
    for (size_t i = at(0.351, sr) + 1; i < at(0.36, sr); ++i)
        REQUIRE(gr[i] + 1.0e-3f >= gr[i - 1]);
    for (float g : gr)
        REQUIRE(std::isfinite(g));
}

TEST_CASE("ducker: disabled or depth 0 is exactly unity; silence gives no reduction", "[ducker]")
{
    const double sr = 48000.0;
    const auto key = sineKey(sr, 0.3, 1000.0, -6.0f, 0.0, 0.3);

    auto p = defaults();
    p.enabled = false;
    Ducker ducker;
    ducker.prepare(sr, 256);
    ducker.setParams(p);
    ducker.computeGains(key.data(), 256);
    for (int i = 0; i < 256; ++i)
        REQUIRE(exactlyEqual(ducker.lastGains()[i], 1.0f)); // bit-exact unity, not merely close
    CHECK(exactlyEqual(ducker.gainReductionDb(), 0.0f));

    p.enabled = true;
    p.depthDb = 0.0f;
    ducker.setParams(p);
    ducker.computeGains(key.data(), 256);
    for (int i = 0; i < 256; ++i)
        REQUIRE(exactlyEqual(ducker.lastGains()[i], 1.0f));

    p.depthDb = 12.0f;
    ducker.setParams(p);
    ducker.reset();
    std::vector<float> silence(256, 0.0f);
    ducker.computeGains(silence.data(), 256);
    CHECK(exactlyEqual(ducker.gainReductionDb(), 0.0f));
}

TEST_CASE("ducker: applies the same gain to every wet channel", "[ducker]")
{
    const double sr = 48000.0;
    const auto key = sineKey(sr, 0.1, 1000.0, -6.0f, 0.0, 0.1);
    std::vector<float> left(key.size(), 1.0f), right(key.size(), -0.5f);
    float* wet[2] = {left.data(), right.data()};

    Ducker ducker;
    ducker.prepare(sr, static_cast<int>(key.size()));
    ducker.setParams(defaults());
    ducker.process(key.data(), wet, 2, static_cast<int>(key.size()));

    for (size_t i = 0; i < key.size(); ++i)
    {
        REQUIRE(left[i] == Approx(ducker.lastGains()[i]));
        REQUIRE(right[i] == Approx(-0.5f * ducker.lastGains()[i]));
    }
    CHECK(left.back() == Approx(std::pow(10.0f, -12.0f / 20.0f)).margin(0.01f));
}

TEST_CASE("ducker: timing holds at 44.1/48/96 kHz and any block size", "[ducker]")
{
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int block : {1, 7, 512, 4096})
        {
            INFO("sr = " << sr << " block = " << block);
            const auto key = sineKey(sr, 0.8, 1000.0, -6.0f, 0.0, 0.3);
            const auto gr = runDucker(defaults(), key, sr, block);
            CHECK(gr[at(0.005, sr)] >= 11.0f);
            CHECK(minIn(gr, 0.1, 0.3, sr) == Approx(12.0f).margin(0.5f));
            CHECK(gr[at(0.3 + 0.060, sr)] >= 11.0f);
            CHECK(gr[at(0.3 + 0.060 + 0.400 + 0.030, sr)] <= 1.0f);
            for (float g : gr)
                REQUIRE(std::isfinite(g));
        }
}
