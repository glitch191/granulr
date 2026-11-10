// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../state/PresetManager.h"

using namespace grn;

GranulrProcessor::GranulrProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "PARAMETERS", createParameterLayout()),
      refs (apvts)
{
    bypassParam = apvts.getParameter (pid::bypass);
    presets = std::make_unique<PresetManager> (*this);
}

GranulrProcessor::~GranulrProcessor() = default;

bool GranulrProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GranulrProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    modulation.prepare (sampleRate);
    bridge.sampleRate.store (sampleRate);
    bypassMix = refs.bypass->load() > 0.5f ? 1.0f : 0.0f;
    lastInputPeak = lastOutputPeak = 0.0f;
    cpuSmoothed = 0.0f;
    internalPpq = 0.0;
}

void GranulrProcessor::reset()
{
    engine.reset();
    modulation.reset();
}

// ---------------------------------------------------------------------------------------------
void GranulrProcessor::readParams (EngineParams& ep, ModSourceParams& mp) const noexcept
{
    auto flag = [] (const std::atomic<float>* value) { return value->load (std::memory_order_relaxed) > 0.5f; };
    auto index = [] (const std::atomic<float>* value) { return static_cast<int> (value->load (std::memory_order_relaxed) + 0.5f); };
    auto real = [] (const std::atomic<float>* value) { return value->load (std::memory_order_relaxed); };

    ep.freeze = flag (refs.freeze);
    ep.positionSync = flag (refs.positionSync);
    ep.positionDiv = index (refs.positionDiv);
    ep.sizeSync = flag (refs.sizeSync);
    ep.sizeDiv = index (refs.sizeDiv);
    ep.densityDiv = index (refs.densityDiv);
    ep.triggerMode = index (refs.triggerMode);
    ep.windowShape = index (refs.windowShape);
    ep.duckAttackMs = real (refs.duckAttack);
    ep.duckReleaseMs = real (refs.duckRelease);
    ep.limiter = flag (refs.limiter);
    ep.quality = index (refs.quality);

    ep.scale = index (refs.scale);
    ep.root = index (refs.root);
    uint16_t custom = 0;
    for (int n = 0; n < 12; ++n)
        if (flag (refs.customNote[n]))
            custom = static_cast<uint16_t> (custom | (1u << n));
    ep.customMask = custom;

    ep.patternLength = index (refs.patternLength);
    ep.patternRate = index (refs.patternRate);
    uint16_t gates = 0;
    for (int s = 0; s < numPatternSteps; ++s)
    {
        if (flag (refs.stepGate[s]))
            gates = static_cast<uint16_t> (gates | (1u << s));
        ep.stepProb[s] = real (refs.stepProb[s]) * 0.01f;
        ep.stepRatchet[s] = static_cast<uint8_t> (index (refs.stepRatchet[s]));
    }
    ep.gateMask = gates;

    ep.filterOn = flag (refs.filterOn);
    ep.filterType = index (refs.filterType);
    ep.filterPlace = index (refs.filterPlace);
    ep.satOn = flag (refs.satOn);
    ep.satType = index (refs.satType);
    ep.satPlace = index (refs.satPlace);
    ep.diffOn = flag (refs.diffOn);
    ep.diffPlace = index (refs.diffPlace);
    ep.resOn = flag (refs.resOn);
    ep.resDamp = real (refs.resDamp) * 0.01f;
    ep.resOctave = static_cast<int> (std::round (real (refs.resOctave)));
    ep.resPlace = index (refs.resPlace);
    ep.onsetSens = real (refs.onsetSens) * 0.01f;
    ep.onsetBurst = index (refs.onsetBurst);

    mp.driftRate = real (refs.driftRate);
    mp.driftPull = real (refs.driftPull) * 0.01f;
    mp.chaosRate = real (refs.chaosRate);
    mp.chaosShape = real (refs.chaosShape) * 0.01f;
    mp.bounceRate = real (refs.bounceRate);
    mp.bounceDamp = real (refs.bounceDamp) * 0.01f;
    mp.gestureRate = real (refs.gestureRate);

    for (int n = 0; n < numLfos; ++n)
    {
        mp.lfoShape[n] = index (refs.lfoShape[n]);
        mp.lfoSync[n] = flag (refs.lfoSync[n]);
        mp.lfoDiv[n] = index (refs.lfoDiv[n]);
        mp.lfoPhase[n] = real (refs.lfoPhase[n]) / 360.0f;
        mp.lfoRetrig[n] = flag (refs.lfoRetrig[n]);
    }
    for (int n = 0; n < numRandoms; ++n)
    {
        mp.randSmooth[n] = real (refs.randSmooth[n]) * 0.01f;
        mp.randSync[n] = flag (refs.randSync[n]);
        mp.randDiv[n] = index (refs.randDiv[n]);
    }
    mp.envSource = index (refs.envSource);
    mp.envAttackMs = real (refs.envAttack);
    mp.envReleaseMs = real (refs.envRelease);
    mp.envGain = dbToGain (real (refs.envGain));
    for (int n = 0; n < numMacros; ++n)
        mp.macro[n] = real (refs.macro[n]) * 0.01f;
}

void GranulrProcessor::applyDestinations (const float* v, EngineParams& ep, ModSourceParams& mp) const noexcept
{
    ep.inputGain = dbToGain (v[dstInputGain]);
    ep.outputGain = dbToGain (v[dstOutputGain]);
    ep.mix = v[dstMix] * 0.01f;
    ep.positionMs = v[dstPosition];
    ep.scan = v[dstScan];
    ep.sizeMs = v[dstSize];
    ep.density = v[dstDensity];
    ep.sprayMs = v[dstSpray];
    ep.jitter = v[dstJitter] * 0.01f;
    ep.pitch = v[dstPitch] + v[dstFine] * 0.01f;
    ep.pitchRandom = v[dstPitchRandom];
    ep.reverse = v[dstReverse] * 0.01f;
    ep.windowSkew = v[dstWindowSkew] * 0.01f;
    ep.panSpread = v[dstPanSpread] * 0.01f;
    ep.width = v[dstWidth] * 0.01f;
    ep.feedback = v[dstFeedback] * 0.01f;
    ep.duckAmount = v[dstDuckAmount] * 0.01f;
    for (int n = 0; n < numHarmonyVoices; ++n)
    {
        ep.voiceInterval[n] = v[dstVoice1Interval + n];
        ep.voiceProb[n] = v[dstVoice1Prob + n] * 0.01f;
    }
    ep.octaveUp = v[dstOctaveUp] * 0.01f;
    ep.octaveDown = v[dstOctaveDown] * 0.01f;
    ep.filterCutoff = v[dstFilterCutoff];
    ep.filterReso = v[dstFilterReso] * 0.01f;
    ep.satDriveDb = v[dstSatDrive];
    ep.diffSize = v[dstDiffSize] * 0.01f;
    ep.diffDecay = v[dstDiffDecay] * 0.01f;
    ep.diffDamp = v[dstDiffDamp] * 0.01f;
    ep.diffMix = v[dstDiffMix] * 0.01f;
    for (int n = 0; n < numLfos; ++n)
        mp.lfoRate[n] = v[dstLfo1Rate + n];
    for (int n = 0; n < numRandoms; ++n)
        mp.randRate[n] = v[dstRand1Rate + n];
    ep.flock = v[dstFlock] * 0.01f;
    ep.evolve = v[dstEvolve] * 0.01f;
    ep.wear = v[dstWear] * 0.01f;
    ep.distance = v[dstDistance] * 0.01f;
    ep.orbit = v[dstOrbit] * 0.01f;
    ep.doppler = v[dstDoppler] * 0.01f;
    ep.resMix = v[dstResMix] * 0.01f;
    ep.resDecay = v[dstResDecay] * 0.01f;
}

// ---------------------------------------------------------------------------------------------
void GranulrProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // The host bypass is exposed as a parameter (getBypassParameter), so this is rarely called.
    if (buffer.getNumChannels() >= 2)
        engine.recordOnly (buffer.getReadPointer (0), buffer.getReadPointer (1), buffer.getNumSamples());
}

void GranulrProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return;

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);
    const bool editorOpen = bridge.editorOpen.load (std::memory_order_relaxed);

    // Transport.
    double bpm = 120.0, ppq = internalPpq;
    bool playing = false;
    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto position = hostPlayHead->getPosition())
        {
            bpm = position->getBpm().orFallback (120.0);
            playing = position->getIsPlaying();
            if (playing)
                ppq = position->getPpqPosition().orFallback (internalPpq);
        }
    }
    bpm = std::clamp (bpm, 20.0, 999.0);
    const double beatsPerSample = bpm / (60.0 * currentSampleRate);

    if (editorOpen)
    {
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            peakL = std::max (peakL, std::abs (left[i]));
            peakR = std::max (peakR, std::abs (right[i]));
        }
        GuiBridge::holdPeak (bridge.inputPeak[0], peakL);
        GuiBridge::holdPeak (bridge.inputPeak[1], peakR);
    }

    const bool bypassed = refs.bypass->load (std::memory_order_relaxed) > 0.5f;
    if (bypassed && bypassMix >= 1.0f)
    {
        // Fully bypassed: the signal passes through untouched, the buffer keeps recording.
        engine.recordOnly (left, right, numSamples);
        internalPpq = ppq + numSamples * beatsPerSample;
    }
    else
    {
        EngineParams ep;
        ModSourceParams mp;
        readParams (ep, mp);
        ep.bpm = mp.bpm = bpm;
        ep.playing = mp.playing = playing;

        float raw[numModDests];
        float values[numModDests];
        for (int d = 0; d < numModDests; ++d)
            raw[d] = refs.dest[d]->load (std::memory_order_relaxed);

        const float bypassStep = 1.0f / static_cast<float> (0.02 * currentSampleRate);

        for (int offset = 0; offset < numSamples; offset += GrainEngine::maxSubBlock)
        {
            const int n = std::min (GrainEngine::maxSubBlock, numSamples - offset);
            ep.ppq = mp.ppq = ppq + offset * beatsPerSample;

            // Modulation matrix.
            float offsets[numModDests] {};
            uint64_t activeMask = 0;
            for (const auto& slot : matrix.slots)
            {
                const int source = slot.source.load (std::memory_order_relaxed);
                const int dest = slot.dest.load (std::memory_order_relaxed);
                if (source < 0 || dest < 0 || source >= numModSources || dest >= numModDests)
                    continue;
                offsets[dest] += slot.depth.load (std::memory_order_relaxed) * modulation.value (source);
                activeMask |= uint64_t (1) << dest;
            }
            for (int d = 0; d < numModDests; ++d)
            {
                if ((activeMask >> d) & 1u)
                {
                    const auto& range = refs.destRange[d];
                    const float norm = std::clamp (range.convertTo0to1 (raw[d]) + offsets[d], 0.0f, 1.0f);
                    values[d] = range.convertFrom0to1 (norm);
                    if (editorOpen)
                        bridge.destNorm[static_cast<size_t> (d)].store (norm, std::memory_order_relaxed);
                }
                else
                {
                    values[d] = raw[d];
                }
            }
            applyDestinations (values, ep, mp);
            modulation.process (mp, n, lastInputPeak, lastOutputPeak, engine.getAnalysisValues());

            float dryL[GrainEngine::maxSubBlock], dryR[GrainEngine::maxSubBlock];
            const bool crossfading = bypassed || bypassMix > 0.0f;
            float inPeak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                inPeak = std::max (inPeak, std::max (std::abs (left[offset + i]), std::abs (right[offset + i])));
                if (crossfading)
                {
                    dryL[i] = left[offset + i];
                    dryR[i] = right[offset + i];
                }
            }

            engine.process (left + offset, right + offset, left + offset, right + offset, n, ep);

            float outPeak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                if (crossfading)
                {
                    bypassMix = std::clamp (bypassMix + (bypassed ? bypassStep : -bypassStep), 0.0f, 1.0f);
                    left[offset + i] += bypassMix * (dryL[i] - left[offset + i]);
                    right[offset + i] += bypassMix * (dryR[i] - right[offset + i]);
                }
                outPeak = std::max (outPeak, std::max (std::abs (left[offset + i]), std::abs (right[offset + i])));
            }
            lastInputPeak = inPeak;
            lastOutputPeak = outPeak;

            if (editorOpen)
                bridge.destActiveMask.store (activeMask, std::memory_order_relaxed);
        }
        internalPpq = ppq + numSamples * beatsPerSample;
    }

    // Editor feed.
    if (editorOpen)
    {
        bridge.pushOutput (left, right, numSamples);
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            peakL = std::max (peakL, std::abs (left[i]));
            peakR = std::max (peakR, std::abs (right[i]));
        }
        GuiBridge::holdPeak (bridge.outputPeak[0], peakL);
        GuiBridge::holdPeak (bridge.outputPeak[1], peakR);

        for (int s = 0; s < numModSources; ++s)
            bridge.sourceValue[static_cast<size_t> (s)].store (modulation.value (s), std::memory_order_relaxed);
        bridge.activeGrains.store (engine.getNumActiveGrains(), std::memory_order_relaxed);
        bridge.currentStep.store (engine.getCurrentStep(), std::memory_order_relaxed);
        bridge.limiting.store (engine.isLimiting(), std::memory_order_relaxed);
        bridge.idle.store (engine.isIdle(), std::memory_order_relaxed);
        bridge.hostPlaying.store (playing, std::memory_order_relaxed);
        const float note = engine.getPitchTracker().latestNote();
        bridge.detectedNote.store (std::isnan (note) ? -1.0f : note, std::memory_order_relaxed);

        // Grain snapshot, at most every 2 ms of audio.
        grainPublishCounter += numSamples;
        if (grainPublishCounter >= static_cast<int> (currentSampleRate * 0.002))
        {
            grainPublishCounter = 0;
            auto& frame = bridge.grains.writeBuffer();
            frame.count = engine.fillGrainViews (frame.views.data(), GrainFrame::maxViews);
            frame.frozen = engine.isFrozen();
            bridge.grains.publish();
        }
    }

    // CPU load: processing time divided by the duration of the block.
    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const auto load = static_cast<float> (elapsed * currentSampleRate / static_cast<double> (numSamples));
    cpuSmoothed += 0.05f * (load - cpuSmoothed);
    bridge.cpuAverage.store (cpuSmoothed, std::memory_order_relaxed);
    GuiBridge::holdPeak (bridge.cpuPeak, load);
}

// ---------------------------------------------------------------------------------------------
juce::ValueTree GranulrProcessor::captureSoundState() const
{
    juce::ValueTree sound ("SOUND");
    sound.addChild (const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState(), -1, nullptr);
    sound.addChild (matrix.toValueTree(), -1, nullptr);
    return sound;
}

void GranulrProcessor::applySoundState (const juce::ValueTree& soundState)
{
    const auto parameters = soundState.getChildWithName ("PARAMETERS");

    // Every parameter is set: the ones missing from the tree return to their default value.
    for (auto* base : getParameters())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (base);
        if (param == nullptr)
            continue;
        const auto identifier = param->getParameterID();
        if (identifier == pid::bypass || identifier == pid::quality)
            continue;

        float normalised = param->getDefaultValue();
        const auto child = parameters.getChildWithProperty ("id", identifier);
        if (child.isValid())
            normalised = param->convertTo0to1 (static_cast<float> (child.getProperty ("value")));
        normalised = std::clamp (normalised, 0.0f, 1.0f);

        if (std::abs (param->getValue() - normalised) > 1.0e-7f)
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (normalised);
            param->endChangeGesture();
        }
    }

    matrix.fromValueTree (soundState.getChildWithName ("MATRIX"));
}

void GranulrProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("GRANULR");
    root.setProperty ("stateVersion", stateVersion, nullptr);
    root.setProperty ("pluginVersion", GRANULR_VERSION_STRING, nullptr);
    root.addChild (apvts.copyState(), -1, nullptr);
    root.addChild (matrix.toValueTree(), -1, nullptr);
    root.addChild (uiState.createCopy(), -1, nullptr);
    root.addChild (presets->toValueTree(), -1, nullptr);

    if (const auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void GranulrProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("GRANULR"))
        return;

    // Version 1 is the first format. Later versions migrate older trees here before use.
    const int version = root.getProperty ("stateVersion", 1);
    juce::ignoreUnused (version);

    const auto parameters = root.getChildWithName ("PARAMETERS");
    if (parameters.isValid())
        apvts.replaceState (parameters.createCopy());
    matrix.fromValueTree (root.getChildWithName ("MATRIX"));

    const auto ui = root.getChildWithName ("UI");
    if (ui.isValid())
        uiState.copyPropertiesFrom (ui, nullptr);

    presets->fromValueTree (root.getChildWithName ("PRESETSTATE"));
    undoManager.clearUndoHistory();
}

juce::AudioProcessorEditor* GranulrProcessor::createEditor()
{
    return new GranulrEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GranulrProcessor();
}
