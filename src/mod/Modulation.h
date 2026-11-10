// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Modulation sources (LFOs, random generators, envelope follower, macros) and the
// modulation matrix. Sources are evaluated at control rate, once per sub-block.
#pragma once

#include "../dsp/Analysis.h"
#include "../dsp/Util.h"
#include "../params/ParameterIds.h"
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>

namespace grn
{

/** A recorded XY gesture, replayed in a loop as two modulation sources. */
struct GestureData
{
    static constexpr int maxPoints = 512;
    int count = 0;          // 0 = no gesture
    float seconds = 1.0f;   // duration of one loop at rate 1
    std::array<float, maxPoints> x {}, y {}; // -1..1
};

/** Settings of every modulation source for one sub-block. */
struct ModSourceParams
{
    int lfoShape[numLfos] {};
    float lfoRate[numLfos] { 1.0f, 1.0f, 1.0f, 1.0f };
    bool lfoSync[numLfos] {};
    int lfoDiv[numLfos] {};
    float lfoPhase[numLfos] {};   // 0..1
    bool lfoRetrig[numLfos] {};
    float randRate[numRandoms] { 1.0f, 1.0f };
    float randSmooth[numRandoms] {};
    bool randSync[numRandoms] {};
    int randDiv[numRandoms] {};
    int envSource = 0;            // 0 input, 1 output
    float envAttackMs = 10.0f, envReleaseMs = 200.0f, envGain = 1.0f;
    float macro[numMacros] {};    // 0..1
    float driftRate = 0.2f, driftPull = 0.3f;
    float chaosRate = 0.3f, chaosShape = 0.0f;
    float bounceRate = 1.0f, bounceDamp = 0.3f;
    float gestureRate = 1.0f;
    const GestureData* gesture = nullptr;
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
};

class ModulationEngine
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        for (auto& lfo : lfos)
            lfo = {};
        for (auto& random : randoms)
            random = {};
        envelope = 0.0f;
        onsetEnv = 0.0f;
        pitchHeld = 0.0f;
        drift = 0.0f;
        lorenzX = 1.0f; lorenzY = 1.0f; lorenzZ = 20.0f;
        ballHeight = 1.0f; ballSpeed = 0.0f;
        gesturePhase = 0.0;
        motionRng.seed (0x51ed270bu);
        values.fill (0.0f);
        wasPlaying = false;
        macrosPrimed = false;
    }

    /** Advances every source by numSamples. The peaks are the levels of the previous sub-block. */
    void process (const ModSourceParams& p, int numSamples, float inputPeak, float outputPeak,
                  const AnalysisValues& analysis = {}) noexcept
    {
        const double seconds = static_cast<double> (numSamples) / fs;
        const double bpm = std::clamp (p.bpm, 20.0, 999.0);
        const bool started = p.playing && ! wasPlaying;
        wasPlaying = p.playing;

        for (int n = 0; n < numLfos; ++n)
        {
            auto& lfo = lfos[static_cast<size_t> (n)];
            if (started && p.lfoRetrig[n])
                lfo.phase = 0.0;

            const double beats = divisionBeats[static_cast<size_t> (std::clamp (p.lfoDiv[n], 0, numDivisions - 1))];
            double cycles;
            if (p.lfoSync[n] && p.playing)
                cycles = p.ppq / beats;
            else
                cycles = lfo.phase + (p.lfoSync[n] ? bpm / (60.0 * beats) : static_cast<double> (p.lfoRate[n])) * seconds;
            lfo.phase = cycles;

            const double shifted = cycles + static_cast<double> (p.lfoPhase[n]);
            const double cycleFloor = std::floor (shifted);
            const auto x = static_cast<float> (shifted - cycleFloor);
            const auto cycleIndex = static_cast<int64_t> (cycleFloor);
            if (cycleIndex != lfo.cycle)
            {
                lfo.cycle = cycleIndex;
                lfo.previous = lfo.current;
                lfo.current = rng.bi();
            }

            float value = 0.0f;
            switch (p.lfoShape[n])
            {
                case lfoSine:         value = std::sin (kTwoPi * x); break;
                case lfoTriangle:     value = x < 0.25f ? 4.0f * x : (x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f); break;
                case lfoSaw:          value = 1.0f - 2.0f * x; break;
                case lfoSquare:       value = x < 0.5f ? 1.0f : -1.0f; break;
                case lfoSmoothRandom: value = lfo.previous + (lfo.current - lfo.previous) * (0.5f - 0.5f * std::cos (kPi * x)); break;
                case lfoSampleHold:   value = lfo.current; break;
                default: break;
            }
            values[static_cast<size_t> (srcLfo1 + n)] = value;

            // Keep the free-running phase bounded.
            if (! (p.lfoSync[n] && p.playing) && lfo.phase > 1.0e6)
                lfo.phase -= 1.0e6;
        }

        for (int n = 0; n < numRandoms; ++n)
        {
            auto& random = randoms[static_cast<size_t> (n)];
            const double beats = divisionBeats[static_cast<size_t> (std::clamp (p.randDiv[n], 0, numDivisions - 1))];
            const double rate = p.randSync[n] ? bpm / (60.0 * beats) : static_cast<double> (p.randRate[n]);
            random.phase += rate * seconds;
            if (random.phase >= 1.0)
            {
                random.phase -= std::floor (random.phase);
                random.target = rng.bi();
            }
            const float smooth = std::clamp (p.randSmooth[n], 0.0f, 1.0f);
            if (smooth < 0.01f)
            {
                random.value = random.target;
            }
            else
            {
                const auto period = static_cast<float> (1.0 / std::max (0.01, rate));
                const float coef = 1.0f - std::exp (-static_cast<float> (seconds) / (smooth * 0.5f * period));
                random.value += coef * (random.target - random.value);
            }
            values[static_cast<size_t> (srcRand1 + n)] = random.value;
        }

        {
            const float level = std::min (1.0f, (p.envSource == 0 ? inputPeak : outputPeak) * p.envGain);
            const float timeMs = level > envelope ? p.envAttackMs : p.envReleaseMs;
            const float coef = 1.0f - std::exp (-static_cast<float> (seconds) / (std::max (0.1f, timeMs) * 0.001f));
            envelope += coef * (level - envelope);
            values[srcEnv] = envelope;
        }

        {
            const float coef = macrosPrimed ? 1.0f - std::exp (-static_cast<float> (seconds) / 0.015f) : 1.0f;
            macrosPrimed = true;
            for (int n = 0; n < numMacros; ++n)
            {
                auto& value = values[static_cast<size_t> (srcMacro1 + n)];
                value += coef * (p.macro[n] - value);
            }
        }

        processMotion (p, static_cast<float> (seconds), analysis);
    }

    /** Sources that listen to the input or move on their own (added after the first version). */
    void processMotion (const ModSourceParams& p, float seconds, const AnalysisValues& analysis) noexcept
    {
        // Listening sources.
        pitchHeld += 0.3f * (analysis.pitch - pitchHeld);
        values[srcPitch] = pitchHeld;
        values[srcBright] = analysis.brightness;
        if (analysis.onset)
            onsetEnv = 1.0f;
        else
            onsetEnv *= std::exp (-seconds / (std::max (10.0f, p.envReleaseMs) * 0.001f));
        values[srcOnset] = onsetEnv;

        // Drift: a random walk between -1 and 1, pulled back to the centre.
        {
            const float rate = std::max (0.01f, p.driftRate);
            const float noise = motionRng.bi() + motionRng.bi() + motionRng.bi();
            drift += -4.0f * rate * p.driftPull * drift * seconds + 1.2f * std::sqrt (rate * seconds) * noise;
            if (drift > 1.0f)
                drift = 2.0f - drift;
            else if (drift < -1.0f)
                drift = -2.0f - drift;
            drift = std::clamp (drift, -1.0f, 1.0f);
            values[srcDrift] = drift;
        }

        // Chaos: Lorenz attractor. One lobe turn lasts about 0.65 time units.
        {
            const float total = std::min (0.25f, seconds * std::max (0.01f, p.chaosRate) * 0.65f);
            const int steps = std::max (1, static_cast<int> (std::ceil (total / 0.004f)));
            const float dt = total / static_cast<float> (steps);
            for (int s = 0; s < steps; ++s)
            {
                const float dx = 10.0f * (lorenzY - lorenzX);
                const float dy = lorenzX * (28.0f - lorenzZ) - lorenzY;
                const float dz = lorenzX * lorenzY - (8.0f / 3.0f) * lorenzZ;
                lorenzX += dx * dt;
                lorenzY += dy * dt;
                lorenzZ += dz * dt;
            }
            if (! (std::abs (lorenzX) < 100.0f && std::abs (lorenzY) < 100.0f && std::abs (lorenzZ) < 200.0f))
            {
                lorenzX = 1.0f; lorenzY = 1.0f; lorenzZ = 20.0f;
            }
            const float wing = std::clamp (lorenzX / 20.0f, -1.0f, 1.0f);
            const float height = std::clamp ((lorenzZ - 24.0f) / 20.0f, -1.0f, 1.0f);
            values[srcChaos] = wing + std::clamp (p.chaosShape, 0.0f, 1.0f) * (height - wing);
        }

        // Bounce: a ball dropped from 1, relaunched when it comes to rest or on an onset.
        {
            const float rate = std::max (0.1f, p.bounceRate);
            const float gravity = 8.0f * rate * rate;
            const float restitution = 0.95f - 0.6f * std::clamp (p.bounceDamp, 0.0f, 1.0f);
            if (analysis.onset)
            {
                ballHeight = 1.0f;
                ballSpeed = 0.0f;
            }
            ballSpeed -= gravity * seconds;
            ballHeight += ballSpeed * seconds;
            if (ballHeight < 0.0f)
            {
                ballHeight = -ballHeight * restitution;
                ballSpeed = -ballSpeed * restitution;
                if (ballSpeed * ballSpeed < 0.04f * gravity)
                {
                    ballHeight = 1.0f;
                    ballSpeed = 0.0f;
                }
            }
            ballHeight = std::clamp (ballHeight, 0.0f, 1.0f);
            values[srcBounce] = ballHeight;
        }

        // Gesture: loops through the recorded points with linear interpolation.
        if (p.gesture != nullptr && p.gesture->count > 1)
        {
            const auto& g = *p.gesture;
            gesturePhase += static_cast<double> (seconds * p.gestureRate / std::max (0.05f, g.seconds));
            gesturePhase -= std::floor (gesturePhase);
            const float position = static_cast<float> (gesturePhase) * static_cast<float> (g.count - 1);
            const int index = std::clamp (static_cast<int> (position), 0, g.count - 2);
            const float t = position - static_cast<float> (index);
            const auto i = static_cast<size_t> (index);
            values[srcGestureX] = g.x[i] + t * (g.x[i + 1] - g.x[i]);
            values[srcGestureY] = g.y[i] + t * (g.y[i + 1] - g.y[i]);
        }
        else
        {
            values[srcGestureX] = 0.0f;
            values[srcGestureY] = 0.0f;
        }
    }

    float value (int source) const noexcept { return values[static_cast<size_t> (source)]; }

private:
    struct LfoState
    {
        double phase = 0.0;
        int64_t cycle = INT64_MIN;
        float previous = 0.0f, current = 0.0f;
    };

    struct RandomState
    {
        double phase = 0.0;
        float target = 0.0f, value = 0.0f;
    };

    double fs = 48000.0;
    std::array<LfoState, numLfos> lfos;
    std::array<RandomState, numRandoms> randoms;
    std::array<float, numModSources> values {};
    float envelope = 0.0f;
    bool wasPlaying = false, macrosPrimed = false;
    float onsetEnv = 0.0f, pitchHeld = 0.0f, drift = 0.0f;
    float lorenzX = 1.0f, lorenzY = 1.0f, lorenzZ = 20.0f;
    float ballHeight = 1.0f, ballSpeed = 0.0f;
    double gesturePhase = 0.0;
    Rng motionRng;
    Rng rng;
};

// ---------------------------------------------------------------------------------------------
/** Modulation matrix shared between the message thread (edits) and the audio thread (reads).
    Each field is an atomic: a slot may be seen half-updated for one sub-block, which is harmless. */
class ModMatrix
{
public:
    struct Slot
    {
        std::atomic<int> source { -1 };
        std::atomic<int> dest { -1 };
        std::atomic<float> depth { 0.0f };
    };

    std::array<Slot, numModSlots> slots;

    void clear() noexcept
    {
        for (auto& slot : slots)
        {
            slot.source.store (-1);
            slot.dest.store (-1);
            slot.depth.store (0.0f);
        }
        bumpVersion();
    }

    int findSlot (int source, int dest) const noexcept
    {
        for (int i = 0; i < numModSlots; ++i)
            if (slots[static_cast<size_t> (i)].source.load() == source && slots[static_cast<size_t> (i)].dest.load() == dest)
                return i;
        return -1;
    }

    int findFreeSlot() const noexcept
    {
        for (int i = 0; i < numModSlots; ++i)
            if (slots[static_cast<size_t> (i)].source.load() < 0 || slots[static_cast<size_t> (i)].dest.load() < 0)
                return i;
        return -1;
    }

    /** Sets the depth of a routing, creating it when needed. Returns the slot or -1 when full. */
    int setRouting (int source, int dest, float depth) noexcept
    {
        int index = findSlot (source, dest);
        if (index < 0)
            index = findFreeSlot();
        if (index < 0)
            return -1;
        auto& slot = slots[static_cast<size_t> (index)];
        slot.depth.store (std::clamp (depth, -1.0f, 1.0f));
        slot.dest.store (dest);
        slot.source.store (source);
        bumpVersion();
        return index;
    }

    void setSlot (int index, int source, int dest, float depth) noexcept
    {
        if (index < 0 || index >= numModSlots)
            return;
        auto& slot = slots[static_cast<size_t> (index)];
        slot.source.store (source);
        slot.dest.store (dest);
        slot.depth.store (std::clamp (depth, -1.0f, 1.0f));
        bumpVersion();
    }

    void removeSlot (int index) noexcept { setSlot (index, -1, -1, 0.0f); }

    /** Sum of the absolute depths applied to a destination by a given source (0 when none). */
    float depthFor (int source, int dest) const noexcept
    {
        const int index = findSlot (source, dest);
        return index < 0 ? 0.0f : slots[static_cast<size_t> (index)].depth.load();
    }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree tree ("MATRIX");
        for (int i = 0; i < numModSlots; ++i)
        {
            const auto& slot = slots[static_cast<size_t> (i)];
            const int source = slot.source.load();
            const int dest = slot.dest.load();
            if (source < 0 || dest < 0)
                continue;
            juce::ValueTree child ("SLOT");
            child.setProperty ("source", source, nullptr);
            child.setProperty ("dest", modDestParamId (dest), nullptr);
            child.setProperty ("depth", slot.depth.load(), nullptr);
            tree.addChild (child, -1, nullptr);
        }
        return tree;
    }

    void fromValueTree (const juce::ValueTree& tree)
    {
        for (auto& slot : slots)
        {
            slot.source.store (-1);
            slot.dest.store (-1);
            slot.depth.store (0.0f);
        }
        int index = 0;
        for (const auto& child : tree)
        {
            if (index >= numModSlots || ! child.hasType ("SLOT"))
                continue;
            const int source = child.getProperty ("source", -1);
            const auto destId = child.getProperty ("dest").toString();
            int dest = -1;
            for (int d = 0; d < numModDests; ++d)
                if (modDestParamId (d) == destId)
                    dest = d;
            if (source < 0 || source >= numModSources || dest < 0)
                continue;
            auto& slot = slots[static_cast<size_t> (index++)];
            slot.depth.store (std::clamp (static_cast<float> (child.getProperty ("depth", 0.0f)), -1.0f, 1.0f));
            slot.dest.store (dest);
            slot.source.store (source);
        }
        bumpVersion();
    }

    uint32_t getVersion() const noexcept { return version.load(); }

private:
    void bumpVersion() noexcept { version.fetch_add (1); }
    std::atomic<uint32_t> version { 0 };
};

} // namespace grn
