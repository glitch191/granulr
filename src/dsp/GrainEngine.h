// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The granular engine: live circular buffer with freeze, grain scheduler (free, synced,
// pattern), grain pool, feedback loop, built-in effects, ducking and safety limiter.
// process() is real-time safe: no allocation, no lock, no system call.
#pragma once

#include "Analysis.h"
#include "EngineParams.h"
#include "Effects.h"
#include "Pitch.h"
#include "Util.h"
#include <array>
#include <atomic>
#include <vector>

namespace grn
{

class GrainEngine
{
public:
    static constexpr int maxSubBlock = 32;
    static constexpr int poolSize = 320;
    static constexpr int tableSize = 1024;
    static constexpr int overviewSize = 1024;
    static constexpr float bufferSeconds = 8.0f;   // addressable delay
    static constexpr float maxSpanSeconds = 4.0f;  // longest stretch of buffer one grain may read

    /** What the editor needs to draw one grain. */
    struct GrainView
    {
        float delayNorm;  // 0 = write head, 1 = oldest addressable audio
        float pitch;      // semitones
        float pan;        // 0..1
        float level;      // current window level 0..1
        float spanNorm;   // length of buffer read by the grain, relative to the buffer
        float depth;      // 0 = close, 1 = far
    };

    GrainEngine();

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Processes up to maxSubBlock samples. Input and output may alias. */
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const EngineParams& params) noexcept;

    /** Bypassed path: keeps recording the input (unless frozen) but plays no grain. Very cheap. */
    void recordOnly (const float* inL, const float* inR, int numSamples) noexcept;

    int getNumActiveGrains() const noexcept { return numActive; }
    bool isIdle() const noexcept { return idle; }
    bool isFrozen() const noexcept { return frozen; }
    bool isLimiting() const noexcept { return limiter.isReducing(); }
    int getCurrentStep() const noexcept { return currentStep; }
    static int maxGrainsForQuality (int quality) noexcept;
    /** Grain limit in use. The cost grows with grains x sample rate, so above 72 kHz the limit shrinks
        in proportion and the worst case stays within the CPU budget at every rate. */
    int maxGrains (int quality) const noexcept
    {
        const double scale = std::min (1.0, 72000.0 / fs);
        return std::max (16, static_cast<int> (maxGrainsForQuality (quality) * scale));
    }

    /** Audio thread only. Returns the number of views written. */
    int fillGrainViews (GrainView* destination, int maxViews) const noexcept;

    /** Peak overview of the buffer for the editor. Lock-free, slightly racy by design. */
    float getOverviewPeak (int binsBehindWriteHead) const noexcept;

    /** Pitch quantisation applied to a grain, exposed for tests. */
    float resolveSemitones (const EngineParams& params, int64_t centreFrame) noexcept;

    PitchTracker& getPitchTracker() noexcept { return tracker; }

    /** Input analysis of the last sub-block, for the modulation sources. */
    AnalysisValues getAnalysisValues() const noexcept { return analysisValues; }

private:
    struct Grain
    {
        uint64_t phase = 0;   // 32.32 fixed point frame position
        int64_t inc = 0;      // 32.32 increment, negative for reverse
        float winPos = 0.0f, winInc = 0.0f, winIncB = 0.0f;
        int toPeak = 0, remaining = 0, total = 0, delay = 0;
        float gainL = 0.0f, gainR = 0.0f;
        float fade = 1.0f, fadeStep = 0.0f;
        const float* table = nullptr;
        uint32_t serial = 0;
        float pitch = 0.0f, pan = 0.5f, span = 0.0f;
        // Space per grain (only used when spatial is set)
        bool spatial = false;
        float depth = 0.0f, lpCoef = 1.0f, lpL = 0.0f, lpR = 0.0f;
        float gainStepL = 0.0f, gainStepR = 0.0f;
        int64_t incStep = 0;
    };

    struct Smoothed
    {
        float value = 0.0f;
        void snap (float target) noexcept { value = target; }
        /** Moves towards target and returns the per-sample step for this sub-block. */
        float advance (float target, float coef, int numSamples, float& start) noexcept
        {
            start = value;
            float next = value + coef * (target - value);
            if (std::abs (target - next) < 1.0e-5f)
                next = target;
            value = next;
            return (next - start) / static_cast<float> (numSamples);
        }
    };

    void spawnGrain (const EngineParams& params, int startDelay, float sprayRandom, float rate) noexcept;
    void stealOldest() noexcept;
    void killAllGrains() noexcept;
    template <bool useHermite, bool spatial>
    void renderGrain (Grain& grain, float* wetL, float* wetR, int numSamples) noexcept;
    void renderGrains (float* wetL, float* wetR, int numSamples, bool useHermite) noexcept;
    void schedule (const EngineParams& params, int numSamples) noexcept;
    void enterFreeze() noexcept;
    void evolveBuffer (const EngineParams& params, const float* wetL, const float* wetR, int numSamples) noexcept;
    float flocked (float& state, float uniform, float amount) noexcept;
    void applyEffects (const EngineParams& params, int place, float* left, float* right, int numSamples) noexcept;
    void updateEffectParams (const EngineParams& params) noexcept;
    double positionFrames (const EngineParams& params) const noexcept;
    double sizeFrames (const EngineParams& params) const noexcept;

    // Window tables, shared by every instance.
    static const std::array<std::array<float, tableSize + 2>, numWindowShapes>& windowTables();

    double fs = 48000.0;
    int bufferLength = 0;
    uint64_t mask = 0;
    std::vector<float> buffer; // interleaved stereo
    int64_t writePos = 0;
    int maxDelayFrames = 0, maxSpanFrames = 0, fadeFrames = 0, guardFrames = 0;
    bool frozen = false;
    float writeFade = 1.0f, writeFadeStep = 0.0f;

    std::array<Grain, poolSize> grains;
    std::array<int, poolSize> activeList {};
    std::array<int, poolSize> freeList {};
    int numActive = 0, numFree = 0;
    uint32_t nextSerial = 1;

    Rng rng;
    PitchTracker tracker;
    Rng spaceRng;                 // separate generator: new features never shift the main sequence
    InputAnalysis analysis;
    AnalysisValues analysisValues;
    float flockSpray = 0.5f, flockPitch = 0.5f, flockPan = 0.5f;
    float flockAmount = 0.0f;     // valid during spawnGrain

    // Onset trigger
    int burstRemaining = 0;
    double nextBurst = 0.0;

    // Living buffer
    int64_t evolveOffset = 0;
    float wearLp[2] { 0.0f, 0.0f };
    float evolvePeak = 0.0f, evolveEnvOld = 0.0f, evolveEnvWet = 0.0f;
    int evolveBin = -1;

    // Scheduler state
    double nextSpawn = 0.0;
    double internalPpq = 0.0;
    int64_t lastSyncIndex = INT64_MIN, lastStepIndex = INT64_MIN;
    bool wasPlaying = false;
    int currentStep = 0;
    double scanOffset = 0.0;

    // Gains
    Smoothed inGain, outGain, dryGain, wetGain, feedbackGain, widthGain;
    bool firstBlock = true;
    float duckEnv = 0.0f;

    // Effects
    StateVariableFilter filter;
    Saturator saturator;
    Diffuser diffuser;
    Resonator resonator;
    SafetyLimiter limiter;
    DcBlocker dcBlocker;
    bool filterWasOn = false, satWasOn = false, diffWasOn = false, resWasOn = false;

    // Idle detection
    int64_t silentWriteFrames = 0, silentWetFrames = 0;
    bool idle = false;

    // Overview for the editor
    std::array<std::atomic<float>, overviewSize> overview;
    std::atomic<int> overviewIndex { 0 };
    float overviewPeak = 0.0f;
    int overviewCount = 0, framesPerOverviewBin = 1;
};

} // namespace grn
