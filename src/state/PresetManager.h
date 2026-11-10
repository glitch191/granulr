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

    /** A variation close to a sound state: small moves of the unlocked groups, same routings. */
    juce::ValueTree mutatedState (const juce::ValueTree& soundState, float amount,
                                  const std::array<bool, numParamGroups>& locked);
    /** A child of two sound states: each group mostly follows one parent, with some blending. */
    juce::ValueTree crossedState (const juce::ValueTree& parentA, const juce::ValueTree& parentB);

    /** Applies a variation of the current sound (undoable). */
    void mutate (float amount, const std::array<bool, numParamGroups>& locked);
    /** Crosses slots A and B into the active slot (undoable). False when a slot is empty. */
    bool breed();
    bool canBreed() const { return slots[0].isValid() || slots[1].isValid(); }

    /** Six variations of the current sound to audition one after the other. */
    static constexpr int numVariations = 6;
    void makeVariations (float amount, const std::array<bool, numParamGroups>& locked);
    bool hasVariations() const { return variationOrigin.isValid(); }
    /** index 0..5 loads a variation, -1 goes back to the sound they were made from. */
    void loadVariation (int index);

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
    juce::ValueTree variationOrigin, variations[numVariations];
};

} // namespace grn
