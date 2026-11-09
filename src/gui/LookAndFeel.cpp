// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "LookAndFeel.h"

#if GRANULR_HAS_BINARY_DATA
 #include "BinaryData.h"
#endif

namespace grn
{
namespace
{
    struct Typefaces
    {
        Typefaces()
        {
           #if GRANULR_HAS_BINARY_DATA
            regular = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
            semiBold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
           #endif
        }

        juce::Typeface::Ptr regular, semiBold;
    };

    Typefaces& typefaces()
    {
        static Typefaces instance;
        return instance;
    }

    juce::Font makeFont (const juce::Typeface::Ptr& typeface, float height, bool bold)
    {
        if (typeface != nullptr)
            return juce::Font (juce::FontOptions (typeface).withHeight (height));
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }
}

juce::Font Fonts::regular (float height)  { return makeFont (typefaces().regular, height, false); }
juce::Font Fonts::semiBold (float height) { return makeFont (typefaces().semiBold, height, true); }

GranulrLookAndFeel::GranulrLookAndFeel()
{
    const auto& t = Theme::get();
    setColour (juce::ResizableWindow::backgroundColourId, t.background);
    setColour (juce::Label::textColourId, t.text);
    setColour (juce::TextButton::buttonColourId, t.control);
    setColour (juce::TextButton::buttonOnColourId, t.accent);
    setColour (juce::TextButton::textColourOffId, t.text);
    setColour (juce::TextButton::textColourOnId, t.background);
    setColour (juce::ComboBox::backgroundColourId, t.control);
    setColour (juce::ComboBox::textColourId, t.text);
    setColour (juce::ComboBox::outlineColourId, t.border);
    setColour (juce::ComboBox::arrowColourId, t.textDim);
    setColour (juce::PopupMenu::backgroundColourId, t.panelRaised);
    setColour (juce::PopupMenu::textColourId, t.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, t.text);
    setColour (juce::PopupMenu::headerTextColourId, t.textDim);
    setColour (juce::TextEditor::backgroundColourId, t.visualBack);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::highlightColourId, t.accentDim);
    setColour (juce::TextEditor::highlightedTextColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.border);
    setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
    setColour (juce::CaretComponent::caretColourId, t.accent);
    setColour (juce::ListBox::backgroundColourId, t.visualBack);
    setColour (juce::ListBox::textColourId, t.text);
    setColour (juce::ScrollBar::thumbColourId, t.controlHover);
    setColour (juce::TooltipWindow::backgroundColourId, t.panelRaised);
    setColour (juce::TooltipWindow::textColourId, t.text);
    setColour (juce::TooltipWindow::outlineColourId, t.border);
    setColour (juce::Slider::trackColourId, t.accent);
    setColour (juce::Slider::backgroundColourId, t.knobTrack);
    setColour (juce::Slider::thumbColourId, t.text);
}

juce::Typeface::Ptr GranulrLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    auto& faces = typefaces();
    if (faces.regular == nullptr)
        return juce::LookAndFeel_V4::getTypefaceForFont (font);
    return font.isBold() && faces.semiBold != nullptr ? faces.semiBold : faces.regular;
}

void GranulrLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                               bool highlighted, bool down)
{
    const auto& t = Theme::get();
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    juce::Colour fill = button.getToggleState() ? t.accent : (highlighted || down ? t.controlHover : t.control);
    if (down && ! button.getToggleState())
        fill = fill.brighter (0.08f);
    if (! button.isEnabled())
        fill = fill.withMultipliedAlpha (0.5f);
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, t.radius);
    g.setColour (button.getToggleState() ? t.accent : t.border);
    g.drawRoundedRectangle (bounds, t.radius, t.borderWidth);
}

void GranulrLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto& t = Theme::get();
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour ((button.getToggleState() ? t.background : t.text).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
}

juce::Font GranulrLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return Fonts::regular (buttonHeight < 22 ? Theme::get().fontSmall : Theme::get().fontBody);
}

void GranulrLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& t = Theme::get();
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (0.5f);
    g.setColour (box.isMouseOver (true) ? t.controlHover : t.control);
    g.fillRoundedRectangle (bounds, t.radius);
    g.setColour (t.border);
    g.drawRoundedRectangle (bounds, t.radius, t.borderWidth);

    juce::Path arrow;
    const float cx = static_cast<float> (width) - 10.0f, cy = static_cast<float> (height) * 0.5f;
    arrow.startNewSubPath (cx - 3.5f, cy - 1.5f);
    arrow.lineTo (cx, cy + 2.0f);
    arrow.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (t.textDim.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (arrow, juce::PathStrokeType (1.3f));
}

juce::Font GranulrLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return Fonts::regular (box.getHeight() < 22 ? Theme::get().fontSmall : Theme::get().fontBody);
}

void GranulrLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 0, box.getWidth() - 22, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

juce::Font GranulrLookAndFeel::getPopupMenuFont() { return Fonts::body(); }

void GranulrLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto& t = Theme::get();
    g.fillAll (t.panelRaised);
    g.setColour (t.border);
    g.drawRect (0, 0, width, height, 1);
}

void GranulrLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                           float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto& t = Theme::get();
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    const float cy = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
    const auto track = juce::Rectangle<float> (static_cast<float> (x), cy - 2.0f, static_cast<float> (width), 4.0f);
    g.setColour (t.knobTrack);
    g.fillRoundedRectangle (track, 2.0f);

    // Bipolar sliders fill from the centre.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float origin = bipolar ? static_cast<float> (x) + static_cast<float> (width) * 0.5f : static_cast<float> (x);
    g.setColour (slider.findColour (juce::Slider::trackColourId).withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (juce::Rectangle<float> (std::min (origin, sliderPos), cy - 2.0f, std::abs (sliderPos - origin), 4.0f), 2.0f);
    g.setColour (t.text);
    g.fillEllipse (sliderPos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
}

juce::Rectangle<int> GranulrLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    const auto font = Fonts::small();
    const int maxWidth = 280;
    juce::AttributedString text;
    text.append (tipText, font);
    juce::TextLayout layout;
    layout.createLayout (text, static_cast<float> (maxWidth));
    const int w = static_cast<int> (std::ceil (layout.getWidth())) + 16;
    const int h = static_cast<int> (std::ceil (layout.getHeight())) + 10;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 16,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 18, w, h)
        .constrainedWithin (parentArea);
}

void GranulrLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto& t = Theme::get();
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    g.setColour (t.panelRaised);
    g.fillRect (bounds);
    g.setColour (t.border);
    g.drawRect (bounds, 1.0f);
    juce::AttributedString attributed;
    attributed.append (text, Fonts::small(), t.text);
    juce::TextLayout layout;
    layout.createLayout (attributed, static_cast<float> (width - 16));
    layout.draw (g, bounds.reduced (8.0f, 5.0f));
}

void GranulrLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                        bool vertical, int thumbStart, int thumbSize, bool mouseOver, bool mouseDown)
{
    const auto& t = Theme::get();
    juce::Rectangle<int> thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, width - 4, thumbSize)
                                          : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, height - 4);
    g.setColour (mouseOver || mouseDown ? t.textFaint : t.controlHover);
    g.fillRoundedRectangle (thumb.toFloat(), 2.0f);
}

juce::Font GranulrLookAndFeel::getLabelFont (juce::Label& label)
{
    const auto current = label.getFont();
    return current.isBold() ? Fonts::semiBold (current.getHeight()) : Fonts::regular (current.getHeight());
}

void GranulrLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height), Theme::get().radius);
}

void GranulrLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? Theme::get().accent : Theme::get().border);
    g.drawRoundedRectangle (0.5f, 0.5f, static_cast<float> (width) - 1.0f, static_cast<float> (height) - 1.0f, Theme::get().radius, 1.0f);
}

} // namespace grn
