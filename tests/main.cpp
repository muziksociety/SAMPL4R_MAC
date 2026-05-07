#include <juce_core/juce_core.h>

int main() {
    juce::UnitTestRunner runner;
    runner.setPassesAreLogged(false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) {
        if (auto* r = runner.getResult(i))
            failures += r->failures;
    }
    return failures == 0 ? 0 : 1;
}
