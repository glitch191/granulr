// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "plugin/PluginProcessor.h"
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>

namespace testutil
{

/** Allocation tracking: counts heap allocations made while `tracking` is set on this thread. */
extern thread_local bool tracking;
extern std::atomic<int> allocationCount;

inline std::unique_ptr<GranulrProcessor> makeProcessor (double sampleRate = 48000.0, int blockSize = 512)
{
    auto processor = std::make_unique<GranulrProcessor>();
    processor->setPlayConfigDetails (2, 2, sampleRate, blockSize);
    processor->prepareToPlay (sampleRate, blockSize);
    return processor;
}

inline void setParam (GranulrProcessor& processor, const juce::String& identifier, float value)
{
    if (auto* param = processor.getState().getParameter (identifier))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    else
        jassertfalse;
}

inline float getParam (GranulrProcessor& processor, const juce::String& identifier)
{
    return processor.getState().getRawParameterValue (identifier)->load();
}

/** Signal generator: fills a stereo block starting at an absolute sample index. */
using Generator = std::function<void (float* left, float* right, int numSamples, int64_t startSample)>;

inline Generator sine (double frequency, double sampleRate, float amplitude = 0.5f)
{
    return [=] (float* left, float* right, int n, int64_t start)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto value = amplitude * static_cast<float> (std::sin (2.0 * 3.14159265358979323846 * frequency
                                                                         * static_cast<double> (start + i) / sampleRate));
            left[i] = value;
            right[i] = value;
        }
    };
}

inline Generator noise (float amplitude = 0.5f, int seed = 1234)
{
    auto random = std::make_shared<juce::Random> (seed);
    return [=] (float* left, float* right, int n, int64_t)
    {
        for (int i = 0; i < n; ++i)
        {
            left[i] = amplitude * (random->nextFloat() * 2.0f - 1.0f);
            right[i] = amplitude * (random->nextFloat() * 2.0f - 1.0f);
        }
    };
}

inline Generator silence()
{
    return [] (float* left, float* right, int n, int64_t)
    {
        std::fill_n (left, n, 0.0f);
        std::fill_n (right, n, 0.0f);
    };
}

struct RunStats
{
    float peak = 0.0f;
    double sumSquares = 0.0;
    int64_t samples = 0;
    bool finite = true;
    float maxDelta = 0.0f;

    float rms() const { return samples > 0 ? static_cast<float> (std::sqrt (sumSquares / static_cast<double> (samples * 2))) : 0.0f; }
};

/** Runs the processor and gathers statistics. Optionally stores the left output channel. */
inline RunStats run (GranulrProcessor& processor, const Generator& generator, int64_t numSamples, int blockSize,
                     int64_t& position, std::vector<float>* capture = nullptr, bool trackAllocations = false)
{
    RunStats stats;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    float last = 0.0f;
    bool hasLast = false;

    int64_t done = 0;
    while (done < numSamples)
    {
        const int n = static_cast<int> (std::min<int64_t> (blockSize, numSamples - done));
        buffer.setSize (2, n, false, false, true);
        generator (buffer.getWritePointer (0), buffer.getWritePointer (1), n, position);

        if (trackAllocations)
            tracking = true;
        processor.processBlock (buffer, midi);
        tracking = false;

        const float* left = buffer.getReadPointer (0);
        const float* right = buffer.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                stats.finite = false;
            stats.peak = std::max (stats.peak, std::max (std::abs (left[i]), std::abs (right[i])));
            stats.sumSquares += static_cast<double> (left[i]) * left[i] + static_cast<double> (right[i]) * right[i];
            if (hasLast)
                stats.maxDelta = std::max (stats.maxDelta, std::abs (left[i] - last));
            last = left[i];
            hasLast = true;
        }
        if (capture != nullptr)
            capture->insert (capture->end(), left, left + n);

        stats.samples += n;
        position += n;
        done += n;
    }
    return stats;
}

} // namespace testutil
