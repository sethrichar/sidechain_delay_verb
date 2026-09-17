#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke: processor constructs and reports sane defaults", "[smoke]")
{
    clearspace::ClearSpaceProcessor processor;
    CHECK(processor.getName() == "Clear Space");
    CHECK_FALSE(processor.acceptsMidi());
    CHECK_FALSE(processor.producesMidi());
    // Defaults: serial, delay 375 ms (+ 2 ms modulation allowance) at 35 % feedback
    // (8 × 0.377 s to −60 dB) plus the 2 s reverb decay.
    CHECK(processor.getTailLengthSeconds() == Catch::Approx(5.016).margin(1e-3));
    CHECK(processor.getEffectiveDelayTimeMs() == Catch::Approx(375.0f));
    CHECK(processor.getDelayGainReductionDb() == Catch::Approx(0.0));
    CHECK(processor.getReverbGainReductionDb() == Catch::Approx(0.0));
    CHECK_FALSE(processor.isSidechainConnected());
    CHECK(processor.getBusCount(true) == 2); // main + sidechain
    CHECK(processor.getBusCount(false) == 1);
}
