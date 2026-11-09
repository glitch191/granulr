// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Parameter identifiers. These strings are saved in DAW projects and automation lanes:
// never rename or reuse an identifier once a version has been published.
#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace grn
{
namespace pid
{
    // Engine
    inline constexpr const char* inputGain    = "inputGain";
    inline constexpr const char* outputGain   = "outputGain";
    inline constexpr const char* mix          = "mix";
    inline constexpr const char* bypass       = "bypass";
    inline constexpr const char* freeze       = "freeze";
    inline constexpr const char* position     = "position";
    inline constexpr const char* positionSync = "positionSync";
    inline constexpr const char* positionDiv  = "positionDiv";
    inline constexpr const char* scan         = "scan";
    inline constexpr const char* size         = "size";
    inline constexpr const char* sizeSync     = "sizeSync";
    inline constexpr const char* sizeDiv      = "sizeDiv";
    inline constexpr const char* density      = "density";
    inline constexpr const char* densityDiv   = "densityDiv";
    inline constexpr const char* triggerMode  = "triggerMode";
    inline constexpr const char* spray        = "spray";
    inline constexpr const char* jitter       = "jitter";
    inline constexpr const char* pitch        = "pitch";
    inline constexpr const char* fine         = "fine";
    inline constexpr const char* pitchRandom  = "pitchRandom";
    inline constexpr const char* reverse      = "reverse";
    inline constexpr const char* windowShape  = "windowShape";
    inline constexpr const char* windowSkew   = "windowSkew";
    inline constexpr const char* panSpread    = "panSpread";
    inline constexpr const char* width        = "width";
    inline constexpr const char* feedback     = "feedback";
    inline constexpr const char* duckAmount   = "duckAmount";
    inline constexpr const char* duckAttack   = "duckAttack";
    inline constexpr const char* duckRelease  = "duckRelease";
    inline constexpr const char* limiter      = "limiter";
    inline constexpr const char* quality      = "quality";

    // Pitch and scale
    inline constexpr const char* scale        = "scale";
    inline constexpr const char* root         = "root";
    inline constexpr const char* octaveUp     = "octaveUp";
    inline constexpr const char* octaveDown   = "octaveDown";
    inline juce::String customNote (int n)     { return "customNote" + juce::String (n); }          // 0..11
    inline juce::String voiceInterval (int n)  { return "voice" + juce::String (n) + "Interval"; }   // 1..4
    inline juce::String voiceProb (int n)      { return "voice" + juce::String (n) + "Prob"; }       // 1..4

    // Pattern
    inline constexpr const char* patternLength = "patternLength";
    inline constexpr const char* patternRate   = "patternRate";
    inline juce::String stepGate (int n)       { return "step" + juce::String (n) + "Gate"; }        // 1..16
    inline juce::String stepProb (int n)       { return "step" + juce::String (n) + "Prob"; }
    inline juce::String stepRatchet (int n)    { return "step" + juce::String (n) + "Ratchet"; }

    // Effects
    inline constexpr const char* filterOn     = "filterOn";
    inline constexpr const char* filterType   = "filterType";
    inline constexpr const char* filterCutoff = "filterCutoff";
    inline constexpr const char* filterReso   = "filterReso";
    inline constexpr const char* filterPlace  = "filterPlace";
    inline constexpr const char* satOn        = "satOn";
    inline constexpr const char* satType      = "satType";
    inline constexpr const char* satDrive     = "satDrive";
    inline constexpr const char* satPlace     = "satPlace";
    inline constexpr const char* diffOn       = "diffOn";
    inline constexpr const char* diffSize     = "diffSize";
    inline constexpr const char* diffDecay    = "diffDecay";
    inline constexpr const char* diffDamp     = "diffDamp";
    inline constexpr const char* diffMix      = "diffMix";
    inline constexpr const char* diffPlace    = "diffPlace";

    // Modulation sources
    inline juce::String lfoShape (int n)   { return "lfo" + juce::String (n) + "Shape"; }   // 1..4
    inline juce::String lfoRate (int n)    { return "lfo" + juce::String (n) + "Rate"; }
    inline juce::String lfoSync (int n)    { return "lfo" + juce::String (n) + "Sync"; }
    inline juce::String lfoDiv (int n)     { return "lfo" + juce::String (n) + "Div"; }
    inline juce::String lfoPhase (int n)   { return "lfo" + juce::String (n) + "Phase"; }
    inline juce::String lfoRetrig (int n)  { return "lfo" + juce::String (n) + "Retrig"; }
    inline juce::String randRate (int n)   { return "rand" + juce::String (n) + "Rate"; }   // 1..2
    inline juce::String randSmooth (int n) { return "rand" + juce::String (n) + "Smooth"; }
    inline juce::String randSync (int n)   { return "rand" + juce::String (n) + "Sync"; }
    inline juce::String randDiv (int n)    { return "rand" + juce::String (n) + "Div"; }
    inline constexpr const char* envSource  = "envSource";
    inline constexpr const char* envAttack  = "envAttack";
    inline constexpr const char* envRelease = "envRelease";
    inline constexpr const char* envGain    = "envGain";
    inline juce::String macro (int n)      { return "macro" + juce::String (n); }           // 1..4
}

// ---------------------------------------------------------------------------------------------
// Shared choice lists

enum TriggerMode { triggerFree = 0, triggerSync, triggerPattern };
enum WindowShape { windowHann = 0, windowTukey, windowTrapezoid, windowPerc, numWindowShapes };
enum Quality { qualityEco = 0, qualityNormal, qualityHigh };
enum FxPlace { placeFeedback = 0, placePost };
enum FilterType { filterLowpass = 0, filterHighpass, filterBandpass };
enum SatType { satSoft = 0, satHard };
enum ScaleType
{
    scaleOff = 0, scaleChromatic, scaleMajor, scaleMinor, scaleMajorPent, scaleMinorPent,
    scaleDorian, scalePhrygian, scaleWholeTone, scaleOctaves, scaleFifths, scaleCustom, numScales
};
enum LfoShape { lfoSine = 0, lfoTriangle, lfoSaw, lfoSquare, lfoSmoothRandom, lfoSampleHold, numLfoShapes };

// Note divisions shared by every tempo-synced parameter (length in quarter notes).
inline constexpr int numDivisions = 20;
inline constexpr std::array<double, numDivisions> divisionBeats {
    0.0625, 1.0 / 12.0, 0.125, 1.0 / 6.0, 0.1875, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0,
    0.75, 1.0, 4.0 / 3.0, 1.5, 2.0, 3.0, 4.0, 8.0, 16.0, 32.0
};
inline juce::StringArray divisionNames()
{
    return { "1/64", "1/32T", "1/32", "1/16T", "1/32.", "1/16", "1/8T", "1/16.", "1/8", "1/4T",
             "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1/2.", "1 bar", "2 bars", "4 bars", "8 bars" };
}
inline constexpr int divisionSixteenth = 5;
inline constexpr int divisionQuarter = 11;

// ---------------------------------------------------------------------------------------------
// Modulation sources and destinations. The order is saved in presets: append only.

enum ModSource
{
    srcLfo1 = 0, srcLfo2, srcLfo3, srcLfo4, srcRand1, srcRand2, srcEnv,
    srcMacro1, srcMacro2, srcMacro3, srcMacro4, numModSources
};

inline const char* modSourceName (int s)
{
    static const char* names[] = { "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Random 1", "Random 2",
                                   "Envelope", "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    return (s >= 0 && s < numModSources) ? names[s] : "None";
}

enum ModDest
{
    dstInputGain = 0, dstOutputGain, dstMix, dstPosition, dstScan, dstSize, dstDensity, dstSpray,
    dstJitter, dstPitch, dstFine, dstPitchRandom, dstReverse, dstWindowSkew, dstPanSpread, dstWidth,
    dstFeedback, dstDuckAmount,
    dstVoice1Interval, dstVoice2Interval, dstVoice3Interval, dstVoice4Interval,
    dstVoice1Prob, dstVoice2Prob, dstVoice3Prob, dstVoice4Prob,
    dstOctaveUp, dstOctaveDown,
    dstFilterCutoff, dstFilterReso, dstSatDrive,
    dstDiffSize, dstDiffDecay, dstDiffDamp, dstDiffMix,
    dstLfo1Rate, dstLfo2Rate, dstLfo3Rate, dstLfo4Rate, dstRand1Rate, dstRand2Rate,
    numModDests
};

/** Parameter identifier of a modulation destination. */
inline juce::String modDestParamId (int d)
{
    switch (d)
    {
        case dstInputGain:   return pid::inputGain;
        case dstOutputGain:  return pid::outputGain;
        case dstMix:         return pid::mix;
        case dstPosition:    return pid::position;
        case dstScan:        return pid::scan;
        case dstSize:        return pid::size;
        case dstDensity:     return pid::density;
        case dstSpray:       return pid::spray;
        case dstJitter:      return pid::jitter;
        case dstPitch:       return pid::pitch;
        case dstFine:        return pid::fine;
        case dstPitchRandom: return pid::pitchRandom;
        case dstReverse:     return pid::reverse;
        case dstWindowSkew:  return pid::windowSkew;
        case dstPanSpread:   return pid::panSpread;
        case dstWidth:       return pid::width;
        case dstFeedback:    return pid::feedback;
        case dstDuckAmount:  return pid::duckAmount;
        case dstVoice1Interval: case dstVoice2Interval: case dstVoice3Interval: case dstVoice4Interval:
            return pid::voiceInterval (d - dstVoice1Interval + 1);
        case dstVoice1Prob: case dstVoice2Prob: case dstVoice3Prob: case dstVoice4Prob:
            return pid::voiceProb (d - dstVoice1Prob + 1);
        case dstOctaveUp:     return pid::octaveUp;
        case dstOctaveDown:   return pid::octaveDown;
        case dstFilterCutoff: return pid::filterCutoff;
        case dstFilterReso:   return pid::filterReso;
        case dstSatDrive:     return pid::satDrive;
        case dstDiffSize:     return pid::diffSize;
        case dstDiffDecay:    return pid::diffDecay;
        case dstDiffDamp:     return pid::diffDamp;
        case dstDiffMix:      return pid::diffMix;
        case dstLfo1Rate: case dstLfo2Rate: case dstLfo3Rate: case dstLfo4Rate:
            return pid::lfoRate (d - dstLfo1Rate + 1);
        case dstRand1Rate: case dstRand2Rate:
            return pid::randRate (d - dstRand1Rate + 1);
        default: break;
    }
    return {};
}

inline constexpr int numModSlots = 32;
inline constexpr int numPatternSteps = 16;
inline constexpr int numHarmonyVoices = 4;
inline constexpr int numLfos = 4;
inline constexpr int numRandoms = 2;
inline constexpr int numMacros = 4;

// Parameter groups used by the randomiser and its locks.
enum ParamGroup { groupGrains = 0, groupPitch, groupEffects, groupModulation, numParamGroups, groupNone };

} // namespace grn
