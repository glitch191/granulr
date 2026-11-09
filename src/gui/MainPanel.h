// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PresetBrowser.h"
#include "Visualizer.h"
#include "Widgets.h"

namespace grn
{

/** The whole interface at 100 % zoom. The editor scales it with a transform. */
class MainPanel final : public juce::Component,
                        public juce::DragAndDropContainer,
                        private juce::ChangeListener
{
public:
    static constexpr int defaultWidth = 1180, defaultHeight = 740;
    static constexpr int minWidth = 1100, minHeight = 520;
    static constexpr int maxWidth = 2400, maxHeight = 1600;

    MainPanel (UiContext& context, VisualEngine& engine);
    ~MainPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called once per display frame on the message thread. */
    void refresh (double nowSeconds);

    juce::Rectangle<int> grainPlot() const;
    juce::Rectangle<int> outputPlot() const;
    int getVisualMode() const { return outputView.getMode(); }
    void setSoftwareRendering (bool software);
    void setZoomDisplay (float zoom);
    void setDiagnostics (bool visible, const juce::String& text);
    void repaintVisuals();
    void showBrowser (bool shouldShow);

    std::function<void (float)> onZoomChosen;
    std::function<void (juce::Component&)> onSettingsMenu;
    std::function<void()> onLayoutChanged;

private:
    class SourceEditor;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    Knob& addKnob (juce::Component& parent, const juce::String& paramId, const juce::String& label, bool small = false);
    ParamChoice& addChoice (juce::Component& parent, const juce::String& paramId);
    ParamButton& addButton (juce::Component& parent, const juce::String& paramId, const juce::String& text);
    juce::Label& addCaption (juce::Component& parent, const juce::String& text);
    void layoutHeader (juce::Rectangle<int> area);
    void layoutGrains();
    void layoutOutput();
    void layoutPitch();
    void layoutTrigger();
    void layoutEffects();
    void layoutModulation();
    void showRandomMenu();
    void updatePresetName();
    void updateSourceEditors();
    void updateEnablement();

    UiContext& context;
    GranulrProcessor& processor;
    bool softwareRendering = false;
    bool modOpen = true;
    double lastSlowRefresh = 0.0;
    bool shownFrozen = false;

    // Header
    juce::Label titleLabel, cpuLabel;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, slotA { "A" }, slotB { "B" }, copyButton { "Copy" },
        randomButton { "Randomize" }, randomOptions { "..." }, undoButton { "Undo" }, redoButton { "Redo" },
        modButton { "Mod" }, settingsButton { "Settings" };
    juce::Slider morphSlider;
    juce::ComboBox zoomBox;
    std::unique_ptr<ParamChoice> qualityChoice;
    std::unique_ptr<ParamButton> bypassButton;
    float randomAmount = 0.5f;
    std::array<bool, numParamGroups> randomLocks {};

    // Visualisers
    VisualView grainView, outputView;
    juce::Label diagnostics;

    // Sections
    Section grainsSection { "Grains" }, outputSection { "Output" }, pitchSection { "Pitch" },
        triggerSection { "Trigger" }, effectsSection { "Effects" }, modSection { "Modulation" };

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<ParamChoice> choices;
    juce::OwnedArray<ParamButton> buttons;
    juce::OwnedArray<juce::Label> captions;

    // Grains
    juce::Array<Knob*> grainKnobs;
    Knob *positionKnob = nullptr, *sizeKnob = nullptr, *densityKnob = nullptr;
    ParamButton *posSync = nullptr, *sizeSync = nullptr, *freezeButton = nullptr;
    ParamChoice *posDiv = nullptr, *sizeDiv = nullptr, *windowChoice = nullptr;
    juce::Label *posCaption = nullptr, *sizeCaption = nullptr, *windowCaption = nullptr;

    // Output
    juce::Array<Knob*> outputKnobs;
    ParamButton* limiterButton = nullptr;

    // Pitch
    juce::Array<Knob*> pitchKnobs, voiceKnobs;
    ParamChoice *scaleChoice = nullptr, *rootChoice = nullptr;
    NoteStrip noteStrip;

    // Trigger
    ParamChoice *modeChoice = nullptr, *densityDiv = nullptr, *patternRate = nullptr;
    juce::Label *rateCaption = nullptr, *stepCaption = nullptr;
    Knob* lengthKnob = nullptr;
    StepGrid stepGrid;

    // Effects
    ParamButton *filterOn = nullptr, *satOn = nullptr, *diffOn = nullptr;
    ParamChoice *filterType = nullptr, *filterPlace = nullptr, *satType = nullptr, *satPlace = nullptr, *diffPlace = nullptr;
    juce::Array<Knob*> filterKnobs, satKnobs, diffKnobs;

    // Modulation
    juce::OwnedArray<ModSourceChip> chips;
    juce::OwnedArray<SourceEditor> sourceEditors;
    juce::Label sourceHint;
    juce::Array<Knob*> macroKnobs;
    ModMatrixView matrixView;

    PresetBrowser browser;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

} // namespace grn
