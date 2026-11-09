// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PresetBrowser.h"

namespace grn
{
namespace
{
    constexpr int kFavouriteWidth = 28;
    constexpr int kFilterAll = 1, kFilterFavourites = 2, kFilterFirstCategory = 10;
}

PresetBrowser::PresetBrowser (UiContext& c) : context (c), presets (c.processor.getPresets())
{
    search.setTextToShowWhenEmpty ("Search presets", Theme::get().textFaint);
    search.setFont (Fonts::body());
    search.onTextChange = [this] { rebuildFilter(); };
    search.onEscapeKey = [this] { if (onClose) onClose(); };
    addAndMakeVisible (search);

    filterBox.setTooltip ("Show every preset, only favourites, or one category.");
    filterBox.onChange = [this] { rebuildFilter(); };
    addAndMakeVisible (filterBox);

    list.setModel (this);
    list.setRowHeight (24);
    list.setOutlineThickness (1);
    list.setColour (juce::ListBox::outlineColourId, Theme::get().border);
    addAndMakeVisible (list);

    nameEditor.setTextToShowWhenEmpty ("Preset name", Theme::get().textFaint);
    nameEditor.setFont (Fonts::body());
    nameEditor.onReturnKey = [this] { save(); };
    addAndMakeVisible (nameEditor);
    categoryEditor.setTextToShowWhenEmpty ("Category", Theme::get().textFaint);
    categoryEditor.setFont (Fonts::body());
    categoryEditor.onReturnKey = [this] { save(); };
    addAndMakeVisible (categoryEditor);

    saveButton.setTooltip ("Save the current sound as a user preset with this name and category.");
    saveButton.onClick = [this] { save(); };
    addAndMakeVisible (saveButton);

    deleteButton.setTooltip ("Delete the selected user preset. Factory presets cannot be deleted.");
    deleteButton.onClick = [this]
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= filtered.size())
            return;
        const int index = filtered[row];
        if (presets.getInfo (index).factory)
            status.setText ("Factory presets cannot be deleted.", juce::dontSendNotification);
        else if (presets.deleteUserPreset (index))
            status.setText ("Preset deleted.", juce::dontSendNotification);
        else
            status.setText ("Could not delete the preset file.", juce::dontSendNotification);
    };
    addAndMakeVisible (deleteButton);

    folderButton.setTooltip ("Open the folder that holds your user presets.");
    folderButton.onClick = []
    {
        const auto directory = PresetManager::getUserDirectory();
        directory.createDirectory();
        directory.revealToUser();
    };
    addAndMakeVisible (folderButton);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    status.setFont (Fonts::small());
    status.setColour (juce::Label::textColourId, Theme::get().textDim);
    addAndMakeVisible (status);

    presets.addChangeListener (this);
    rebuildCategories();
    rebuildFilter();
}

PresetBrowser::~PresetBrowser()
{
    presets.removeChangeListener (this);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    g.setColour (t.panelRaised);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), t.radius);
    g.setColour (t.border);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), t.radius, 1.0f);
}

void PresetBrowser::paintOverChildren (juce::Graphics& g)
{
    const auto& t = Theme::get();
    if (filtered.isEmpty())
    {
        g.setColour (t.textFaint);
        g.setFont (Fonts::body());
        g.drawFittedText (presets.getNumPresets() == 0 ? "No preset yet. Type a name below and press Save."
                                                        : "No preset matches. Clear the search or change the filter.",
                          list.getBounds().reduced (12), juce::Justification::centred, 2);
    }
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto top = area.removeFromTop (26);
    closeButton.setBounds (top.removeFromRight (60));
    top.removeFromRight (6);
    filterBox.setBounds (top.removeFromRight (150));
    top.removeFromRight (6);
    search.setBounds (top);
    area.removeFromTop (8);

    auto bottom = area.removeFromBottom (26);
    folderButton.setBounds (bottom.removeFromRight (96));
    bottom.removeFromRight (6);
    deleteButton.setBounds (bottom.removeFromRight (64));
    bottom.removeFromRight (6);
    saveButton.setBounds (bottom.removeFromRight (56));
    bottom.removeFromRight (6);
    categoryEditor.setBounds (bottom.removeFromRight (130));
    bottom.removeFromRight (6);
    nameEditor.setBounds (bottom);
    area.removeFromBottom (4);
    status.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (4);
    list.setBounds (area);
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        status.setText ({}, juce::dontSendNotification);
        nameEditor.setText (presets.getCurrentName(), false);
        const int current = filtered.indexOf (presets.getCurrentIndex());
        list.selectRow (current, true, true);
        search.grabKeyboardFocus();
    }
}

void PresetBrowser::rebuildCategories()
{
    const int previous = filterBox.getSelectedId();
    const auto previousText = filterBox.getText();
    filterBox.clear (juce::dontSendNotification);
    filterBox.addItem ("All presets", kFilterAll);
    filterBox.addItem ("Favourites", kFilterFavourites);
    const auto categories = presets.getCategories();
    for (int i = 0; i < categories.size(); ++i)
        filterBox.addItem (categories[i], kFilterFirstCategory + i);
    int restored = kFilterAll;
    if (previous == kFilterFavourites)
        restored = kFilterFavourites;
    else if (previous >= kFilterFirstCategory && categories.contains (previousText))
        restored = kFilterFirstCategory + categories.indexOf (previousText);
    filterBox.setSelectedId (restored, juce::dontSendNotification);
}

void PresetBrowser::rebuildFilter()
{
    filtered.clearQuick();
    const auto query = search.getText().trim();
    const int filter = filterBox.getSelectedId();
    const auto category = filterBox.getText();
    for (int i = 0; i < presets.getNumPresets(); ++i)
    {
        const auto& info = presets.getInfo (i);
        if (query.isNotEmpty() && ! info.name.containsIgnoreCase (query) && ! info.category.containsIgnoreCase (query))
            continue;
        if (filter == kFilterFavourites && ! presets.isFavourite (info.name))
            continue;
        if (filter >= kFilterFirstCategory && info.category != category)
            continue;
        filtered.add (i);
    }
    list.updateContent();
    list.selectRow (filtered.indexOf (presets.getCurrentIndex()), true, true);
    list.repaint();
    repaint();
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= filtered.size())
        return;
    const auto& t = Theme::get();
    const auto& info = presets.getInfo (filtered[row]);
    if (selected)
    {
        g.setColour (t.accentDim);
        g.fillRect (0, 0, width, height);
    }

    juce::Path star;
    star.addStar ({ static_cast<float> (width) - kFavouriteWidth * 0.5f, static_cast<float> (height) * 0.5f }, 5, 3.0f, 6.5f);
    if (presets.isFavourite (info.name))
    {
        g.setColour (t.warning);
        g.fillPath (star);
    }
    else
    {
        g.setColour (t.textFaint.withAlpha (0.6f));
        g.strokePath (star, juce::PathStrokeType (1.0f));
    }

    g.setFont (Fonts::body());
    g.setColour (t.text);
    g.drawFittedText (info.name, 8, 0, width - kFavouriteWidth - 150, height, juce::Justification::centredLeft, 1, 0.85f);
    g.setFont (Fonts::small());
    g.setColour (t.textDim);
    g.drawFittedText (info.category + (info.factory ? "" : " (user)"), width - kFavouriteWidth - 140, 0, 136, height,
                      juce::Justification::centredRight, 1, 0.85f);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= filtered.size())
        return;
    const int index = filtered[row];
    if (e.x >= list.getVisibleRowWidth() - kFavouriteWidth)
    {
        presets.toggleFavourite (presets.getInfo (index).name);
        return;
    }
    presets.load (index);
    nameEditor.setText (presets.getCurrentName(), false);
    categoryEditor.setText (presets.getInfo (index).factory ? juce::String() : presets.getInfo (index).category, false);
}

void PresetBrowser::returnKeyPressed (int row)
{
    if (row >= 0 && row < filtered.size())
        presets.load (filtered[row]);
}

void PresetBrowser::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildCategories();
    rebuildFilter();
}

void PresetBrowser::save()
{
    const auto error = presets.saveUserPreset (nameEditor.getText(), categoryEditor.getText());
    status.setText (error.isEmpty() ? "Preset saved." : error, juce::dontSendNotification);
}

} // namespace grn
