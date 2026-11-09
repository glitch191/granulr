// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "LookAndFeel.h"
#include "../plugin/PluginProcessor.h"

namespace grn
{

/** State shared by the widgets of one editor. */
struct UiContext
{
    explicit UiContext (GranulrProcessor& p) : processor (p) {}

    GranulrProcessor& processor;
    int selectedSource = -1;                 // modulation source selected in the modulation panel
    std::function<void()> onSelectionChanged;

    void selectSource (int source)
    {
        selectedSource = source;
        if (onSelectionChanged)
            onSelectionChanged();
    }
};

/** Index of the modulation destination driven by a parameter, or -1. */
int modDestForParam (const juce::String& paramId);

// ---------------------------------------------------------------------------------------------
/** Rotary control bound to a parameter, with modulation rings and drag and drop assignment. */
class Knob final : public juce::Slider, public juce::DragAndDropTarget
{
public:
    Knob (UiContext& context, const juce::String& paramId, const juce::String& label, bool small = false);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** Called once per display frame: repaints only when the modulation picture changed. */
    void refreshModulation();

    static int heightFor (bool small);
    static int widthFor (bool small);

private:
    void showMenu();
    void showValueEditor();
    juce::String valueText() const;

    UiContext& context;
    juce::RangedAudioParameter* param = nullptr;
    juce::String label;
    bool small = false, hovering = false, dragging = false, dropHover = false, depthDrag = false;
    float depthStart = 0.0f;
    int dest = -1;
    float shownModNorm = -1.0f;
    uint32_t shownMatrixVersion = 0;
    int shownSelection = -2;
    std::unique_ptr<juce::TextEditor> valueEditor;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// ---------------------------------------------------------------------------------------------
class ParamButton final : public juce::TextButton
{
public:
    ParamButton (UiContext& context, const juce::String& paramId, const juce::String& text);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

class ParamChoice final : public juce::ComboBox
{
public:
    ParamChoice (UiContext& context, const juce::String& paramId);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// ---------------------------------------------------------------------------------------------
/** 16-step pattern editor: gate, chance and ratchet per step. */
class StepGrid final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StepGrid (UiContext& context);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();

private:
    enum Zone { zoneGate, zoneChance, zoneRatchet };
    juce::Rectangle<float> cell (int step, Zone zone) const;
    int stepAt (juce::Point<float>) const;
    void setChanceFromMouse (const juce::MouseEvent&);

    UiContext& context;
    juce::RangedAudioParameter* gates[numPatternSteps] {};
    juce::RangedAudioParameter* chances[numPatternSteps] {};
    juce::RangedAudioParameter* ratchets[numPatternSteps] {};
    juce::RangedAudioParameter* lengthParam = nullptr;
    juce::RangedAudioParameter* modeParam = nullptr;
    int dragStep = -1;
    uint64_t shownHash = 0;
};

/** Twelve pitch classes: shows the active scale, and edits the custom scale. */
class NoteStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit NoteStrip (UiContext& context);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();

private:
    UiContext& context;
    juce::RangedAudioParameter* notes[12] {};
    uint32_t shownHash = 0xffffffffu;
};

// ---------------------------------------------------------------------------------------------
/** A modulation source: click to select it, drag it onto a knob to assign it. */
class ModSourceChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModSourceChip (UiContext& context, int source);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void refresh();

private:
    UiContext& context;
    int source;
    float shownValue = 0.0f;
    bool shownSelected = false;
};

/** List of the active modulation routings, with depth sliders. */
class ModMatrixView final : public juce::Component
{
public:
    explicit ModMatrixView (UiContext& context);
    ~ModMatrixView() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();

private:
    struct Row;
    void rebuild();

    UiContext& context;
    juce::ComboBox sourceBox, destBox;
    juce::TextButton addButton { "Add" };
    juce::Viewport viewport;
    juce::Component list;
    juce::OwnedArray<Row> rows;
    uint32_t shownVersion = 0xffffffffu;
};

/** Titled panel that frames a group of controls. */
class Section : public juce::Component
{
public:
    explicit Section (const juce::String& titleText) : title (titleText) {}

    void paint (juce::Graphics&) override;
    juce::Rectangle<int> contentArea() const;

    juce::String title;
};

} // namespace grn
