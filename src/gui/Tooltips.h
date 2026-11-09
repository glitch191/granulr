// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../params/ParameterIds.h"

namespace grn
{

/** Short help text for a parameter, shown as a tooltip. */
inline juce::String tooltipFor (const juce::String& id)
{
    struct Entry { const char* id; const char* text; };
    static const Entry entries[] = {
        { pid::inputGain,    "Input: level entering the plugin, before the grain buffer." },
        { pid::outputGain,   "Output: final level after the dry/wet mix." },
        { pid::mix,          "Mix: balance between the dry signal and the grains." },
        { pid::bypass,       "Bypass: passes the input through. The buffer keeps recording." },
        { pid::freeze,       "Freeze: stops recording and holds the buffer so grains keep playing it." },
        { pid::position,     "Position: how far behind the live input the grains start reading." },
        { pid::positionSync, "Position Sync: sets the position as a note value at the host tempo." },
        { pid::positionDiv,  "Position Division: note value used when Position Sync is on." },
        { pid::scan,         "Scan: moves the read position through the buffer over time. 0 keeps it fixed." },
        { pid::size,         "Size: duration of each grain." },
        { pid::sizeSync,     "Size Sync: sets the grain size as a note value at the host tempo." },
        { pid::sizeDiv,      "Size Division: note value used when Size Sync is on." },
        { pid::density,      "Density: grains started per second in Free mode." },
        { pid::densityDiv,   "Density Division: one grain per note value in Sync mode." },
        { pid::triggerMode,  "Trigger: Free runs at the density, Sync follows a note value, Pattern follows the step sequencer." },
        { pid::spray,        "Spray: random offset added to the read position of each grain." },
        { pid::jitter,       "Jitter: random timing of grain starts." },
        { pid::pitch,        "Pitch: transposition of the grains in semitones." },
        { pid::fine,         "Fine: transposition in cents." },
        { pid::pitchRandom,  "Pitch Random: random transposition range per grain." },
        { pid::reverse,      "Reverse: chance that a grain plays backwards." },
        { pid::windowShape,  "Window: envelope shape of each grain." },
        { pid::windowSkew,   "Skew: moves the peak of the grain envelope earlier or later." },
        { pid::panSpread,    "Pan Spread: random stereo position of each grain." },
        { pid::width,        "Width: stereo width of the grain cloud." },
        { pid::feedback,     "Feedback: amount of grain output recorded back into the buffer." },
        { pid::duckAmount,   "Duck: lowers the grains while the input is loud." },
        { pid::duckAttack,   "Duck Attack: how fast the grains are lowered." },
        { pid::duckRelease,  "Duck Release: how fast the grains come back." },
        { pid::limiter,      "Limiter: safety limiter on the output." },
        { pid::quality,      "Quality: Eco uses fewer grains and lighter processing, High allows the densest clouds." },
        { pid::scale,        "Scale: snaps grain pitches to a scale. Off leaves them free." },
        { pid::root,         "Root: root note of the scale." },
        { pid::octaveUp,     "Octave Up: chance that a grain jumps one octave up." },
        { pid::octaveDown,   "Octave Down: chance that a grain jumps one octave down." },
        { pid::patternLength, "Pattern Length: number of steps played before the pattern restarts." },
        { pid::patternRate,  "Pattern Rate: note value of one step." },
        { pid::filterOn,     "Filter: turns the filter on or off." },
        { pid::filterType,   "Filter Type: low-pass, high-pass or band-pass." },
        { pid::filterCutoff, "Filter Cutoff: corner frequency of the filter." },
        { pid::filterReso,   "Filter Resonance: emphasis around the cutoff." },
        { pid::filterPlace,  "Filter Placement: inside the feedback loop or after the grains." },
        { pid::satOn,        "Saturation: turns the saturation on or off." },
        { pid::satType,      "Saturation Type: soft (rounded) or hard (clipped)." },
        { pid::satDrive,     "Saturation Drive: amount of gain pushed into the saturation." },
        { pid::satPlace,     "Saturation Placement: inside the feedback loop or after the grains." },
        { pid::diffOn,       "Diffusion: turns the diffusion and reverb on or off." },
        { pid::diffSize,     "Diffusion Size: size of the simulated space." },
        { pid::diffDecay,    "Diffusion Decay: length of the tail." },
        { pid::diffDamp,     "Diffusion Damping: how fast high frequencies fade in the tail." },
        { pid::diffMix,      "Diffusion Mix: balance between direct grains and diffused sound." },
        { pid::diffPlace,    "Diffusion Placement: inside the feedback loop or after the grains." },
        { pid::envSource,    "Envelope Source: follow the input or the output level." },
        { pid::envAttack,    "Envelope Attack: how fast the envelope rises." },
        { pid::envRelease,   "Envelope Release: how fast the envelope falls." },
        { pid::envGain,      "Envelope Gain: sensitivity of the envelope follower." },
    };

    for (const auto& entry : entries)
        if (id == entry.id)
            return entry.text;

    const auto number = id.retainCharacters ("0123456789");
    if (id.startsWith ("customNote"))   return "Custom scale: allow or block this note.";
    if (id.endsWith ("Interval"))       return "Voice " + number + " Interval: transposition of this harmony voice.";
    if (id.startsWith ("voice"))        return "Voice " + number + " Chance: how often grains use this harmony voice.";
    if (id.startsWith ("step"))         return "Step " + number + ": gate, chance and ratchet of this pattern step.";
    if (id.startsWith ("macro"))        return "Macro " + number + ": drag its chip onto a knob to control several parameters at once.";
    if (id.startsWith ("lfo"))
    {
        if (id.endsWith ("Shape"))  return "LFO " + number + " Shape: waveform of the LFO.";
        if (id.endsWith ("Rate"))   return "LFO " + number + " Rate: speed of the LFO in hertz.";
        if (id.endsWith ("Sync"))   return "LFO " + number + " Sync: follow a note value at the host tempo.";
        if (id.endsWith ("Div"))    return "LFO " + number + " Division: note value of one LFO cycle.";
        if (id.endsWith ("Phase"))  return "LFO " + number + " Phase: start point of the waveform.";
        if (id.endsWith ("Retrig")) return "LFO " + number + " Retrigger: restart the LFO when the transport starts.";
    }
    if (id.startsWith ("rand"))
    {
        if (id.endsWith ("Rate"))   return "Random " + number + " Rate: how often a new random value is drawn.";
        if (id.endsWith ("Smooth")) return "Random " + number + " Smooth: glide between random values.";
        if (id.endsWith ("Sync"))   return "Random " + number + " Sync: follow a note value at the host tempo.";
        if (id.endsWith ("Div"))    return "Random " + number + " Division: note value between two random values.";
    }
    return {};
}

} // namespace grn
