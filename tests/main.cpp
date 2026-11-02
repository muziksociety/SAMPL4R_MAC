// SamplarTests - runs every registered juce::UnitTest.
//
//   SamplarTests.exe                 run all tests
//   SamplarTests.exe --list          list test names
//   SamplarTests.exe <substr> ...    run tests whose name contains any <substr> (case-insensitive)
//   SamplarTests.exe --verbose       also echo the code-under-test log to the console
//
// Exit code: 0 = all passed, 1 = at least one failure (or no test matched), 2 = crash in a test.
// The code-under-test log (juce::Logger) goes to %TEMP%/SamplarTests.log.

#include <juce_events/juce_events.h>

#include <iostream>

namespace {

class TestLogger : public juce::Logger
{
public:
    TestLogger (const juce::File& f, bool echo) : file_ (f), echo_ (echo)
    {
        file_.replaceWithText ({});
        stream_ = std::make_unique<juce::FileOutputStream> (file_);
    }
    void logMessage (const juce::String& message) override
    {
        const juce::ScopedLock sl (lock_);
        if (stream_ != nullptr && stream_->openedOk())
        {
            *stream_ << message << juce::newLine;
            stream_->flush();
        }
        if (echo_)
            std::cout << "    | " << message.toStdString() << "\n";
    }
    const juce::File& file() const { return file_; }
private:
    juce::File file_;
    bool echo_;
    juce::CriticalSection lock_;
    std::unique_ptr<juce::FileOutputStream> stream_;
};

class ConsoleRunner : public juce::UnitTestRunner
{
public:
    void logMessage (const juce::String& message) override
    {
        std::cout << message.toStdString() << std::endl;
    }
};

} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // MessageManager on this (the main) thread

    bool list = false, verbose = false;
    juce::StringArray filters;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--list")         list = true;
        else if (a == "--verbose") verbose = true;
        else                       filters.add (a);
    }

    auto& all = juce::UnitTest::getAllTests();
    if (list)
    {
        for (auto* t : all)
            std::cout << t->getCategory().toStdString() << " / " << t->getName().toStdString() << "\n";
        return 0;
    }

    juce::Array<juce::UnitTest*> selected;
    for (auto* t : all)
    {
        bool match = filters.isEmpty();
        for (const auto& f : filters)
            match = match || t->getName().containsIgnoreCase (f) || t->getCategory().containsIgnoreCase (f);
        if (match)
            selected.add (t);
    }
    if (selected.isEmpty())
    {
        std::cout << "No tests matched." << std::endl;
        return 1;
    }

    TestLogger logger (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SamplarTests.log"), verbose);
    juce::Logger::setCurrentLogger (&logger);

    ConsoleRunner runner;
    runner.setPassesAreLogged (false);
    runner.setAssertOnFailure (false);

    int crashed = 0;
    try
    {
        runner.runTests (selected, (juce::int64) 0x5A4D504C);
    }
    catch (const std::exception& e)
    {
        std::cout << "!!! Uncaught exception in a test: " << e.what() << std::endl;
        crashed = 1;
    }

    juce::Logger::setCurrentLogger (nullptr);

    int passes = 0, failures = 0;
    std::cout << "\n==================== SamplarTests summary ====================\n";
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult (i);
        passes += r->passes;
        failures += r->failures;
        std::cout << (r->failures == 0 ? "  PASS  " : "  FAIL  ")
                  << r->unitTestName.toStdString() << " / " << r->subcategoryName.toStdString()
                  << "  (" << r->passes << " ok, " << r->failures << " failed)\n";
        for (const auto& m : r->messages)
            std::cout << "          " << m.toStdString() << "\n";
    }
    std::cout << "--------------------------------------------------------------\n"
              << "  " << selected.size() << " test class(es), " << passes << " checks passed, "
              << failures << " failed" << (crashed ? ", CRASHED" : "") << "\n"
              << "  code-under-test log: " << logger.file().getFullPathName().toStdString() << "\n"
              << "==============================================================" << std::endl;

    if (crashed) return 2;
    return failures == 0 ? 0 : 1;
}
