// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Scale quantisation and a light monophonic pitch tracker (normalised square difference
// function, after McLeod). The tracker keeps a history so that a grain reading old audio
// can look up the pitch that was detected at that point of the buffer.
#pragma once

#include "Util.h"
#include "../params/ParameterIds.h"
#include <array>
#include <cstring>
#include <limits>

namespace grn
{

/** Allowed pitch classes above the root, bit n = n semitones above the root. */
inline uint16_t scaleMask (int scale, uint16_t customMask) noexcept
{
    switch (scale)
    {
        case scaleChromatic: return 0x0fff;
        case scaleMajor:     return 0x0ab5; // 0 2 4 5 7 9 11
        case scaleMinor:     return 0x05ad; // 0 2 3 5 7 8 10
        case scaleMajorPent: return 0x0295; // 0 2 4 7 9
        case scaleMinorPent: return 0x04a9; // 0 3 5 7 10
        case scaleDorian:    return 0x06ad; // 0 2 3 5 7 9 10
        case scalePhrygian:  return 0x05ab; // 0 1 3 5 7 8 10
        case scaleWholeTone: return 0x0555; // 0 2 4 6 8 10
        case scaleOctaves:   return 0x0001;
        case scaleFifths:    return 0x0081; // 0 7
        case scaleCustom:    return (customMask & 0x0fff) != 0 ? static_cast<uint16_t> (customMask & 0x0fff)
                                                               : static_cast<uint16_t> (0x0fff);
        default: break;
    }
    return 0;
}

/** Nearest value to x (in semitones or MIDI notes) whose pitch class belongs to the scale. */
inline float nearestInScale (float x, uint16_t mask, int root) noexcept
{
    if (mask == 0)
        return x;

    const int base = static_cast<int> (std::floor (x + 0.5f));
    float best = x;
    float bestDistance = std::numeric_limits<float>::max();
    for (int offset = -6; offset <= 6; ++offset)
    {
        const int candidate = base + offset;
        const int pitchClass = (((candidate - root) % 12) + 12) % 12;
        if (((mask >> pitchClass) & 1u) == 0)
            continue;
        const float distance = std::abs (static_cast<float> (candidate) - x);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = static_cast<float> (candidate);
        }
    }
    return best;
}

/** Transposition (semitones) to apply so that the grain lands on the scale.
    detectedNote is the MIDI note of the source at the read position, or NaN when unknown:
    in that case the interval itself is snapped to the scale degrees. */
inline float quantiseTransposition (float semitones, float detectedNote, uint16_t mask, int root) noexcept
{
    if (mask == 0)
        return semitones;
    if (std::isnan (detectedNote))
        return nearestInScale (semitones, mask, 0);
    return nearestInScale (detectedNote + semitones, mask, root) - detectedNote;
}

// ---------------------------------------------------------------------------------------------
class PitchTracker
{
public:
    static constexpr int windowSize = 512;
    static constexpr int hopSize = 256;
    static constexpr int maxTau = 256;
    static constexpr int minTau = 5;
    static constexpr int historySize = 2048;

    void prepare (double sampleRate) noexcept
    {
        decimation = std::max (1, static_cast<int> (std::floor (sampleRate / 11025.0 + 0.5)));
        decimatedRate = static_cast<float> (sampleRate) / static_cast<float> (decimation);
        framesPerHop = hopSize * decimation;
        tausPerSample = static_cast<float> (maxTau + 1) / (static_cast<float> (framesPerHop) * 0.5f);
        reset();
    }

    void reset() noexcept
    {
        collected = 0;
        decimCount = 0;
        decimSum = 0.0f;
        nextHopIndex = 0;
        lastComputedHop = -1;
        analysing = false;
        tauBudget = 0.0f;
        history.fill (std::numeric_limits<float>::quiet_NaN());
    }

    /** Feeds the mono signal written into the grain buffer and advances the analysis. */
    void process (const float* mono, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            decimSum += mono[i];
            if (++decimCount >= decimation)
            {
                collect[static_cast<size_t> (collected++)] = decimSum / static_cast<float> (decimation);
                decimSum = 0.0f;
                decimCount = 0;
                if (collected >= windowSize)
                    startAnalysis();
            }
        }

        if (analysing)
        {
            tauBudget += tausPerSample * static_cast<float> (numSamples);
            while (analysing && tauBudget >= 1.0f)
            {
                tauBudget -= 1.0f;
                computeNextTau();
            }
        }
    }

    /** MIDI note detected around an absolute frame of the buffer, NaN when unvoiced or unknown. */
    float noteAt (int64_t frame) const noexcept
    {
        if (lastComputedHop < 0)
            return std::numeric_limits<float>::quiet_NaN();
        int64_t hop = (frame + framesPerHop / 2) / framesPerHop - 1;
        hop = std::min<int64_t> (hop, lastComputedHop);
        if (hop < 0 || hop <= lastComputedHop - historySize)
            return std::numeric_limits<float>::quiet_NaN();
        return history[static_cast<size_t> (hop & (historySize - 1))];
    }

    float latestNote() const noexcept
    {
        return lastComputedHop < 0 ? std::numeric_limits<float>::quiet_NaN()
                                   : history[static_cast<size_t> (lastComputedHop & (historySize - 1))];
    }

private:
    void startAnalysis() noexcept
    {
        if (analysing)
            finishAnalysis(); // should not happen, but never drop a hop silently

        std::memcpy (work.data(), collect.data(), sizeof (float) * windowSize);
        std::memmove (collect.data(), collect.data() + hopSize, sizeof (float) * (windowSize - hopSize));
        collected = windowSize - hopSize;

        float energy = 0.0f, mean = 0.0f;
        for (float v : work)
            mean += v;
        mean /= static_cast<float> (windowSize);
        for (float& v : work)
        {
            v -= mean;
            energy += v * v;
        }
        windowRms = std::sqrt (energy / static_cast<float> (windowSize));
        analysisHop = nextHopIndex++;
        tauCursor = 0;
        analysing = true;
    }

    void computeNextTau() noexcept
    {
        const int tau = tauCursor;
        float acf = 0.0f, norm = 0.0f;
        const int count = windowSize - tau;
        const float* a = work.data();
        const float* b = work.data() + tau;
        for (int j = 0; j < count; ++j)
        {
            acf += a[j] * b[j];
            norm += a[j] * a[j] + b[j] * b[j];
        }
        nsdf[static_cast<size_t> (tau)] = norm > 1.0e-12f ? 2.0f * acf / norm : 0.0f;
        if (++tauCursor > maxTau)
            finishAnalysis();
    }

    void finishAnalysis() noexcept
    {
        // Any tau not yet computed counts as zero (only reachable on an overrun).
        for (int t = tauCursor; t <= maxTau; ++t)
            nsdf[static_cast<size_t> (t)] = 0.0f;

        float note = std::numeric_limits<float>::quiet_NaN();
        if (windowRms > 1.0e-3f)
        {
            // Skip the lobe around tau = 0, then collect the key maxima.
            int t = 1;
            while (t <= maxTau && nsdf[static_cast<size_t> (t)] > 0.0f)
                ++t;

            float highest = 0.0f;
            for (int u = t; u < maxTau; ++u)
                highest = std::max (highest, nsdf[static_cast<size_t> (u)]);

            if (highest > 0.8f)
            {
                const float threshold = 0.9f * highest;
                for (int u = std::max (t, minTau); u < maxTau; ++u)
                {
                    const float y0 = nsdf[static_cast<size_t> (u - 1)];
                    const float y1 = nsdf[static_cast<size_t> (u)];
                    const float y2 = nsdf[static_cast<size_t> (u + 1)];
                    if (y1 >= threshold && y1 >= y0 && y1 > y2)
                    {
                        const float denom = y0 - 2.0f * y1 + y2;
                        const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (y0 - y2) / denom : 0.0f;
                        const float period = static_cast<float> (u) + std::clamp (shift, -1.0f, 1.0f);
                        const float frequency = decimatedRate / period;
                        note = 69.0f + 12.0f * std::log2 (frequency / 440.0f);
                        break;
                    }
                }
            }
        }

        history[static_cast<size_t> (analysisHop & (historySize - 1))] = note;
        lastComputedHop = analysisHop;
        analysing = false;
    }

    int decimation = 4, framesPerHop = 1024;
    float decimatedRate = 12000.0f, tausPerSample = 0.5f;
    std::array<float, windowSize> collect {};
    std::array<float, windowSize> work {};
    std::array<float, maxTau + 2> nsdf {};
    std::array<float, historySize> history {};
    int collected = 0, decimCount = 0, tauCursor = 0;
    float decimSum = 0.0f, windowRms = 0.0f, tauBudget = 0.0f;
    int64_t nextHopIndex = 0, analysisHop = 0, lastComputedHop = -1;
    bool analysing = false;
};

} // namespace grn
