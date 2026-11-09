// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../dsp/GrainEngine.h"
#include "../mod/Modulation.h"
#include "../params/Params.h"
#include "../shared/GuiBridge.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace grn
{
class PresetManager;
}

class GranulrProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int stateVersion = 1;

    GranulrProcessor();
    ~GranulrProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "granulr"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Sound state only (parameters and modulation matrix), used by presets and A/B. */
    juce::ValueTree captureSoundState() const;
    void applySoundState (const juce::ValueTree& soundState);

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    grn::ModMatrix& getMatrix() noexcept { return matrix; }
    grn::GuiBridge& getBridge() noexcept { return bridge; }
    grn::GrainEngine& getEngine() noexcept { return engine; }
    grn::PresetManager& getPresets() noexcept { return *presets; }

    /** Editor settings saved with the plugin state (message thread only). */
    juce::ValueTree& getUiState() noexcept { return uiState; }

private:
    void readParams (grn::EngineParams& ep, grn::ModSourceParams& mp) const noexcept;
    void applyDestinations (const float* values, grn::EngineParams& ep, grn::ModSourceParams& mp) const noexcept;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    grn::ParamRefs refs;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    grn::ModMatrix matrix;
    grn::ModulationEngine modulation;
    grn::GrainEngine engine;
    grn::GuiBridge bridge;
    std::unique_ptr<grn::PresetManager> presets;
    juce::ValueTree uiState { "UI" };

    double currentSampleRate = 48000.0;
    float bypassMix = 0.0f;         // 0 = processed, 1 = bypassed
    float lastInputPeak = 0.0f, lastOutputPeak = 0.0f;
    float cpuSmoothed = 0.0f;
    double internalPpq = 0.0;
    int grainPublishCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GranulrProcessor)
};
