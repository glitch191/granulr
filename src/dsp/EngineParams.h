// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../params/ParameterIds.h"
#include <cstdint>

namespace grn
{

/** Control-rate snapshot of every value the engine needs, after modulation. */
struct EngineParams
{
    float inputGain = 1.0f;       // linear
    float outputGain = 1.0f;      // linear
    float mix = 0.5f;             // 0..1
    bool freeze = false;

    float positionMs = 250.0f;
    bool positionSync = false;
    int positionDiv = divisionQuarter;
    float scan = 0.0f;            // -2..2

    float sizeMs = 120.0f;
    bool sizeSync = false;
    int sizeDiv = divisionSixteenth;

    float density = 20.0f;        // grains per second (Free mode)
    int densityDiv = divisionSixteenth;
    int triggerMode = triggerFree;

    float sprayMs = 20.0f;
    float jitter = 0.1f;          // 0..1
    float pitch = 0.0f;           // semitones, fine tuning included
    float pitchRandom = 0.0f;     // semitones
    float reverse = 0.0f;         // probability 0..1
    int windowShape = windowHann;
    float windowSkew = 0.0f;      // -1..1
    float panSpread = 0.3f;       // 0..1
    float width = 1.0f;           // 0..2
    float feedback = 0.0f;        // 0..1
    float duckAmount = 0.0f;      // 0..1
    float duckAttackMs = 10.0f;
    float duckReleaseMs = 250.0f;
    bool limiter = true;
    int quality = qualityNormal;

    // Pitch and scale
    int scale = scaleOff;
    int root = 0;                 // 0 = C
    uint16_t customMask = 0x0fff; // bit n = pitch class n above the root is allowed
    float voiceInterval[numHarmonyVoices] { 0.0f, 7.0f, 12.0f, -12.0f };
    float voiceProb[numHarmonyVoices] { 1.0f, 0.0f, 0.0f, 0.0f };
    float octaveUp = 0.0f;        // probability 0..1
    float octaveDown = 0.0f;

    // Pattern
    int patternLength = numPatternSteps;
    int patternRate = divisionSixteenth;
    uint16_t gateMask = 0xffff;
    float stepProb[numPatternSteps] {};
    uint8_t stepRatchet[numPatternSteps] {};

    // Effects
    bool filterOn = false;
    int filterType = filterLowpass;
    float filterCutoff = 8000.0f;
    float filterReso = 0.2f;      // 0..1
    int filterPlace = placePost;
    bool satOn = false;
    int satType = satSoft;
    float satDriveDb = 6.0f;
    int satPlace = placePost;
    bool diffOn = false;
    float diffSize = 0.5f;
    float diffDecay = 0.5f;
    float diffDamp = 0.3f;
    float diffMix = 0.3f;
    int diffPlace = placePost;

    // Transport, valid for the first sample of the sub-block
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;

    EngineParams()
    {
        for (int i = 0; i < numPatternSteps; ++i)
        {
            stepProb[i] = 1.0f;
            stepRatchet[i] = 1;
        }
    }
};

} // namespace grn
