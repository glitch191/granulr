// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"

/** Temporary editor used until the real interface lands. */
class GranulrEditor final : public juce::GenericAudioProcessorEditor
{
public:
    explicit GranulrEditor (GranulrProcessor& p) : juce::GenericAudioProcessorEditor (p) {}
};
