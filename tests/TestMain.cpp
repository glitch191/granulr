// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Console test runner. Returns a non-zero exit code when any test fails.
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    int passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* result = runner.getResult (i);
        failures += result->failures;
        passes += result->passes;
        if (result->failures > 0)
        {
            std::cout << "FAILED: " << result->unitTestName << " / " << result->subcategoryName << "\n";
            for (const auto& message : result->messages)
                std::cout << "    " << message << "\n";
        }
    }

    std::cout << "\nTests finished: " << passes << " checks passed, " << failures << " failed.\n";
    return failures == 0 ? 0 : 1;
}
