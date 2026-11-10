// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ParameterIds.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace grn
{

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Randomiser group of a parameter (groupNone when the randomiser must leave it alone). */
ParamGroup paramGroupOf (const juce::String& paramId);

/** True for parameters that the A/B morph can interpolate (continuous values). */
bool isMorphable (const juce::RangedAudioParameter& param);

/** Raw value pointers, resolved once on the message thread and read on the audio thread. */
struct ParamRefs
{
    explicit ParamRefs (juce::AudioProcessorValueTreeState& state);

    using Raw = std::atomic<float>*;

    Raw inputGain, outputGain, mix, bypass, freeze, position, positionSync, positionDiv, scan, size,
        sizeSync, sizeDiv, density, densityDiv, triggerMode, spray, jitter, pitch, fine, pitchRandom,
        reverse, windowShape, windowSkew, panSpread, width, feedback, duckAmount, duckAttack,
        duckRelease, limiter, quality;
    Raw scale, root, octaveUp, octaveDown;
    Raw customNote[12];
    Raw voiceInterval[numHarmonyVoices], voiceProb[numHarmonyVoices];
    Raw patternLength, patternRate;
    Raw stepGate[numPatternSteps], stepProb[numPatternSteps], stepRatchet[numPatternSteps];
    Raw filterOn, filterType, filterCutoff, filterReso, filterPlace;
    Raw satOn, satType, satDrive, satPlace;
    Raw diffOn, diffSize, diffDecay, diffDamp, diffMix, diffPlace;
    Raw lfoShape[numLfos], lfoRate[numLfos], lfoSync[numLfos], lfoDiv[numLfos], lfoPhase[numLfos],
        lfoRetrig[numLfos];
    Raw randRate[numRandoms], randSmooth[numRandoms], randSync[numRandoms], randDiv[numRandoms];
    Raw envSource, envAttack, envRelease, envGain;
    Raw macro[numMacros];
    Raw onsetSens, onsetBurst;
    Raw resOn, resDamp, resOctave, resPlace;
    Raw driftRate, driftPull, chaosRate, chaosShape, bounceRate, bounceDamp, gestureRate;

    // Modulation destinations
    Raw dest[numModDests];
    juce::NormalisableRange<float> destRange[numModDests];
};

} // namespace grn
