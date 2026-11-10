// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MainPanel.h"

namespace grn
{
namespace
{
    constexpr int kMargin = 6, kGap = 6;
    constexpr int kRowHeight = 92, kEffectsHeight = 116, kModHeight = 152;
    constexpr int kControlHeight = 22;

    void placeKnobs (juce::Rectangle<int> area, const juce::Array<Knob*>& knobs, bool small = false)
    {
        if (knobs.isEmpty())
            return;
        const float cell = static_cast<float> (area.getWidth()) / static_cast<float> (knobs.size());
        const int height = std::min (area.getHeight(), Knob::heightFor (small));
        const int y = area.getY() + (area.getHeight() - height) / 2;
        for (int i = 0; i < knobs.size(); ++i)
        {
            const int x0 = area.getX() + juce::roundToInt (cell * static_cast<float> (i));
            const int x1 = area.getX() + juce::roundToInt (cell * static_cast<float> (i + 1));
            knobs[i]->setBounds (x0, y, x1 - x0, height);
        }
    }

    void styleCaption (juce::Label& label)
    {
        label.setFont (Fonts::small());
        label.setColour (juce::Label::textColourId, Theme::get().textDim);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setBorderSize ({ 0, 0, 0, 0 });
        label.setInterceptsMouseClicks (false, false);
    }
}

// ---------------------------------------------------------------------------------------------
/** Controls of one modulation source, shown when its chip is selected. */
class MainPanel::SourceEditor final : public juce::Component
{
public:
    SourceEditor (MainPanel& owner, int sourceIndex) : source (sourceIndex)
    {
        if (source <= srcLfo4)
        {
            const int n = source - srcLfo1 + 1;
            topChoice = &owner.addChoice (*this, pid::lfoShape (n));
            sync = &owner.addButton (*this, pid::lfoSync (n), "Sync");
            div = &owner.addChoice (*this, pid::lfoDiv (n));
            retrig = &owner.addButton (*this, pid::lfoRetrig (n), "Retrigger");
            knobs.add (&owner.addKnob (*this, pid::lfoRate (n), "Rate"));
            knobs.add (&owner.addKnob (*this, pid::lfoPhase (n), "Phase"));
        }
        else if (source <= srcRand2)
        {
            const int n = source - srcRand1 + 1;
            sync = &owner.addButton (*this, pid::randSync (n), "Sync");
            div = &owner.addChoice (*this, pid::randDiv (n));
            knobs.add (&owner.addKnob (*this, pid::randRate (n), "Rate"));
            knobs.add (&owner.addKnob (*this, pid::randSmooth (n), "Smooth"));
        }
        else
        {
            topChoice = &owner.addChoice (*this, pid::envSource);
            knobs.add (&owner.addKnob (*this, pid::envAttack, "Attack"));
            knobs.add (&owner.addKnob (*this, pid::envRelease, "Release"));
            knobs.add (&owner.addKnob (*this, pid::envGain, "Gain"));
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (124);
        area.removeFromLeft (8);
        if (topChoice != nullptr)
        {
            topChoice->setBounds (left.removeFromTop (kControlHeight));
            left.removeFromTop (4);
        }
        if (sync != nullptr && div != nullptr)
        {
            auto row = left.removeFromTop (kControlHeight);
            sync->setBounds (row.removeFromLeft (46));
            row.removeFromLeft (4);
            div->setBounds (row);
            left.removeFromTop (4);
        }
        if (retrig != nullptr)
            retrig->setBounds (left.removeFromTop (kControlHeight));
        placeKnobs (area.withWidth (std::min (area.getWidth(), knobs.size() * 62)), knobs);
    }

    void updateEnablement()
    {
        if (sync != nullptr && div != nullptr)
        {
            div->setEnabled (sync->getToggleState());
            if (! knobs.isEmpty())
                knobs[0]->setEnabled (! sync->getToggleState());
        }
    }

    int source;
    ParamChoice *topChoice = nullptr, *div = nullptr;
    ParamButton *sync = nullptr, *retrig = nullptr;
    juce::Array<Knob*> knobs;
};

// ---------------------------------------------------------------------------------------------
MainPanel::MainPanel (UiContext& c, VisualEngine& engine)
    : context (c), processor (c.processor),
      grainView (c.processor, engine, VisualView::grains),
      outputView (c.processor, engine, VisualView::output),
      noteStrip (c), stepGrid (c), matrixView (c), browser (c)
{
    const auto& t = Theme::get();
    auto& presets = processor.getPresets();
    auto& ui = processor.getUiState();

    // Header ----------------------------------------------------------------------------------
    titleLabel.setText ("granulr", juce::dontSendNotification);
    titleLabel.setFont (Fonts::large());
    titleLabel.setColour (juce::Label::textColourId, t.text);
    addAndMakeVisible (titleLabel);
    titleLabel.setTooltip ("granulr " GRANULR_VERSION_STRING);

    prevButton.setTooltip ("Previous preset.");
    prevButton.onClick = [&presets] { presets.loadRelative (-1); };
    nextButton.setTooltip ("Next preset.");
    nextButton.onClick = [&presets] { presets.loadRelative (1); };
    presetButton.setTooltip ("Open the preset browser: search, categories, favourites, save.");
    presetButton.onClick = [this]
    {
        browser.setVisible (! browser.isVisible());
        if (browser.isVisible())
            browser.toFront (true);
    };
    browser.onClose = [this] { browser.setVisible (false); };

    slotA.setTooltip ("Slot A: compare two versions of a sound.");
    slotB.setTooltip ("Slot B: compare two versions of a sound.");
    slotA.onClick = [&presets] { presets.selectSlot (0); };
    slotB.onClick = [&presets] { presets.selectSlot (1); };
    copyButton.setTooltip ("Copy the active slot to the other slot.");
    copyButton.onClick = [&presets] { presets.copyActiveToOther(); };

    morphSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    morphSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    morphSlider.setRange (0.0, 1.0, 0.001);
    morphSlider.setTooltip ("Morph: blends every continuous parameter between slot A (left) and slot B (right).");
    morphSlider.onDragStart = [&presets] { presets.beginMorph(); };
    morphSlider.onValueChange = [this, &presets]
    {
        if (morphSlider.isMouseButtonDown())
            presets.setMorph (static_cast<float> (morphSlider.getValue()));
    };

    randomButton.setTooltip ("Randomize the unlocked parameter groups. Undo brings the previous sound back.");
    randomButton.onClick = [this, &presets] { presets.randomise (randomAmount, randomLocks); };
    randomOptions.setTooltip ("Randomize options: amount and locked groups.");
    randomOptions.onClick = [this] { showRandomMenu(); };

    undoButton.setTooltip ("Undo the last change (Ctrl + Z).");
    undoButton.onClick = [this] { processor.getUndoManager().undo(); };
    redoButton.setTooltip ("Redo (Ctrl + Y).");
    redoButton.onClick = [this] { processor.getUndoManager().redo(); };

    modOpen = ui.getProperty ("modOpen", true);
    modButton.setClickingTogglesState (true);
    modButton.setToggleState (modOpen, juce::dontSendNotification);
    modButton.setTooltip ("Show or hide the modulation panel.");
    modButton.onClick = [this]
    {
        modOpen = modButton.getToggleState();
        processor.getUiState().setProperty ("modOpen", modOpen, nullptr);
        resized();
        if (onLayoutChanged)
            onLayoutChanged();
    };

    qualityChoice = std::make_unique<ParamChoice> (context, pid::quality);
    for (int percent : { 100, 110, 125, 150, 175, 200 })
        zoomBox.addItem (juce::String (percent) + " %", percent);
    zoomBox.setTooltip ("Interface zoom (Ctrl + plus, Ctrl + minus, Ctrl + 0 for 125 %).");
    zoomBox.onChange = [this]
    {
        if (onZoomChosen && zoomBox.getSelectedId() > 0)
            onZoomChosen (static_cast<float> (zoomBox.getSelectedId()) / 100.0f);
    };
    settingsButton.setTooltip ("Display settings, diagnostics and information.");
    settingsButton.onClick = [this]
    {
        if (onSettingsMenu)
            onSettingsMenu (settingsButton);
    };
    cpuLabel.setFont (Fonts::small());
    cpuLabel.setColour (juce::Label::textColourId, t.textDim);
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    cpuLabel.setTooltip ("Audio CPU load of this instance on one core (average), and number of active grains.");
    bypassButton = std::make_unique<ParamButton> (context, pid::bypass, "Bypass");

    for (auto* component : std::initializer_list<juce::Component*> { &prevButton, &nextButton, &presetButton, &slotA, &slotB,
                                                                     &copyButton, &morphSlider, &randomButton, &randomOptions,
                                                                     &undoButton, &redoButton, &modButton, qualityChoice.get(),
                                                                     &zoomBox, &settingsButton, &cpuLabel, bypassButton.get() })
        addAndMakeVisible (component);

    // Visualisers -----------------------------------------------------------------------------
    addAndMakeVisible (grainView);
    addAndMakeVisible (outputView);
    outputView.setMode (ui.getProperty ("visualMode", visSpectrum));
    outputView.onModeChanged = [this] (int mode) { processor.getUiState().setProperty ("visualMode", mode, nullptr); };

    for (auto* section : { &grainsSection, &outputSection, &pitchSection, &triggerSection, &effectsSection, &modSection })
        addAndMakeVisible (section);

    // Grains ----------------------------------------------------------------------------------
    positionKnob = &addKnob (grainsSection, pid::position, "Position");
    sizeKnob = &addKnob (grainsSection, pid::size, "Size");
    densityKnob = &addKnob (grainsSection, pid::density, "Density");
    grainKnobs.addArray ({ positionKnob, sizeKnob, densityKnob,
                           &addKnob (grainsSection, pid::spray, "Spray"), &addKnob (grainsSection, pid::jitter, "Jitter"),
                           &addKnob (grainsSection, pid::reverse, "Reverse"), &addKnob (grainsSection, pid::scan, "Scan"),
                           &addKnob (grainsSection, pid::windowSkew, "Skew"), &addKnob (grainsSection, pid::panSpread, "Pan"),
                           &addKnob (grainsSection, pid::width, "Width") });
    posCaption = &addCaption (grainsSection, "Pos");
    posSync = &addButton (grainsSection, pid::positionSync, "Sync");
    posDiv = &addChoice (grainsSection, pid::positionDiv);
    sizeCaption = &addCaption (grainsSection, "Size");
    sizeSync = &addButton (grainsSection, pid::sizeSync, "Sync");
    sizeDiv = &addChoice (grainsSection, pid::sizeDiv);
    windowCaption = &addCaption (grainsSection, "Window");
    windowChoice = &addChoice (grainsSection, pid::windowShape);
    freezeButton = &addButton (grainsSection, pid::freeze, "Freeze");
    freezeButton->setColour (juce::TextButton::buttonOnColourId, t.frozen);

    // Output ----------------------------------------------------------------------------------
    outputKnobs.addArray ({ &addKnob (outputSection, pid::feedback, "Feedback"), &addKnob (outputSection, pid::duckAmount, "Duck"),
                            &addKnob (outputSection, pid::duckAttack, "Attack"), &addKnob (outputSection, pid::duckRelease, "Release"),
                            &addKnob (outputSection, pid::inputGain, "Input"), &addKnob (outputSection, pid::mix, "Mix"),
                            &addKnob (outputSection, pid::outputGain, "Output") });
    limiterButton = &addButton (outputSection, pid::limiter, "Limiter");

    // Pitch -----------------------------------------------------------------------------------
    pitchKnobs.addArray ({ &addKnob (pitchSection, pid::pitch, "Pitch"), &addKnob (pitchSection, pid::fine, "Fine"),
                           &addKnob (pitchSection, pid::pitchRandom, "Random"), &addKnob (pitchSection, pid::octaveUp, "Oct +"),
                           &addKnob (pitchSection, pid::octaveDown, "Oct -") });
    for (int v = 1; v <= numHarmonyVoices; ++v)
    {
        voiceKnobs.add (&addKnob (pitchSection, pid::voiceInterval (v), "V" + juce::String (v) + " st", true));
        voiceKnobs.add (&addKnob (pitchSection, pid::voiceProb (v), "V" + juce::String (v) + " %", true));
    }
    scaleChoice = &addChoice (pitchSection, pid::scale);
    rootChoice = &addChoice (pitchSection, pid::root);
    pitchSection.addAndMakeVisible (noteStrip);

    // Trigger ---------------------------------------------------------------------------------
    modeChoice = &addChoice (triggerSection, pid::triggerMode);
    rateCaption = &addCaption (triggerSection, "Rate");
    densityDiv = &addChoice (triggerSection, pid::densityDiv);
    stepCaption = &addCaption (triggerSection, "Step");
    patternRate = &addChoice (triggerSection, pid::patternRate);
    lengthKnob = &addKnob (triggerSection, pid::patternLength, "Length", true);
    triggerSection.addAndMakeVisible (stepGrid);

    // Effects ---------------------------------------------------------------------------------
    filterOn = &addButton (effectsSection, pid::filterOn, "Filter");
    filterType = &addChoice (effectsSection, pid::filterType);
    filterPlace = &addChoice (effectsSection, pid::filterPlace);
    filterKnobs.addArray ({ &addKnob (effectsSection, pid::filterCutoff, "Cutoff"), &addKnob (effectsSection, pid::filterReso, "Resonance") });
    satOn = &addButton (effectsSection, pid::satOn, "Saturation");
    satType = &addChoice (effectsSection, pid::satType);
    satPlace = &addChoice (effectsSection, pid::satPlace);
    satKnobs.add (&addKnob (effectsSection, pid::satDrive, "Drive"));
    diffOn = &addButton (effectsSection, pid::diffOn, "Diffusion");
    diffPlace = &addChoice (effectsSection, pid::diffPlace);
    diffKnobs.addArray ({ &addKnob (effectsSection, pid::diffSize, "Size"), &addKnob (effectsSection, pid::diffDecay, "Decay"),
                          &addKnob (effectsSection, pid::diffDamp, "Damping"), &addKnob (effectsSection, pid::diffMix, "Mix") });

    // Modulation ------------------------------------------------------------------------------
    for (int s = 0; s < numModSources; ++s)
        modSection.addAndMakeVisible (chips.add (new ModSourceChip (context, s)));
    for (int s = 0; s <= srcEnv; ++s)
        modSection.addChildComponent (sourceEditors.add (new SourceEditor (*this, s)));
    sourceHint.setFont (Fonts::small());
    sourceHint.setColour (juce::Label::textColourId, t.textFaint);
    sourceHint.setJustificationType (juce::Justification::centred);
    sourceHint.setInterceptsMouseClicks (false, false);
    modSection.addAndMakeVisible (sourceHint);
    for (int n = 1; n <= numMacros; ++n)
        macroKnobs.add (&addKnob (modSection, pid::macro (n), "Macro " + juce::String (n)));
    modSection.addAndMakeVisible (matrixView);

    // Overlays --------------------------------------------------------------------------------
    diagnostics.setFont (Fonts::small());
    diagnostics.setColour (juce::Label::textColourId, t.text);
    diagnostics.setColour (juce::Label::backgroundColourId, t.panelRaised.withAlpha (0.92f));
    diagnostics.setColour (juce::Label::outlineColourId, t.border);
    diagnostics.setJustificationType (juce::Justification::topLeft);
    diagnostics.setInterceptsMouseClicks (false, false);
    addChildComponent (diagnostics);
    addChildComponent (browser);

    tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 650);

    context.onSelectionChanged = [this] { updateSourceEditors(); };
    presets.addChangeListener (this);
    updatePresetName();
    updateSourceEditors();
    updateEnablement();
    setSize (defaultWidth, defaultHeight);
}

MainPanel::~MainPanel()
{
    processor.getPresets().removeChangeListener (this);
    context.onSelectionChanged = nullptr;
}

Knob& MainPanel::addKnob (juce::Component& parent, const juce::String& paramId, const juce::String& label, bool small)
{
    auto* knob = knobs.add (new Knob (context, paramId, label, small));
    parent.addAndMakeVisible (knob);
    return *knob;
}

ParamChoice& MainPanel::addChoice (juce::Component& parent, const juce::String& paramId)
{
    auto* choice = choices.add (new ParamChoice (context, paramId));
    parent.addAndMakeVisible (choice);
    return *choice;
}

ParamButton& MainPanel::addButton (juce::Component& parent, const juce::String& paramId, const juce::String& text)
{
    auto* button = buttons.add (new ParamButton (context, paramId, text));
    parent.addAndMakeVisible (button);
    return *button;
}

juce::Label& MainPanel::addCaption (juce::Component& parent, const juce::String& text)
{
    auto* label = captions.add (new juce::Label ({}, text));
    styleCaption (*label);
    parent.addAndMakeVisible (label);
    return *label;
}

// ---------------------------------------------------------------------------------------------
void MainPanel::paint (juce::Graphics& g)
{
    // With GPU rendering the two plots are drawn underneath this component layer,
    // so they must stay transparent here.
    if (! softwareRendering)
    {
        g.excludeClipRegion (grainPlot());
        g.excludeClipRegion (outputPlot());
    }
    g.fillAll (Theme::get().background);
}

juce::Rectangle<int> MainPanel::grainPlot() const  { return grainView.plotArea() + grainView.getPosition(); }
juce::Rectangle<int> MainPanel::outputPlot() const { return outputView.plotArea() + outputView.getPosition(); }

void MainPanel::setSoftwareRendering (bool software)
{
    softwareRendering = software;
    grainView.setSoftwareRendering (software);
    outputView.setSoftwareRendering (software);
    repaint();
}

void MainPanel::repaintVisuals()
{
    grainView.repaint();
    outputView.repaint();
}

void MainPanel::setZoomDisplay (float zoom)
{
    zoomBox.setSelectedId (juce::roundToInt (zoom * 100.0f), juce::dontSendNotification);
    if (zoomBox.getSelectedId() == 0)
        zoomBox.setText (juce::String (juce::roundToInt (zoom * 100.0f)) + " %", juce::dontSendNotification);
}

void MainPanel::setDiagnostics (bool visible, const juce::String& text)
{
    diagnostics.setVisible (visible);
    if (visible)
        diagnostics.setText (text, juce::dontSendNotification);
}

void MainPanel::resized()
{
    const auto& t = Theme::get();
    auto area = getLocalBounds().reduced (kMargin);
    layoutHeader (area.removeFromTop (t.headerHeight - 2 * kMargin));
    area.removeFromTop (kGap);

    const int fixed = kRowHeight * 2 + kEffectsHeight + kGap * 3;
    const bool showMod = modOpen && area.getHeight() - fixed - (kModHeight + kGap) >= 110;
    modSection.setVisible (showMod);
    if (showMod)
    {
        modSection.setBounds (area.removeFromBottom (kModHeight));
        area.removeFromBottom (kGap);
    }
    effectsSection.setBounds (area.removeFromBottom (kEffectsHeight));
    area.removeFromBottom (kGap);

    auto row2 = area.removeFromBottom (kRowHeight);
    area.removeFromBottom (kGap);
    triggerSection.setBounds (row2.removeFromRight (juce::jmax (400, row2.getWidth() * 37 / 100)));
    row2.removeFromRight (kGap);
    pitchSection.setBounds (row2);

    auto row1 = area.removeFromBottom (kRowHeight);
    area.removeFromBottom (kGap);
    outputSection.setBounds (row1.removeFromRight (juce::jmax (336, row1.getWidth() * 34 / 100)));
    row1.removeFromRight (kGap);
    grainsSection.setBounds (row1);

    grainView.setBounds (area.removeFromLeft (area.getWidth() * 56 / 100));
    area.removeFromLeft (kGap);
    outputView.setBounds (area);

    layoutGrains();
    layoutOutput();
    layoutPitch();
    layoutTrigger();
    layoutEffects();
    if (showMod)
        layoutModulation();

    diagnostics.setBounds (grainView.getX() + 8, grainView.getY() + 28, 270, 118);
    browser.setBounds (juce::Rectangle<int> (0, 0, juce::jmin (600, getWidth() - 40), juce::jmin (420, getHeight() - t.headerHeight - 20))
                           .withPosition (titleLabel.getRight() + 20, t.headerHeight));
}

void MainPanel::layoutHeader (juce::Rectangle<int> area)
{
    auto take = [&area] (int width, int gap = 4)
    {
        auto piece = area.removeFromLeft (width);
        area.removeFromLeft (gap);
        return piece;
    };
    auto takeRight = [&area] (int width, int gap = 4)
    {
        auto piece = area.removeFromRight (width);
        area.removeFromRight (gap);
        return piece;
    };

    titleLabel.setBounds (take (62, 8));
    prevButton.setBounds (take (24, 2));

    // Right side first, so the preset name takes what is left.
    bypassButton->setBounds (takeRight (60));
    cpuLabel.setBounds (takeRight (100));
    settingsButton.setBounds (takeRight (60));
    zoomBox.setBounds (takeRight (72));
    qualityChoice->setBounds (takeRight (76));
    modButton.setBounds (takeRight (42, 8));
    redoButton.setBounds (takeRight (42));
    undoButton.setBounds (takeRight (42, 8));
    randomOptions.setBounds (takeRight (26));
    randomButton.setBounds (takeRight (82, 8));
    morphSlider.setBounds (takeRight (72));
    copyButton.setBounds (takeRight (44));
    slotB.setBounds (takeRight (26, 2));
    slotA.setBounds (takeRight (26, 8));
    nextButton.setBounds (takeRight (24, 2));
    presetButton.setBounds (area);
}

void MainPanel::layoutGrains()
{
    auto area = grainsSection.contentArea();
    auto left = area.removeFromLeft (150);
    area.removeFromLeft (8);
    auto row = left.removeFromTop (kControlHeight);
    posCaption->setBounds (row.removeFromLeft (30));
    posSync->setBounds (row.removeFromLeft (44));
    row.removeFromLeft (4);
    posDiv->setBounds (row);
    left.removeFromTop (1);
    row = left.removeFromTop (kControlHeight);
    sizeCaption->setBounds (row.removeFromLeft (30));
    sizeSync->setBounds (row.removeFromLeft (44));
    row.removeFromLeft (4);
    sizeDiv->setBounds (row);
    left.removeFromTop (1);
    row = left.removeFromTop (kControlHeight);
    windowCaption->setBounds (row.removeFromLeft (48));
    windowChoice->setBounds (row);

    freezeButton->setBounds (area.removeFromRight (60).withSizeKeepingCentre (60, 44));
    area.removeFromRight (6);
    placeKnobs (area, grainKnobs);
}

void MainPanel::layoutOutput()
{
    auto area = outputSection.contentArea();
    limiterButton->setBounds (area.removeFromRight (58).withSizeKeepingCentre (58, 24));
    area.removeFromRight (4);
    placeKnobs (area, outputKnobs);
}

void MainPanel::layoutPitch()
{
    auto area = pitchSection.contentArea();
    auto left = area.removeFromLeft (168);
    area.removeFromLeft (8);
    left.removeFromTop (6);
    auto row = left.removeFromTop (kControlHeight);
    rootChoice->setBounds (row.removeFromRight (54));
    row.removeFromRight (4);
    scaleChoice->setBounds (row);
    left.removeFromTop (6);
    noteStrip.setBounds (left.removeFromTop (20));

    const int mainWeight = pitchKnobs.size() * 52, voiceWeight = voiceKnobs.size() * 42;
    const int mainWidth = area.getWidth() * mainWeight / (mainWeight + voiceWeight);
    placeKnobs (area.removeFromLeft (mainWidth), pitchKnobs);
    placeKnobs (area, voiceKnobs, true);
}

void MainPanel::layoutTrigger()
{
    auto area = triggerSection.contentArea();
    auto left = area.removeFromLeft (118);
    area.removeFromLeft (6);
    modeChoice->setBounds (left.removeFromTop (kControlHeight));
    left.removeFromTop (1);
    auto row = left.removeFromTop (kControlHeight);
    rateCaption->setBounds (row.removeFromLeft (32));
    densityDiv->setBounds (row);
    left.removeFromTop (1);
    row = left.removeFromTop (kControlHeight);
    stepCaption->setBounds (row.removeFromLeft (32));
    patternRate->setBounds (row);

    lengthKnob->setBounds (area.removeFromLeft (50).withSizeKeepingCentre (50, Knob::heightFor (true)));
    area.removeFromLeft (4);
    stepGrid.setBounds (area.reduced (0, 1));
}

void MainPanel::layoutEffects()
{
    auto area = effectsSection.contentArea();
    const int total = area.getWidth();
    auto block = [&area, total] (int percent)
    {
        auto piece = area.removeFromLeft (total * percent / 100);
        return piece.reduced (6, 0);
    };

    auto filter = block (30);
    auto row = filter.removeFromTop (kControlHeight);
    filterOn->setBounds (row.removeFromLeft (64));
    row.removeFromLeft (4);
    filterPlace->setBounds (row.removeFromRight (juce::jmin (104, row.getWidth() / 2)));
    row.removeFromRight (4);
    filterType->setBounds (row);
    placeKnobs (filter.withWidth (juce::jmin (filter.getWidth(), 160)), filterKnobs);

    auto sat = block (28);
    row = sat.removeFromTop (kControlHeight);
    satOn->setBounds (row.removeFromLeft (82));
    row.removeFromLeft (4);
    satPlace->setBounds (row.removeFromRight (juce::jmin (104, row.getWidth() * 3 / 5)));
    row.removeFromRight (4);
    satType->setBounds (row);
    placeKnobs (sat.withWidth (juce::jmin (sat.getWidth(), 80)), satKnobs);

    auto diff = area.reduced (6, 0);
    row = diff.removeFromTop (kControlHeight);
    diffOn->setBounds (row.removeFromLeft (76));
    row.removeFromLeft (4);
    diffPlace->setBounds (row.removeFromLeft (110));
    placeKnobs (diff.withWidth (juce::jmin (diff.getWidth(), 320)), diffKnobs);
}

void MainPanel::layoutModulation()
{
    auto area = modSection.contentArea();

    // Chips: two columns.
    auto chipArea = area.removeFromLeft (204);
    area.removeFromLeft (10);
    const int rows = (chips.size() + 1) / 2;
    const int chipHeight = juce::jmin (21, chipArea.getHeight() / rows);
    for (int i = 0; i < chips.size(); ++i)
    {
        const int column = i / rows, r = i % rows;
        chips[i]->setBounds (chipArea.getX() + column * 103, chipArea.getY() + r * chipHeight, 101, chipHeight - 1);
    }

    matrixView.setBounds (area.removeFromRight (juce::jmax (260, area.getWidth() * 38 / 100)));
    area.removeFromRight (10);
    placeKnobs (area.removeFromRight (juce::jmin (232, area.getWidth() / 2)), macroKnobs);
    area.removeFromRight (10);
    for (auto* editor : sourceEditors)
        editor->setBounds (area.reduced (0, 8));
    sourceHint.setBounds (area);
}

// ---------------------------------------------------------------------------------------------
void MainPanel::showRandomMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Amount");
    for (int percent : { 25, 50, 75, 100 })
        menu.addItem (percent, juce::String (percent) + " %", true, juce::roundToInt (randomAmount * 100.0f) == percent);
    menu.addSectionHeader ("Locked groups");
    static const char* groupNames[numParamGroups] { "Grains", "Pitch", "Effects", "Modulation" };
    for (int g = 0; g < numParamGroups; ++g)
        menu.addItem (200 + g, groupNames[g], true, randomLocks[static_cast<size_t> (g)]);

    juce::Component::SafePointer<MainPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (randomOptions), [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result >= 200)
            safe->randomLocks[static_cast<size_t> (result - 200)] = ! safe->randomLocks[static_cast<size_t> (result - 200)];
        else
            safe->randomAmount = static_cast<float> (result) / 100.0f;
    });
}

void MainPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updatePresetName();
}

void MainPanel::updatePresetName()
{
    auto& presets = processor.getPresets();
    presetButton.setButtonText (presets.getCurrentName());
    slotA.setToggleState (presets.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (presets.getActiveSlot() == 1, juce::dontSendNotification);
    if (! morphSlider.isMouseButtonDown())
        morphSlider.setValue (presets.getMorph(), juce::dontSendNotification);
}

void MainPanel::updateSourceEditors()
{
    const int selected = context.selectedSource;
    for (auto* editor : sourceEditors)
        editor->setVisible (editor->source == selected);
    const bool hasEditor = selected >= 0 && selected <= srcEnv;
    sourceHint.setVisible (! hasEditor);
    sourceHint.setText (selected >= srcMacro1
                            ? juce::String (modSourceName (selected)) + ": drag its chip onto a knob, then turn the macro knob."
                            : juce::String ("Select a source to edit it. Drag a source onto a knob to modulate it."),
                        juce::dontSendNotification);
}

void MainPanel::updateEnablement()
{
    const bool positionSynced = posSync->getToggleState();
    positionKnob->setEnabled (! positionSynced);
    posDiv->setEnabled (positionSynced);
    const bool sizeSynced = sizeSync->getToggleState();
    sizeKnob->setEnabled (! sizeSynced);
    sizeDiv->setEnabled (sizeSynced);

    const int mode = modeChoice->getSelectedItemIndex();
    densityKnob->setEnabled (mode == triggerFree);
    densityDiv->setEnabled (mode == triggerSync);
    patternRate->setEnabled (mode == triggerPattern);
    lengthKnob->setEnabled (mode == triggerPattern);

    for (auto* knob : filterKnobs) knob->setEnabled (filterOn->getToggleState());
    filterType->setEnabled (filterOn->getToggleState());
    filterPlace->setEnabled (filterOn->getToggleState());
    for (auto* knob : satKnobs) knob->setEnabled (satOn->getToggleState());
    satType->setEnabled (satOn->getToggleState());
    satPlace->setEnabled (satOn->getToggleState());
    for (auto* knob : diffKnobs) knob->setEnabled (diffOn->getToggleState());
    diffPlace->setEnabled (diffOn->getToggleState());

    for (auto* editor : sourceEditors)
        editor->updateEnablement();
}

void MainPanel::refresh (double nowSeconds)
{
    // Every frame: only cheap comparisons, repaints happen when something really changed.
    for (auto* knob : knobs)
        knob->refreshModulation();
    for (auto* chip : chips)
        chip->refresh();
    stepGrid.refresh();

    if (nowSeconds - lastSlowRefresh < 0.1)
        return;
    lastSlowRefresh = nowSeconds;

    noteStrip.refresh();
    matrixView.refresh();
    updateEnablement();

    auto& bridge = processor.getBridge();
    const float cpu = bridge.cpuAverage.load (std::memory_order_relaxed) * 100.0f;
    cpuLabel.setText ("CPU " + juce::String (cpu, 1) + " %  " + juce::String (bridge.activeGrains.load (std::memory_order_relaxed)) + " gr",
                      juce::dontSendNotification);

    auto& undo = processor.getUndoManager();
    undoButton.setEnabled (undo.canUndo());
    redoButton.setEnabled (undo.canRedo());

    const bool frozen = freezeButton->getToggleState();
    if (frozen != shownFrozen)
    {
        shownFrozen = frozen;
        grainView.repaint();
    }
}

} // namespace grn

namespace grn
{
void MainPanel::showBrowser (bool shouldShow)
{
    browser.setVisible (shouldShow);
    if (shouldShow)
        browser.toFront (false);
}
} // namespace grn
