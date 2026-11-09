// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Console test runner. Returns a non-zero exit code when any test fails.
#include "TestUtil.h"
#include <cstdlib>
#include <iostream>
#include <new>

// ---------------------------------------------------------------------------------------------
// Allocation tracker: every heap allocation made while `tracking` is set is counted, which lets
// the tests prove that the audio callback never allocates.
namespace testutil
{
thread_local bool tracking = false;
std::atomic<int> allocationCount { 0 };
}

void* operator new (std::size_t size)
{
    if (testutil::tracking)
        ++testutil::allocationCount;
    if (void* memory = std::malloc (size != 0 ? size : 1))
        return memory;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size) { return operator new (size); }

void* operator new (std::size_t size, std::align_val_t alignment)
{
    if (testutil::tracking)
        ++testutil::allocationCount;
    if (void* memory = _aligned_malloc (size != 0 ? size : 1, static_cast<std::size_t> (alignment)))
        return memory;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size, std::align_val_t alignment) { return operator new (size, alignment); }

void operator delete (void* memory) noexcept { std::free (memory); }
void operator delete[] (void* memory) noexcept { std::free (memory); }
void operator delete (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete (void* memory, std::align_val_t) noexcept { _aligned_free (memory); }
void operator delete[] (void* memory, std::align_val_t) noexcept { _aligned_free (memory); }
void operator delete (void* memory, std::size_t, std::align_val_t) noexcept { _aligned_free (memory); }
void operator delete[] (void* memory, std::size_t, std::align_val_t) noexcept { _aligned_free (memory); }

// ---------------------------------------------------------------------------------------------
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
