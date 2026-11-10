// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestUtil.h"
#include "state/PresetManager.h"

using namespace grn;
using namespace testutil;

namespace
{

/** Musical test signal: a decaying chord every 750 ms, with a short noise burst now and then. */
Generator chordPulses (double sampleRate)
{
    auto random = std::make_shared<juce::Random> (9);
    return [=] (float* left, float* right, int n, int64_t start)
    {
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (start + i) / sampleRate;
            const double envelope = std::exp (-4.0 * std::fmod (t, 0.75));
            const double chord = std::sin (6.2831853 * 220.0 * t) + 0.7 * std::sin (6.2831853 * 277.18 * t)
                               + 0.6 * std::sin (6.2831853 * 329.63 * t);
            const float burst = std::fmod (t, 1.5) < 0.03 ? (random->nextFloat() - 0.5f) * 0.5f : 0.0f;
            left[i] = static_cast<float> (0.25 * chord * envelope) + burst;
            right[i] = static_cast<float> (0.25 * chord * envelope) + burst;
        }
    };
}

class PresetTests final : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest ("Presets", "granulr") {}

    void runTest() override
    {
        beginTest ("Factory presets only use known parameters and destinations");
        {
            auto processor = makeProcessor();
            auto& presets = processor->getPresets();
            int factoryCount = 0;
            juce::StringArray names;
            for (int i = 0; i < presets.getNumPresets(); ++i)
            {
                const auto& info = presets.getInfo (i);
                if (! info.factory)
                    continue;
                ++factoryCount;
                expect (! names.contains (info.name), "duplicate preset name " + info.name);
                names.add (info.name);
                expect (info.name.isNotEmpty() && info.category.isNotEmpty());
                expect (! info.name.containsAnyOf (juce::String::fromUTF8 ("\xe2\x80\x93\xe2\x80\x94")), "long dash in " + info.name);

                for (const auto& param : info.tree.getChildWithName ("PARAMETERS"))
                {
                    const auto identifier = param.getProperty ("id").toString();
                    auto* target = processor->getState().getParameter (identifier);
                    expect (target != nullptr, info.name + ": unknown parameter " + identifier);
                    if (target != nullptr)
                    {
                        const auto range = target->getNormalisableRange();
                        const float value = param.getProperty ("value");
                        expect (value >= range.start && value <= range.end, info.name + ": " + identifier + " out of range");
                    }
                }
                for (const auto& slot : info.tree.getChildWithName ("MATRIX"))
                {
                    const auto dest = slot.getProperty ("dest").toString();
                    expect (modDestForName (dest) >= 0, info.name + ": unknown destination " + dest);
                    const int source = slot.getProperty ("source", -1);
                    expect (source >= 0 && source < numModSources, info.name + ": bad source");
                }
            }
            expect (factoryCount >= 12, "only " + juce::String (factoryCount) + " factory presets");
        }

        beginTest ("Every factory preset renders a healthy signal");
        {
            auto lister = makeProcessor();
            const int count = lister->getPresets().getNumPresets();
            for (int i = 0; i < count; ++i)
            {
                if (! lister->getPresets().getInfo (i).factory)
                    continue;
                auto processor = makeProcessor();
                const auto presetName = processor->getPresets().getInfo (i).name;
                int64_t position = 0;
                const auto signal = chordPulses (48000.0);
                // Audio is already running when the preset is chosen, as in a real session.
                run (*processor, signal, 48000 * 4, 256, position);
                processor->getPresets().load (i);
                run (*processor, signal, 48000 * 4, 256, position);
                const auto stats = run (*processor, signal, 48000 * 6, 256, position);

                expect (stats.finite, presetName + ": non-finite output");
                expect (stats.peak <= 1.0f, presetName + ": peak " + juce::String (stats.peak));
                const float rmsDb = 20.0f * std::log10 (std::max (stats.rms(), 1.0e-9f));
                expect (rmsDb > -40.0f, presetName + ": too quiet (" + juce::String (rmsDb, 1) + " dB)");
                expect (rmsDb < -6.0f, presetName + ": too loud (" + juce::String (rmsDb, 1) + " dB)");
                logMessage (presetName + ": " + juce::String (rmsDb, 1) + " dB RMS, peak " + juce::String (stats.peak, 2)
                            + ", " + juce::String (processor->getEngine().getNumActiveGrains()) + " grains");
            }
        }
    }

private:
    static int modDestForName (const juce::String& paramId)
    {
        for (int d = 0; d < numModDests; ++d)
            if (modDestParamId (d) == paramId)
                return d;
        return -1;
    }
};

PresetTests presetTests;

} // namespace
