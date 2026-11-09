// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "../state/PresetManager.h"

namespace grn
{

/** Preset browser panel: search, category filter, favourites, save and delete. */
class PresetBrowser final : public juce::Component,
                            private juce::ListBoxModel,
                            private juce::ChangeListener
{
public:
    explicit PresetBrowser (UiContext& context);
    ~PresetBrowser() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void paintOverChildren (juce::Graphics&) override;

    std::function<void()> onClose;

private:
    int getNumRows() override { return filtered.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuildFilter();
    void rebuildCategories();
    void save();

    UiContext& context;
    PresetManager& presets;
    juce::TextEditor search, nameEditor, categoryEditor;
    juce::ComboBox filterBox;
    juce::ListBox list;
    juce::TextButton saveButton { "Save" }, deleteButton { "Delete" }, folderButton { "Open folder" }, closeButton { "Close" };
    juce::Label status;
    juce::Array<int> filtered;
};

} // namespace grn
