// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Renders the real editor off screen (software path) while feeding it a synthetic signal,
// and saves a PNG. Used to check the layout at several zoom levels and sizes, and to produce
// the README screenshots.
//
//   granulr_shot --out file.png [--zoom 1.25] [--width 1180] [--height 740] [--mode 0..3]
//                [--mod 0|1] [--preset "Name"] [--select N] [--browser 1] [--diag 1]
#include "plugin/PluginEditor.h"
#include "state/PresetManager.h"
#include <iostream>

using namespace grn;

namespace
{
void setParam (GranulrProcessor& processor, const juce::String& identifier, float value)
{
    if (auto* param = processor.getState().getParameter (identifier))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void feed (GranulrProcessor& processor, double seconds, int64_t& position)
{
    const double sampleRate = 48000.0;
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    juce::Random random (4);
    const auto blocks = static_cast<int> (seconds * sampleRate / 256.0);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 256; ++i)
        {
            const double t = static_cast<double> (position + i) / sampleRate;
            const double envelope = std::exp (-4.0 * std::fmod (t, 0.75));
            const double chord = std::sin (6.2831853 * 220.0 * t) + 0.7 * std::sin (6.2831853 * 277.18 * t)
                               + 0.6 * std::sin (6.2831853 * 329.63 * t) + 0.3 * std::sin (6.2831853 * 880.0 * t);
            const float noise = std::fmod (t, 1.5) < 0.04 ? (random.nextFloat() - 0.5f) * 0.6f : 0.0f;
            buffer.setSample (0, i, static_cast<float> (0.22 * chord * envelope) + noise);
            buffer.setSample (1, i, static_cast<float> (0.22 * chord * envelope * (0.8 + 0.2 * std::sin (3.0 * t))) + noise);
        }
        processor.processBlock (buffer, midi);
        position += 256;
    }
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String out = "shot.png", preset;
    float zoom = 1.25f;
    int width = MainPanel::defaultWidth, height = MainPanel::defaultHeight, mode = 0, select = -1;
    bool mod = true, browser = false, diag = false;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const juce::String key (argv[i]), value (argv[i + 1]);
        if (key == "--out") out = value;
        else if (key == "--zoom") zoom = value.getFloatValue();
        else if (key == "--width") width = value.getIntValue();
        else if (key == "--height") height = value.getIntValue();
        else if (key == "--mode") mode = value.getIntValue();
        else if (key == "--mod") mod = value.getIntValue() != 0;
        else if (key == "--preset") preset = value;
        else if (key == "--select") select = value.getIntValue();
        else if (key == "--browser") browser = value.getIntValue() != 0;
        else if (key == "--diag") diag = value.getIntValue() != 0;
    }

    GranulrEditor::forceSoftwareRendering = true;

    GranulrProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    bool presetFound = false;
    auto& presets = processor.getPresets();
    for (int i = 0; i < presets.getNumPresets(); ++i)
        if (preset.isNotEmpty() && presets.getInfo (i).name == preset)
        {
            presets.load (i);
            presetFound = true;
        }
    if (! presetFound)
    {
        // A lively default picture: shimmer-like cloud with some modulation.
        setParam (processor, pid::mix, 70.0f);
        setParam (processor, pid::density, 45.0f);
        setParam (processor, pid::size, 180.0f);
        setParam (processor, pid::spray, 400.0f);
        setParam (processor, pid::pitchRandom, 0.0f);
        setParam (processor, pid::octaveUp, 35.0f);
        setParam (processor, pid::scale, static_cast<float> (scaleMajorPent));
        setParam (processor, pid::root, 9.0f);
        setParam (processor, pid::voiceProb (2), 40.0f);
        setParam (processor, pid::feedback, 35.0f);
        setParam (processor, pid::diffOn, 1.0f);
        setParam (processor, pid::filterOn, 1.0f);
        setParam (processor, pid::filterCutoff, 5200.0f);
        setParam (processor, pid::panSpread, 80.0f);
        setParam (processor, pid::macro (1), 60.0f);
        processor.getMatrix().setRouting (srcLfo1, dstPosition, 0.2f);
        processor.getMatrix().setRouting (srcLfo2, dstFilterCutoff, 0.3f);
        processor.getMatrix().setRouting (srcRand1, dstPanSpread, -0.25f);
        processor.getMatrix().setRouting (srcMacro1, dstDiffMix, 0.4f);
        processor.getMatrix().setRouting (srcEnv, dstDensity, 0.3f);
    }

    processor.getUiState().setProperty ("zoom", zoom, nullptr);
    processor.getUiState().setProperty ("width", width, nullptr);
    processor.getUiState().setProperty ("height", height, nullptr);
    processor.getUiState().setProperty ("visualMode", mode, nullptr);
    processor.getUiState().setProperty ("modOpen", mod, nullptr);

    std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
    auto* editor = dynamic_cast<GranulrEditor*> (base.get());
    if (editor == nullptr)
        return 2;
    editor->prepareForCapture (select, browser, diag);
    std::cout << "editor " << editor->getWidth() << " x " << editor->getHeight() << ", panel " << editor->getChildComponent (0)->getBounds().toString() << std::endl;

    // Run audio and frames together so that meters, spectrum and grains are in a live state.
    int64_t position = 0;
    for (int frame = 0; frame < 240; ++frame)
    {
        feed (processor, 1.0 / 60.0, position);
        editor->advanceFrameForCapture (1.0 / 60.0);
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile (out);
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::cout << "Could not write " << file.getFullPathName() << "\n";
        return 1;
    }
    std::cout << "Wrote " << file.getFullPathName() << " (" << image.getWidth() << " x " << image.getHeight() << ")\n";
    return 0;
}
