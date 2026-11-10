// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "GrainEngine.h"
#include <cstring>

namespace grn
{
namespace
{
    constexpr float kSilence = 1.0e-7f;
    constexpr double kFixedOne = 4294967296.0; // 2^32
    constexpr float kFracScale = 1.0f / 4294967296.0f;

    inline float hermite (float y0, float y1, float y2, float y3, float t) noexcept
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
}

const std::array<std::array<float, GrainEngine::tableSize + 2>, numWindowShapes>& GrainEngine::windowTables()
{
    static const auto tables = []
    {
        std::array<std::array<float, tableSize + 2>, numWindowShapes> t {};
        for (int i = 0; i < tableSize + 2; ++i)
        {
            const float x = std::min (1.0f, static_cast<float> (i) / static_cast<float> (tableSize));

            t[windowHann][static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (kTwoPi * x);

            float tukey = 1.0f;
            if (x < 0.25f)
                tukey = 0.5f - 0.5f * std::cos (kPi * x / 0.25f);
            else if (x > 0.75f)
                tukey = 0.5f - 0.5f * std::cos (kPi * (1.0f - x) / 0.25f);
            t[windowTukey][static_cast<size_t> (i)] = tukey;

            t[windowTrapezoid][static_cast<size_t> (i)] = std::clamp (std::min (x, 1.0f - x) / 0.2f, 0.0f, 1.0f);

            float perc;
            if (x < 0.5f)
                perc = std::sin (kPi * x);
            else
            {
                const float d = (x - 0.5f) * 2.0f;
                perc = std::exp (-4.0f * d) * (1.0f - d);
            }
            t[windowPerc][static_cast<size_t> (i)] = perc;
        }
        return t;
    }();
    return tables;
}

GrainEngine::GrainEngine()
{
    windowTables();
    for (auto& bin : overview)
        bin.store (0.0f, std::memory_order_relaxed);
}

int GrainEngine::maxGrainsForQuality (int quality) noexcept
{
    return quality == qualityEco ? 64 : (quality == qualityHigh ? 256 : 128);
}

void GrainEngine::prepare (double sampleRate)
{
    fs = sampleRate;
    maxDelayFrames = static_cast<int> (bufferSeconds * fs);
    maxSpanFrames = static_cast<int> (maxSpanSeconds * fs);
    fadeFrames = std::max (16, static_cast<int> (0.010 * fs));
    guardFrames = static_cast<int> (0.05 * fs);
    bufferLength = nextPowerOfTwo (maxDelayFrames + maxSpanFrames + guardFrames + 4 * maxSubBlock);
    mask = static_cast<uint64_t> (bufferLength - 1);
    buffer.assign (static_cast<size_t> (bufferLength) * 2, 0.0f);
    framesPerOverviewBin = std::max (1, maxDelayFrames / overviewSize);

    tracker.prepare (fs);
    filter.prepare (fs);
    diffuser.prepare (fs);
    limiter.prepare (fs);
    dcBlocker.prepare (fs);
    reset();
}

void GrainEngine::reset() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
    frozen = false;
    writeFade = 1.0f;
    writeFadeStep = 0.0f;

    numActive = 0;
    numFree = poolSize;
    for (int i = 0; i < poolSize; ++i)
        freeList[static_cast<size_t> (i)] = poolSize - 1 - i;

    nextSpawn = 0.0;
    internalPpq = 0.0;
    lastSyncIndex = lastStepIndex = INT64_MIN;
    wasPlaying = false;
    currentStep = 0;
    scanOffset = 0.0;
    firstBlock = true;
    duckEnv = 0.0f;
    inGain.snap (1.0f);

    tracker.reset();
    filter.reset();
    saturator.reset();
    diffuser.reset();
    limiter.reset();
    dcBlocker.reset();
    filterWasOn = satWasOn = diffWasOn = false;

    silentWriteFrames = bufferLength;
    silentWetFrames = 0;
    idle = false;

    for (auto& bin : overview)
        bin.store (0.0f, std::memory_order_relaxed);
    overviewIndex.store (0, std::memory_order_relaxed);
    overviewPeak = 0.0f;
    overviewCount = 0;
}

// ---------------------------------------------------------------------------------------------
double GrainEngine::positionFrames (const EngineParams& p) const noexcept
{
    double frames;
    if (p.positionSync)
        frames = divisionBeats[static_cast<size_t> (std::clamp (p.positionDiv, 0, numDivisions - 1))] * 60.0 / std::max (20.0, p.bpm) * fs;
    else
        frames = static_cast<double> (p.positionMs) * 0.001 * fs;
    return std::clamp (frames, 0.0, static_cast<double> (maxDelayFrames));
}

double GrainEngine::sizeFrames (const EngineParams& p) const noexcept
{
    double frames;
    if (p.sizeSync)
        frames = divisionBeats[static_cast<size_t> (std::clamp (p.sizeDiv, 0, numDivisions - 1))] * 60.0 / std::max (20.0, p.bpm) * fs;
    else
        frames = static_cast<double> (p.sizeMs) * 0.001 * fs;
    return std::clamp (frames, std::max (16.0, 0.001 * fs), 2.0 * fs);
}

float GrainEngine::resolveSemitones (const EngineParams& p, int64_t centreFrame) noexcept
{
    float semitones = p.pitch + p.pitchRandom * rng.bi();

    float totalWeight = 0.0f;
    for (float weight : p.voiceProb)
        totalWeight += std::max (0.0f, weight);
    if (totalWeight > 1.0e-4f)
    {
        float pick = rng.uni() * totalWeight;
        for (int v = 0; v < numHarmonyVoices; ++v)
        {
            pick -= std::max (0.0f, p.voiceProb[v]);
            if (pick < 0.0f || v == numHarmonyVoices - 1)
            {
                semitones += p.voiceInterval[v];
                break;
            }
        }
    }

    if (p.octaveUp > 0.0f && rng.uni() < p.octaveUp)
        semitones += 12.0f;
    if (p.octaveDown > 0.0f && rng.uni() < p.octaveDown)
        semitones -= 12.0f;

    const uint16_t allowed = scaleMask (p.scale, p.customMask);
    if (allowed != 0)
        semitones = quantiseTransposition (semitones, tracker.noteAt (centreFrame), allowed, p.root);

    return std::clamp (semitones, -48.0f, 48.0f);
}

void GrainEngine::stealOldest() noexcept
{
    int oldest = -1;
    uint32_t oldestAge = 0;
    for (int a = 0; a < numActive; ++a)
    {
        const auto& g = grains[static_cast<size_t> (activeList[static_cast<size_t> (a)])];
        if (g.fadeStep < 0.0f)
            continue;
        const uint32_t age = nextSerial - g.serial;
        if (age >= oldestAge)
        {
            oldestAge = age;
            oldest = activeList[static_cast<size_t> (a)];
        }
    }
    if (oldest >= 0)
        grains[static_cast<size_t> (oldest)].fadeStep = -1.0f / static_cast<float> (0.002 * fs);
}

void GrainEngine::killAllGrains() noexcept
{
    numActive = 0;
    numFree = poolSize;
    for (int i = 0; i < poolSize; ++i)
        freeList[static_cast<size_t> (i)] = poolSize - 1 - i;
}

void GrainEngine::spawnGrain (const EngineParams& p, int startDelay, float sprayRandom, float rate) noexcept
{
    int fading = 0;
    for (int a = 0; a < numActive; ++a)
        if (grains[static_cast<size_t> (activeList[static_cast<size_t> (a)])].fadeStep < 0.0f)
            ++fading;
    if (numActive - fading >= maxGrains (p.quality))
        stealOldest();
    if (numFree == 0)
        return;

    double duration = sizeFrames (p);
    const bool reversed = p.reverse > 0.0f && rng.uni() < p.reverse;

    // Delay behind the write head where the grain starts reading.
    double delay = positionFrames (p) + static_cast<double> (sprayRandom) * p.sprayMs * 0.001 * fs + scanOffset;
    const double wrap = static_cast<double> (maxDelayFrames);
    if (delay >= wrap || delay < 0.0)
        delay -= wrap * std::floor (delay / wrap);

    const double direction = reversed ? -1.0 : 1.0;
    const auto centreFrame = static_cast<int64_t> (static_cast<double> (writePos) - delay + direction * 0.5 * duration);
    const float semitones = resolveSemitones (p, centreFrame);
    const double ratio = static_cast<double> (semitonesToRatio (semitones));

    double span = ratio * duration;
    if (span > static_cast<double> (maxSpanFrames))
    {
        span = static_cast<double> (maxSpanFrames);
        duration = span / ratio;
    }

    // Keep the read pointer on valid audio: never past the write head, never past the oldest data.
    const double margin = static_cast<double> (maxSubBlock + 4);
    double minDelay = margin;
    if (! reversed)
        minDelay += frozen ? span : std::max (0.0, (ratio - 1.0) * duration);
    delay = std::clamp (delay, minDelay, std::max (minDelay, wrap));

    const int index = freeList[static_cast<size_t> (--numFree)];
    activeList[static_cast<size_t> (numActive++)] = index;
    auto& g = grains[static_cast<size_t> (index)];

    const double startFrame = static_cast<double> (writePos) - delay;
    const double startFloor = std::floor (startFrame);
    const auto startIndex = static_cast<int64_t> (startFloor);
    g.phase = ((static_cast<uint64_t> (startIndex) & mask) << 32)
              | static_cast<uint64_t> (static_cast<uint32_t> ((startFrame - startFloor) * (kFixedOne - 1.0)));
    g.inc = static_cast<int64_t> (ratio * kFixedOne) * (reversed ? -1 : 1);

    const int total = std::max (8, static_cast<int> (duration));
    const float peakBase = p.windowShape == windowPerc ? 0.03f : 0.5f;
    const float peakPos = std::clamp (peakBase + p.windowSkew * 0.48f, 0.01f, 0.99f);
    g.toPeak = std::clamp (static_cast<int> (peakPos * static_cast<float> (total)), 1, total - 1);
    g.total = total;
    g.remaining = total;
    g.delay = std::max (0, startDelay);
    g.winPos = 0.0f;
    g.winInc = (0.5f * tableSize) / static_cast<float> (g.toPeak);
    g.winIncB = (0.5f * tableSize) / static_cast<float> (total - g.toPeak);
    g.table = windowTables()[static_cast<size_t> (std::clamp (p.windowShape, 0, numWindowShapes - 1))].data();

    // Overlap compensation keeps the level steady when density or size change.
    const float overlap = rate * static_cast<float> (duration / fs);
    const float amplitude = 1.0f / std::sqrt (std::max (1.0f, 0.4f * overlap));
    const float pan = std::clamp (0.5f + 0.5f * p.panSpread * rng.bi(), 0.0f, 1.0f);
    g.gainL = amplitude * std::min (1.0f, 1.41421356f * std::cos (pan * 0.5f * kPi));
    g.gainR = amplitude * std::min (1.0f, 1.41421356f * std::sin (pan * 0.5f * kPi));
    g.fade = 1.0f;
    g.fadeStep = 0.0f;
    g.serial = nextSerial++;
    g.pitch = semitones;
    g.pan = pan;
    g.span = static_cast<float> (span);
}

// ---------------------------------------------------------------------------------------------
template <bool useHermite>
void GrainEngine::renderGrain (Grain& g, float* wetL, float* wetR, int numSamples) noexcept
{
    int i = 0;
    if (g.delay > 0)
    {
        const int skipped = std::min (g.delay, numSamples);
        g.delay -= skipped;
        i = skipped;
    }

    const float* data = buffer.data();
    const uint64_t m = mask;

    while (i < numSamples && g.remaining > 0)
    {
        int count = std::min (numSamples - i, g.remaining);
        const bool rising = g.toPeak > 0;
        if (rising)
            count = std::min (count, g.toPeak);
        if (g.fadeStep < 0.0f)
        {
            const int untilSilent = static_cast<int> (std::ceil (g.fade / -g.fadeStep));
            if (untilSilent <= 0)
            {
                g.remaining = 0;
                break;
            }
            count = std::min (count, untilSilent);
        }

        uint64_t phase = g.phase;
        const uint64_t inc = static_cast<uint64_t> (g.inc);
        float winPos = g.winPos;
        const float winInc = rising ? g.winInc : g.winIncB;
        float fade = g.fade;
        const float fadeStep = g.fadeStep;
        const float* table = g.table;
        const float gainL = g.gainL, gainR = g.gainR;
        float* outL = wetL + i;
        float* outR = wetR + i;

        for (int s = 0; s < count; ++s)
        {
            const uint64_t idx = phase >> 32;
            const float t = static_cast<float> (static_cast<uint32_t> (phase)) * kFracScale;
            float sampleL, sampleR;
            if constexpr (useHermite)
            {
                const float* p0 = data + (((idx - 1) & m) << 1);
                const float* p1 = data + ((idx & m) << 1);
                const float* p2 = data + (((idx + 1) & m) << 1);
                const float* p3 = data + (((idx + 2) & m) << 1);
                sampleL = hermite (p0[0], p1[0], p2[0], p3[0], t);
                sampleR = hermite (p0[1], p1[1], p2[1], p3[1], t);
            }
            else
            {
                const float* p1 = data + ((idx & m) << 1);
                const float* p2 = data + (((idx + 1) & m) << 1);
                sampleL = p1[0] + t * (p2[0] - p1[0]);
                sampleR = p1[1] + t * (p2[1] - p1[1]);
            }

            const int wi = static_cast<int> (winPos);
            const float wf = winPos - static_cast<float> (wi);
            const float window = (table[wi] + wf * (table[wi + 1] - table[wi])) * fade;
            outL[s] += sampleL * window * gainL;
            outR[s] += sampleR * window * gainR;

            phase += inc;
            winPos += winInc;
            fade += fadeStep;
        }

        g.phase = phase;
        g.winPos = std::min (winPos, static_cast<float> (tableSize));
        g.fade = fade;
        g.remaining -= count;
        if (rising)
        {
            g.toPeak -= count;
            if (g.toPeak == 0)
                g.winPos = 0.5f * tableSize;
        }
        if (fadeStep < 0.0f && fade <= 0.0f)
            g.remaining = 0;
        i += count;
    }
}

void GrainEngine::renderGrains (float* wetL, float* wetR, int numSamples, bool useHermite) noexcept
{
    for (int a = 0; a < numActive;)
    {
        const int index = activeList[static_cast<size_t> (a)];
        auto& g = grains[static_cast<size_t> (index)];
        if (useHermite)
            renderGrain<true> (g, wetL, wetR, numSamples);
        else
            renderGrain<false> (g, wetL, wetR, numSamples);

        if (g.remaining <= 0)
        {
            activeList[static_cast<size_t> (a)] = activeList[static_cast<size_t> (--numActive)];
            freeList[static_cast<size_t> (numFree++)] = index;
        }
        else
        {
            ++a;
        }
    }
}

// ---------------------------------------------------------------------------------------------
void GrainEngine::schedule (const EngineParams& p, int n) noexcept
{
    const double bpm = std::clamp (p.bpm, 20.0, 999.0);
    const double beatsPerSample = bpm / (60.0 * fs);

    if (p.playing != wasPlaying)
    {
        wasPlaying = p.playing;
        lastSyncIndex = lastStepIndex = INT64_MIN;
    }

    const double ppq0 = p.playing ? p.ppq : internalPpq;
    const double ppq1 = ppq0 + static_cast<double> (n) * beatsPerSample;
    internalPpq = ppq1;

    auto offsetOf = [&] (double boundaryPpq)
    {
        return std::clamp (static_cast<int> ((boundaryPpq - ppq0) / beatsPerSample), 0, n - 1);
    };

    if (p.triggerMode == triggerFree)
    {
        const double interval = fs / std::max (0.1, static_cast<double> (p.density));
        int guard = 0;
        while (nextSpawn < static_cast<double> (n) && guard++ < 16)
        {
            spawnGrain (p, std::max (0, static_cast<int> (nextSpawn)), rng.uni(), p.density);
            nextSpawn += std::max (1.0, interval * (1.0 + 0.95 * static_cast<double> (p.jitter * rng.bi())));
        }
        nextSpawn = std::max (0.0, nextSpawn - static_cast<double> (n));
        nextSpawn = std::min (nextSpawn, interval * 2.0);
    }
    else if (p.triggerMode == triggerSync)
    {
        const double length = divisionBeats[static_cast<size_t> (std::clamp (p.densityDiv, 0, numDivisions - 1))];
        const auto index = static_cast<int64_t> (std::floor (ppq1 / length));
        if (index != lastSyncIndex)
        {
            lastSyncIndex = index;
            const double intervalSamples = length / beatsPerSample;
            const int late = static_cast<int> (p.jitter * rng.uni() * 0.5 * intervalSamples);
            spawnGrain (p, offsetOf (static_cast<double> (index) * length) + late, rng.uni(),
                        static_cast<float> (fs / intervalSamples));
        }
        nextSpawn = 0.0;
    }
    else
    {
        const double length = divisionBeats[static_cast<size_t> (std::clamp (p.patternRate, 0, numDivisions - 1))];
        const auto index = static_cast<int64_t> (std::floor (ppq1 / length));
        if (index != lastStepIndex)
        {
            lastStepIndex = index;
            const int steps = std::clamp (p.patternLength, 1, numPatternSteps);
            const int step = static_cast<int> (((index % steps) + steps) % steps);
            currentStep = step;
            const bool gateOpen = ((p.gateMask >> step) & 1u) != 0;
            if (gateOpen && rng.uni() < p.stepProb[step])
            {
                const double stepSamples = length / beatsPerSample;
                const int hits = std::clamp (static_cast<int> (p.stepRatchet[step]), 1, 4);
                const int offset = offsetOf (static_cast<double> (index) * length);
                const float sprayRandom = rng.uni();
                const float rate = static_cast<float> (fs * hits / stepSamples);
                for (int h = 0; h < hits; ++h)
                {
                    const int late = static_cast<int> (p.jitter * rng.uni() * 0.25 * stepSamples / hits);
                    spawnGrain (p, offset + static_cast<int> (stepSamples * h / hits) + late, sprayRandom, rate);
                }
            }
        }
        nextSpawn = 0.0;
    }
}

void GrainEngine::enterFreeze() noexcept
{
    frozen = true;

    // Fade the newest audio to zero and clear a guard zone after the write head, so a grain
    // that reaches the seam reads a smooth fade into silence instead of stale audio.
    for (int k = 0; k < fadeFrames; ++k)
    {
        const auto idx = static_cast<size_t> ((static_cast<uint64_t> (writePos - 1 - k) & mask) << 1);
        const float gain = static_cast<float> (k) / static_cast<float> (fadeFrames);
        buffer[idx] *= gain;
        buffer[idx + 1] *= gain;
    }
    for (int k = 0; k < guardFrames; ++k)
    {
        const auto idx = static_cast<size_t> ((static_cast<uint64_t> (writePos + k) & mask) << 1);
        buffer[idx] = 0.0f;
        buffer[idx + 1] = 0.0f;
    }

    // Forward grains that would run past the write head are faded out quickly.
    const float stealStep = -1.0f / static_cast<float> (0.002 * fs);
    for (int a = 0; a < numActive; ++a)
    {
        auto& g = grains[static_cast<size_t> (activeList[static_cast<size_t> (a)])];
        if (g.inc <= 0 || g.fadeStep < 0.0f)
            continue;
        const uint64_t behind = (static_cast<uint64_t> (writePos) - (g.phase >> 32)) & mask;
        const uint64_t needed = ((static_cast<uint64_t> (g.inc) >> 16) * static_cast<uint64_t> (g.remaining) >> 16) + 4;
        if (needed >= behind)
            g.fadeStep = stealStep;
    }
}

void GrainEngine::updateEffectParams (const EngineParams& p) noexcept
{
    if (p.filterOn)
    {
        if (! filterWasOn)
            filter.reset();
        filter.setParams (p.filterCutoff, p.filterReso, p.filterType);
    }
    filterWasOn = p.filterOn;

    if (p.satOn)
    {
        saturator.setParams (p.satDriveDb, p.satType);
        if (! satWasOn)
            saturator.reset();
    }
    satWasOn = p.satOn;

    if (p.diffOn)
    {
        diffuser.setParams (p.diffSize, p.diffDecay, p.diffDamp, p.diffMix,
                            p.quality == qualityEco ? 4 : Diffuser::maxLines);
        if (! diffWasOn)
            diffuser.reset();
    }
    diffWasOn = p.diffOn;
}

void GrainEngine::applyEffects (const EngineParams& p, int place, float* left, float* right, int n) noexcept
{
    if (p.filterOn && p.filterPlace == place)
        filter.process (left, right, n);
    if (p.satOn && p.satPlace == place)
        saturator.process (left, right, n);
    if (p.diffOn && p.diffPlace == place)
        diffuser.process (left, right, n);
}

// ---------------------------------------------------------------------------------------------
void GrainEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                           int numSamples, const EngineParams& p) noexcept
{
    const int n = std::min (numSamples, maxSubBlock);
    if (n <= 0)
        return;

    const auto fsF = static_cast<float> (fs);
    const float coef = 1.0f - std::exp (-static_cast<float> (n) / (0.012f * fsF));
    const float mixAngle = std::clamp (p.mix, 0.0f, 1.0f) * 0.5f * kPi;
    const float dryTarget = std::cos (mixAngle);
    const float wetTarget = std::sin (mixAngle);

    if (firstBlock)
    {
        firstBlock = false;
        inGain.snap (p.inputGain);
        outGain.snap (p.outputGain);
        dryGain.snap (dryTarget);
        wetGain.snap (wetTarget);
        feedbackGain.snap (p.feedback);
        widthGain.snap (p.width);
    }

    float x[2][maxSubBlock];
    float wet[2][maxSubBlock];
    float duck[maxSubBlock];

    // Input gain, ducking detector.
    float start = 0.0f;
    float step = inGain.advance (p.inputGain, coef, n, start);
    const float attack = onePoleCoef (p.duckAttackMs * 0.001f, fsF);
    const float release = onePoleCoef (p.duckReleaseMs * 0.001f, fsF);
    float inputPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = start + step * static_cast<float> (i + 1);
        x[0][i] = inL[i] * g;
        x[1][i] = inR[i] * g;
        const float level = std::max (std::abs (x[0][i]), std::abs (x[1][i]));
        inputPeak = std::max (inputPeak, level);
        duckEnv += (level > duckEnv ? attack : release) * (level - duckEnv);
        duck[i] = 1.0f - p.duckAmount * std::min (1.0f, duckEnv * 4.0f);
    }

    // Freeze transitions.
    if (p.freeze && ! frozen)
    {
        enterFreeze();
    }
    else if (! p.freeze && frozen)
    {
        frozen = false;
        writeFade = 0.0f;
        writeFadeStep = 1.0f / static_cast<float> (fadeFrames);
    }

    // Scan: the spawn point travels through the buffer.
    if (std::abs (p.scan) > 1.0e-3f)
    {
        scanOffset -= static_cast<double> (p.scan) * n;
        const double wrap = static_cast<double> (maxDelayFrames);
        if (scanOffset >= wrap || scanOffset < 0.0)
            scanOffset -= wrap * std::floor (scanOffset / wrap);
    }
    else
    {
        scanOffset = 0.0;
    }

    if (idle && ! frozen && inputPeak >= kSilence)
        idle = false;

    std::memset (wet[0], 0, sizeof (float) * static_cast<size_t> (n));
    std::memset (wet[1], 0, sizeof (float) * static_cast<size_t> (n));

    if (! idle)
    {
        const bool bufferSilent = silentWriteFrames >= bufferLength && (frozen || inputPeak < kSilence);
        if (bufferSilent)
        {
            if (numActive > 0)
                killAllGrains();
        }
        else
        {
            schedule (p, n);
            renderGrains (wet[0], wet[1], n, p.quality != qualityEco);
        }

        // Stereo width of the grain cloud.
        step = widthGain.advance (p.width, coef, n, start);
        if (start != 1.0f || step != 0.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                const float w = start + step * static_cast<float> (i + 1);
                const float mid = 0.5f * (wet[0][i] + wet[1][i]);
                const float side = 0.5f * (wet[0][i] - wet[1][i]) * w;
                wet[0][i] = mid + side;
                wet[1][i] = mid - side;
            }
        }

        updateEffectParams (p);
        applyEffects (p, placeFeedback, wet[0], wet[1], n);

        // Write input plus feedback into the buffer.
        step = feedbackGain.advance (p.feedback, coef, n, start);
        if (! frozen)
        {
            const bool hasFeedback = start > 0.0f || step != 0.0f;
            float mono[maxSubBlock];
            float writePeak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                float wl = x[0][i] * writeFade;
                float wr = x[1][i] * writeFade;
                if (writeFade < 1.0f)
                    writeFade = std::min (1.0f, writeFade + writeFadeStep);
                if (hasFeedback)
                {
                    const float fb = start + step * static_cast<float> (i + 1);
                    wl += softClip (dcBlocker.process (0, wet[0][i] * fb));
                    wr += softClip (dcBlocker.process (1, wet[1][i] * fb));
                }
                const auto idx = static_cast<size_t> ((static_cast<uint64_t> (writePos + i) & mask) << 1);
                buffer[idx] = wl;
                buffer[idx + 1] = wr;
                mono[i] = 0.5f * (wl + wr);
                const float level = std::max (std::abs (wl), std::abs (wr));
                writePeak = std::max (writePeak, level);

                overviewPeak = std::max (overviewPeak, level);
                if (++overviewCount >= framesPerOverviewBin)
                {
                    const int bin = overviewIndex.load (std::memory_order_relaxed);
                    overview[static_cast<size_t> (bin)].store (overviewPeak, std::memory_order_relaxed);
                    overviewIndex.store ((bin + 1) % overviewSize, std::memory_order_relaxed);
                    overviewPeak = 0.0f;
                    overviewCount = 0;
                }
            }
            writePos += n;
            tracker.process (mono, n);
            silentWriteFrames = writePeak < kSilence ? std::min<int64_t> (silentWriteFrames + n, bufferLength) : 0;
        }

        applyEffects (p, placePost, wet[0], wet[1], n);

        float wetPeak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            wet[0][i] *= duck[i];
            wet[1][i] *= duck[i];
            wetPeak = std::max (wetPeak, std::max (std::abs (wet[0][i]), std::abs (wet[1][i])));
        }
        silentWetFrames = wetPeak < kSilence ? silentWetFrames + n : 0;

        if (silentWriteFrames >= bufferLength && silentWetFrames > static_cast<int64_t> (0.5 * fs)
            && (frozen || inputPeak < kSilence))
        {
            idle = true;
            killAllGrains();
        }
    }

    // Dry/wet mix, output gain, limiter.
    float dryStart = 0.0f, wetStart = 0.0f, outStart = 0.0f;
    const float dryStep = dryGain.advance (dryTarget, coef, n, dryStart);
    const float wetStep = wetGain.advance (wetTarget, coef, n, wetStart);
    const float outStep = outGain.advance (p.outputGain, coef, n, outStart);
    float check = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto k = static_cast<float> (i + 1);
        const float d = dryStart + dryStep * k;
        const float w = wetStart + wetStep * k;
        const float o = outStart + outStep * k;
        const float left = (x[0][i] * d + wet[0][i] * w) * o;
        const float right = (x[1][i] * d + wet[1][i] * w) * o;
        outL[i] = left;
        outR[i] = right;
        check += std::abs (left) + std::abs (right);
    }

    if (! (check < 1.0e6f))
    {
        // Something went non-finite or absurdly loud: recover instead of sending it out.
        reset();
        std::memset (outL, 0, sizeof (float) * static_cast<size_t> (n));
        std::memset (outR, 0, sizeof (float) * static_cast<size_t> (n));
        return;
    }

    if (p.limiter)
        limiter.process (outL, outR, n);
}

void GrainEngine::recordOnly (const float* inL, const float* inR, int numSamples) noexcept
{
    if (numActive > 0)
        killAllGrains();
    if (frozen)
        return;

    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min (maxSubBlock, numSamples - done);
        float mono[maxSubBlock];
        float writePeak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float wl = inL[done + i] * inGain.value;
            const float wr = inR[done + i] * inGain.value;
            const auto idx = static_cast<size_t> ((static_cast<uint64_t> (writePos + i) & mask) << 1);
            buffer[idx] = wl;
            buffer[idx + 1] = wr;
            mono[i] = 0.5f * (wl + wr);
            const float level = std::max (std::abs (wl), std::abs (wr));
            writePeak = std::max (writePeak, level);
            overviewPeak = std::max (overviewPeak, level);
            if (++overviewCount >= framesPerOverviewBin)
            {
                const int bin = overviewIndex.load (std::memory_order_relaxed);
                overview[static_cast<size_t> (bin)].store (overviewPeak, std::memory_order_relaxed);
                overviewIndex.store ((bin + 1) % overviewSize, std::memory_order_relaxed);
                overviewPeak = 0.0f;
                overviewCount = 0;
            }
        }
        writePos += n;
        tracker.process (mono, n);
        silentWriteFrames = writePeak < kSilence ? std::min<int64_t> (silentWriteFrames + n, bufferLength) : 0;
        done += n;
    }
    idle = false;
}

// ---------------------------------------------------------------------------------------------
int GrainEngine::fillGrainViews (GrainView* destination, int maxViews) const noexcept
{
    const int count = std::min (numActive, maxViews);
    const float invDelay = 1.0f / static_cast<float> (std::max (1, maxDelayFrames));
    for (int a = 0; a < count; ++a)
    {
        const auto& g = grains[static_cast<size_t> (activeList[static_cast<size_t> (a)])];
        const uint64_t behind = (static_cast<uint64_t> (writePos) - (g.phase >> 32)) & mask;
        auto& view = destination[a];
        view.delayNorm = static_cast<float> (behind) * invDelay;
        view.pitch = g.pitch;
        view.pan = g.pan;
        view.spanNorm = g.span * invDelay * (g.inc < 0 ? -1.0f : 1.0f);
        if (g.delay > 0 || g.table == nullptr)
            view.level = 0.0f;
        else
            view.level = g.table[std::clamp (static_cast<int> (g.winPos), 0, tableSize)] * std::max (0.0f, g.fade);
    }
    return count;
}

float GrainEngine::getOverviewPeak (int binsBehindWriteHead) const noexcept
{
    const int head = overviewIndex.load (std::memory_order_relaxed);
    const int bin = ((head - 1 - binsBehindWriteHead) % overviewSize + overviewSize) % overviewSize;
    return overview[static_cast<size_t> (bin)].load (std::memory_order_relaxed);
}

} // namespace grn
