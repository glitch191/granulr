// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tests of the listening, motion, living buffer, space and resonator features.
#include "TestUtil.h"
#include "dsp/Analysis.h"
#include "dsp/Effects.h"
#include "mod/Modulation.h"
#include "state/PresetManager.h"

using namespace grn;
using namespace testutil;

namespace
{

/** A decaying chord every 750 ms. */
Generator pulses (double sampleRate)
{
    return [=] (float* left, float* right, int n, int64_t start)
    {
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (start + i) / sampleRate;
            const double envelope = std::exp (-5.0 * std::fmod (t, 0.75));
            const double chord = std::sin (6.2831853 * 220.0 * t) + 0.7 * std::sin (6.2831853 * 277.18 * t);
            left[i] = right[i] = static_cast<float> (0.3 * chord * envelope);
        }
    };
}

int countOnsets (const Generator& generator, double sampleRate, double seconds, float sensitivity, double skipSeconds)
{
    InputAnalysis analysis;
    analysis.prepare (sampleRate);
    float left[32], right[32];
    int count = 0;
    const auto total = static_cast<int64_t> (seconds * sampleRate);
    for (int64_t position = 0; position < total; position += 32)
    {
        generator (left, right, 32, position);
        analysis.process (left, right, 32, sensitivity);
        if (analysis.getOnsetOffset() >= 0 && static_cast<double> (position) > skipSeconds * sampleRate)
            ++count;
    }
    return count;
}

/** Runs the engine alone on a generator and returns statistics of the output. */
RunStats runEngine (GrainEngine& engine, const EngineParams& params, const Generator& generator,
                    double seconds, double sampleRate, int64_t& position)
{
    RunStats stats;
    float left[32], right[32];
    const auto total = static_cast<int64_t> (seconds * sampleRate);
    for (int64_t done = 0; done < total; done += 32)
    {
        generator (left, right, 32, position);
        engine.process (left, right, left, right, 32, params);
        for (int i = 0; i < 32; ++i)
        {
            if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                stats.finite = false;
            stats.peak = std::max (stats.peak, std::max (std::abs (left[i]), std::abs (right[i])));
            stats.sumSquares += static_cast<double> (left[i]) * left[i] + static_cast<double> (right[i]) * right[i];
        }
        stats.samples += 32;
        position += 32;
    }
    return stats;
}

class FeatureTests final : public juce::UnitTest
{
public:
    FeatureTests() : juce::UnitTest ("Features", "granulr") {}

    void runTest() override
    {
        beginTest ("Onsets are found on attacks and not on steady tones");
        {
            for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                const int found = countOnsets (pulses (rate), rate, 6.0, 0.5f, 0.0);
                expect (found >= 7 && found <= 9, "pulses at " + juce::String (rate) + ": " + juce::String (found) + " onsets");
                for (double frequency : { 60.0, 220.0, 2000.0 })
                    for (float sensitivity : { 0.5f, 1.0f })
                        expectEquals (countOnsets (sine (frequency, rate), rate, 4.0, sensitivity, 0.5), 0,
                                      "steady " + juce::String (frequency) + " Hz at " + juce::String (rate));
            }
        }

        beginTest ("Onset trigger plays grains after attacks only");
        {
            GrainEngine engine;
            engine.prepare (48000.0);
            EngineParams params;
            params.triggerMode = triggerOnset;
            params.mix = 1.0f;
            params.onsetBurst = 6;
            params.density = 30.0f;
            params.positionMs = 60.0f;
            int64_t position = 0;
            const auto withAttacks = runEngine (engine, params, pulses (48000.0), 4.0, 48000.0, position);
            expect (withAttacks.finite && withAttacks.rms() > 1.0e-3f, "no sound from the onset trigger");

            engine.reset();
            position = 0;
            runEngine (engine, params, sine (220.0, 48000.0), 2.0, 48000.0, position);
            const auto steady = runEngine (engine, params, sine (220.0, 48000.0), 2.0, 48000.0, position);
            expect (steady.rms() < 1.0e-5f, "grains keep playing on a steady tone");
            expectEquals (engine.getNumActiveGrains(), 0);
        }

        beginTest ("Listening sources follow the input");
        {
            GrainEngine engine;
            engine.prepare (48000.0);
            EngineParams params;
            int64_t position = 0;
            runEngine (engine, params, sine (150.0, 48000.0), 1.0, 48000.0, position);
            const float dark = engine.getAnalysisValues().brightness;
            const float lowNote = engine.getAnalysisValues().pitch;
            runEngine (engine, params, sine (880.0, 48000.0), 1.0, 48000.0, position);
            const float midNote = engine.getAnalysisValues().pitch;
            runEngine (engine, params, sine (5000.0, 48000.0), 1.0, 48000.0, position);
            const float bright = engine.getAnalysisValues().brightness;
            expect (dark < 0.15f, "150 Hz brightness " + juce::String (dark));
            expect (bright > 0.75f, "5 kHz brightness " + juce::String (bright));
            // 150 Hz is about MIDI 50, 880 Hz is MIDI 81: (note - 60) / 24.
            expectWithinAbsoluteError (lowNote, (50.37f - 60.0f) / 24.0f, 0.03f);
            expectWithinAbsoluteError (midNote, (81.0f - 60.0f) / 24.0f, 0.03f);
        }

        beginTest ("Motion sources stay in range and keep moving");
        {
            for (float rate : { 0.01f, 0.3f, 10.0f })
            {
                ModulationEngine modulation;
                modulation.prepare (48000.0);
                ModSourceParams params;
                params.driftRate = params.chaosRate = rate;
                params.bounceRate = std::max (0.1f, rate);
                params.driftPull = 0.0f;
                GestureData gesture;
                gesture.count = 4;
                gesture.seconds = 0.5f;
                gesture.x = { -1.0f, 1.0f, 0.5f, -0.5f };
                gesture.y = { 0.0f, 1.0f, -1.0f, 0.25f };
                params.gesture = &gesture;

                float low[numModSources], high[numModSources];
                std::fill_n (low, numModSources, 10.0f);
                std::fill_n (high, numModSources, -10.0f);
                bool finite = true;
                for (int block = 0; block < 90000; ++block) // 60 s
                {
                    AnalysisValues analysis;
                    analysis.onset = block % 3000 == 0;
                    modulation.process (params, 32, 0.2f, 0.2f, analysis);
                    for (int s = 0; s < numModSources; ++s)
                    {
                        const float value = modulation.value (s);
                        finite = finite && std::isfinite (value);
                        low[s] = std::min (low[s], value);
                        high[s] = std::max (high[s], value);
                    }
                }
                expect (finite);
                for (int s = 0; s < numModSources; ++s)
                    expect (low[s] >= -1.0001f && high[s] <= 1.0001f, juce::String (modSourceName (s)) + " out of range");
                if (rate >= 0.3f)
                {
                    expect (high[srcDrift] - low[srcDrift] > 0.8f, "drift barely moves");
                    expect (high[srcChaos] - low[srcChaos] > 0.8f, "chaos barely moves");
                }
                expect (low[srcBounce] >= 0.0f && high[srcBounce] > 0.95f, "bounce range");
                expect (high[srcOnset] > 0.99f && low[srcOnset] < 0.05f, "onset source range");
                expect (high[srcGestureX] > 0.9f && low[srcGestureX] < -0.9f, "gesture range");
            }
        }

        beginTest ("Flock keeps successive grains together");
        {
            auto spread = [] (float flock)
            {
                GrainEngine engine;
                engine.prepare (48000.0);
                EngineParams params;
                params.mix = 1.0f;
                params.sprayMs = 2000.0f;
                params.positionMs = 3000.0f;
                params.density = 60.0f;
                params.sizeMs = 200.0f;
                params.flock = flock;
                int64_t position = 0;
                runEngine (engine, params, noise (0.3f), 6.0, 48000.0, position);
                double total = 0.0;
                int measures = 0;
                for (int m = 0; m < 40; ++m)
                {
                    runEngine (engine, params, noise (0.3f), 0.05, 48000.0, position);
                    GrainEngine::GrainView views[256];
                    const int count = engine.fillGrainViews (views, 256);
                    if (count < 4)
                        continue;
                    double mean = 0.0, variance = 0.0;
                    for (int i = 0; i < count; ++i)
                        mean += views[i].delayNorm;
                    mean /= count;
                    for (int i = 0; i < count; ++i)
                        variance += (views[i].delayNorm - mean) * (views[i].delayNorm - mean);
                    total += std::sqrt (variance / count);
                    ++measures;
                }
                return measures > 0 ? total / measures : 0.0;
            };
            const double independent = spread (0.0f);
            const double together = spread (1.0f);
            expect (independent > 0.04, "independent spread " + juce::String (independent));
            expect (together < 0.4 * independent, "flock spread " + juce::String (together) + " against " + juce::String (independent));
        }

        beginTest ("Living buffer changes the frozen audio and stays bounded for 60 seconds");
        {
            auto frozenRun = [this] (float evolve, float wear, double seconds, float& endRms)
            {
                GrainEngine engine;
                engine.prepare (48000.0);
                EngineParams params;
                params.mix = 1.0f;
                params.density = 40.0f;
                params.sizeMs = 300.0f;
                params.sprayMs = 1500.0f;
                params.positionMs = 2000.0f;
                params.pitch = 7.0f;
                int64_t position = 0;
                runEngine (engine, params, pulses (48000.0), 9.0, 48000.0, position);
                params.freeze = true;
                params.evolve = evolve;
                params.wear = wear;
                const auto stats = runEngine (engine, params, silence(), seconds, 48000.0, position);
                const auto tail = runEngine (engine, params, silence(), 2.0, 48000.0, position);
                endRms = tail.rms();
                expect (stats.finite && tail.finite, "non-finite output");
                expect (stats.peak <= 1.0f, "peak " + juce::String (stats.peak));
                return stats;
            };
            float plainEnd = 0.0f, evolvedEnd = 0.0f, wornEnd = 0.0f;
            frozenRun (0.0f, 0.0f, 60.0, plainEnd);
            frozenRun (1.0f, 0.0f, 60.0, evolvedEnd);
            frozenRun (0.0f, 1.0f, 60.0, wornEnd);
            float bothEnd = 0.0f;
            frozenRun (1.0f, 1.0f, 60.0, bothEnd);
            expect (plainEnd > 1.0e-3f, "frozen buffer went silent");
            expect (std::abs (evolvedEnd - plainEnd) > 0.02f * plainEnd, "evolve had no effect");
            expect (wornEnd < 0.9f * plainEnd, "wear did not erode the buffer");
            logMessage ("frozen RMS: plain " + juce::String (plainEnd, 4) + ", evolve " + juce::String (evolvedEnd, 4)
                        + ", wear " + juce::String (wornEnd, 4) + ", both " + juce::String (bothEnd, 4));
        }

        beginTest ("Space per grain: distance lowers the level, everything stays finite");
        {
            auto level = [this] (float distance, float orbit, float doppler)
            {
                GrainEngine engine;
                engine.prepare (48000.0);
                EngineParams params;
                params.mix = 1.0f;
                params.density = 80.0f;
                params.sizeMs = 250.0f;
                params.reverse = 0.3f;
                params.pitch = 5.0f;
                params.distance = distance;
                params.orbit = orbit;
                params.doppler = doppler;
                int64_t position = 0;
                const auto stats = runEngine (engine, params, noise (0.3f), 6.0, 48000.0, position);
                expect (stats.finite);
                return stats.rms();
            };
            const float close = level (0.0f, 0.0f, 0.0f);
            const float far = level (1.0f, 0.0f, 0.0f);
            const float moving = level (0.0f, 1.0f, 1.0f);
            const float everything = level (1.0f, 1.0f, 1.0f);
            expect (far < 0.8f * close, "distance: " + juce::String (far) + " against " + juce::String (close));
            expect (moving > 0.5f * close && moving < 1.5f * close, "orbit and doppler changed the level too much");
            expect (everything > 0.0f);

            // Doppler on a frozen buffer must never read past the write head.
            GrainEngine engine;
            engine.prepare (48000.0);
            EngineParams params;
            params.mix = 1.0f;
            params.density = 100.0f;
            params.sizeMs = 2000.0f;
            params.positionMs = 0.0f;
            params.pitch = 24.0f;
            params.doppler = 1.0f;
            int64_t position = 0;
            runEngine (engine, params, noise (0.3f), 9.0, 48000.0, position);
            params.freeze = true;
            const auto frozen = runEngine (engine, params, silence(), 10.0, 48000.0, position);
            expect (frozen.finite && frozen.peak <= 1.0f);
        }

        beginTest ("Resonator lines are tuned to the scale");
        {
            for (double rate : { 44100.0, 48000.0, 96000.0 })
                for (int octave : { -1, 0, 1 })
                {
                    Resonator resonator;
                    resonator.prepare (rate);
                    // One line only: the root, here D (root 2).
                    resonator.setParams (0x0001, 2, octave, 0.9f, 0.3f, 1.0f, 1);
                    resonator.reset();
                    const float expected = resonator.getFrequency (0);
                    expectWithinAbsoluteError (expected, 146.83f * std::exp2 (static_cast<float> (octave)), 0.05f);

                    juce::Random random (7);
                    std::vector<float> tail;
                    float left[32], right[32];
                    for (int block = 0; block < static_cast<int> (rate * 1.2 / 32.0); ++block)
                    {
                        for (int i = 0; i < 32; ++i)
                            left[i] = right[i] = block < 40 ? random.nextFloat() - 0.5f : 0.0f;
                        resonator.process (left, right, 32);
                        if (block > 200)
                            tail.insert (tail.end(), left, left + 32);
                    }

                    // Period by autocorrelation around the expected lag, with parabolic interpolation.
                    const double period = rate / expected;
                    const int first = static_cast<int> (period * 0.92), last = static_cast<int> (period * 1.08) + 1;
                    const int window = static_cast<int> (tail.size()) - last - 2;
                    std::vector<double> correlation;
                    for (int lag = first; lag <= last; ++lag)
                    {
                        double sum = 0.0;
                        for (int i = 0; i < window; ++i)
                            sum += static_cast<double> (tail[static_cast<size_t> (i)]) * tail[static_cast<size_t> (i + lag)];
                        correlation.push_back (sum);
                    }
                    const auto best = static_cast<int> (std::max_element (correlation.begin() + 1, correlation.end() - 1) - correlation.begin());
                    const double y0 = correlation[static_cast<size_t> (best - 1)], y1 = correlation[static_cast<size_t> (best)],
                                 y2 = correlation[static_cast<size_t> (best + 1)];
                    const double shift = 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
                    const double measured = rate / (first + best + shift);
                    const double cents = 1200.0 * std::log2 (measured / expected);
                    expect (std::abs (cents) < 5.0, "resonator at " + juce::String (rate) + " Hz, octave " + juce::String (octave)
                                                        + ": " + juce::String (cents, 2) + " cents off");
                }

            // A full scale gives six distinct notes of that scale.
            Resonator resonator;
            resonator.prepare (48000.0);
            resonator.setParams (scaleMask (scaleMajor, 0), 0, 0, 0.5f, 0.3f, 0.5f, 6);
            for (int l = 0; l < 6; ++l)
            {
                const float note = 69.0f + 12.0f * std::log2 (resonator.getFrequency (l) / 440.0f);
                const int pitchClass = (static_cast<int> (std::round (note)) % 12 + 12) % 12;
                expect (((scaleMask (scaleMajor, 0) >> pitchClass) & 1u) != 0, "line " + juce::String (l) + " is off the scale");
                expectWithinAbsoluteError (note, std::round (note), 0.001f);
            }
        }

        beginTest ("No allocation and a bounded output with every new feature on");
        {
            auto processor = makeProcessor (48000.0, 256);
            setParam (*processor, pid::flock, 70.0f);
            setParam (*processor, pid::distance, 100.0f);
            setParam (*processor, pid::orbit, 100.0f);
            setParam (*processor, pid::doppler, 100.0f);
            setParam (*processor, pid::resOn, 1.0f);
            setParam (*processor, pid::resPlace, 0.0f);
            setParam (*processor, pid::feedback, 80.0f);
            setParam (*processor, pid::triggerMode, static_cast<float> (triggerOnset));
            setParam (*processor, pid::scale, static_cast<float> (scaleMinorPent));
            auto& matrix = processor->getMatrix();
            matrix.setRouting (srcPitch, dstPosition, 0.5f);
            matrix.setRouting (srcBright, dstSize, 0.5f);
            matrix.setRouting (srcOnset, dstPitchRandom, 0.5f);
            matrix.setRouting (srcDrift, dstFlock, 0.5f);
            matrix.setRouting (srcChaos, dstDistance, 0.5f);
            matrix.setRouting (srcBounce, dstResMix, 0.5f);

            int64_t position = 0;
            run (*processor, pulses (48000.0), 48000 * 2, 256, position);
            allocationCount.store (0);
            const auto stats = run (*processor, pulses (48000.0), 48000 * 8, 256, position, nullptr, true);
            expectEquals (allocationCount.load(), 0, "allocation in the audio callback");
            expect (stats.finite && stats.peak <= 1.0f, "peak " + juce::String (stats.peak));
            expect (stats.rms() > 1.0e-3f);

            setParam (*processor, pid::freeze, 1.0f);
            setParam (*processor, pid::evolve, 100.0f);
            setParam (*processor, pid::wear, 60.0f);
            setParam (*processor, pid::triggerMode, static_cast<float> (triggerFree));
            const auto frozen = run (*processor, pulses (48000.0), 48000 * 10, 256, position, nullptr, true);
            expectEquals (allocationCount.load(), 0, "allocation while the frozen buffer evolves");
            expect (frozen.finite && frozen.peak <= 1.0f);
        }

        beginTest ("The gesture is saved with the plugin state and with presets, and it plays");
        {
            GestureData gesture;
            gesture.count = 5;
            gesture.seconds = 0.4f;
            gesture.x = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
            gesture.y = { 0.25f, 1.0f, -1.0f, 0.5f, -0.25f };

            auto source = makeProcessor();
            source->setGesture (gesture);
            source->getMatrix().setRouting (srcGestureX, dstPosition, -0.5f);
            juce::MemoryBlock state;
            source->getStateInformation (state);

            auto restored = makeProcessor();
            restored->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            const auto& copy = restored->getGesture();
            expectEquals (copy.count, 5);
            expectWithinAbsoluteError (copy.seconds, 0.4f, 1.0e-4f);
            for (int i = 0; i < 5; ++i)
            {
                expectWithinAbsoluteError (copy.x[static_cast<size_t> (i)], gesture.x[static_cast<size_t> (i)], 1.0e-3f);
                expectWithinAbsoluteError (copy.y[static_cast<size_t> (i)], gesture.y[static_cast<size_t> (i)], 1.0e-3f);
            }

            const auto sound = source->captureSoundState();
            auto other = makeProcessor();
            other->applySoundState (sound);
            expectEquals (other->getGesture().count, 5);
            other->applySoundState (makeProcessor()->captureSoundState());
            expectEquals (other->getGesture().count, 0, "a sound without gesture must clear it");

            // The audio thread replays it: the X source sweeps its whole range.
            restored->getBridge().editorOpen.store (true);
            int64_t position = 0;
            float low = 1.0f, high = -1.0f;
            for (int block = 0; block < 200; ++block)
            {
                run (*restored, noise (0.2f), 256, 256, position);
                const float value = restored->getBridge().sourceValue[srcGestureX].load();
                low = std::min (low, value);
                high = std::max (high, value);
            }
            expect (low < -0.8f && high > 0.8f, "gesture playback range " + juce::String (low) + " to " + juce::String (high));
        }

        beginTest ("Mutate stays close, Breed stays between the parents, both can be undone");
        {
            auto processor = makeProcessor();
            auto& presets = processor->getPresets();
            presets.load (0);
            std::array<bool, numParamGroups> locks {};
            locks[groupPitch] = true;

            auto normalised = [&] (const char* identifier) { return processor->getState().getParameter (identifier)->getValue(); };
            const float sizeBefore = normalised (pid::size), pitchBefore = normalised (pid::pitch);
            float largest = 0.0f;
            for (int i = 0; i < 20; ++i)
            {
                presets.mutate (0.5f, locks);
                largest = std::max (largest, std::abs (normalised (pid::size) - sizeBefore));
                expectEquals (normalised (pid::pitch), pitchBefore, "locked group moved");
                processor->getUndoManager().undo();
                expectWithinAbsoluteError (normalised (pid::size), sizeBefore, 1.0e-4f);
            }
            expect (largest > 0.0f && largest <= 0.16f, "mutation size move " + juce::String (largest));

            // Two different parents in slots A and B.
            setParam (*processor, pid::size, 100.0f);
            setParam (*processor, pid::windowShape, 0.0f);
            presets.selectSlot (1);
            setParam (*processor, pid::size, 900.0f);
            setParam (*processor, pid::windowShape, 3.0f);
            expect (presets.canBreed());
            for (int i = 0; i < 20; ++i)
            {
                expect (presets.breed());
                const float size = getParam (*processor, pid::size);
                expect (size >= 99.0f && size <= 901.0f, "bred size " + juce::String (size));
                const float shape = getParam (*processor, pid::windowShape);
                expect (shape == 0.0f || shape == 3.0f, "bred choice must come from a parent");
                processor->getUndoManager().undo();
                expectWithinAbsoluteError (getParam (*processor, pid::size), 900.0f, 0.5f);
            }

            presets.makeVariations (0.5f, {});
            expect (presets.hasVariations());
            presets.loadVariation (2);
            presets.loadVariation (-1);
            expectWithinAbsoluteError (getParam (*processor, pid::size), 900.0f, 0.5f);
            int64_t position = 0;
            expect (run (*processor, noise (0.5f), 48000, 512, position).finite);
        }
    }
};

FeatureTests featureTests;

} // namespace
