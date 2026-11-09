// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PresetManager.h"
#include "../params/Params.h"
#include "../plugin/PluginProcessor.h"

#if GRANULR_HAS_BINARY_DATA
 #include "BinaryData.h"
#endif

namespace grn
{
namespace
{
    constexpr int presetFormatVersion = 1;

    /** Factory presets are parsed once per process and shared by every instance. */
    struct FactoryBank
    {
        FactoryBank()
        {
           #if GRANULR_HAS_BINARY_DATA
            for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            {
                const juce::String original (BinaryData::originalFilenames[i]);
                if (! original.endsWithIgnoreCase (".xml"))
                    continue;
                int size = 0;
                const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
                if (data == nullptr || size <= 0)
                    continue;
                const auto xml = juce::parseXML (juce::String::fromUTF8 (data, size));
                if (xml == nullptr || ! xml->hasTagName ("GranulrPreset"))
                    continue;
                PresetManager::Info info;
                info.tree = juce::ValueTree::fromXml (*xml);
                info.name = info.tree.getProperty ("name", original.upToLastOccurrenceOf (".", false, false)).toString();
                info.category = info.tree.getProperty ("category", "Factory").toString();
                info.factory = true;
                items.add (info);
            }
           #endif
        }

        juce::Array<PresetManager::Info> items;
    };

    struct MatrixAction final : juce::UndoableAction
    {
        MatrixAction (ModMatrix& m, juce::ValueTree b, juce::ValueTree a)
            : matrix (m), before (std::move (b)), after (std::move (a)) {}

        bool perform() override { matrix.fromValueTree (after); return true; }
        bool undo() override    { matrix.fromValueTree (before); return true; }

        ModMatrix& matrix;
        juce::ValueTree before, after;
    };

    float normalisedFromTree (const juce::ValueTree& parameters, juce::RangedAudioParameter& param)
    {
        const auto child = parameters.getChildWithProperty ("id", param.getParameterID());
        if (! child.isValid())
            return param.getDefaultValue();
        return juce::jlimit (0.0f, 1.0f, param.convertTo0to1 (static_cast<float> (child.getProperty ("value"))));
    }

    void setParam (juce::RangedAudioParameter& param, float normalised)
    {
        if (std::abs (param.getValue() - normalised) < 1.0e-7f)
            return;
        param.beginChangeGesture();
        param.setValueNotifyingHost (normalised);
        param.endChangeGesture();
    }
}

PresetManager::PresetManager (GranulrProcessor& p) : processor (p)
{
    rescan();
}

juce::File PresetManager::getUserDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("granulr")
        .getChildFile ("Presets");
}

void PresetManager::rescan()
{
    presets.clearQuick();

    juce::SharedResourcePointer<FactoryBank> bank;
    presets.addArray (bank->items);

    const auto directory = getUserDirectory();
    if (directory.isDirectory())
    {
        for (const auto& file : directory.findChildFiles (juce::File::findFiles, true, "*" + getFileExtension()))
        {
            const auto xml = juce::parseXML (file);
            if (xml == nullptr || ! xml->hasTagName ("GranulrPreset"))
                continue;
            Info info;
            info.tree = juce::ValueTree::fromXml (*xml);
            info.name = info.tree.getProperty ("name", file.getFileNameWithoutExtension()).toString();
            info.category = info.tree.getProperty ("category", "User").toString();
            info.factory = false;
            info.file = file;
            presets.add (info);
        }
    }

    std::stable_sort (presets.begin(), presets.end(), [] (const Info& a, const Info& b)
    {
        if (a.category != b.category)
            return a.category.compareIgnoreCase (b.category) < 0;
        return a.name.compareIgnoreCase (b.name) < 0;
    });

    currentIndex = -1;
    for (int i = 0; i < presets.size(); ++i)
        if (presets.getReference (i).name == currentName)
            currentIndex = i;

    sendChangeMessage();
}

juce::StringArray PresetManager::getCategories() const
{
    juce::StringArray categories;
    for (const auto& info : presets)
        categories.addIfNotAlreadyThere (info.category);
    return categories;
}

void PresetManager::applyUndoable (const juce::ValueTree& soundState, const juce::String& actionName)
{
    auto& undo = processor.getUndoManager();
    undo.beginNewTransaction (actionName);
    const auto matrixBefore = processor.getMatrix().toValueTree();
    processor.applySoundState (soundState);
    undo.perform (new MatrixAction (processor.getMatrix(), matrixBefore, processor.getMatrix().toValueTree()));
    // Flush the parameter changes into the state tree now, inside this transaction.
    processor.getState().copyState();
    undo.beginNewTransaction();
}

void PresetManager::load (int index)
{
    if (index < 0 || index >= presets.size())
        return;
    const auto& info = presets.getReference (index);
    applyUndoable (info.tree, "Load preset");
    currentIndex = index;
    currentName = info.name;
    slots[0] = slots[1] = juce::ValueTree();
    activeSlot = 0;
    morph = 0.0f;
    sendChangeMessage();
}

void PresetManager::loadRelative (int delta)
{
    if (presets.isEmpty())
        return;
    const int count = presets.size();
    const int base = currentIndex < 0 ? (delta > 0 ? -1 : 0) : currentIndex;
    load (((base + delta) % count + count) % count);
}

juce::String PresetManager::saveUserPreset (const juce::String& name, const juce::String& category)
{
    const auto cleanName = name.trim();
    if (cleanName.isEmpty())
        return "Enter a preset name.";

    const auto directory = getUserDirectory();
    if (! directory.isDirectory() && ! directory.createDirectory())
        return "Could not create the preset folder: " + directory.getFullPathName();

    const auto sound = processor.captureSoundState();
    juce::ValueTree preset ("GranulrPreset");
    preset.setProperty ("name", cleanName, nullptr);
    preset.setProperty ("category", category.trim().isEmpty() ? juce::String ("User") : category.trim(), nullptr);
    preset.setProperty ("formatVersion", presetFormatVersion, nullptr);
    for (const auto& child : sound)
        preset.addChild (child.createCopy(), -1, nullptr);

    const auto file = directory.getChildFile (juce::File::createLegalFileName (cleanName) + getFileExtension());
    const auto xml = preset.createXml();
    if (xml == nullptr || ! xml->writeTo (file))
        return "Could not write the preset file: " + file.getFullPathName();

    currentName = cleanName;
    rescan();
    return {};
}

bool PresetManager::deleteUserPreset (int index)
{
    if (index < 0 || index >= presets.size() || presets.getReference (index).factory)
        return false;
    const bool deleted = presets.getReference (index).file.deleteFile();
    rescan();
    return deleted;
}

bool PresetManager::isFavourite (const juce::String& name) const
{
    return settings->getFavourites().contains (name);
}

void PresetManager::toggleFavourite (const juce::String& name)
{
    auto favourites = settings->getFavourites();
    if (favourites.contains (name))
        favourites.removeString (name);
    else
        favourites.add (name);
    settings->setFavourites (favourites);
    sendChangeMessage();
}

// ---------------------------------------------------------------------------------------------
void PresetManager::selectSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == activeSlot)
        return;
    slots[activeSlot] = processor.captureSoundState();
    if (! slots[slot].isValid())
        slots[slot] = slots[activeSlot].createCopy();
    applyUndoable (slots[slot], slot == 0 ? "Recall A" : "Recall B");
    activeSlot = slot;
    morph = slot == 0 ? 0.0f : 1.0f;
    sendChangeMessage();
}

void PresetManager::copyActiveToOther()
{
    slots[activeSlot] = processor.captureSoundState();
    slots[1 - activeSlot] = slots[activeSlot].createCopy();
    sendChangeMessage();
}

void PresetManager::beginMorph()
{
    // The slot that matches the current end of the morph is refreshed from the live sound.
    if (morph <= 0.0f || morph >= 1.0f || ! slots[0].isValid() || ! slots[1].isValid())
    {
        const int live = morph >= 1.0f ? 1 : 0;
        slots[live] = processor.captureSoundState();
        if (! slots[1 - live].isValid())
            slots[1 - live] = slots[live].createCopy();
    }
    processor.getUndoManager().beginNewTransaction ("Morph");
}

void PresetManager::setMorph (float amount)
{
    morph = juce::jlimit (0.0f, 1.0f, amount);
    if (! slots[0].isValid() || ! slots[1].isValid())
        return;

    const auto paramsA = slots[0].getChildWithName ("PARAMETERS");
    const auto paramsB = slots[1].getChildWithName ("PARAMETERS");
    for (auto* base : processor.getParameters())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (base);
        if (param == nullptr)
            continue;
        const auto identifier = param->getParameterID();
        if (identifier == pid::bypass || identifier == pid::quality)
            continue;
        const float a = normalisedFromTree (paramsA, *param);
        const float b = normalisedFromTree (paramsB, *param);
        setParam (*param, isMorphable (*param) ? a + morph * (b - a) : (morph < 0.5f ? a : b));
    }
    processor.getMatrix().fromValueTree (slots[morph < 0.5f ? 0 : 1].getChildWithName ("MATRIX"));
    activeSlot = morph < 0.5f ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------
void PresetManager::randomise (float amount, const std::array<bool, numParamGroups>& locked)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    auto& undo = processor.getUndoManager();
    undo.beginNewTransaction ("Randomize");

    for (auto* base : processor.getParameters())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (base);
        if (param == nullptr)
            continue;
        const auto identifier = param->getParameterID();
        const auto group = paramGroupOf (identifier);
        if (group == groupNone || locked[static_cast<size_t> (group)])
            continue;

        float value = param->getValue();
        if (isMorphable (*param))
        {
            value = amount >= 0.99f ? random.nextFloat()
                                    : juce::jlimit (0.0f, 1.0f, value + amount * (random.nextFloat() * 2.0f - 1.0f));
            if (identifier == pid::feedback)
                value = juce::jmin (value, 0.85f);
            // Whole semitones for pitch-like parameters.
            value = param->convertTo0to1 (param->getNormalisableRange().snapToLegalValue (param->convertFrom0to1 (value)));
        }
        else if (random.nextFloat() < amount * 0.5f)
        {
            const int steps = juce::jmax (2, param->getNumSteps());
            value = static_cast<float> (random.nextInt (steps)) / static_cast<float> (steps - 1);
        }
        setParam (*param, value);
    }

    if (! locked[groupModulation])
    {
        auto& matrix = processor.getMatrix();
        const auto before = matrix.toValueTree();
        matrix.clear();
        static const int musicalDests[] { dstPosition, dstSize, dstDensity, dstSpray, dstPitchRandom, dstReverse,
                                          dstWindowSkew, dstPanSpread, dstWidth, dstFeedback, dstFilterCutoff,
                                          dstFilterReso, dstDiffSize, dstDiffMix, dstOctaveUp, dstScan };
        const int routes = juce::roundToInt (amount * 6.0f);
        for (int r = 0; r < routes; ++r)
        {
            const int source = random.nextInt (srcEnv + 1);
            const int dest = musicalDests[random.nextInt (static_cast<int> (std::size (musicalDests)))];
            const float depth = (random.nextFloat() * 2.0f - 1.0f) * (0.1f + 0.35f * amount);
            matrix.setRouting (source, dest, depth);
        }
        undo.perform (new MatrixAction (matrix, before, matrix.toValueTree()));
    }

    processor.getState().copyState();
    undo.beginNewTransaction();
    sendChangeMessage();
}

// ---------------------------------------------------------------------------------------------
juce::ValueTree PresetManager::toValueTree() const
{
    juce::ValueTree tree ("PRESETSTATE");
    tree.setProperty ("name", currentName, nullptr);
    tree.setProperty ("activeSlot", activeSlot, nullptr);
    tree.setProperty ("morph", morph, nullptr);
    for (int s = 0; s < 2; ++s)
    {
        if (! slots[s].isValid())
            continue;
        juce::ValueTree slot (s == 0 ? "SLOTA" : "SLOTB");
        slot.addChild (slots[s].createCopy(), -1, nullptr);
        tree.addChild (slot, -1, nullptr);
    }
    return tree;
}

void PresetManager::fromValueTree (const juce::ValueTree& tree)
{
    slots[0] = slots[1] = juce::ValueTree();
    activeSlot = 0;
    morph = 0.0f;
    if (tree.isValid())
    {
        currentName = tree.getProperty ("name", "Init").toString();
        activeSlot = juce::jlimit (0, 1, static_cast<int> (tree.getProperty ("activeSlot", 0)));
        morph = juce::jlimit (0.0f, 1.0f, static_cast<float> (tree.getProperty ("morph", 0.0f)));
        const auto a = tree.getChildWithName ("SLOTA");
        const auto b = tree.getChildWithName ("SLOTB");
        if (a.isValid() && a.getNumChildren() > 0)
            slots[0] = a.getChild (0).createCopy();
        if (b.isValid() && b.getNumChildren() > 0)
            slots[1] = b.getChild (0).createCopy();
    }
    currentIndex = -1;
    for (int i = 0; i < presets.size(); ++i)
        if (presets.getReference (i).name == currentName)
            currentIndex = i;
    sendChangeMessage();
}

} // namespace grn
