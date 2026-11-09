// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Widgets.h"
#include "Tooltips.h"
#include "../dsp/Pitch.h"

namespace grn
{
namespace
{
    constexpr float kStartAngle = juce::MathConstants<float>::pi * 1.25f;
    constexpr float kEndAngle = juce::MathConstants<float>::pi * 2.75f;
    constexpr const char* kDragPrefix = "modsrc:";

    float angleFor (float normalised)
    {
        return kStartAngle + juce::jlimit (0.0f, 1.0f, normalised) * (kEndAngle - kStartAngle);
    }

    void strokeArc (juce::Graphics& g, juce::Point<float> centre, float radius, float from, float to, float thickness)
    {
        if (std::abs (to - from) < 1.0e-4f)
            return;
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, std::min (from, to), std::max (from, to), true);
        g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void setParamValue (juce::RangedAudioParameter* param, float normalised)
    {
        if (param == nullptr)
            return;
        param->beginChangeGesture();
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
        param->endChangeGesture();
    }
}

int modDestForParam (const juce::String& paramId)
{
    for (int d = 0; d < numModDests; ++d)
        if (modDestParamId (d) == paramId)
            return d;
    return -1;
}

// ---------------------------------------------------------------------------------------------
Knob::Knob (UiContext& c, const juce::String& paramId, const juce::String& labelText, bool isSmall)
    : context (c), label (labelText), small (isSmall)
{
    auto& state = context.processor.getState();
    param = state.getParameter (paramId);
    jassert (param != nullptr);
    dest = modDestForParam (paramId);

    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (kStartAngle, kEndAngle, true);
    setPopupMenuEnabled (false);
    setMouseDragSensitivity (180);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, *this);

    auto tip = tooltipFor (paramId);
    if (tip.isEmpty() && param != nullptr)
        tip = param->getName (64);
    if (dest >= 0)
        tip << " Drop a modulation source here, or Alt + drag to set its depth.";
    setTooltip (tip);
}

int Knob::heightFor (bool isSmall)
{
    const auto& t = Theme::get();
    return (isSmall ? t.knobSmall : t.knobSize) + t.labelHeight;
}

int Knob::widthFor (bool isSmall)
{
    const auto& t = Theme::get();
    return (isSmall ? t.knobSmall : t.knobSize) + 14;
}

juce::String Knob::valueText() const
{
    if (param == nullptr)
        return {};
    auto text = param->getCurrentValueAsText();
    const auto unit = param->getLabel();
    if (unit.isNotEmpty())
        text << " " << unit;
    return text;
}

void Knob::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    auto bounds = getLocalBounds();
    const auto labelArea = bounds.removeFromBottom (t.labelHeight);
    const float diameter = static_cast<float> (std::min (bounds.getWidth(), bounds.getHeight()));
    const auto centre = bounds.toFloat().getCentre();
    const float radius = diameter * 0.5f;
    const float trackRadius = radius - 5.0f;
    const float modRadius = radius - 1.5f;

    const auto norm = static_cast<float> (valueToProportionOfLength (getValue()));
    const bool bipolar = getMinimum() < 0.0 && std::abs (getMinimum() + getMaximum()) < 1.0e-6;
    const float alpha = isEnabled() ? 1.0f : 0.4f;

    g.setColour (t.knobTrack.withMultipliedAlpha (alpha));
    strokeArc (g, centre, trackRadius, kStartAngle, kEndAngle, 3.0f);
    g.setColour (t.accent.withMultipliedAlpha (alpha));
    strokeArc (g, centre, trackRadius, angleFor (bipolar ? 0.5f : 0.0f), angleFor (norm), 3.0f);

    const float bodyRadius = trackRadius - 4.5f;
    g.setColour ((hovering || dragging ? t.controlHover : t.control).withMultipliedAlpha (alpha));
    g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);
    const float pointerAngle = angleFor (norm);
    const juce::Point<float> direction (std::sin (pointerAngle), -std::cos (pointerAngle));
    g.setColour (t.text.withMultipliedAlpha (alpha));
    g.drawLine ({ centre + direction * (bodyRadius * 0.35f), centre + direction * (bodyRadius - 1.5f) }, 1.8f);

    // Modulation rings: one arc per source, from the base value to the base plus the depth.
    if (dest >= 0)
    {
        auto& matrix = context.processor.getMatrix();
        for (const auto& slot : matrix.slots)
        {
            const int source = slot.source.load();
            if (slot.dest.load() != dest || source < 0)
                continue;
            const float depth = slot.depth.load();
            g.setColour (t.sourceColour (source));
            strokeArc (g, centre, modRadius, angleFor (norm), angleFor (norm + depth),
                       source == context.selectedSource ? 3.0f : 2.0f);
        }

        if (shownModNorm >= 0.0f)
        {
            const float a = angleFor (shownModNorm);
            const auto dot = centre + juce::Point<float> (std::sin (a), -std::cos (a)) * trackRadius;
            g.setColour (t.text);
            g.fillEllipse (dot.x - 2.5f, dot.y - 2.5f, 5.0f, 5.0f);
        }
    }

    if (dropHover)
    {
        g.setColour (t.sourceColour (context.selectedSource));
        g.drawEllipse (centre.x - radius + 0.5f, centre.y - radius + 0.5f, diameter - 1.0f, diameter - 1.0f, 1.5f);
    }

    const bool showValue = hovering || dragging || depthDrag;
    g.setFont (Fonts::small());
    g.setColour ((showValue ? t.text : t.textDim).withMultipliedAlpha (alpha));
    juce::String text = showValue ? valueText() : label;
    if (depthDrag && dest >= 0 && context.selectedSource >= 0)
        text = juce::String (juce::roundToInt (context.processor.getMatrix().depthFor (context.selectedSource, dest) * 100.0f)) + " %";
    g.drawFittedText (text, labelArea, juce::Justification::centred, 1, 0.7f);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    if (e.mods.isAltDown() && dest >= 0 && context.selectedSource >= 0)
    {
        depthDrag = true;
        depthStart = context.processor.getMatrix().depthFor (context.selectedSource, dest);
        return;
    }
    context.processor.getUndoManager().beginNewTransaction();
    dragging = true;
    juce::Slider::mouseDown (e);
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (depthDrag)
    {
        const float depth = juce::jlimit (-1.0f, 1.0f, depthStart - static_cast<float> (e.getDistanceFromDragStartY()) / 150.0f);
        context.processor.getMatrix().setRouting (context.selectedSource, dest, depth);
        repaint();
        return;
    }
    if (! e.mods.isPopupMenu())
        juce::Slider::mouseDrag (e);
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (depthDrag)
    {
        depthDrag = false;
        repaint();
        return;
    }
    if (dragging)
    {
        dragging = false;
        juce::Slider::mouseUp (e);
        repaint();
    }
}

void Knob::mouseEnter (const juce::MouseEvent& e)
{
    hovering = true;
    juce::Slider::mouseEnter (e);
    repaint();
}

void Knob::mouseExit (const juce::MouseEvent& e)
{
    hovering = false;
    juce::Slider::mouseExit (e);
    repaint();
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Ctrl + wheel zooms the interface: let the editor handle it.
    if (e.mods.isCtrlDown())
        juce::Component::mouseWheelMove (e, wheel);
    else
        juce::Slider::mouseWheelMove (e, wheel);
}

bool Knob::isInterestedInDragSource (const SourceDetails& details)
{
    return dest >= 0 && details.description.toString().startsWith (kDragPrefix);
}

void Knob::itemDragEnter (const SourceDetails&)
{
    dropHover = true;
    repaint();
}

void Knob::itemDragExit (const SourceDetails&)
{
    dropHover = false;
    repaint();
}

void Knob::itemDropped (const SourceDetails& details)
{
    dropHover = false;
    const int source = details.description.toString().fromFirstOccurrenceOf (kDragPrefix, false, false).getIntValue();
    auto& matrix = context.processor.getMatrix();
    if (matrix.findSlot (source, dest) < 0)
        matrix.setRouting (source, dest, 0.25f);
    context.selectSource (source);
    repaint();
}

void Knob::refreshModulation()
{
    if (dest < 0)
        return;
    auto& bridge = context.processor.getBridge();
    const bool active = ((bridge.destActiveMask.load (std::memory_order_relaxed) >> dest) & 1u) != 0;
    const float modNorm = active ? bridge.destNorm[static_cast<size_t> (dest)].load (std::memory_order_relaxed) : -1.0f;
    const auto version = context.processor.getMatrix().getVersion();
    if (std::abs (modNorm - shownModNorm) > 0.002f || version != shownMatrixVersion || shownSelection != context.selectedSource)
    {
        shownModNorm = modNorm;
        shownMatrixVersion = version;
        shownSelection = context.selectedSource;
        repaint();
    }
}

void Knob::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Enter value...");
    menu.addItem (2, "Reset to default");
    auto& matrix = context.processor.getMatrix();
    if (dest >= 0)
    {
        menu.addSeparator();
        if (context.selectedSource >= 0 && matrix.findSlot (context.selectedSource, dest) < 0)
            menu.addItem (3, juce::String ("Modulate with ") + modSourceName (context.selectedSource));
        for (int i = 0; i < numModSlots; ++i)
        {
            const auto& slot = matrix.slots[static_cast<size_t> (i)];
            if (slot.dest.load() == dest && slot.source.load() >= 0)
                menu.addItem (100 + i, juce::String ("Remove ") + modSourceName (slot.source.load()) + " modulation");
        }
    }

    juce::Component::SafePointer<Knob> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& self = *safe;
        if (result == 1)
            self.showValueEditor();
        else if (result == 2)
            setParamValue (self.param, self.param->getDefaultValue());
        else if (result == 3)
            self.context.processor.getMatrix().setRouting (self.context.selectedSource, self.dest, 0.25f);
        else if (result >= 100)
            self.context.processor.getMatrix().removeSlot (result - 100);
        self.repaint();
    });
}

void Knob::showValueEditor()
{
    valueEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*valueEditor);
    const auto& t = Theme::get();
    valueEditor->setBounds (getLocalBounds().removeFromBottom (t.labelHeight + 4).expanded (2, 0));
    valueEditor->setFont (Fonts::small());
    valueEditor->setJustification (juce::Justification::centred);
    valueEditor->setIndents (2, 1);
    valueEditor->setText (param->getCurrentValueAsText(), false);
    valueEditor->selectAll();
    valueEditor->grabKeyboardFocus();

    juce::Component::SafePointer<Knob> safe (this);
    auto close = [safe]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
                safe->valueEditor.reset();
        });
    };
    valueEditor->onReturnKey = [safe, close]
    {
        if (safe != nullptr && safe->valueEditor != nullptr)
        {
            safe->context.processor.getUndoManager().beginNewTransaction();
            setParamValue (safe->param, safe->param->getValueForText (safe->valueEditor->getText()));
        }
        close();
    };
    valueEditor->onEscapeKey = close;
    valueEditor->onFocusLost = close;
}

// ---------------------------------------------------------------------------------------------
ParamButton::ParamButton (UiContext& context, const juce::String& paramId, const juce::String& text)
    : juce::TextButton (text)
{
    setClickingTogglesState (true);
    setTooltip (tooltipFor (paramId));
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (context.processor.getState(), paramId, *this);
}

ParamChoice::ParamChoice (UiContext& context, const juce::String& paramId)
{
    auto& state = context.processor.getState();
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        addItemList (choice->choices, 1);
    setTooltip (tooltipFor (paramId));
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, *this);
}

// ---------------------------------------------------------------------------------------------
StepGrid::StepGrid (UiContext& c) : context (c)
{
    auto& state = context.processor.getState();
    for (int s = 0; s < numPatternSteps; ++s)
    {
        gates[s] = state.getParameter (pid::stepGate (s + 1));
        chances[s] = state.getParameter (pid::stepProb (s + 1));
        ratchets[s] = state.getParameter (pid::stepRatchet (s + 1));
    }
    lengthParam = state.getParameter (pid::patternLength);
    modeParam = state.getParameter (pid::triggerMode);
    setTooltip ("Pattern: top row toggles a step, the bar sets its chance (drag), the number sets its ratchet count (click).");
}

juce::Rectangle<float> StepGrid::cell (int step, Zone zone) const
{
    const auto bounds = getLocalBounds().toFloat();
    const float width = bounds.getWidth() / static_cast<float> (numPatternSteps);
    const float gateHeight = 16.0f, ratchetHeight = 15.0f;
    juce::Rectangle<float> column (bounds.getX() + width * static_cast<float> (step), bounds.getY(), width, bounds.getHeight());
    column = column.reduced (1.0f, 0.0f);
    if (zone == zoneGate)
        return column.removeFromTop (gateHeight);
    if (zone == zoneRatchet)
        return column.removeFromBottom (ratchetHeight);
    return column.withTrimmedTop (gateHeight + 2.0f).withTrimmedBottom (ratchetHeight + 2.0f);
}

int StepGrid::stepAt (juce::Point<float> position) const
{
    const float width = static_cast<float> (getWidth()) / static_cast<float> (numPatternSteps);
    return juce::jlimit (0, numPatternSteps - 1, static_cast<int> (position.x / width));
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    const int length = juce::roundToInt (lengthParam->convertFrom0to1 (lengthParam->getValue()));
    const bool patternMode = juce::roundToInt (modeParam->convertFrom0to1 (modeParam->getValue())) == triggerPattern;
    const int playing = patternMode ? context.processor.getBridge().currentStep.load() : -1;

    g.setFont (Fonts::small());
    for (int s = 0; s < numPatternSteps; ++s)
    {
        const bool inLength = s < length;
        const bool on = gates[s]->getValue() > 0.5f;
        const float alpha = (inLength ? 1.0f : 0.3f) * (patternMode ? 1.0f : 0.55f);

        const auto gate = cell (s, zoneGate);
        g.setColour ((on ? t.accent : t.control).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (gate, 2.0f);
        if (s == playing && inLength)
        {
            g.setColour (t.text);
            g.drawRoundedRectangle (gate.reduced (0.5f), 2.0f, 1.5f);
        }

        const auto chance = cell (s, zoneChance);
        g.setColour (t.visualBack);
        g.fillRect (chance);
        g.setColour ((on ? t.accentDim : t.knobTrack).withMultipliedAlpha (alpha));
        g.fillRect (chance.withTop (chance.getBottom() - chance.getHeight() * chances[s]->getValue()));

        const auto ratchet = cell (s, zoneRatchet);
        const int count = juce::roundToInt (ratchets[s]->convertFrom0to1 (ratchets[s]->getValue()));
        g.setColour ((count > 1 ? t.text : t.textFaint).withMultipliedAlpha (alpha));
        g.drawText (juce::String (count), ratchet, juce::Justification::centred, false);
    }
}

void StepGrid::setChanceFromMouse (const juce::MouseEvent& e)
{
    if (dragStep < 0)
        return;
    const auto area = cell (dragStep, zoneChance);
    const float value = juce::jlimit (0.0f, 1.0f, (area.getBottom() - e.position.y) / area.getHeight());
    chances[dragStep]->setValueNotifyingHost (value);
    repaint();
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    context.processor.getUndoManager().beginNewTransaction();
    const int step = stepAt (e.position);
    if (cell (step, zoneGate).expanded (1.0f).contains (e.position))
    {
        setParamValue (gates[step], gates[step]->getValue() > 0.5f ? 0.0f : 1.0f);
    }
    else if (cell (step, zoneRatchet).expanded (1.0f).contains (e.position))
    {
        const int count = juce::roundToInt (ratchets[step]->convertFrom0to1 (ratchets[step]->getValue()));
        setParamValue (ratchets[step], ratchets[step]->convertTo0to1 (static_cast<float> (count % 4 + 1)));
    }
    else
    {
        dragStep = step;
        chances[step]->beginChangeGesture();
        setChanceFromMouse (e);
    }
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e) { setChanceFromMouse (e); }

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    if (dragStep >= 0)
        chances[dragStep]->endChangeGesture();
    dragStep = -1;
}

void StepGrid::refresh()
{
    uint64_t hash = static_cast<uint64_t> (context.processor.getBridge().currentStep.load()) * 1315423911u;
    hash ^= static_cast<uint64_t> (lengthParam->getValue() * 64.0f) << 8;
    hash ^= static_cast<uint64_t> (modeParam->getValue() * 8.0f) << 4;
    for (int s = 0; s < numPatternSteps; ++s)
    {
        hash = hash * 31u + static_cast<uint64_t> (gates[s]->getValue() * 2.0f);
        hash = hash * 31u + static_cast<uint64_t> (chances[s]->getValue() * 200.0f);
        hash = hash * 31u + static_cast<uint64_t> (ratchets[s]->getValue() * 8.0f);
    }
    if (hash != shownHash)
    {
        shownHash = hash;
        repaint();
    }
}

// ---------------------------------------------------------------------------------------------
NoteStrip::NoteStrip (UiContext& c) : context (c)
{
    for (int n = 0; n < 12; ++n)
        notes[n] = context.processor.getState().getParameter (pid::customNote (n));
    setTooltip ("Notes allowed by the scale, starting from the root. With the Custom scale, click a note to allow or block it.");
}

void NoteStrip::paint (juce::Graphics& g)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto& t = Theme::get();
    auto& state = context.processor.getState();
    const int scale = juce::roundToInt (state.getRawParameterValue (pid::scale)->load());
    const int root = juce::roundToInt (state.getRawParameterValue (pid::root)->load());
    uint16_t custom = 0;
    for (int n = 0; n < 12; ++n)
        if (notes[n]->getValue() > 0.5f)
            custom = static_cast<uint16_t> (custom | (1u << n));
    const uint16_t mask = scale == scaleOff ? static_cast<uint16_t> (0x0fff) : scaleMask (scale, custom);

    const float width = static_cast<float> (getWidth()) / 12.0f;
    g.setFont (Fonts::small());
    for (int n = 0; n < 12; ++n)
    {
        // Cell n shows the pitch class n semitones above the root.
        const auto area = juce::Rectangle<float> (width * static_cast<float> (n), 0.0f, width, static_cast<float> (getHeight())).reduced (1.0f, 0.0f);
        const bool allowed = ((mask >> n) & 1u) != 0;
        const float alpha = scale == scaleOff ? 0.4f : 1.0f;
        g.setColour ((allowed ? (scale == scaleCustom ? t.accent : t.accentDim) : t.control).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (area, 2.0f);
        g.setColour ((allowed ? t.text : t.textFaint).withMultipliedAlpha (alpha));
        g.drawFittedText (names[(root + n) % 12], area.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }
}

void NoteStrip::mouseDown (const juce::MouseEvent& e)
{
    auto& state = context.processor.getState();
    if (juce::roundToInt (state.getRawParameterValue (pid::scale)->load()) != scaleCustom)
        return;
    const int n = juce::jlimit (0, 11, static_cast<int> (e.position.x * 12.0f / static_cast<float> (getWidth())));
    context.processor.getUndoManager().beginNewTransaction();
    setParamValue (notes[n], notes[n]->getValue() > 0.5f ? 0.0f : 1.0f);
    repaint();
}

void NoteStrip::refresh()
{
    auto& state = context.processor.getState();
    uint32_t hash = static_cast<uint32_t> (state.getRawParameterValue (pid::scale)->load()) * 131u
                    + static_cast<uint32_t> (state.getRawParameterValue (pid::root)->load()) * 17u;
    for (int n = 0; n < 12; ++n)
        hash = hash * 2u + (notes[n]->getValue() > 0.5f ? 1u : 0u);
    if (hash != shownHash)
    {
        shownHash = hash;
        repaint();
    }
}

// ---------------------------------------------------------------------------------------------
ModSourceChip::ModSourceChip (UiContext& c, int sourceIndex) : context (c), source (sourceIndex)
{
    setTooltip (juce::String (modSourceName (source)) + ": click to select, then drag onto a knob to modulate it.");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ModSourceChip::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto colour = t.sourceColour (source);
    const bool selected = context.selectedSource == source;

    g.setColour (selected ? t.controlHover : t.control);
    g.fillRoundedRectangle (bounds, t.radius);
    g.setColour (selected ? colour : t.border);
    g.drawRoundedRectangle (bounds, t.radius, selected ? 1.5f : 1.0f);

    g.setColour (colour);
    g.fillEllipse (bounds.getX() + 6.0f, bounds.getCentreY() - 3.5f, 7.0f, 7.0f);
    g.setColour (t.text);
    g.setFont (Fonts::small());
    g.drawFittedText (modSourceName (source), getLocalBounds().withTrimmedLeft (17).withTrimmedRight (4), juce::Justification::centredLeft, 1, 0.8f);

    // Live value along the bottom edge.
    const auto meter = bounds.reduced (5.0f, 0.0f).removeFromBottom (2.0f).translated (0.0f, -1.0f);
    const bool unipolar = source >= srcEnv;
    const float origin = unipolar ? meter.getX() : meter.getCentreX();
    const float extent = shownValue * (unipolar ? meter.getWidth() : meter.getWidth() * 0.5f);
    g.setColour (colour.withAlpha (0.9f));
    g.fillRect (juce::Rectangle<float> (std::min (origin, origin + extent), meter.getY(), std::abs (extent), meter.getHeight()));
}

void ModSourceChip::mouseDown (const juce::MouseEvent&)
{
    context.selectSource (context.selectedSource == source ? -1 : source);
}

void ModSourceChip::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 5)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        if (! container->isDragAndDropActive())
        {
            if (context.selectedSource != source)
                context.selectSource (source);
            container->startDragging (juce::String (kDragPrefix) + juce::String (source), this);
        }
    }
}

void ModSourceChip::refresh()
{
    const float value = context.processor.getBridge().sourceValue[static_cast<size_t> (source)].load (std::memory_order_relaxed);
    const bool selected = context.selectedSource == source;
    if (std::abs (value - shownValue) > 0.01f || selected != shownSelected)
    {
        shownValue = value;
        shownSelected = selected;
        repaint();
    }
}

// ---------------------------------------------------------------------------------------------
struct ModMatrixView::Row final : public juce::Component
{
    Row (UiContext& c, int slotIndex, uint32_t& versionRef) : context (c), slot (slotIndex), shownVersion (versionRef)
    {
        auto& matrix = context.processor.getMatrix();
        source = matrix.slots[static_cast<size_t> (slot)].source.load();
        dest = matrix.slots[static_cast<size_t> (slot)].dest.load();

        depth.setSliderStyle (juce::Slider::LinearHorizontal);
        depth.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        depth.setRange (-100.0, 100.0, 1.0);
        depth.setDoubleClickReturnValue (true, 0.0);
        depth.setValue (matrix.slots[static_cast<size_t> (slot)].depth.load() * 100.0f, juce::dontSendNotification);
        depth.setColour (juce::Slider::trackColourId, Theme::get().sourceColour (source));
        depth.setTooltip ("Modulation depth. Double-click to reset.");
        depth.onValueChange = [this]
        {
            auto& m = context.processor.getMatrix();
            m.setSlot (slot, source, dest, static_cast<float> (depth.getValue()) * 0.01f);
            shownVersion = m.getVersion(); // our own edit: no rebuild needed
            repaint();
        };
        addAndMakeVisible (depth);

        remove.setButtonText ("x");
        remove.setTooltip ("Remove this modulation.");
        remove.onClick = [this] { context.processor.getMatrix().removeSlot (slot); };
        addAndMakeVisible (remove);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        remove.setBounds (area.removeFromRight (20).reduced (0, 2));
        area.removeFromRight (36);
        depth.setBounds (area.removeFromRight (juce::jmax (60, area.getWidth() / 3)));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        auto area = getLocalBounds();
        area.removeFromRight (20);
        const auto valueArea = area.removeFromRight (36);
        area.removeFromRight (juce::jmax (60, area.getWidth() / 3));

        g.setColour (t.sourceColour (source));
        g.fillEllipse (2.0f, static_cast<float> (getHeight()) * 0.5f - 3.5f, 7.0f, 7.0f);
        g.setFont (Fonts::small());
        g.setColour (t.text);
        juce::String destName = "?";
        if (auto* param = context.processor.getState().getParameter (modDestParamId (dest)))
            destName = param->getName (40);
        g.drawFittedText (juce::String (modSourceName (source)) + " > " + destName, area.withTrimmedLeft (13),
                          juce::Justification::centredLeft, 1, 0.75f);
        g.setColour (t.textDim);
        g.drawText (juce::String (juce::roundToInt (depth.getValue())) + " %", valueArea, juce::Justification::centredRight, false);
    }

    UiContext& context;
    int slot, source = -1, dest = -1;
    uint32_t& shownVersion;
    juce::Slider depth;
    juce::TextButton remove;
};

ModMatrixView::ModMatrixView (UiContext& c) : context (c)
{
    for (int s = 0; s < numModSources; ++s)
        sourceBox.addItem (modSourceName (s), s + 1);
    for (int d = 0; d < numModDests; ++d)
        if (auto* param = context.processor.getState().getParameter (modDestParamId (d)))
            destBox.addItem (param->getName (40), d + 1);
    sourceBox.setSelectedId (1, juce::dontSendNotification);
    destBox.setSelectedId (dstPosition + 1, juce::dontSendNotification);
    sourceBox.setTooltip ("Modulation source for a new routing.");
    destBox.setTooltip ("Parameter modulated by the new routing.");
    addButton.setTooltip ("Add this routing to the modulation matrix.");
    addButton.onClick = [this]
    {
        context.processor.getMatrix().setRouting (sourceBox.getSelectedId() - 1, destBox.getSelectedId() - 1, 0.25f);
    };
    addAndMakeVisible (sourceBox);
    addAndMakeVisible (destBox);
    addAndMakeVisible (addButton);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

ModMatrixView::~ModMatrixView() = default;

void ModMatrixView::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (22);
    addButton.setBounds (top.removeFromRight (44));
    top.removeFromRight (4);
    sourceBox.setBounds (top.removeFromLeft (top.getWidth() * 2 / 5));
    top.removeFromLeft (4);
    destBox.setBounds (top);
    area.removeFromTop (4);
    viewport.setBounds (area);
    shownVersion = 0xffffffffu;
    refresh();
}

void ModMatrixView::paint (juce::Graphics& g)
{
    if (rows.isEmpty())
    {
        g.setColour (Theme::get().textFaint);
        g.setFont (Fonts::small());
        g.drawFittedText ("Drag a source onto a knob to modulate it.", viewport.getBounds().reduced (4), juce::Justification::centred, 2);
    }
}

void ModMatrixView::refresh()
{
    const auto version = context.processor.getMatrix().getVersion();
    if (version != shownVersion)
    {
        shownVersion = version;
        rebuild();
    }
}

void ModMatrixView::rebuild()
{
    rows.clear();
    auto& matrix = context.processor.getMatrix();
    const int rowHeight = 22;
    const int width = juce::jmax (40, viewport.getWidth() - 10);
    for (int i = 0; i < numModSlots; ++i)
    {
        const auto& slot = matrix.slots[static_cast<size_t> (i)];
        if (slot.source.load() < 0 || slot.dest.load() < 0)
            continue;
        auto* row = rows.add (new Row (context, i, shownVersion));
        list.addAndMakeVisible (row);
        row->setBounds (0, (rows.size() - 1) * rowHeight, width, rowHeight);
    }
    list.setSize (width, juce::jmax (1, rows.size() * rowHeight));
    repaint();
}

// ---------------------------------------------------------------------------------------------
void Section::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (t.panel);
    g.fillRoundedRectangle (bounds, t.radius);
    g.setColour (t.border);
    g.drawRoundedRectangle (bounds, t.radius, t.borderWidth);
    g.setColour (t.textDim);
    g.setFont (Fonts::title());
    g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (t.sectionTitleHeight).withTrimmedLeft (t.spacing),
                juce::Justification::centredLeft, false);
}

juce::Rectangle<int> Section::contentArea() const
{
    const auto& t = Theme::get();
    return getLocalBounds().withTrimmedTop (t.sectionTitleHeight).reduced (t.spacing, 0).withTrimmedBottom (t.spacingSmall);
}

} // namespace grn
