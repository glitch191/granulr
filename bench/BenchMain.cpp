// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// CPU benchmark. Processes synthetic audio offline and reports, for each case, the processing
// time of the audio callback as a percentage of real time on one core.
//
//   granulr_bench [--seconds N] [--quick] [--check] [--presets]
//
//   --seconds N   audio duration per case (default 10)
//   --quick       only the reference cases (48 kHz and 96 kHz, 128 samples)
//   --check       exit code 1 when a CPU ceiling is exceeded
//   --presets     also measure every factory preset at 96 kHz
#include "plugin/PluginProcessor.h"
#include "state/PresetManager.h"
#include <algorithm>
#include <cstdio>
#include <vector>

using namespace grn;

namespace
{

void setParam (GranulrProcessor& processor, const juce::String& identifier, float value)
{
    if (auto* param = processor.getState().getParameter (identifier))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void configureWorstCase (GranulrProcessor& processor)
{
    setParam (processor, pid::quality, static_cast<float> (qualityHigh));
    setParam (processor, pid::mix, 100.0f);
    setParam (processor, pid::density, 400.0f);
    setParam (processor, pid::size, 2000.0f);
    setParam (processor, pid::spray, 2000.0f);
    setParam (processor, pid::jitter, 100.0f);
    setParam (processor, pid::pitchRandom, 24.0f);
    setParam (processor, pid::reverse, 50.0f);
    setParam (processor, pid::panSpread, 100.0f);
    setParam (processor, pid::width, 150.0f);
    setParam (processor, pid::feedback, 90.0f);
    setParam (processor, pid::duckAmount, 50.0f);
    setParam (processor, pid::scale, static_cast<float> (scaleMajor));
    for (int v = 1; v <= numHarmonyVoices; ++v)
        setParam (processor, pid::voiceProb (v), 100.0f);
    setParam (processor, pid::octaveUp, 50.0f);
    setParam (processor, pid::octaveDown, 50.0f);
    setParam (processor, pid::filterOn, 1.0f);
    setParam (processor, pid::filterPlace, static_cast<float> (placeFeedback));
    setParam (processor, pid::satOn, 1.0f);
    setParam (processor, pid::satPlace, static_cast<float> (placePost));
    setParam (processor, pid::diffOn, 1.0f);
    setParam (processor, pid::diffPlace, static_cast<float> (placeFeedback));
    setParam (processor, pid::diffDecay, 80.0f);

    // Everything added after the first version: flock, space per grain, resonator, gesture.
    setParam (processor, pid::flock, 50.0f);
    setParam (processor, pid::distance, 100.0f);
    setParam (processor, pid::orbit, 100.0f);
    setParam (processor, pid::doppler, 100.0f);
    setParam (processor, pid::resOn, 1.0f);
    setParam (processor, pid::resPlace, static_cast<float> (placeFeedback));
    setParam (processor, pid::resDecay, 80.0f);
    GestureData gesture;
    gesture.count = 64;
    gesture.seconds = 2.0f;
    for (int i = 0; i < gesture.count; ++i)
    {
        gesture.x[static_cast<size_t> (i)] = std::sin (0.1f * static_cast<float> (i));
        gesture.y[static_cast<size_t> (i)] = std::cos (0.23f * static_cast<float> (i));
    }
    processor.setGesture (gesture);

    // A full modulation matrix.
    auto& matrix = processor.getMatrix();
    for (int slot = 0; slot < numModSlots; ++slot)
        matrix.setSlot (slot, slot % numModSources, (slot * 7) % numModDests, 0.15f);
    // The editor is assumed open: the audio thread also feeds the visualisers.
    processor.getBridge().editorOpen.store (true);
}

/** Dense test signal: detuned saws with a slow tremolo plus noise bursts. */
void fillInput (juce::AudioBuffer<float>& buffer, int numSamples, int64_t position, double sampleRate, juce::Random& random)
{
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        const double t = static_cast<double> (position + i) / sampleRate;
        const double saw1 = 2.0 * (t * 110.0 - std::floor (t * 110.0)) - 1.0;
        const double saw2 = 2.0 * (t * 164.8 - std::floor (t * 164.8)) - 1.0;
        const double tremolo = 0.6 + 0.4 * std::sin (2.0 * 3.14159265358979 * 0.7 * t);
        const bool burst = std::fmod (t, 0.5) < 0.05;
        const float noise = burst ? (random.nextFloat() * 2.0f - 1.0f) * 0.5f : 0.0f;
        left[i] = static_cast<float> (0.3 * saw1 * tremolo) + noise;
        right[i] = static_cast<float> (0.3 * saw2 * tremolo) + noise;
    }
}

struct Result
{
    double mean = 0.0, p99 = 0.0, worst = 0.0;
};

Result measure (GranulrProcessor& processor, double sampleRate, int blockSize, double seconds, bool prepare = true)
{
    if (prepare)
    {
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
    }

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random random (1);
    const auto totalBlocks = static_cast<int> (seconds * sampleRate / blockSize);
    const auto warmupBlocks = static_cast<int> (2.0 * sampleRate / blockSize); // lets the grain pool fill up
    const double blockSeconds = static_cast<double> (blockSize) / sampleRate;

    std::vector<double> loads;
    loads.reserve (static_cast<size_t> (totalBlocks));
    int64_t position = 0;
    for (int block = 0; block < totalBlocks + warmupBlocks; ++block)
    {
        fillInput (buffer, blockSize, position, sampleRate, random);
        const auto start = juce::Time::getHighResolutionTicks();
        processor.processBlock (buffer, midi);
        const auto elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
        if (block >= warmupBlocks)
            loads.push_back (100.0 * elapsed / blockSeconds);
        position += blockSize;
    }

    Result result;
    if (loads.empty())
        return result;
    double sum = 0.0;
    for (double load : loads)
        sum += load;
    result.mean = sum / static_cast<double> (loads.size());
    std::sort (loads.begin(), loads.end());
    result.p99 = loads[static_cast<size_t> (static_cast<double> (loads.size() - 1) * 0.99)];
    result.worst = loads.back();
    return result;
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    double seconds = 10.0;
    bool quick = false, check = false, withPresets = false;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--seconds" && i + 1 < argc)
            seconds = juce::String (argv[++i]).getDoubleValue();
        else if (arg == "--quick")
            quick = true;
        else if (arg == "--check")
            check = true;
        else if (arg == "--presets")
            withPresets = true;
    }

    const std::vector<double> rates = quick ? std::vector<double> { 48000.0, 96000.0 }
                                            : std::vector<double> { 44100.0, 48000.0, 96000.0, 192000.0 };
    const std::vector<int> blocks = quick ? std::vector<int> { 128 } : std::vector<int> { 32, 64, 128, 256, 1024 };

    std::printf ("granulr benchmark, %s, %.0f s of audio per case\n", GRANULR_VERSION_STRING, seconds);
    std::printf ("CPU: %s, %d logical cores\n\n", juce::SystemStats::getCpuModel().toRawUTF8(), juce::SystemStats::getNumCpus());
    std::printf ("%-30s %8s %7s %9s %9s %9s\n", "case", "rate", "block", "mean %", "p99 %", "worst %");

    bool failed = false;
    auto report = [&] (const char* name, double rate, int block, const Result& r, double ceiling)
    {
        const bool over = ceiling > 0.0 && r.mean > ceiling;
        std::printf ("%-30s %8.0f %7d %9.2f %9.2f %9.2f%s\n", name, rate, block, r.mean, r.p99, r.worst,
                     over ? "   OVER BUDGET" : "");
        if (over)
            failed = true;
    };

    for (double rate : rates)
        for (int block : blocks)
        {
            GranulrProcessor processor;
            const bool reference = rate == 48000.0 && block == 128;
            report ("default", rate, block, measure (processor, rate, block, seconds), reference ? 5.0 : 0.0);
        }

    for (double rate : rates)
        for (int block : blocks)
        {
            GranulrProcessor processor;
            configureWorstCase (processor);
            const bool reference = rate == 48000.0 && block == 128;
            report ("worst case", rate, block, measure (processor, rate, block, seconds), reference ? 20.0 : 0.0);
        }

    for (double rate : quick ? std::vector<double> { 48000.0 } : std::vector<double> { 48000.0, 96000.0, 192000.0 })
    {
        // Frozen buffer with the living buffer rewriting it, on top of the worst case.
        GranulrProcessor processor;
        configureWorstCase (processor);
        setParam (processor, pid::evolve, 100.0f);
        setParam (processor, pid::wear, 50.0f);
        processor.setPlayConfigDetails (2, 2, rate, 128);
        processor.prepareToPlay (rate, 128);
        juce::AudioBuffer<float> buffer (2, 128);
        juce::MidiBuffer midi;
        juce::Random random (3);
        int64_t position = 0;
        for (int block = 0; block < static_cast<int> (9.0 * rate / 128.0); ++block, position += 128)
        {
            fillInput (buffer, 128, position, rate, random);
            processor.processBlock (buffer, midi);
        }
        setParam (processor, pid::freeze, 1.0f);
        report ("worst case, frozen evolving", rate, 128, measure (processor, rate, 128, seconds, false), rate == 48000.0 ? 20.0 : 0.0);
    }

    {
        GranulrProcessor processor;
        setParam (processor, pid::bypass, 1.0f);
        report ("bypassed", 48000.0, 128, measure (processor, 48000.0, 128, seconds), 0.0);
    }

    if (withPresets)
    {
        GranulrProcessor lister;
        const int count = lister.getPresets().getNumPresets();
        std::printf ("\n%d presets at 96 kHz, 128 samples\n", count);
        for (int i = 0; i < count; ++i)
        {
            GranulrProcessor processor;
            processor.getPresets().load (i);
            processor.getBridge().editorOpen.store (true);
            const auto name = processor.getPresets().getInfo (i).name;
            report (name.toRawUTF8(), 96000.0, 128, measure (processor, 96000.0, 128, seconds), 20.0);
        }
    }

    if (check && failed)
    {
        std::printf ("\nAt least one case is over its CPU ceiling.\n");
        return 1;
    }
    return 0;
}
