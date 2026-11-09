// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Design tokens. Every colour, size, radius and spacing used by the interface lives here,
// so the look can be replaced later without touching layout or logic.
#pragma once

#include "../params/ParameterIds.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace grn
{

struct Theme
{
    // Surfaces
    juce::Colour background    { 0xff15161a };
    juce::Colour panel         { 0xff1c1e23 };
    juce::Colour panelRaised   { 0xff25282e };
    juce::Colour control       { 0xff2d3138 };
    juce::Colour controlHover  { 0xff383d46 };
    juce::Colour border        { 0xff343840 };
    juce::Colour visualBack    { 0xff101114 };
    juce::Colour grid          { 0xff262a31 };

    // Text
    juce::Colour text          { 0xffe7e9ec };
    juce::Colour textDim       { 0xffa3a9b3 };
    juce::Colour textFaint     { 0xff7c838e };

    // Accent and states
    juce::Colour accent        { 0xff4fb8a9 };
    juce::Colour accentDim     { 0xff2f6f67 };
    juce::Colour warning       { 0xffe2a84b };
    juce::Colour danger        { 0xffe26d5c };
    juce::Colour knobTrack     { 0xff3b4049 };
    juce::Colour waveform      { 0xff4a515c };
    juce::Colour grain         { 0xff7fe0d2 };
    juce::Colour frozen        { 0xff6aa9e8 };
    juce::Colour meterLow      { 0xff4fb8a9 };
    juce::Colour meterHigh     { 0xffe2a84b };
    juce::Colour meterClip     { 0xffe26d5c };

    // One colour per modulation source, so a ring tells where the modulation comes from.
    juce::Colour sourceColours[numModSources] {
        juce::Colour (0xffe8a23d), juce::Colour (0xffd9655b), juce::Colour (0xffb77be0), juce::Colour (0xff5b9be8),
        juce::Colour (0xff6cc56a), juce::Colour (0xff3fc1c9), juce::Colour (0xffe87bb0),
        juce::Colour (0xfff2d15c), juce::Colour (0xfff29a5c), juce::Colour (0xff9ad1f2), juce::Colour (0xffc4f25c)
    };

    // Typography (sizes at 100 % zoom)
    float fontBody = 13.0f;
    float fontSmall = 11.0f;
    float fontTitle = 11.5f;
    float fontLarge = 15.0f;

    // Metrics (at 100 % zoom)
    float radius = 4.0f;
    float borderWidth = 1.0f;
    int spacing = 8;
    int spacingSmall = 4;
    int headerHeight = 44;
    int controlHeight = 24;
    int knobSize = 42;
    int knobSmall = 34;
    int labelHeight = 15;
    int sectionTitleHeight = 22;

    juce::Colour sourceColour (int source) const
    {
        return source >= 0 && source < numModSources ? sourceColours[source] : textDim;
    }

    static const Theme& get()
    {
        static const Theme theme;
        return theme;
    }
};

} // namespace grn
