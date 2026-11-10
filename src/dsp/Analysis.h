// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Input analysis at control rate: onset detection and a brightness estimate. The results
// drive the Onset trigger mode and the Pitch, Brightness and Onset modulation sources.
#pragma once

#include "Util.h"
#include <algorithm>
#include <cmath>

namespace grn
{

/** Analysis results handed to the modulation engine (one sub-block late, like the peaks). */
struct AnalysisValues
{
    float pitch = 0.0f;      // -1..1, two octaves around middle C, held while unvoiced
    float brightness = 0.0f; // 0..1, 100 Hz to 10 kHz on a log scale
    bool onset = false;      // an onset was detected in the last sub-block
};

class InputAnalysis
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = static_cast<float> (sampleRate);
        peakRelease = std::exp (-1.0f / (0.025f * fs));
        slowCoef = 1.0f - std::exp (-1.0f / (0.060f * fs));
        energyCoef = 1.0f - std::exp (-1.0f / (0.030f * fs));
        refractorySamples = static_cast<int> (0.060f * fs);
        reset();
    }

    void reset() noexcept
    {
        peakEnv = slowEnv = 0.0f;
        energy = diffEnergy = 0.0f;
        previous = 0.0f;
        refractory = 0;
        brightness = 0.0f;
        onsetOffset = -1;
    }

    /** Analyses one sub-block. sensitivity is 0..1. */
    void process (const float* left, const float* right, int numSamples, float sensitivity) noexcept
    {
        // Ratio between the fast and the slow envelope that counts as an attack.
        const float ratio = 4.0f * std::pow (0.35f, std::clamp (sensitivity, 0.0f, 1.0f));
        onsetOffset = -1;

        for (int i = 0; i < numSamples; ++i)
        {
            const float mono = 0.5f * (left[i] + right[i]);
            const float level = std::abs (mono);

            peakEnv = std::max (level, peakEnv * peakRelease);
            if (refractory > 0)
                --refractory;
            else if (peakEnv > 0.003f && peakEnv > ratio * slowEnv && onsetOffset < 0)
            {
                onsetOffset = i;
                refractory = refractorySamples;
            }
            slowEnv += slowCoef * (peakEnv - slowEnv);

            const float difference = mono - previous;
            previous = mono;
            energy += energyCoef * (mono * mono - energy);
            diffEnergy += energyCoef * (difference * difference - diffEnergy);
        }

        // For a sine, sqrt (diffEnergy / energy) = 2 sin (pi f / fs): a cheap spectral centroid.
        if (energy > 1.0e-8f)
        {
            const float r = std::min (2.0f, std::sqrt (diffEnergy / energy));
            const float frequency = fs / kPi * std::asin (0.5f * r);
            const float target = std::clamp (std::log2 (std::max (frequency, 100.0f) / 100.0f) / 6.643856f, 0.0f, 1.0f);
            brightness += 0.2f * (target - brightness);
        }
    }

    /** Sample offset of the onset found by the last process() call, or -1. */
    int getOnsetOffset() const noexcept { return onsetOffset; }
    float getBrightness() const noexcept { return brightness; }

private:
    float fs = 48000.0f;
    float peakRelease = 0.999f, slowCoef = 0.001f, energyCoef = 0.001f;
    float peakEnv = 0.0f, slowEnv = 0.0f, energy = 0.0f, diffEnergy = 0.0f, previous = 0.0f;
    float brightness = 0.0f;
    int refractory = 0, refractorySamples = 2880, onsetOffset = -1;
};

} // namespace grn
