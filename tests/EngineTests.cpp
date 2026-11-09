// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestUtil.h"
#include "state/PresetManager.h"
#include <juce_dsp/juce_dsp.h>

using namespace grn;
using namespace testutil;

namespace
{

void randomiseAllParams (GranulrProcessor& processor, juce::Random& random, bool keepAudible = false)
{
    for (auto* base : processor.getParameters())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (base);
        if (param == nullptr)
            continue;
        const auto identifier = param->getParameterID();
        if (keepAudible && (identifier == pid::bypass || identifier == pid::mix))
            continue;
        const auto& range = param->getNormalisableRange();
        param->setValueNotifyingHost (param->convertTo0to1 (range.snapToLegalValue (param->convertFrom0to1 (random.nextFloat()))));
    }
}

void randomiseMatrix (GranulrProcessor& processor, juce::Random& random)
{
    auto& matrix = processor.getMatrix();
    matrix.clear();
    for (int i = 0; i < numModSlots; ++i)
        matrix.setSlot (i, random.nextInt (numModSources), random.nextInt (numModDests), random.nextFloat() * 2.0f - 1.0f);
}

/** Dominant frequency of a signal, by FFT with parabolic interpolation. */
float dominantFrequency (const std::vector<float>& signal, double sampleRate)
{
    constexpr int order = 15;
    constexpr int size = 1 << order;
    if (static_cast<int> (signal.size()) < size)
        return 0.0f;
    juce::dsp::FFT fft (order);
    std::vector<float> data (static_cast<size_t> (size) * 2, 0.0f);
    const size_t start = signal.size() - static_cast<size_t> (size);
    for (int i = 0; i < size; ++i)
    {
        const float window = 0.5f - 0.5f * std::cos (6.2831853f * static_cast<float> (i) / static_cast<float> (size - 1));
        data[static_cast<size_t> (i)] = signal[start + static_cast<size_t> (i)] * window;
    }
    fft.performFrequencyOnlyForwardTransform (data.data());
    int best = 1;
    for (int i = 2; i < size / 2 - 1; ++i)
        if (data[static_cast<size_t> (i)] > data[static_cast<size_t> (best)])
            best = i;
    const float y0 = data[static_cast<size_t> (best - 1)], y1 = data[static_cast<size_t> (best)], y2 = data[static_cast<size_t> (best + 1)];
    const float denom = y0 - 2.0f * y1 + y2;
    const float shift = std::abs (denom) > 1.0e-12f ? 0.5f * (y0 - y2) / denom : 0.0f;
    return (static_cast<float> (best) + shift) * static_cast<float> (sampleRate) / static_cast<float> (size);
}

// ---------------------------------------------------------------------------------------------
class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Engine", "granulr") {}

    void runTest() override
    {
        beginTest ("Mix at 0 % is a bit-exact dry signal");
        {
            auto processor = makeProcessor();
            setParam (*processor, pid::mix, 0.0f);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::AudioBuffer<float> reference (2, 512);
            juce::MidiBuffer midi;
            auto generator = noise (0.5f);
            bool identical = true;
            for (int block = 0; block < 200; ++block)
            {
                generator (buffer.getWritePointer (0), buffer.getWritePointer (1), 512, 0);
                reference.makeCopyOf (buffer);
                processor->processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    if (std::memcmp (buffer.getReadPointer (ch), reference.getReadPointer (ch), sizeof (float) * 512) != 0)
                        identical = false;
            }
            expect (identical);
        }

        beginTest ("Grains produce sound and every engine parameter is finite at defaults");
        {
            auto processor = makeProcessor();
            setParam (*processor, pid::mix, 100.0f);
            int64_t position = 0;
            const auto stats = run (*processor, sine (220.0, 48000.0), 48000 * 3, 512, position);
            expect (stats.finite);
            expect (stats.rms() > 0.05f, "wet signal is too quiet: " + juce::String (stats.rms()));
            expect (stats.peak <= 1.0f);
            expect (processor->getEngine().getNumActiveGrains() > 0);
        }

        beginTest ("Random and extreme parameters never produce NaN or runaway levels");
        {
            juce::Random random (42);
            auto processor = makeProcessor();
            int64_t position = 0;
            bool finite = true;
            float worstPeak = 0.0f;
            auto generator = noise (0.9f);
            for (int iteration = 0; iteration < 300; ++iteration)
            {
                randomiseAllParams (*processor, random);
                randomiseMatrix (*processor, random);
                if (iteration % 3 == 0)
                {
                    // Extremes: everything at minimum or maximum.
                    for (auto* param : processor->getParameters())
                        param->setValueNotifyingHost (random.nextBool() ? 0.0f : 1.0f);
                }
                const auto stats = run (*processor, generator, 4096, 1 + random.nextInt (700), position);
                finite = finite && stats.finite;
                worstPeak = std::max (worstPeak, stats.peak);
            }
            expect (finite);
            // Input and output gains can add up to +48 dB when the limiter is switched off.
            expect (worstPeak < 300.0f, "peak " + juce::String (worstPeak));
        }

        beginTest ("Output stays under full scale with the limiter on, whatever the settings");
        {
            juce::Random random (7);
            auto processor = makeProcessor();
            int64_t position = 0;
            float worstPeak = 0.0f;
            auto generator = noise (1.0f);
            for (int iteration = 0; iteration < 150; ++iteration)
            {
                randomiseAllParams (*processor, random);
                randomiseMatrix (*processor, random);
                setParam (*processor, pid::limiter, 1.0f);
                setParam (*processor, pid::bypass, 0.0f);
                worstPeak = std::max (worstPeak, run (*processor, generator, 4096, 256, position).peak);
            }
            expect (worstPeak <= 1.0f, "peak " + juce::String (worstPeak));
        }

        beginTest ("Freeze holds the buffer for 60 seconds");
        {
            auto processor = makeProcessor();
            setParam (*processor, pid::mix, 100.0f);
            setParam (*processor, pid::position, 500.0f);
            int64_t position = 0;
            run (*processor, sine (330.0, 48000.0), 48000 * 3, 512, position);
            setParam (*processor, pid::freeze, 1.0f);
            run (*processor, silence(), 48000 * 2, 512, position);
            const auto early = run (*processor, silence(), 48000 * 3, 512, position);
            run (*processor, silence(), 48000 * 52, 512, position);
            const auto late = run (*processor, silence(), 48000 * 3, 512, position);
            expect (early.rms() > 0.03f, "frozen output is silent: " + juce::String (early.rms()));
            expect (late.finite);
            expect (std::abs (late.rms() - early.rms()) < 0.25f * early.rms(),
                    "level drifted from " + juce::String (early.rms()) + " to " + juce::String (late.rms()));

            // Unfreezing with silent input lets the buffer empty itself.
            setParam (*processor, pid::freeze, 0.0f);
            run (*processor, silence(), 48000 * 14, 512, position);
            const auto after = run (*processor, silence(), 48000, 512, position);
            expect (after.peak < 1.0e-4f);
        }

        beginTest ("Feedback at 100 % stays bounded");
        {
            for (int limiterOn = 0; limiterOn < 2; ++limiterOn)
            {
                auto processor = makeProcessor();
                setParam (*processor, pid::mix, 100.0f);
                setParam (*processor, pid::feedback, 100.0f);
                setParam (*processor, pid::density, 200.0f);
                setParam (*processor, pid::size, 400.0f);
                setParam (*processor, pid::limiter, static_cast<float> (limiterOn));
                setParam (*processor, pid::diffOn, 1.0f);
                setParam (*processor, pid::diffPlace, static_cast<float> (placeFeedback));
                setParam (*processor, pid::diffDecay, 100.0f);
                setParam (*processor, pid::satOn, 1.0f);
                setParam (*processor, pid::satPlace, static_cast<float> (placeFeedback));
                setParam (*processor, pid::satDrive, 36.0f);
                int64_t position = 0;
                run (*processor, noise (1.0f), 48000 * 4, 512, position);
                const auto stats = run (*processor, silence(), 48000 * 40, 512, position);
                expect (stats.finite);
                expect (stats.peak <= (limiterOn ? 1.0f : 8.0f), "peak " + juce::String (stats.peak));
            }
        }

        beginTest ("Sample rates from 44.1 to 192 kHz and block sizes from 1 to 4096");
        {
            for (double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
            {
                juce::Random random (static_cast<int> (sampleRate));
                auto processor = makeProcessor (sampleRate, 4096);
                setParam (*processor, pid::mix, 100.0f);
                setParam (*processor, pid::density, 60.0f);
                int64_t position = 0;
                bool finite = true;
                double sumSquares = 0.0;
                for (int blockSize : { 1, 2, 7, 31, 32, 33, 64, 257, 1024, 4096 })
                {
                    const auto stats = run (*processor, sine (440.0, sampleRate), static_cast<int64_t> (sampleRate * 0.4), blockSize, position);
                    finite = finite && stats.finite;
                    sumSquares += stats.sumSquares;
                }
                // Varying block sizes from one call to the next.
                for (int i = 0; i < 400; ++i)
                {
                    const auto stats = run (*processor, sine (440.0, sampleRate), 1 + random.nextInt (900), 1 + random.nextInt (900), position);
                    finite = finite && stats.finite;
                }
                expect (finite, "non-finite output at " + juce::String (sampleRate));
                expect (sumSquares > 1.0, "silent output at " + juce::String (sampleRate));
            }
        }

        beginTest ("Abrupt parameter changes do not create discontinuities");
        {
            auto processor = makeProcessor();
            setParam (*processor, pid::mix, 100.0f);
            int64_t position = 0;
            auto generator = sine (220.0, 48000.0, 0.5f);
            run (*processor, generator, 48000 * 2, 512, position);
            const float baseline = run (*processor, generator, 48000, 512, position).maxDelta;

            juce::Random random (99);
            float worst = 0.0f;
            const juce::StringArray jumpy { pid::mix, pid::outputGain, pid::inputGain, pid::feedback, pid::width,
                                            pid::position, pid::size, pid::density, pid::pitch, pid::filterCutoff,
                                            pid::spray, pid::panSpread, pid::duckAmount, pid::satDrive };
            setParam (*processor, pid::filterOn, 1.0f);
            setParam (*processor, pid::satOn, 1.0f);
            for (int i = 0; i < 400; ++i)
            {
                const auto& identifier = jumpy[random.nextInt (jumpy.size())];
                auto* param = processor->getState().getParameter (identifier);
                float target = random.nextBool() ? 0.0f : 1.0f;
                if (identifier == pid::outputGain || identifier == pid::inputGain)
                    target = 0.25f + 0.25f * target; // between -12 and 0 dB
                param->setValueNotifyingHost (target);
                worst = std::max (worst, run (*processor, generator, 2048, 512, position).maxDelta);
            }
            // A full-scale step would be close to 1.0. Smooth changes stay near the signal slope.
            expect (worst < 0.35f, "largest sample step " + juce::String (worst) + " (baseline " + juce::String (baseline) + ")");
        }

        beginTest ("The audio callback never allocates");
        {
            juce::Random random (5);
            auto processor = makeProcessor();
            processor->getBridge().editorOpen.store (true);
            int64_t position = 0;
            auto generator = noise (0.5f);
            run (*processor, generator, 4096, 512, position);
            allocationCount = 0;
            for (int iteration = 0; iteration < 200; ++iteration)
            {
                randomiseAllParams (*processor, random);
                randomiseMatrix (*processor, random);
                run (*processor, generator, 2048, 1 + random.nextInt (600), position, nullptr, true);
            }
            expectEquals (allocationCount.load(), 0);
        }

        beginTest ("Silence makes the engine idle and input wakes it up");
        {
            auto processor = makeProcessor();
            setParam (*processor, pid::mix, 100.0f);
            int64_t position = 0;
            run (*processor, noise (0.5f), 48000, 512, position);
            expect (! processor->getEngine().isIdle());
            run (*processor, silence(), 48000 * 30, 512, position);
            expect (processor->getEngine().isIdle());
            const auto stats = run (*processor, noise (0.5f), 48000 * 2, 512, position);
            expect (! processor->getEngine().isIdle());
            expect (stats.rms() > 0.01f);
        }

        beginTest ("Silent tails do not slow down (no denormals)");
        {
            auto processor = makeProcessor();
            setParam (*processor, pid::mix, 100.0f);
            setParam (*processor, pid::feedback, 95.0f);
            setParam (*processor, pid::diffOn, 1.0f);
            setParam (*processor, pid::filterOn, 1.0f);
            int64_t position = 0;
            run (*processor, noise (0.5f), 48000 * 2, 512, position);
            const auto t0 = juce::Time::getHighResolutionTicks();
            run (*processor, noise (0.5f), 48000 * 4, 512, position);
            const auto t1 = juce::Time::getHighResolutionTicks();
            run (*processor, silence(), 48000 * 6, 512, position);
            const auto t2 = juce::Time::getHighResolutionTicks();
            run (*processor, silence(), 48000 * 4, 512, position);
            const auto t3 = juce::Time::getHighResolutionTicks();
            const double active = juce::Time::highResolutionTicksToSeconds (t1 - t0);
            const double tail = juce::Time::highResolutionTicksToSeconds (t3 - t2);
            expect (tail < active * 3.0 + 0.05, "tail " + juce::String (tail) + " s, active " + juce::String (active) + " s");
        }

        beginTest ("Bypass passes the input through unchanged");
        {
            auto processor = makeProcessor();
            setParam (*processor, pid::mix, 100.0f);
            int64_t position = 0;
            run (*processor, noise (0.5f), 48000, 512, position);
            setParam (*processor, pid::bypass, 1.0f);
            run (*processor, noise (0.5f), 4800, 512, position);

            juce::AudioBuffer<float> buffer (2, 512), reference (2, 512);
            juce::MidiBuffer midi;
            auto generator = noise (0.5f, 77);
            generator (buffer.getWritePointer (0), buffer.getWritePointer (1), 512, 0);
            reference.makeCopyOf (buffer);
            processor->processBlock (buffer, midi);
            expect (std::memcmp (buffer.getReadPointer (0), reference.getReadPointer (0), sizeof (float) * 512) == 0);

            setParam (*processor, pid::bypass, 0.0f);
            const auto stats = run (*processor, noise (0.5f), 48000, 512, position);
            expect (stats.finite && stats.rms() > 0.01f);
        }
    }
};

// ---------------------------------------------------------------------------------------------
class PitchTests final : public juce::UnitTest
{
public:
    PitchTests() : juce::UnitTest ("Pitch", "granulr") {}

    void runTest() override
    {
        beginTest ("Scale quantiser snaps to allowed pitch classes");
        {
            const auto major = scaleMask (scaleMajor, 0);
            expectWithinAbsoluteError (nearestInScale (61.2f, major, 0), 62.0f, 0.001f); // C#4 -> D4
            expectWithinAbsoluteError (nearestInScale (60.4f, major, 0), 60.0f, 0.001f);
            expectWithinAbsoluteError (nearestInScale (66.0f, major, 2), 66.0f, 0.001f); // F# is in D major
            expectWithinAbsoluteError (quantiseTransposition (1.0f, 60.0f, major, 0), 0.0f, 0.001f);
            expectWithinAbsoluteError (quantiseTransposition (3.3f, std::numeric_limits<float>::quiet_NaN(), major, 0), 4.0f, 0.001f);
            expectWithinAbsoluteError (quantiseTransposition (5.0f, 60.3f, scaleMask (scaleOctaves, 0), 0), -0.3f, 0.001f);
            for (int scale = scaleChromatic; scale < numScales; ++scale)
            {
                const auto mask = scaleMask (scale, 0x0091);
                for (float x = -30.0f; x < 30.0f; x += 0.37f)
                {
                    const float q = nearestInScale (x, mask, 3);
                    const int pitchClass = ((static_cast<int> (q) - 3) % 12 + 12) % 12;
                    expect (((mask >> pitchClass) & 1u) != 0);
                    expect (std::abs (q - x) <= 6.5f);
                }
            }
        }

        beginTest ("Pitch tracker finds sine frequencies at every sample rate");
        {
            for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                for (float note : { 40.0f, 52.0f, 57.0f, 64.0f, 69.0f, 76.0f, 81.0f })
                {
                    PitchTracker tracker;
                    tracker.prepare (sampleRate);
                    const double frequency = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
                    std::vector<float> block (32);
                    int64_t n = 0;
                    for (int b = 0; b < static_cast<int> (sampleRate * 0.5) / 32; ++b)
                    {
                        for (auto& sample : block)
                            sample = 0.4f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * frequency * static_cast<double> (n++) / sampleRate));
                        tracker.process (block.data(), 32);
                    }
                    const float detected = tracker.latestNote();
                    expect (! std::isnan (detected), "no pitch at " + juce::String (frequency) + " Hz, " + juce::String (sampleRate));
                    if (! std::isnan (detected))
                        expectWithinAbsoluteError (detected, note, 0.15f);
                }
            }
        }

        beginTest ("Pitch tracker reports noise as unvoiced");
        {
            PitchTracker tracker;
            tracker.prepare (48000.0);
            juce::Random random (3);
            std::vector<float> block (32);
            for (int b = 0; b < 1500; ++b)
            {
                for (auto& sample : block)
                    sample = random.nextFloat() - 0.5f;
                tracker.process (block.data(), 32);
            }
            expect (std::isnan (tracker.latestNote()));
        }

        beginTest ("Every grain of a sine lands on the chosen scale");
        {
            auto processor = makeProcessor();
            setParam (*processor, pid::mix, 100.0f);
            setParam (*processor, pid::scale, static_cast<float> (scaleMajor));
            setParam (*processor, pid::root, 0.0f);
            int64_t position = 0;
            run (*processor, sine (440.0, 48000.0), 48000, 512, position); // A4, in C major

            EngineParams params;
            params.scale = scaleMajor;
            params.root = 0;
            params.pitchRandom = 24.0f;
            params.octaveUp = 0.3f;
            params.octaveDown = 0.3f;
            params.voiceProb[1] = 0.5f;
            params.voiceInterval[1] = 6.0f;
            const auto mask = scaleMask (scaleMajor, 0);
            bool allOnScale = true;
            float worstError = 0.0f;
            for (int i = 0; i < 2000; ++i)
            {
                params.pitch = -12.0f + 0.013f * static_cast<float> (i);
                const float semitones = processor->getEngine().resolveSemitones (params, 40000);
                const float resulting = 69.0f + semitones;
                const float rounded = std::round (resulting);
                worstError = std::max (worstError, std::abs (resulting - rounded));
                const int pitchClass = ((static_cast<int> (rounded)) % 12 + 12) % 12;
                if (((mask >> pitchClass) & 1u) == 0)
                    allOnScale = false;
            }
            expect (allOnScale);
            expect (worstError < 0.15f, "tuning error " + juce::String (worstError) + " st");
        }

        beginTest ("Quantised output measures at the expected frequency");
        {
            auto processor = makeProcessor();
            setParam (*processor, pid::mix, 100.0f);
            setParam (*processor, pid::scale, static_cast<float> (scaleMajor));
            setParam (*processor, pid::root, 0.0f);
            setParam (*processor, pid::pitch, 2.0f);      // A4 + 2 = B4, in C major
            setParam (*processor, pid::fine, 40.0f);      // detuned on purpose, must snap back to B4
            setParam (*processor, pid::density, 40.0f);
            setParam (*processor, pid::size, 200.0f);
            setParam (*processor, pid::jitter, 0.0f);
            int64_t position = 0;
            std::vector<float> output;
            run (*processor, sine (440.0, 48000.0), 48000, 512, position);
            run (*processor, sine (440.0, 48000.0), 48000 * 2, 512, position, &output);
            const float frequency = dominantFrequency (output, 48000.0);
            const float expected = 440.0f * std::pow (2.0f, 2.0f / 12.0f);
            expectWithinAbsoluteError (frequency, expected, expected * 0.01f);

            // Without quantisation the detune is audible in the measurement.
            auto raw = makeProcessor();
            setParam (*raw, pid::mix, 100.0f);
            setParam (*raw, pid::pitch, 2.0f);
            setParam (*raw, pid::fine, 40.0f);
            setParam (*raw, pid::density, 40.0f);
            setParam (*raw, pid::size, 200.0f);
            position = 0;
            output.clear();
            run (*raw, sine (440.0, 48000.0), 48000, 512, position);
            run (*raw, sine (440.0, 48000.0), 48000 * 2, 512, position, &output);
            const float detuned = 440.0f * std::pow (2.0f, 2.4f / 12.0f);
            expectWithinAbsoluteError (dominantFrequency (output, 48000.0), detuned, detuned * 0.01f);
        }
    }
};

// ---------------------------------------------------------------------------------------------
class ModulationTests final : public juce::UnitTest
{
public:
    ModulationTests() : juce::UnitTest ("Modulation", "granulr") {}

    void runTest() override
    {
        beginTest ("Every destination follows a macro through the matrix");
        {
            auto processor = makeProcessor();
            processor->getBridge().editorOpen.store (true);
            int64_t position = 0;
            for (int dest = 0; dest < numModDests; ++dest)
            {
                processor->getMatrix().clear();
                auto* param = processor->getState().getParameter (modDestParamId (dest));
                expect (param != nullptr);
                if (param == nullptr)
                    continue;
                param->setValueNotifyingHost (0.25f);
                const float base = param->getValue(); // may be snapped
                setParam (*processor, pid::macro (1), 100.0f);
                processor->getMatrix().setRouting (srcMacro1, dest, 0.5f);
                run (*processor, noise (0.2f), 9600, 512, position);
                const float modulated = processor->getBridge().destNorm[static_cast<size_t> (dest)].load();
                expectWithinAbsoluteError (modulated, juce::jlimit (0.0f, 1.0f, base + 0.5f), 0.02f,
                                           "destination " + modDestParamId (dest));
                expect (((processor->getBridge().destActiveMask.load() >> dest) & 1u) != 0);
            }
        }

        beginTest ("An LFO on the output gain moves the level smoothly");
        {
            auto processor = makeProcessor();
            setParam (*processor, pid::mix, 0.0f);
            setParam (*processor, pid::outputGain, -6.0f);
            setParam (*processor, pid::lfoRate (1), 2.0f);
            processor->getMatrix().setRouting (srcLfo1, dstOutputGain, 0.4f);
            int64_t position = 0;
            std::vector<float> output;
            const auto stats = run (*processor, sine (1000.0, 48000.0, 0.25f), 48000 * 2, 512, position, &output);
            float low = 10.0f, high = 0.0f;
            for (size_t start = 0; start + 2400 <= output.size(); start += 2400)
            {
                float peak = 0.0f;
                for (size_t i = start; i < start + 2400; ++i)
                    peak = std::max (peak, std::abs (output[i]));
                low = std::min (low, peak);
                high = std::max (high, peak);
            }
            expect (high > low * 3.0f, "level range " + juce::String (low) + " to " + juce::String (high));
            // The slope of a 1 kHz sine at this level is about 0.13 per sample at most.
            expect (stats.maxDelta < 0.2f, "step " + juce::String (stats.maxDelta));
        }

        beginTest ("LFO shapes stay within range");
        {
            ModulationEngine engine;
            engine.prepare (48000.0);
            ModSourceParams params;
            for (int shape = 0; shape < numLfoShapes; ++shape)
            {
                for (int n = 0; n < numLfos; ++n)
                {
                    params.lfoShape[n] = shape;
                    params.lfoRate[n] = 3.0f + static_cast<float> (n);
                }
                float low = 0.0f, high = 0.0f;
                for (int i = 0; i < 4000; ++i)
                {
                    engine.process (params, 32, 0.5f, 0.5f);
                    for (int s = 0; s < numModSources; ++s)
                    {
                        low = std::min (low, engine.value (s));
                        high = std::max (high, engine.value (s));
                    }
                }
                expect (low >= -1.0001f && high <= 1.0001f);
                expect (high > 0.5f && low < -0.5f);
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------
class StateTests final : public juce::UnitTest
{
public:
    StateTests() : juce::UnitTest ("State", "granulr") {}

    void runTest() override
    {
        beginTest ("Plugin state round trip restores every parameter and the matrix");
        {
            juce::Random random (11);
            auto source = makeProcessor();
            randomiseAllParams (*source, random);
            randomiseMatrix (*source, random);
            source->getUiState().setProperty ("zoom", 1.5, nullptr);

            juce::MemoryBlock block;
            source->getStateInformation (block);

            auto restored = makeProcessor();
            restored->setStateInformation (block.getData(), static_cast<int> (block.getSize()));

            bool identical = true;
            for (int i = 0; i < source->getParameters().size(); ++i)
                if (std::abs (source->getParameters()[i]->getValue() - restored->getParameters()[i]->getValue()) > 1.0e-5f)
                    identical = false;
            expect (identical);
            expect (source->getMatrix().toValueTree().isEquivalentTo (restored->getMatrix().toValueTree()));
            expectWithinAbsoluteError (static_cast<double> (restored->getUiState().getProperty ("zoom")), 1.5, 1.0e-9);

            // A second save produces the same bytes.
            juce::MemoryBlock again;
            restored->getStateInformation (again);
            expect (block == again);
        }

        beginTest ("Sound state round trip (presets)");
        {
            juce::Random random (21);
            auto processor = makeProcessor();
            randomiseAllParams (*processor, random);
            randomiseMatrix (*processor, random);
            const auto sound = processor->captureSoundState();
            const float bypassBefore = getParam (*processor, pid::bypass);

            randomiseAllParams (*processor, random);
            randomiseMatrix (*processor, random);
            setParam (*processor, pid::bypass, bypassBefore);
            const float qualityNow = getParam (*processor, pid::quality);
            processor->applySoundState (sound);

            const auto parameters = sound.getChildWithName ("PARAMETERS");
            bool identical = true;
            for (auto* base : processor->getParameters())
            {
                auto* param = dynamic_cast<juce::RangedAudioParameter*> (base);
                const auto identifier = param->getParameterID();
                if (identifier == pid::bypass || identifier == pid::quality)
                    continue;
                const float saved = parameters.getChildWithProperty ("id", identifier).getProperty ("value");
                if (std::abs (param->convertFrom0to1 (param->getValue()) - saved) > 1.0e-3f * (1.0f + std::abs (saved)))
                    identical = false;
            }
            expect (identical);
            expect (processor->getMatrix().toValueTree().isEquivalentTo (sound.getChildWithName ("MATRIX")));
            expectEquals (getParam (*processor, pid::quality), qualityNow);
        }

        beginTest ("Garbage state data is ignored");
        {
            auto processor = makeProcessor();
            const char garbage[] = "not a state";
            processor->setStateInformation (garbage, static_cast<int> (sizeof (garbage)));
            processor->setStateInformation (nullptr, 0);
            int64_t position = 0;
            expect (run (*processor, noise (0.5f), 4096, 512, position).finite);
        }

        beginTest ("A/B slots, morph and randomise keep the sound state consistent");
        {
            auto processor = makeProcessor();
            auto& presets = processor->getPresets();
            setParam (*processor, pid::size, 100.0f);
            presets.selectSlot (1);                       // B starts as a copy of A
            setParam (*processor, pid::size, 500.0f);     // edit B
            presets.selectSlot (0);
            expectWithinAbsoluteError (getParam (*processor, pid::size), 100.0f, 0.5f);
            presets.selectSlot (1);
            expectWithinAbsoluteError (getParam (*processor, pid::size), 500.0f, 0.5f);

            presets.beginMorph();
            presets.setMorph (0.0f);
            expectWithinAbsoluteError (getParam (*processor, pid::size), 100.0f, 0.5f);
            presets.setMorph (0.5f);
            const float middle = getParam (*processor, pid::size);
            expect (middle > 100.0f && middle < 500.0f);

            std::array<bool, numParamGroups> locks {};
            locks[groupPitch] = true;
            const float pitchBefore = getParam (*processor, pid::pitch);
            presets.randomise (1.0f, locks);
            expectEquals (getParam (*processor, pid::pitch), pitchBefore);
            expect (getParam (*processor, pid::feedback) <= 85.5f);
            int64_t position = 0;
            expect (run (*processor, noise (0.5f), 48000, 512, position).finite);
        }
    }
};

EngineTests engineTests;
PitchTests pitchTests;
ModulationTests modulationTests;
StateTests stateTests;

} // namespace
