// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lock-free data path from the audio thread to the editor. Nothing is published while the
// editor is closed, so a closed editor costs nothing.
#pragma once

#include "../dsp/GrainEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>

namespace grn
{

/** Single-producer, single-consumer triple buffer: the reader always gets the latest frame. */
template <typename Type>
class TripleBuffer
{
public:
    Type& writeBuffer() noexcept { return buffers[static_cast<size_t> (back)]; }

    void publish() noexcept { back = middle.exchange (back | dirtyBit) & indexMask; }

    /** Returns true when a new frame became available. */
    bool update() noexcept
    {
        if ((middle.load() & dirtyBit) == 0)
            return false;
        front = middle.exchange (front) & indexMask;
        return true;
    }

    const Type& read() const noexcept { return buffers[static_cast<size_t> (front)]; }

private:
    static constexpr int dirtyBit = 4, indexMask = 3;
    std::array<Type, 3> buffers {};
    std::atomic<int> middle { 1 };
    int back = 0, front = 2;
};

struct GrainFrame
{
    static constexpr int maxViews = 256;
    int count = 0;
    bool frozen = false;
    std::array<GrainEngine::GrainView, maxViews> views {};
};

struct GuiBridge
{
    static constexpr int fifoSize = 1 << 16;

    GuiBridge() : fifo (fifoSize), fifoL (fifoSize, 0.0f), fifoR (fifoSize, 0.0f)
    {
        for (auto& value : destNorm)
            value.store (0.0f);
        for (auto& value : sourceValue)
            value.store (0.0f);
    }

    // Audio thread ----------------------------------------------------------------------------
    void pushOutput (const float* left, const float* right, int numSamples) noexcept
    {
        const auto scope = fifo.write (numSamples);
        if (scope.blockSize1 > 0)
        {
            std::copy_n (left, scope.blockSize1, fifoL.data() + scope.startIndex1);
            std::copy_n (right, scope.blockSize1, fifoR.data() + scope.startIndex1);
        }
        if (scope.blockSize2 > 0)
        {
            std::copy_n (left + scope.blockSize1, scope.blockSize2, fifoL.data() + scope.startIndex2);
            std::copy_n (right + scope.blockSize1, scope.blockSize2, fifoR.data() + scope.startIndex2);
        }
    }

    static void holdPeak (std::atomic<float>& target, float value) noexcept
    {
        if (value > target.load (std::memory_order_relaxed))
            target.store (value, std::memory_order_relaxed);
    }

    // Editor thread ---------------------------------------------------------------------------
    int popOutput (float* left, float* right, int maxSamples) noexcept
    {
        const auto scope = fifo.read (std::min (maxSamples, fifo.getNumReady()));
        if (scope.blockSize1 > 0)
        {
            std::copy_n (fifoL.data() + scope.startIndex1, scope.blockSize1, left);
            std::copy_n (fifoR.data() + scope.startIndex1, scope.blockSize1, right);
        }
        if (scope.blockSize2 > 0)
        {
            std::copy_n (fifoL.data() + scope.startIndex2, scope.blockSize2, left + scope.blockSize1);
            std::copy_n (fifoR.data() + scope.startIndex2, scope.blockSize2, right + scope.blockSize1);
        }
        return scope.blockSize1 + scope.blockSize2;
    }

    std::atomic<bool> editorOpen { false };
    std::atomic<double> sampleRate { 48000.0 };

    juce::AbstractFifo fifo;
    std::vector<float> fifoL, fifoR;

    // Peak holds: the audio thread raises them, the editor reads and clears them.
    std::atomic<float> inputPeak[2] { 0.0f, 0.0f };
    std::atomic<float> outputPeak[2] { 0.0f, 0.0f };

    std::atomic<float> cpuAverage { 0.0f }; // 0..1 of one core
    std::atomic<float> cpuPeak { 0.0f };
    std::atomic<int> activeGrains { 0 };
    std::atomic<int> currentStep { 0 };
    std::atomic<bool> limiting { false };
    std::atomic<bool> idle { false };
    std::atomic<bool> hostPlaying { false };
    std::atomic<float> detectedNote { -1.0f }; // MIDI note, negative when unvoiced

    std::array<std::atomic<float>, numModDests> destNorm;      // modulated value, normalised
    std::atomic<uint64_t> destActiveMask { 0 };                // bit d set when destination d is modulated
    std::array<std::atomic<float>, numModSources> sourceValue; // current source outputs

    TripleBuffer<GrainFrame> grains;
};

} // namespace grn
