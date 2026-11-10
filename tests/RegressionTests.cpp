// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Guards the sound of existing settings: features added later must be neutral at their default
// values, so these fingerprints only change when an existing sound really changes.
#include "TestUtil.h"
#include "state/PresetManager.h"

using namespace grn;
using namespace testutil;

namespace
{

/** Deterministic musical signal: a decaying chord every 750 ms plus a click every 1.5 s. */
Generator referenceSignal (double sampleRate)
{
    return [=] (float* left, float* right, int n, int64_t start)
    {
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (start + i) / sampleRate;
            const double envelope = std::exp (-4.0 * std::fmod (t, 0.75));
            const double chord = std::sin (6.2831853 * 220.0 * t) + 0.7 * std::sin (6.2831853 * 277.18 * t)
                               + 0.6 * std::sin (6.2831853 * 329.63 * t);
            const double click = std::fmod (t, 1.5) < 0.004 ? 0.3 * std::sin (6.2831853 * 3100.0 * t) : 0.0;
            left[i] = static_cast<float> (0.25 * chord * envelope + click);
            right[i] = static_cast<float> (0.22 * chord * envelope - click);
        }
    };
}

struct Fingerprint
{
    double level = 0.0;   // mean absolute value
    double pattern = 0.0; // projection on a fixed pseudo-random pattern
};

Fingerprint render (const juce::String& presetName)
{
    auto processor = makeProcessor (48000.0, 256);
    if (presetName.isNotEmpty())
    {
        auto& presets = processor->getPresets();
        for (int i = 0; i < presets.getNumPresets(); ++i)
            if (presets.getInfo (i).factory && presets.getInfo (i).name == presetName)
                presets.load (i);
    }

    int64_t position = 0;
    std::vector<float> capture;
    run (*processor, referenceSignal (48000.0), 48000 * 6, 256, position, &capture);

    Fingerprint result;
    uint32_t state = 0x1234567u;
    for (const float sample : capture)
    {
        state = state * 1664525u + 1013904223u;
        const double weight = static_cast<double> (state >> 8) / 8388608.0 - 1.0;
        result.level += std::abs (static_cast<double> (sample));
        result.pattern += weight * static_cast<double> (sample);
    }
    result.level /= static_cast<double> (capture.size());
    result.pattern /= std::sqrt (static_cast<double> (capture.size()));
    return result;
}

class RegressionTests final : public juce::UnitTest
{
public:
    RegressionTests() : juce::UnitTest ("Regression", "granulr") {}

    void runTest() override
    {
        beginTest ("Existing sounds keep their fingerprint");

        struct Reference
        {
            const char* preset;
            double level, pattern;
        };
        const Reference references[] {
            { "", 0.066549353, 0.068124563 },
            { "Slow Clouds", 0.054501803, 0.015057727 },
            { "Sixteenth Shards", 0.045577735, 0.057187871 },
            { "Octave Halo", 0.065437531, 0.120419321 },
        };

        for (const auto& reference : references)
        {
            const juce::String name (reference.preset);
            const auto measured = render (name);
            const auto label = name.isEmpty() ? juce::String ("defaults") : name;
            logMessage (label + ": { " + juce::String (measured.level, 9) + ", " + juce::String (measured.pattern, 9) + " }");
            expectWithinAbsoluteError (measured.level, reference.level, 2.0e-6, label + " level");
            expectWithinAbsoluteError (measured.pattern, reference.pattern, 2.0e-5, label + " pattern");
        }
    }
};

RegressionTests regressionTests;

} // namespace
