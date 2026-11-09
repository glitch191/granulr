// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "../gui/MainPanel.h"
#include "../state/GlobalSettings.h"

/** Plugin window: scales the main panel (zoom), drives the frame pacing and owns the renderer. */
class GranulrEditor final : public juce::AudioProcessorEditor,
                            private juce::OpenGLRenderer
{
public:
    explicit GranulrEditor (GranulrProcessor&);
    ~GranulrEditor() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    float getZoom() const noexcept { return zoom; }

    /** Tests and screenshots: draw everything in software, whatever the user setting. */
    static inline std::atomic<bool> forceSoftwareRendering { false };
    void prepareForCapture (int selectedSource, bool showBrowser, bool showDiagnostics);
    void advanceFrameForCapture (double dtSeconds);
    void setZoom (float newZoom);

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void applyRenderMode();
    void updateRenderAreas();
    void onVBlank (double timestampSeconds);
    void stepZoom (int direction);
    void showSettingsMenu (juce::Component& target);
    juce::String diagnosticsText();

    GranulrProcessor& processor;
    grn::GranulrLookAndFeel lookAndFeel;
    juce::SharedResourcePointer<grn::GlobalSettings> settings;
    grn::UiContext context;
    grn::VisualEngine engine;
    grn::MainPanel panel;

    float zoom = grn::GlobalSettings::defaultZoom;
    bool constructed = false;
    bool gpuActive = false, diagnosticsOn = false;
    int frameMode = grn::frameDisplaySync, frameCap = 60;

    juce::OpenGLContext glContext;
    grn::GlCanvas canvas;
    grn::FrameStats frameStats;
    std::unique_ptr<juce::VBlankAttachment> vblank;

    // Shared with the render thread.
    juce::SpinLock areaLock;
    juce::Rectangle<float> grainArea, outputArea;
    std::atomic<float> zoomForRender { 1.25f };
    std::atomic<int> visualMode { 0 };
    std::atomic<bool> glFailed { false }, glReady { false };
    double lastRenderTime = 0.0; // render thread only
    juce::File captureFile;      // debugging aid, see renderOpenGL
    int captureCountdown = 0;

    // Message thread timing.
    double lastVBlankStamp = 0.0, vblankPeriod = 1.0 / 60.0, lastFrameTime = 0.0, lastDiagnostics = 0.0,
           glAttachTime = 0.0, wheelAccumulator = 0.0, captureClock = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GranulrEditor)
};
