// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "GlobalSettings.h"
#include "../params/ParameterIds.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

class GranulrProcessor;

namespace grn
{

/** Factory and user presets, A/B comparison with morph, and grouped randomisation.
    Message thread only. */
class PresetManager final : public juce::ChangeBroadcaster
{
public:
    struct Info
    {
        juce::String name, category;
        bool factory = false;
        juce::File file;          // user presets
        juce::ValueTree tree;     // parsed content
    };

    explicit PresetManager (GranulrProcessor& processor);

    static juce::File getUserDirectory();
    static juce::String getFileExtension() { return ".granulrpreset"; }

    void rescan();
    int getNumPresets() const { return presets.size(); }
    const Info& getInfo (int index) const { return presets.getReference (index); }
    juce::StringArray getCategories() const;

    void load (int index);
    void loadRelative (int delta);
    int getCurrentIndex() const { return currentIndex; }
    juce::String getCurrentName() const { return currentName; }

    /** Saves the current sound as a user preset. Returns an empty string on success, else the error. */
    juce::String saveUserPreset (const juce::String& name, const juce::String& category);
    bool deleteUserPreset (int index);

    bool isFavourite (const juce::String& name) const;
    void toggleFavourite (const juce::String& name);

    // A/B comparison
    int getActiveSlot() const { return activeSlot; }
    void selectSlot (int slot);
    void copyActiveToOther();
    void beginMorph();
    void setMorph (float amount);
    float getMorph() const { return morph; }

    /** Randomises the unlocked groups. amount 0..1 sets how far values may move. */
    void randomise (float amount, const std::array<bool, numParamGroups>& locked);

    /** Applies a sound state as one undoable action. */
    void applyUndoable (const juce::ValueTree& soundState, const juce::String& actionName);

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

    GlobalSettings& getSettings() { return *settings; }

private:
    GranulrProcessor& processor;
    juce::SharedResourcePointer<GlobalSettings> settings;
    juce::Array<Info> presets;
    int currentIndex = -1;
    juce::String currentName { "Init" };
    juce::ValueTree slots[2];
    int activeSlot = 0;
    float morph = 0.0f;
    juce::Random random;
};

} // namespace grn
