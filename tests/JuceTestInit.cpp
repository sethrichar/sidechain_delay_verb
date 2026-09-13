// Brings JUCE up and down around the whole Catch2 run so the message manager exists and
// the leak detector runs after every DeletedAtShutdown singleton is gone.

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <juce_events/juce_events.h>

#include <memory>

namespace
{

class JuceLifetimeListener final : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override
    {
        juceInit = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    }

    void testRunEnded(const Catch::TestRunStats&) override { juceInit.reset(); }

private:
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juceInit;
};

} // namespace

CATCH_REGISTER_LISTENER(JuceLifetimeListener)
