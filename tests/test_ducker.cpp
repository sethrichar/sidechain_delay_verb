// Phase 1 acceptance (CLAUDE.md §6, BUILD_PLAN Phase 1): ducker depth, attack, hold,
// release and key HPF, measured directly on dsp::Ducker with synthetic keys.

#include "Measure.h"
#include "dsp/Biquad.h"
#include "dsp/Ducker.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace clearspace;
using namespace clearspace::test;
using Catch::Approx;
using dsp::Ducker;

namespace
{

std::vector<float> sineKey(double sr, double seconds, double freqHz, float peakDb,
                           double onFrom = 0.0, double onTo = 1.0e9)
{
    std::vector<float> key(static_cast<size_t>(std::lround(seconds * sr)), 0.0f);
    const float amp = std::pow(10.0f, peakDb / 20.0f);
    const double inc = 2.0 * std::numbers::pi * freqHz / sr;
    for (size_t i = 0; i < key.size(); ++i)
    {
        const double t = static_cast<double>(i) / sr;
        if (t >= onFrom && t < onTo)
            key[i] = amp * static_cast<float>(std::sin(inc * static_cast<double>(i)));
    }
    return key;
}

/** Runs the ducker in blocks and returns the gain reduction in dB per sample. */
std::vector<float> runDucker(const Ducker::Params& params, const std::vector<float>& key, double sr,
                             int block = 64)
{
    Ducker ducker;
    ducker.prepare(sr, block);
    ducker.setParams(params);

    std::vector<float> gain(key.size()), gr(key.size());
    for (size_t pos = 0; pos < key.size(); pos += static_cast<size_t>(block))
    {
        const auto n = static_cast<int>(std::min(static_cast<size_t>(block), key.size() - pos));
        ducker.process(key.data() + pos, gain.data() + pos, n);
    }
    for (size_t i = 0; i < key.size(); ++i)
        gr[i] = -20.0f * std::log10(std::max(gain[i], 1.0e-9f));
    return gr;
}

float meanOver(const std::vector<float>& v, double from, double to, double sr)
{
    const auto a = static_cast<size_t>(toSample(from, sr));
    const auto b = std::min(v.size(), static_cast<size_t>(toSample(to, sr)));
    double acc = 0.0;
    for (size_t i = a; i < b; ++i)
        acc += v[i];
    return b > a ? static_cast<float>(acc / static_cast<double>(b - a)) : 0.0f;
}

float maxOver(const std::vector<float>& v, double from, double to, double sr)
{
    const auto a = static_cast<size_t>(toSample(from, sr));
    const auto b = std::min(v.size(), static_cast<size_t>(toSample(to, sr)));
    float m = 0.0f;
    for (size_t i = a; i < b; ++i)
        m = std::max(m, v[i]);
    return m;
}

struct Timing
{
    double attackMs = -1.0;      // 5 % → (depth − 1 dB) after key onset
    double holdMs = -1.0;        // key off → first sample below depth − 0.5 dB
    double releaseMs = -1.0;     // release start → 1 dB
    double onsetToFullMs = -1.0; // key onset → depth − 1 dB (includes detector lag)
};

Timing measure(const std::vector<float>& gr, double sr, double keyOn, double keyOff, float depthDb)
{
    Timing t;
    const int on = toSample(keyOn, sr);
    const int off = toSample(keyOff, sr);
    const int start = firstAtLeast(gr, on, 0.05f * depthDb);
    const int full = firstAtLeast(gr, on, depthDb - 1.0f);
    if (start >= 0 && full >= 0)
    {
        t.attackMs = (full - start) * 1000.0 / sr;
        t.onsetToFullMs = (full - on) * 1000.0 / sr;
    }
    const int releaseStart = firstAtMost(gr, off, depthDb - 0.5f);
    if (releaseStart >= 0)
    {
        t.holdMs = (releaseStart - off) * 1000.0 / sr;
        const int released = firstAtMost(gr, releaseStart, 1.0f);
        if (released >= 0)
            t.releaseMs = (released - releaseStart) * 1000.0 / sr;
    }
    return t;
}

} // namespace

TEST_CASE("ducker: gain computer is a 20:1 soft-knee compressor clamped to depth", "[ducker]")
{
    // Threshold −30, knee 6 dB → knee spans −33 … −27.
    CHECK(Ducker::computeGainReductionDb(-40.0f, -30.0f, 12.0f) == Approx(0.0f));
    CHECK(Ducker::computeGainReductionDb(-33.0f, -30.0f, 12.0f) == Approx(0.0f).margin(1e-6));
    CHECK(Ducker::computeGainReductionDb(-30.0f, -30.0f, 12.0f) ==
          Approx(0.95f * 9.0f / 12.0f)); // mid-knee: (1−1/R)·(x−T+W/2)²/(2W)
    CHECK(Ducker::computeGainReductionDb(-27.0f, -30.0f, 12.0f) == Approx(0.95f * 3.0f));
    CHECK(Ducker::computeGainReductionDb(-20.0f, -30.0f, 12.0f) == Approx(0.95f * 10.0f));
    CHECK(Ducker::computeGainReductionDb(0.0f, -30.0f, 12.0f) == Approx(12.0f)); // clamped
    CHECK(Ducker::computeGainReductionDb(0.0f, -30.0f, 40.0f) == Approx(28.5f));
    CHECK(Ducker::computeGainReductionDb(0.0f, -30.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("ducker: steady key reaches Depth within ±1 dB", "[ducker][depth]")
{
    const double sr = 48000.0;
    const auto key = sineKey(sr, 0.5, 1000.0, -6.0f); // −6 dBFS peak = −9 dBFS RMS

    Ducker::Params p;
    SECTION("depth 12 @ −30 dBFS threshold (defaults)")
    {
        const auto gr = runDucker(p, key, sr);
        CHECK(meanOver(gr, 0.3, 0.5, sr) == Approx(12.0f).margin(1.0));
        CHECK(meanOver(gr, 0.3, 0.5, sr) == Approx(12.0f).margin(0.15)); // tighter than spec
    }
    SECTION("depth 24 @ −40 dBFS threshold")
    {
        p.depthDb = 24.0f;
        p.thresholdDb = -40.0f;
        const auto gr = runDucker(p, key, sr);
        CHECK(meanOver(gr, 0.3, 0.5, sr) == Approx(24.0f).margin(1.0));
    }
    SECTION("depth 3")
    {
        p.depthDb = 3.0f;
        const auto gr = runDucker(p, key, sr);
        CHECK(meanOver(gr, 0.3, 0.5, sr) == Approx(3.0f).margin(0.5));
    }
    SECTION("depth 0 leaves the gain at exactly unity")
    {
        p.depthDb = 0.0f;
        const auto gr = runDucker(p, key, sr);
        CHECK(maxOver(gr, 0.0, 0.5, sr) == Approx(0.0f).margin(0.0f));
    }
    SECTION("disabled leaves the gain at exactly unity")
    {
        p.enabled = false;
        const auto gr = runDucker(p, key, sr);
        CHECK(maxOver(gr, 0.0, 0.5, sr) == Approx(0.0f).margin(0.0f));
    }
    SECTION("key below threshold − knee does nothing")
    {
        const auto quiet = sineKey(sr, 0.5, 1000.0, -40.0f);
        const auto gr = runDucker(p, quiet, sr);
        CHECK(maxOver(gr, 0.0, 0.5, sr) == Approx(0.0f).margin(0.0f));
    }
}

TEST_CASE("ducker: attack, hold and release follow their parameters", "[ducker][ballistics]")
{
    const double sr = 48000.0;
    const double keyOn = 0.1, keyOff = 0.5;
    const auto key = sineKey(sr, 1.5, 1000.0, -6.0f, keyOn, keyOff);

    Ducker::Params p; // attack 5, hold 60, release 400, depth 12

    SECTION("defaults: 5 ms / 60 ms / 400 ms")
    {
        const auto gr = runDucker(p, key, sr);
        const auto t = measure(gr, sr, keyOn, keyOff, p.depthDb);
        INFO("attack " << t.attackMs << " ms, onset→full " << t.onsetToFullMs << " ms, hold "
                       << t.holdMs << " ms, release " << t.releaseMs << " ms");
        REQUIRE(t.attackMs >= 0.0);
        CHECK(t.attackMs <= p.attackMs);
        CHECK(t.attackMs >= 0.3 * p.attackMs);      // not instantaneous
        CHECK(t.onsetToFullMs <= p.attackMs + 4.0); // detector lag allowance

        REQUIRE(t.holdMs >= 0.0);
        CHECK(t.holdMs >= p.holdMs);        // held at least as long as asked
        CHECK(t.holdMs <= p.holdMs + 40.0); // plus detector fall time
        CHECK(maxOver(gr, keyOff, keyOff + p.holdMs * 1.0e-3, sr) >= p.depthDb - 0.5f);

        REQUIRE(t.releaseMs >= 0.0);
        CHECK(t.releaseMs <= p.releaseMs);
        CHECK(t.releaseMs >= 0.4 * p.releaseMs);
        // Recovered to within 0.1 dB well before the end (the release is exponential).
        CHECK(maxOver(gr, 1.3, 1.5, sr) < 0.1f);
    }

    SECTION("slow: 50 ms / 200 ms / 1500 ms")
    {
        p.attackMs = 50.0f;
        p.holdMs = 200.0f;
        p.releaseMs = 1500.0f;
        const auto slowKey = sineKey(sr, 3.0, 1000.0, -6.0f, keyOn, keyOff);
        const auto gr = runDucker(p, slowKey, sr);
        const auto t = measure(gr, sr, keyOn, keyOff, p.depthDb);
        INFO("attack " << t.attackMs << " ms, hold " << t.holdMs << " ms, release " << t.releaseMs
                       << " ms");
        REQUIRE(t.attackMs >= 0.0);
        CHECK(t.attackMs <= p.attackMs);
        CHECK(t.attackMs >= 0.5 * p.attackMs);
        REQUIRE(t.holdMs >= 0.0);
        CHECK(t.holdMs >= p.holdMs);
        CHECK(t.holdMs <= p.holdMs + 40.0);
        REQUIRE(t.releaseMs >= 0.0);
        CHECK(t.releaseMs <= p.releaseMs);
        CHECK(t.releaseMs >= 0.5 * p.releaseMs);
    }

    SECTION("zero hold releases as soon as the detector falls")
    {
        p.holdMs = 0.0f;
        const auto gr = runDucker(p, key, sr);
        const auto t = measure(gr, sr, keyOn, keyOff, p.depthDb);
        REQUIRE(t.holdMs >= 0.0);
        CHECK(t.holdMs <= 40.0);
    }

    SECTION("timings are sample-rate independent")
    {
        Timing ref;
        for (double rate : {44100.0, 48000.0, 96000.0})
        {
            const auto k = sineKey(rate, 1.5, 1000.0, -6.0f, keyOn, keyOff);
            const auto t = measure(runDucker(p, k, rate, 128), rate, keyOn, keyOff, p.depthDb);
            INFO("rate " << rate);
            REQUIRE(t.attackMs >= 0.0);
            REQUIRE(t.releaseMs >= 0.0);
            if (ref.attackMs < 0.0)
                ref = t;
            CHECK(t.attackMs == Approx(ref.attackMs).margin(0.5));
            CHECK(t.holdMs == Approx(ref.holdMs).margin(2.0));
            CHECK(t.releaseMs == Approx(ref.releaseMs).margin(10.0));
        }
    }

    SECTION("block size does not change the result")
    {
        const auto a = runDucker(p, key, sr, 1);
        const auto b = runDucker(p, key, sr, 4096);
        REQUIRE(a.size() == b.size());
        float maxDiff = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
            maxDiff = std::max(maxDiff, std::abs(a[i] - b[i]));
        CHECK(maxDiff < 1.0e-4f);
    }
}

TEST_CASE("ducker: a re-trigger during release restarts the attack cleanly", "[ducker]")
{
    const double sr = 48000.0;
    Ducker::Params p;
    // First burst 0.1–0.4 s; second onset at 0.4 + hold + release/2 ≈ 0.66 s.
    auto key = sineKey(sr, 1.2, 1000.0, -6.0f, 0.1, 0.4);
    const auto second = sineKey(sr, 1.2, 1000.0, -6.0f, 0.66, 1.2);
    for (size_t i = 0; i < key.size(); ++i)
        key[i] += second[i];

    const auto gr = runDucker(p, key, sr);
    // Mid-release at the second onset: partially recovered.
    const auto before = gr[static_cast<size_t>(toSample(0.659, sr))];
    CHECK(before > 1.0f);
    CHECK(before < 11.0f);
    // Back at depth within attack + detector lag of the second onset, and it stays there.
    const int full = firstAtLeast(gr, toSample(0.66, sr), p.depthDb - 1.0f);
    REQUIRE(full >= 0);
    CHECK((full - toSample(0.66, sr)) * 1000.0 / sr <= p.attackMs + 4.0);
    CHECK(meanOver(gr, 0.9, 1.2, sr) == Approx(p.depthDb).margin(0.2));
    // No overshoot beyond depth at any point.
    CHECK(maxOver(gr, 0.0, 1.2, sr) <= p.depthDb + 1.0e-3f);
}

TEST_CASE("ducker: key HPF stops low-frequency keys from triggering", "[ducker][hpf]")
{
    const double sr = 48000.0;
    Ducker::Params p; // key HPF 120 Hz, threshold −30 dBFS
    // −24 dBFS peak = −27 dBFS RMS: 3 dB over threshold, upper edge of the knee.
    const auto low = sineKey(sr, 0.5, 40.0, -24.0f);
    const auto mid = sineKey(sr, 0.5, 1000.0, -24.0f);

    const auto grLow = runDucker(p, low, sr);
    const auto grMid = runDucker(p, mid, sr);
    INFO("40 Hz → " << meanOver(grLow, 0.3, 0.5, sr) << " dB, 1 kHz → "
                    << meanOver(grMid, 0.3, 0.5, sr) << " dB");
    CHECK(meanOver(grMid, 0.3, 0.5, sr) > 2.0f); // triggers
    CHECK(maxOver(grLow, 0.0, 0.5, sr) < 0.1f);  // does not

    // Same 40 Hz key with the HPF parked at 20 Hz does trigger, proving the HPF did it.
    p.keyHpfHz = 20.0f;
    const auto grLowOpen = runDucker(p, low, sr);
    CHECK(meanOver(grLowOpen, 0.3, 0.5, sr) > 1.5f);

    // A hot 40 Hz key (−6 dBFS) still gets far less ducking than the same 1 kHz key.
    p.keyHpfHz = 120.0f;
    const auto hotLow = runDucker(p, sineKey(sr, 0.5, 40.0, -6.0f), sr);
    const auto hotMid = runDucker(p, sineKey(sr, 0.5, 1000.0, -6.0f), sr);
    CHECK(meanOver(hotMid, 0.3, 0.5, sr) - meanOver(hotLow, 0.3, 0.5, sr) > 8.0f);
}

TEST_CASE("ducker: disabling lets go of the gain quickly and never produces NaN", "[ducker]")
{
    const double sr = 48000.0;
    const auto key = sineKey(sr, 0.4, 1000.0, -6.0f);

    Ducker ducker;
    ducker.prepare(sr, 64);
    Ducker::Params p;
    ducker.setParams(p);
    std::vector<float> gain(key.size());
    const int half = static_cast<int>(key.size() / 2);
    ducker.process(key.data(), gain.data(), half);
    CHECK(ducker.getGainReductionDb() == Approx(12.0f).margin(0.2));
    CHECK(ducker.getBlockPeakGainReductionDb() == Approx(12.0f).margin(0.2));

    p.enabled = false;
    ducker.setParams(p);
    ducker.process(key.data() + half, gain.data() + half, half);
    // Within 40 ms of disabling the gain is back at unity, and it is exactly 1 at the end.
    const int at40ms = half + toSample(0.04, sr);
    CHECK(gain[static_cast<size_t>(at40ms)] == Approx(1.0f).margin(0.02));
    CHECK(gain.back() == Approx(1.0f).margin(0.0f));

    SECTION("silence, huge and tiny inputs stay finite")
    {
        p.enabled = true;
        ducker.setParams(p);
        std::vector<float> weird(4096, 0.0f);
        std::vector<float> g(weird.size());
        ducker.process(weird.data(), g.data(), static_cast<int>(weird.size()));
        for (auto v : g)
            REQUIRE(std::isfinite(v));
        std::fill(weird.begin(), weird.end(), 1000.0f);
        ducker.process(weird.data(), g.data(), static_cast<int>(weird.size()));
        for (auto v : g)
            REQUIRE(std::isfinite(v));
        std::fill(weird.begin(), weird.end(), 1.0e-30f);
        ducker.process(weird.data(), g.data(), static_cast<int>(weird.size()));
        for (auto v : g)
            REQUIRE(std::isfinite(v));
        // A hot, then silent, key returns exactly to unity (no lingering denormals).
        std::fill(weird.begin(), weird.end(), 0.0f);
        for (int i = 0; i < 100; ++i)
            ducker.process(weird.data(), g.data(), static_cast<int>(weird.size()));
        CHECK(g.back() == Approx(1.0f).margin(0.0f));
    }
}

TEST_CASE("biquad: RBJ highpass and lowpass have the expected response", "[dsp][biquad]")
{
    const double sr = 48000.0;
    auto gainAt = [&](dsp::Biquad& f, double hz)
    {
        f.reset();
        const auto x = sineKey(sr, 0.5, hz, 0.0f);
        std::vector<float> y(x);
        f.process(y.data(), static_cast<int>(y.size()));
        double acc = 0.0;
        const auto from = static_cast<size_t>(toSample(0.25, sr));
        for (size_t i = from; i < y.size(); ++i)
            acc += static_cast<double>(y[i]) * y[i];
        const double rms = std::sqrt(acc / static_cast<double>(y.size() - from));
        return 20.0 * std::log10(rms * std::numbers::sqrt2); // relative to the sine's peak
    };

    dsp::Biquad hpf;
    hpf.setHighpass(sr, 120.0);
    CHECK(gainAt(hpf, 120.0) == Approx(-3.0).margin(0.3));
    CHECK(gainAt(hpf, 40.0) < -18.0); // 2nd order: ~ −19 dB at 1/3 of the cutoff
    CHECK(gainAt(hpf, 1000.0) == Approx(0.0).margin(0.3));

    dsp::Biquad lpf;
    lpf.setLowpass(sr, 8000.0);
    CHECK(gainAt(lpf, 8000.0) == Approx(-3.0).margin(0.3));
    CHECK(gainAt(lpf, 100.0) == Approx(0.0).margin(0.1));
    CHECK(gainAt(lpf, 20000.0) < -12.0);

    dsp::Biquad flat;
    flat.setBypass();
    CHECK(gainAt(flat, 1000.0) == Approx(0.0).margin(1e-3));
}
