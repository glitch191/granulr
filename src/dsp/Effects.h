// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Built-in effects: state variable filter, saturation, diffusion, limiter, DC blocker.
// Everything here is allocation free after prepare() and safe to call on the audio thread.
#pragma once

#include "Util.h"
#include "../params/ParameterIds.h"
#include <vector>

namespace grn
{

// ---------------------------------------------------------------------------------------------
/** Stereo TPT state variable filter (Zavalishin). Stable under fast modulation. */
class StateVariableFilter
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = static_cast<float> (sampleRate);
        reset();
    }

    void reset() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }

    /** cutoff in Hz, resonance 0..1. Call at control rate. */
    void setParams (float cutoffHz, float resonance, int filterType) noexcept
    {
        const float fc = std::clamp (cutoffHz, 16.0f, fs * 0.47f);
        g = std::tan (kPi * fc / fs);
        const float q = 0.5f * std::pow (40.0f, std::clamp (resonance, 0.0f, 1.0f));
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        type = filterType;
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* channels[2] { left, right };
        for (int ch = 0; ch < 2; ++ch)
        {
            float s1 = ic1[ch], s2 = ic2[ch];
            float* data = channels[ch];
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = data[i];
                const float v3 = x - s2;
                const float v1 = a1 * s1 + a2 * v3;
                const float v2 = s2 + a2 * s1 + a3 * v3;
                s1 = 2.0f * v1 - s1;
                s2 = 2.0f * v2 - s2;
                data[i] = type == filterLowpass ? v2 : (type == filterBandpass ? v1 : x - k * v1 - v2);
            }
            ic1[ch] = s1;
            ic2[ch] = s2;
        }
    }

private:
    float fs = 48000.0f;
    float g = 0.5f, k = 1.0f, a1 = 0.5f, a2 = 0.25f, a3 = 0.125f;
    float ic1[2] {}, ic2[2] {};
    int type = filterLowpass;
};

// ---------------------------------------------------------------------------------------------
/** Soft (tanh) or hard (clip) saturation with first-order antiderivative anti-aliasing. */
class Saturator
{
public:
    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            lastIn[ch] = 0.0f;
            lastAnti[ch] = 0.0f;
        }
        driveSmoothed = driveTarget;
    }

    void setParams (float driveDb, int saturationType) noexcept
    {
        driveTarget = dbToGain (std::clamp (driveDb, 0.0f, 36.0f));
        type = saturationType;
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        const float driveStart = driveSmoothed;
        const float driveStep = (driveTarget - driveStart) / static_cast<float> (std::max (1, numSamples));
        const float compStart = std::pow (driveStart, -0.6f);
        const float compStep = (std::pow (driveTarget, -0.6f) - compStart) / static_cast<float> (std::max (1, numSamples));
        float* channels[2] { left, right };
        for (int ch = 0; ch < 2; ++ch)
        {
            float x0 = lastIn[ch];
            float f0 = lastAnti[ch];
            float drive = driveStart;
            float comp = compStart;
            float* data = channels[ch];
            for (int i = 0; i < numSamples; ++i)
            {
                drive += driveStep;
                comp += compStep;
                const float x1 = data[i] * drive;
                const float f1 = type == satSoft ? antiSoft (x1) : antiHard (x1);
                const float dx = x1 - x0;
                float y;
                if (std::abs (dx) > 1.0e-4f)
                    y = (f1 - f0) / dx;
                else
                    y = shape (0.5f * (x1 + x0));
                // Level compensation keeps the perceived loudness roughly constant with drive.
                data[i] = y * comp;
                x0 = x1;
                f0 = f1;
            }
            lastIn[ch] = x0;
            lastAnti[ch] = f0;
        }
        driveSmoothed = driveTarget;
    }

private:
    float shape (float x) const noexcept
    {
        return type == satSoft ? std::tanh (x) : std::clamp (x, -1.0f, 1.0f);
    }

    static float antiSoft (float x) noexcept
    {
        // log(cosh(x)) written to stay finite for large |x|.
        const float ax = std::abs (x);
        return ax + std::log1p (std::exp (-2.0f * ax)) - 0.69314718f;
    }

    static float antiHard (float x) noexcept
    {
        const float ax = std::abs (x);
        return ax <= 1.0f ? 0.5f * x * x : ax - 0.5f;
    }

    float driveTarget = 1.0f, driveSmoothed = 1.0f;
    float lastIn[2] {}, lastAnti[2] {};
    int type = satSoft;
};

// ---------------------------------------------------------------------------------------------
/** Tuned resonator: a bank of damped comb filters set on the notes of a scale, excited by the
    signal like sympathetic strings. */
class Resonator
{
public:
    static constexpr int maxLines = 6;

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        lineLength = nextPowerOfTwo (static_cast<int> (fs / 30.0f) + 16);
        lineMask = lineLength - 1;
        for (auto& line : lines)
            line.assign (static_cast<size_t> (lineLength), 0.0f);
        slewCoef = onePoleCoef (0.02f, fs);
        reset();
    }

    void reset() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        for (int l = 0; l < maxLines; ++l)
        {
            dampState[l] = 0.0f;
            delay[l] = targetDelay[l];
        }
        writeIndex = 0;
    }

    /** Frequency of a line in Hz after the last setParams call (for tests and display). */
    float getFrequency (int line) const noexcept { return frequency[std::clamp (line, 0, maxLines - 1)]; }
    int getNumLines() const noexcept { return activeLines; }

    /** scaleBits: bit n set when pitch class n above the root is allowed (0 = no scale, uses
        root and fifth). decay, damping and mixAmount are 0..1. Call at control rate. */
    void setParams (uint16_t scaleBits, int root, int octave, float decay, float damping,
                    float mixAmount, int numLines) noexcept
    {
        activeLines = std::clamp (numLines, 1, maxLines);
        mix = std::clamp (mixAmount, 0.0f, 1.0f);
        dampCoef = 1.0f - 0.8f * std::clamp (damping, 0.0f, 1.0f);
        const float t60 = 0.15f * std::pow (60.0f, std::clamp (decay, 0.0f, 1.0f));

        if ((scaleBits & 0x0fff) == 0)
            scaleBits = 0x0081;

        // The most consonant degrees first, then octave copies when the scale has few notes.
        static const int priority[12] { 0, 7, 4, 3, 10, 11, 2, 5, 9, 8, 6, 1 };
        int notes[maxLines];
        int count = 0;
        for (int k = 0; k < 12 && count < activeLines; ++k)
            if ((scaleBits >> priority[k]) & 1u)
                notes[count++] = priority[k];
        const int distinct = count;
        for (int k = 0; count < activeLines; ++k)
            notes[count++] = notes[k % distinct] + 12 * (1 + k / distinct);
        std::sort (notes, notes + activeLines);

        const float base = 48.0f + static_cast<float> (root) + 12.0f * static_cast<float> (octave);
        const float filterDelay = (1.0f - dampCoef) / dampCoef;
        for (int l = 0; l < activeLines; ++l)
        {
            const float hz = 440.0f * std::exp2 ((base + static_cast<float> (notes[l]) - 69.0f) / 12.0f);
            frequency[l] = hz;
            targetDelay[l] = std::clamp (fs / hz - filterDelay, 2.0f, static_cast<float> (lineLength - 4));
            feedback[l] = std::pow (10.0f, -3.0f / (hz * t60));
            // Between a constant level for noise (sqrt (1 - g^2)) and a constant peak for a note
            // that sits exactly on the string (1 - g): long decays would otherwise explode on
            // tuned material, which is exactly what this effect is fed with.
            const float noiseGain = std::sqrt (std::max (0.0f, 1.0f - feedback[l] * feedback[l]));
            const float peakGain = 3.0f * (1.0f - feedback[l]);
            inputGain[l] = std::sqrt (noiseGain * peakGain);
            const float pan = activeLines > 1 ? static_cast<float> (l) / static_cast<float> (activeLines - 1) : 0.5f;
            const float angle = (0.2f + 0.6f * ((l & 1) ? 1.0f - pan : pan)) * 0.5f * kPi;
            panL[l] = std::cos (angle);
            panR[l] = std::sin (angle);
        }
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        const int n = activeLines;
        const float norm = 1.6f / std::sqrt (static_cast<float> (n));

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = 0.5f * (left[i] + right[i]);
            float outL = 0.0f, outR = 0.0f;
            for (int l = 0; l < n; ++l)
            {
                delay[l] += slewCoef * (targetDelay[l] - delay[l]);
                const float readPos = static_cast<float> (writeIndex) - delay[l];
                const float floorPos = std::floor (readPos);
                const float frac = readPos - floorPos;
                const int i0 = static_cast<int> (floorPos) & lineMask;
                const int i1 = (i0 + 1) & lineMask;
                auto& line = lines[l];
                const float tap = line[static_cast<size_t> (i0)]
                                  + frac * (line[static_cast<size_t> (i1)] - line[static_cast<size_t> (i0)]);
                dampState[l] += dampCoef * (tap - dampState[l]);
                // Each string saturates softly at half scale, so a sustained note cannot run away.
                const float y = 0.5f * softClip (2.0f * (input * inputGain[l] + feedback[l] * dampState[l]));
                line[static_cast<size_t> (writeIndex)] = y;
                outL += y * panL[l];
                outR += y * panR[l];
            }
            writeIndex = (writeIndex + 1) & lineMask;
            left[i] += mix * (outL * norm - left[i]);
            right[i] += mix * (outR * norm - right[i]);
        }
    }

private:
    float fs = 48000.0f;
    int lineLength = 2048, lineMask = 2047, writeIndex = 0, activeLines = maxLines;
    std::array<std::vector<float>, maxLines> lines;
    float delay[maxLines] {}, targetDelay[maxLines] { 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f };
    float feedback[maxLines] {}, inputGain[maxLines] {}, dampState[maxLines] {};
    float panL[maxLines] {}, panR[maxLines] {}, frequency[maxLines] {};
    float dampCoef = 1.0f, slewCoef = 0.001f, mix = 0.4f;
};

// ---------------------------------------------------------------------------------------------
/** Light diffusion and reverb: 4 allpass stages followed by a feedback delay network. */
class Diffuser
{
public:
    static constexpr int maxLines = 8;
    static constexpr int numAllpasses = 4;

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        lineLength = nextPowerOfTwo (static_cast<int> (fs * 0.12f) + 8);
        lineMask = lineLength - 1;
        for (auto& line : lines)
            line.assign (static_cast<size_t> (lineLength), 0.0f);

        static const float allpassMs[2][numAllpasses] { { 4.7f, 7.9f, 11.3f, 15.1f }, { 5.3f, 8.3f, 12.1f, 16.3f } };
        for (int ch = 0; ch < 2; ++ch)
            for (int a = 0; a < numAllpasses; ++a)
            {
                allpassLength[ch][a] = std::max (2, static_cast<int> (allpassMs[ch][a] * 0.001f * fs));
                allpassBuffer[ch][a].assign (static_cast<size_t> (allpassLength[ch][a]), 0.0f);
                allpassIndex[ch][a] = 0;
            }

        sizeCoef = onePoleCoef (0.15f, fs);
        reset();
    }

    void reset() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        for (int ch = 0; ch < 2; ++ch)
            for (int a = 0; a < numAllpasses; ++a)
                std::fill (allpassBuffer[ch][a].begin(), allpassBuffer[ch][a].end(), 0.0f);
        for (auto& state : dampState)
            state = 0.0f;
        writeIndex = 0;
        scaleSmoothed = scaleTarget;
    }

    /** All arguments 0..1. numLines is 4 (Eco) or 8. Call at control rate. */
    void setParams (float size, float decay, float damping, float mixAmount, int numLines) noexcept
    {
        scaleTarget = 0.3f + 1.2f * std::clamp (size, 0.0f, 1.0f);
        const float t60 = 0.15f * std::pow (20.0f / 0.15f, std::clamp (decay, 0.0f, 1.0f));
        decayPerSample = -3.0f / (t60 * fs);
        damp = std::clamp (damping, 0.0f, 1.0f) * 0.85f;
        mix = std::clamp (mixAmount, 0.0f, 1.0f);
        activeLines = numLines >= maxLines ? maxLines : 4;
        for (int l = 0; l < maxLines; ++l)
            lineGain[l] = std::pow (10.0f, decayPerSample * baseMs[l] * 0.001f * fs * scaleTarget);
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        const int n = activeLines;
        const float norm = n == maxLines ? 0.35355339f : 0.5f;
        const float outGain = n == maxLines ? 0.5f : 0.70710678f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = left[i];
            const float dryR = right[i];
            float xl = dryL;
            float xr = dryR;

            // Series allpass diffusers.
            for (int a = 0; a < numAllpasses; ++a)
            {
                xl = allpass (0, a, xl);
                xr = allpass (1, a, xr);
            }

            scaleSmoothed += sizeCoef * (scaleTarget - scaleSmoothed);

            float y[maxLines];
            for (int l = 0; l < n; ++l)
            {
                const float delay = baseMs[l] * 0.001f * fs * scaleSmoothed;
                const float readPos = static_cast<float> (writeIndex) - delay;
                const float floorPos = std::floor (readPos);
                const float frac = readPos - floorPos;
                const int i0 = static_cast<int> (floorPos) & lineMask;
                const int i1 = (i0 + 1) & lineMask;
                const float value = lines[l][static_cast<size_t> (i0)]
                                    + frac * (lines[l][static_cast<size_t> (i1)] - lines[l][static_cast<size_t> (i0)]);
                dampState[l] += (1.0f - damp) * (value - dampState[l]);
                y[l] = dampState[l];
            }

            float wetL = 0.0f, wetR = 0.0f;
            for (int l = 0; l < n; l += 2)
            {
                wetL += y[l];
                wetR += y[l + 1];
            }

            // Fast Walsh-Hadamard transform (lossless mixing matrix).
            for (int step = 1; step < n; step <<= 1)
                for (int start = 0; start < n; start += step << 1)
                    for (int j = start; j < start + step; ++j)
                    {
                        const float a = y[j];
                        const float b = y[j + step];
                        y[j] = a + b;
                        y[j + step] = a - b;
                    }

            for (int l = 0; l < n; ++l)
            {
                const float input = (l & 1) == 0 ? xl : xr;
                lines[l][static_cast<size_t> (writeIndex)] = input * 0.5f + y[l] * norm * lineGain[l];
            }
            writeIndex = (writeIndex + 1) & lineMask;

            left[i] = dryL + mix * (wetL * outGain - dryL);
            right[i] = dryR + mix * (wetR * outGain - dryR);
        }
    }

private:
    float allpass (int ch, int a, float x) noexcept
    {
        auto& buffer = allpassBuffer[ch][a];
        int& index = allpassIndex[ch][a];
        const float delayed = buffer[static_cast<size_t> (index)];
        const float input = x + 0.6f * delayed;
        buffer[static_cast<size_t> (index)] = input;
        if (++index >= allpassLength[ch][a])
            index = 0;
        return delayed - 0.6f * input;
    }

    static constexpr float baseMs[maxLines] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.3f, 67.1f, 73.3f };
    float lineGain[maxLines] {};
    float fs = 48000.0f;
    int lineLength = 8192, lineMask = 8191, writeIndex = 0, activeLines = maxLines;
    std::vector<float> lines[maxLines];
    float dampState[maxLines] {};
    std::vector<float> allpassBuffer[2][numAllpasses];
    int allpassLength[2][numAllpasses] {};
    int allpassIndex[2][numAllpasses] {};
    float scaleTarget = 0.9f, scaleSmoothed = 0.9f, sizeCoef = 0.001f;
    float decayPerSample = -0.0001f, damp = 0.2f, mix = 0.3f;
};

// ---------------------------------------------------------------------------------------------
/** Stereo-linked safety limiter without lookahead. Exactly transparent below the threshold. */
class SafetyLimiter
{
public:
    void prepare (double sampleRate) noexcept
    {
        releaseCoef = onePoleCoef (0.12f, static_cast<float> (sampleRate));
        gain = 1.0f;
    }

    void reset() noexcept { gain = 1.0f; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        constexpr float threshold = 0.966f; // about -0.3 dBFS
        for (int i = 0; i < numSamples; ++i)
        {
            const float peak = std::max (std::abs (left[i]), std::abs (right[i]));
            if (gain < 1.0f)
                gain = std::min (1.0f, gain + releaseCoef * (1.0f - gain) + 1.0e-7f);
            if (peak * gain > threshold)
                gain = threshold / peak;

            if (gain < 1.0f)
            {
                left[i] = std::clamp (left[i] * gain, -1.0f, 1.0f);
                right[i] = std::clamp (right[i] * gain, -1.0f, 1.0f);
            }
        }
    }

    bool isReducing() const noexcept { return gain < 0.999f; }

private:
    float gain = 1.0f, releaseCoef = 0.001f;
};

// ---------------------------------------------------------------------------------------------
/** First-order DC blocker (about 10 Hz). */
class DcBlocker
{
public:
    void prepare (double sampleRate) noexcept
    {
        r = 1.0f - kTwoPi * 10.0f / static_cast<float> (sampleRate);
        reset();
    }

    void reset() noexcept { x1[0] = x1[1] = y1[0] = y1[1] = 0.0f; }

    float process (int ch, float x) noexcept
    {
        const float y = x - x1[ch] + r * y1[ch];
        x1[ch] = x;
        y1[ch] = y;
        return y;
    }

private:
    float r = 0.999f;
    float x1[2] {}, y1[2] {};
};

} // namespace grn
