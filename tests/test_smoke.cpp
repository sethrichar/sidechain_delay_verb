#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke: processor constructs and reports sane defaults", "[smoke]")
{
    clearspace::ClearSpaceProcessor processor;
    CHECK(processor.getName() == "Clear Space");
    CHECK_FALSE(processor.acceptsMidi());
    CHECK_FALSE(processor.producesMidi());
    // Defaults: serial, delay 500 ms stand-in at 35 % feedback (8 × 0.5 s to −60 dB) plus
    // the 2 s reverb decay.
    CHECK(processor.getTailLengthSeconds() == Catch::Approx(6.0));
    CHECK(processor.getDelayGainReductionDb() == Catch::Approx(0.0));
    CHECK(processor.getReverbGainReductionDb() == Catch::Approx(0.0));
    CHECK_FALSE(processor.isSidechainConnected());
    CHECK(processor.getBusCount(true) == 2); // main + sidechain
    CHECK(processor.getBusCount(false) == 1);
}
