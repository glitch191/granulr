// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Params.h"

namespace grn
{
namespace
{
    using Float = juce::AudioParameterFloat;
    using Bool = juce::AudioParameterBool;
    using Choice = juce::AudioParameterChoice;
    using Int = juce::AudioParameterInt;
    using Range = juce::NormalisableRange<float>;
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    juce::ParameterID versioned (const juce::String& identifier) { return { identifier, 1 }; }

    Range skewed (float low, float high, float centre, float interval = 0.0f)
    {
        Range range (low, high, interval);
        range.setSkewForCentre (centre);
        return range;
    }

    void addFloat (Layout& layout, const juce::String& identifier, const juce::String& name,
                   Range range, float defaultValue, const juce::String& unit, int decimals)
    {
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withLabel (unit)
                              .withStringFromValueFunction ([decimals] (float value, int)
                                                            { return juce::String (value, decimals); })
                              .withValueFromStringFunction ([] (const juce::String& text)
                                                            { return text.getFloatValue(); });
        layout.add (std::make_unique<Float> (versioned (identifier), name, range, defaultValue, attributes));
    }

    void addBool (Layout& layout, const juce::String& identifier, const juce::String& name, bool defaultValue)
    {
        layout.add (std::make_unique<Bool> (versioned (identifier), name, defaultValue));
    }

    void addChoice (Layout& layout, const juce::String& identifier, const juce::String& name,
                    const juce::StringArray& choices, int defaultIndex)
    {
        layout.add (std::make_unique<Choice> (versioned (identifier), name, choices, defaultIndex));
    }

    void addInt (Layout& layout, const juce::String& identifier, const juce::String& name,
                 int low, int high, int defaultValue)
    {
        layout.add (std::make_unique<Int> (versioned (identifier), name, low, high, defaultValue));
    }

    const juce::StringArray placeNames { "In feedback", "Post" };
    const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Layout layout;
    const auto divisions = divisionNames();

    // Engine
    addFloat (layout, pid::inputGain, "Input", Range (-24.0f, 24.0f, 0.01f), 0.0f, "dB", 1);
    addFloat (layout, pid::outputGain, "Output", Range (-24.0f, 24.0f, 0.01f), 0.0f, "dB", 1);
    addFloat (layout, pid::mix, "Mix", Range (0.0f, 100.0f, 0.01f), 50.0f, "%", 0);
    addBool (layout, pid::bypass, "Bypass", false);
    addBool (layout, pid::freeze, "Freeze", false);
    addFloat (layout, pid::position, "Position", skewed (0.0f, 8000.0f, 600.0f), 250.0f, "ms", 0);
    addBool (layout, pid::positionSync, "Position Sync", false);
    addChoice (layout, pid::positionDiv, "Position Division", divisions, divisionQuarter);
    addFloat (layout, pid::scan, "Scan", Range (-2.0f, 2.0f, 0.001f), 0.0f, "x", 2);
    addFloat (layout, pid::size, "Size", skewed (1.0f, 2000.0f, 150.0f), 120.0f, "ms", 0);
    addBool (layout, pid::sizeSync, "Size Sync", false);
    addChoice (layout, pid::sizeDiv, "Size Division", divisions, divisionSixteenth);
    addFloat (layout, pid::density, "Density", skewed (0.5f, 400.0f, 20.0f), 20.0f, "gr/s", 1);
    addChoice (layout, pid::densityDiv, "Density Division", divisions, divisionSixteenth);
    addChoice (layout, pid::triggerMode, "Trigger", { "Free", "Sync", "Pattern" }, triggerFree);
    addFloat (layout, pid::spray, "Spray", skewed (0.0f, 2000.0f, 150.0f), 20.0f, "ms", 0);
    addFloat (layout, pid::jitter, "Jitter", Range (0.0f, 100.0f, 0.01f), 10.0f, "%", 0);
    addFloat (layout, pid::pitch, "Pitch", Range (-24.0f, 24.0f, 1.0f), 0.0f, "st", 0);
    addFloat (layout, pid::fine, "Fine", Range (-100.0f, 100.0f, 0.1f), 0.0f, "ct", 0);
    addFloat (layout, pid::pitchRandom, "Pitch Random", skewed (0.0f, 24.0f, 4.0f), 0.0f, "st", 2);
    addFloat (layout, pid::reverse, "Reverse", Range (0.0f, 100.0f, 0.01f), 0.0f, "%", 0);
    addChoice (layout, pid::windowShape, "Window", { "Hann", "Tukey", "Trapezoid", "Perc" }, windowHann);
    addFloat (layout, pid::windowSkew, "Skew", Range (-100.0f, 100.0f, 0.01f), 0.0f, "%", 0);
    addFloat (layout, pid::panSpread, "Pan Spread", Range (0.0f, 100.0f, 0.01f), 30.0f, "%", 0);
    addFloat (layout, pid::width, "Width", Range (0.0f, 200.0f, 0.01f), 100.0f, "%", 0);
    addFloat (layout, pid::feedback, "Feedback", Range (0.0f, 100.0f, 0.01f), 0.0f, "%", 0);
    addFloat (layout, pid::duckAmount, "Duck", Range (0.0f, 100.0f, 0.01f), 0.0f, "%", 0);
    addFloat (layout, pid::duckAttack, "Duck Attack", skewed (1.0f, 200.0f, 20.0f), 10.0f, "ms", 0);
    addFloat (layout, pid::duckRelease, "Duck Release", skewed (20.0f, 2000.0f, 250.0f), 250.0f, "ms", 0);
    addBool (layout, pid::limiter, "Limiter", true);
    addChoice (layout, pid::quality, "Quality", { "Eco", "Normal", "High" }, qualityNormal);

    // Pitch and scale
    addChoice (layout, pid::scale, "Scale",
               { "Off", "Chromatic", "Major", "Minor", "Major Pent.", "Minor Pent.", "Dorian", "Phrygian",
                 "Whole Tone", "Octaves", "Fifths + Octaves", "Custom" },
               scaleOff);
    addChoice (layout, pid::root, "Root", noteNames, 0);
    for (int n = 0; n < 12; ++n)
        addBool (layout, pid::customNote (n), "Custom " + noteNames[n], true);
    const float defaultIntervals[numHarmonyVoices] { 0.0f, 7.0f, 12.0f, -12.0f };
    for (int v = 1; v <= numHarmonyVoices; ++v)
    {
        addFloat (layout, pid::voiceInterval (v), "Voice " + juce::String (v) + " Interval",
                  Range (-24.0f, 24.0f, 1.0f), defaultIntervals[v - 1], "st", 0);
        addFloat (layout, pid::voiceProb (v), "Voice " + juce::String (v) + " Chance",
                  Range (0.0f, 100.0f, 0.01f), v == 1 ? 100.0f : 0.0f, "%", 0);
    }
    addFloat (layout, pid::octaveUp, "Octave Up", Range (0.0f, 100.0f, 0.01f), 0.0f, "%", 0);
    addFloat (layout, pid::octaveDown, "Octave Down", Range (0.0f, 100.0f, 0.01f), 0.0f, "%", 0);

    // Pattern
    addInt (layout, pid::patternLength, "Pattern Length", 1, numPatternSteps, numPatternSteps);
    addChoice (layout, pid::patternRate, "Pattern Rate", divisions, divisionSixteenth);
    for (int s = 1; s <= numPatternSteps; ++s)
    {
        const auto label = "Step " + juce::String (s);
        addBool (layout, pid::stepGate (s), label, true);
        addFloat (layout, pid::stepProb (s), label + " Chance", Range (0.0f, 100.0f, 0.01f), 100.0f, "%", 0);
        addInt (layout, pid::stepRatchet (s), label + " Ratchet", 1, 4, 1);
    }

    // Effects
    addBool (layout, pid::filterOn, "Filter", false);
    addChoice (layout, pid::filterType, "Filter Type", { "Low-pass", "High-pass", "Band-pass" }, filterLowpass);
    addFloat (layout, pid::filterCutoff, "Filter Cutoff", skewed (20.0f, 20000.0f, 1000.0f), 8000.0f, "Hz", 0);
    addFloat (layout, pid::filterReso, "Filter Resonance", Range (0.0f, 100.0f, 0.01f), 20.0f, "%", 0);
    addChoice (layout, pid::filterPlace, "Filter Placement", placeNames, placePost);
    addBool (layout, pid::satOn, "Saturation", false);
    addChoice (layout, pid::satType, "Saturation Type", { "Soft", "Hard" }, satSoft);
    addFloat (layout, pid::satDrive, "Saturation Drive", Range (0.0f, 36.0f, 0.01f), 6.0f, "dB", 1);
    addChoice (layout, pid::satPlace, "Saturation Placement", placeNames, placePost);
    addBool (layout, pid::diffOn, "Diffusion", false);
    addFloat (layout, pid::diffSize, "Diffusion Size", Range (0.0f, 100.0f, 0.01f), 50.0f, "%", 0);
    addFloat (layout, pid::diffDecay, "Diffusion Decay", Range (0.0f, 100.0f, 0.01f), 50.0f, "%", 0);
    addFloat (layout, pid::diffDamp, "Diffusion Damping", Range (0.0f, 100.0f, 0.01f), 30.0f, "%", 0);
    addFloat (layout, pid::diffMix, "Diffusion Mix", Range (0.0f, 100.0f, 0.01f), 30.0f, "%", 0);
    addChoice (layout, pid::diffPlace, "Diffusion Placement", placeNames, placePost);

    // Modulation sources
    const juce::StringArray lfoShapes { "Sine", "Triangle", "Saw", "Square", "Smooth Random", "Sample and Hold" };
    const float defaultLfoRates[numLfos] { 0.5f, 0.21f, 2.0f, 0.07f };
    for (int n = 1; n <= numLfos; ++n)
    {
        const auto label = "LFO " + juce::String (n);
        addChoice (layout, pid::lfoShape (n), label + " Shape", lfoShapes, lfoSine);
        addFloat (layout, pid::lfoRate (n), label + " Rate", skewed (0.01f, 40.0f, 1.0f), defaultLfoRates[n - 1], "Hz", 2);
        addBool (layout, pid::lfoSync (n), label + " Sync", false);
        addChoice (layout, pid::lfoDiv (n), label + " Division", divisions, divisionQuarter);
        addFloat (layout, pid::lfoPhase (n), label + " Phase", Range (0.0f, 360.0f, 0.1f), 0.0f, "deg", 0);
        addBool (layout, pid::lfoRetrig (n), label + " Retrigger", false);
    }
    for (int n = 1; n <= numRandoms; ++n)
    {
        const auto label = "Random " + juce::String (n);
        addFloat (layout, pid::randRate (n), label + " Rate", skewed (0.01f, 40.0f, 1.0f), n == 1 ? 1.0f : 4.0f, "Hz", 2);
        addFloat (layout, pid::randSmooth (n), label + " Smooth", Range (0.0f, 100.0f, 0.01f), 50.0f, "%", 0);
        addBool (layout, pid::randSync (n), label + " Sync", false);
        addChoice (layout, pid::randDiv (n), label + " Division", divisions, divisionQuarter);
    }
    addChoice (layout, pid::envSource, "Envelope Source", { "Input", "Output" }, 0);
    addFloat (layout, pid::envAttack, "Envelope Attack", skewed (1.0f, 500.0f, 30.0f), 10.0f, "ms", 0);
    addFloat (layout, pid::envRelease, "Envelope Release", skewed (10.0f, 5000.0f, 300.0f), 200.0f, "ms", 0);
    addFloat (layout, pid::envGain, "Envelope Gain", Range (-24.0f, 24.0f, 0.01f), 0.0f, "dB", 1);
    for (int n = 1; n <= numMacros; ++n)
        addFloat (layout, pid::macro (n), "Macro " + juce::String (n), Range (0.0f, 100.0f, 0.01f), 0.0f, "%", 0);

    return layout;
}

ParamGroup paramGroupOf (const juce::String& paramId)
{
    static const juce::StringArray grains { pid::position, pid::scan, pid::size, pid::density, pid::spray,
                                            pid::jitter, pid::reverse, pid::windowShape, pid::windowSkew,
                                            pid::panSpread, pid::width, pid::feedback };
    static const juce::StringArray pitches { pid::pitch, pid::pitchRandom, pid::octaveUp, pid::octaveDown };

    if (grains.contains (paramId))
        return groupGrains;
    if (pitches.contains (paramId) || paramId.startsWith ("voice"))
        return groupPitch;
    if (paramId.startsWith ("filter") || paramId.startsWith ("sat") || paramId.startsWith ("diff"))
        return groupEffects;
    if (paramId.startsWith ("lfo") || paramId.startsWith ("rand") || paramId == pid::envAttack
        || paramId == pid::envRelease)
        return groupModulation;
    return groupNone;
}

bool isMorphable (const juce::RangedAudioParameter& param)
{
    return dynamic_cast<const juce::AudioParameterFloat*> (&param) != nullptr;
}

ParamRefs::ParamRefs (juce::AudioProcessorValueTreeState& state)
{
    auto raw = [&state] (const juce::String& identifier)
    {
        auto* value = state.getRawParameterValue (identifier);
        jassert (value != nullptr);
        return value;
    };

    inputGain = raw (pid::inputGain);       outputGain = raw (pid::outputGain);
    mix = raw (pid::mix);                   bypass = raw (pid::bypass);
    freeze = raw (pid::freeze);             position = raw (pid::position);
    positionSync = raw (pid::positionSync); positionDiv = raw (pid::positionDiv);
    scan = raw (pid::scan);                 size = raw (pid::size);
    sizeSync = raw (pid::sizeSync);         sizeDiv = raw (pid::sizeDiv);
    density = raw (pid::density);           densityDiv = raw (pid::densityDiv);
    triggerMode = raw (pid::triggerMode);   spray = raw (pid::spray);
    jitter = raw (pid::jitter);             pitch = raw (pid::pitch);
    fine = raw (pid::fine);                 pitchRandom = raw (pid::pitchRandom);
    reverse = raw (pid::reverse);           windowShape = raw (pid::windowShape);
    windowSkew = raw (pid::windowSkew);     panSpread = raw (pid::panSpread);
    width = raw (pid::width);               feedback = raw (pid::feedback);
    duckAmount = raw (pid::duckAmount);     duckAttack = raw (pid::duckAttack);
    duckRelease = raw (pid::duckRelease);   limiter = raw (pid::limiter);
    quality = raw (pid::quality);
    scale = raw (pid::scale);               root = raw (pid::root);
    octaveUp = raw (pid::octaveUp);         octaveDown = raw (pid::octaveDown);
    for (int n = 0; n < 12; ++n)
        customNote[n] = raw (pid::customNote (n));
    for (int v = 0; v < numHarmonyVoices; ++v)
    {
        voiceInterval[v] = raw (pid::voiceInterval (v + 1));
        voiceProb[v] = raw (pid::voiceProb (v + 1));
    }
    patternLength = raw (pid::patternLength);
    patternRate = raw (pid::patternRate);
    for (int s = 0; s < numPatternSteps; ++s)
    {
        stepGate[s] = raw (pid::stepGate (s + 1));
        stepProb[s] = raw (pid::stepProb (s + 1));
        stepRatchet[s] = raw (pid::stepRatchet (s + 1));
    }
    filterOn = raw (pid::filterOn);         filterType = raw (pid::filterType);
    filterCutoff = raw (pid::filterCutoff); filterReso = raw (pid::filterReso);
    filterPlace = raw (pid::filterPlace);
    satOn = raw (pid::satOn);               satType = raw (pid::satType);
    satDrive = raw (pid::satDrive);         satPlace = raw (pid::satPlace);
    diffOn = raw (pid::diffOn);             diffSize = raw (pid::diffSize);
    diffDecay = raw (pid::diffDecay);       diffDamp = raw (pid::diffDamp);
    diffMix = raw (pid::diffMix);           diffPlace = raw (pid::diffPlace);
    for (int n = 0; n < numLfos; ++n)
    {
        lfoShape[n] = raw (pid::lfoShape (n + 1));
        lfoRate[n] = raw (pid::lfoRate (n + 1));
        lfoSync[n] = raw (pid::lfoSync (n + 1));
        lfoDiv[n] = raw (pid::lfoDiv (n + 1));
        lfoPhase[n] = raw (pid::lfoPhase (n + 1));
        lfoRetrig[n] = raw (pid::lfoRetrig (n + 1));
    }
    for (int n = 0; n < numRandoms; ++n)
    {
        randRate[n] = raw (pid::randRate (n + 1));
        randSmooth[n] = raw (pid::randSmooth (n + 1));
        randSync[n] = raw (pid::randSync (n + 1));
        randDiv[n] = raw (pid::randDiv (n + 1));
    }
    envSource = raw (pid::envSource);       envAttack = raw (pid::envAttack);
    envRelease = raw (pid::envRelease);     envGain = raw (pid::envGain);
    for (int n = 0; n < numMacros; ++n)
        macro[n] = raw (pid::macro (n + 1));

    for (int d = 0; d < numModDests; ++d)
    {
        const auto identifier = modDestParamId (d);
        dest[d] = raw (identifier);
        if (auto* param = state.getParameter (identifier))
            destRange[d] = param->getNormalisableRange();
    }
}

} // namespace grn
