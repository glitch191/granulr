// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"
#include "../state/PresetManager.h"

using namespace grn;

namespace
{
    const float kZoomSteps[] { 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f };
    const int kFrameCaps[] { 30, 60, 120, 144, 240 };

    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
}

GranulrEditor::GranulrEditor (GranulrProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p), context (p), engine (p), panel (context, engine)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);
    setWantsKeyboardFocus (true);

    auto& ui = processor.getUiState();
    zoom = juce::jlimit (1.0f, 2.0f, static_cast<float> (ui.getProperty ("zoom", settings->getDefaultZoom())));
    const int logicalWidth = juce::jlimit (MainPanel::minWidth, MainPanel::maxWidth, static_cast<int> (ui.getProperty ("width", MainPanel::defaultWidth)));
    const int logicalHeight = juce::jlimit (MainPanel::minHeight, MainPanel::maxHeight, static_cast<int> (ui.getProperty ("height", MainPanel::defaultHeight)));
    frameMode = settings->getFrameMode();
    frameCap = settings->getFrameCap();

    panel.onZoomChosen = [this] (float chosen) { setZoom (chosen); };
    panel.onSettingsMenu = [this] (juce::Component& target) { showSettingsMenu (target); };
    panel.onLayoutChanged = [this] { updateRenderAreas(); };
    panel.setZoomDisplay (zoom);

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (MainPanel::minWidth * zoom), juce::roundToInt (MainPanel::minHeight * zoom),
                     juce::roundToInt (MainPanel::maxWidth * zoom), juce::roundToInt (MainPanel::maxHeight * zoom));
    constructed = true;
    setSize (juce::roundToInt (static_cast<float> (logicalWidth) * zoom), juce::roundToInt (static_cast<float> (logicalHeight) * zoom));

    resized(); // the size may already match, in which case setSize does not call it

    const auto capturePath = juce::SystemStats::getEnvironmentVariable ("GRANULR_GL_CAPTURE", {});
    if (capturePath.isNotEmpty())
        captureFile = juce::File (capturePath);

    processor.getBridge().editorOpen.store (true);
    applyRenderMode();
    vblank = std::make_unique<juce::VBlankAttachment> (this, [this] (double timestamp) { onVBlank (timestamp); });
}

GranulrEditor::~GranulrEditor()
{
    vblank.reset();
    if (gpuActive)
        glContext.detach();
    processor.getBridge().editorOpen.store (false);
    settings->flush();
    setLookAndFeel (nullptr);
}

void GranulrEditor::resized()
{
    if (! constructed)
        return;
    const int logicalWidth = juce::roundToInt (static_cast<float> (getWidth()) / zoom);
    const int logicalHeight = juce::roundToInt (static_cast<float> (getHeight()) / zoom);
    panel.setTransform (juce::AffineTransform::scale (zoom));
    panel.setBounds (0, 0, logicalWidth, logicalHeight);

    auto& ui = processor.getUiState();
    ui.setProperty ("width", logicalWidth, nullptr);
    ui.setProperty ("height", logicalHeight, nullptr);
    updateRenderAreas();
}

void GranulrEditor::setZoom (float newZoom)
{
    newZoom = juce::jlimit (1.0f, 2.0f, newZoom);
    if (std::abs (newZoom - zoom) < 0.001f)
    {
        panel.setZoomDisplay (zoom);
        return;
    }
    const int logicalWidth = panel.getWidth(), logicalHeight = panel.getHeight();
    zoom = newZoom;
    processor.getUiState().setProperty ("zoom", zoom, nullptr);
    settings->setDefaultZoom (zoom); // the last zoom chosen becomes the default of new instances
    panel.setZoomDisplay (zoom);
    setResizeLimits (juce::roundToInt (MainPanel::minWidth * zoom), juce::roundToInt (MainPanel::minHeight * zoom),
                     juce::roundToInt (MainPanel::maxWidth * zoom), juce::roundToInt (MainPanel::maxHeight * zoom));
    setSize (juce::roundToInt (static_cast<float> (logicalWidth) * zoom), juce::roundToInt (static_cast<float> (logicalHeight) * zoom));
    resized();
}

void GranulrEditor::stepZoom (int direction)
{
    int index = 0;
    float best = 10.0f;
    for (int i = 0; i < static_cast<int> (std::size (kZoomSteps)); ++i)
        if (std::abs (kZoomSteps[i] - zoom) < best)
        {
            best = std::abs (kZoomSteps[i] - zoom);
            index = i;
        }
    setZoom (kZoomSteps[juce::jlimit (0, static_cast<int> (std::size (kZoomSteps)) - 1, index + direction)]);
}

bool GranulrEditor::keyPressed (const juce::KeyPress& key)
{
    if (! key.getModifiers().isCtrlDown())
        return false;
    const auto code = key.getKeyCode();
    const auto character = key.getTextCharacter();
    if (code == '=' || code == '+' || character == '+' || character == '=' || code == juce::KeyPress::numberPadAdd)
    {
        stepZoom (1);
        return true;
    }
    if (code == '-' || character == '-' || code == juce::KeyPress::numberPadSubtract)
    {
        stepZoom (-1);
        return true;
    }
    if (code == '0' || code == juce::KeyPress::numberPad0)
    {
        setZoom (GlobalSettings::defaultZoom);
        return true;
    }
    if (code == 'Z' || code == 'z')
    {
        if (key.getModifiers().isShiftDown())
            processor.getUndoManager().redo();
        else
            processor.getUndoManager().undo();
        return true;
    }
    if (code == 'Y' || code == 'y')
    {
        processor.getUndoManager().redo();
        return true;
    }
    if (code == 'D' || code == 'd')
    {
        diagnosticsOn = ! diagnosticsOn;
        frameStats.clear();
        panel.setDiagnostics (diagnosticsOn, diagnosticsText());
        return true;
    }
    return false;
}

void GranulrEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! e.mods.isCtrlDown())
        return;
    wheelAccumulator += wheel.deltaY;
    if (std::abs (wheelAccumulator) >= 0.2)
    {
        stepZoom (wheelAccumulator > 0.0 ? 1 : -1);
        wheelAccumulator = 0.0;
    }
}

// ---------------------------------------------------------------------------------------------
void GranulrEditor::applyRenderMode()
{
    const bool wantGpu = settings->getGpuRendering() && ! glFailed.load() && ! forceSoftwareRendering.load();
    if (wantGpu && ! gpuActive)
    {
        juce::OpenGLPixelFormat format;
        format.multisamplingLevel = 4;
        glContext.setPixelFormat (format);
        glContext.setMultisamplingEnabled (true);
        glContext.setRenderer (this);
        glContext.setComponentPaintingEnabled (true);
        glContext.setContinuousRepainting (frameMode == frameUnlimited);
        glReady.store (false);
        glAttachTime = nowSeconds();
        glContext.attachTo (*this);
        gpuActive = true;
    }
    else if (! wantGpu && gpuActive)
    {
        glContext.detach();
        gpuActive = false;
    }

    if (gpuActive)
        glContext.setContinuousRepainting (frameMode == frameUnlimited);
    panel.setSoftwareRendering (! gpuActive);
    frameStats.clear();
    updateRenderAreas();
}

void GranulrEditor::updateRenderAreas()
{
    const juce::SpinLock::ScopedLockType lock (areaLock);
    grainArea = panel.grainPlot().toFloat();
    outputArea = panel.outputPlot().toFloat();
    zoomForRender.store (zoom);
}

void GranulrEditor::newOpenGLContextCreated()
{
    canvas.initialise (glContext);
    glContext.setSwapInterval (0); // pacing is done by the display's vertical blank, never by blocking swaps
    lastRenderTime = 0.0;
    glReady.store (true);
}

void GranulrEditor::openGLContextClosing()
{
    canvas.shutdown();
}

void GranulrEditor::renderOpenGL()
{
    using namespace juce::gl;
    const double now = nowSeconds();
    const double dt = lastRenderTime > 0.0 ? now - lastRenderTime : 1.0 / 60.0;
    lastRenderTime = now;
    frameStats.addFrame (dt);

    engine.update (dt);

    GLint viewport[4] { 0, 0, 1, 1 };
    glGetIntegerv (GL_VIEWPORT, viewport);

    // Debugging aid: when GRANULR_GL_CAPTURE names a PNG file, the frame shown on screen
    // (components included) is read back once and saved there.
    if (captureFile != juce::File() && ++captureCountdown == 30)
    {
        const int w = viewport[2], h = viewport[3];
        std::vector<uint8_t> pixels (static_cast<size_t> (w) * static_cast<size_t> (h) * 4);
        glReadBuffer (GL_FRONT);
        glPixelStorei (GL_PACK_ALIGNMENT, 1);
        glReadPixels (0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glReadBuffer (GL_BACK);
        juce::Image image (juce::Image::RGB, w, h, false);
        {
            juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const uint8_t* p = pixels.data() + (static_cast<size_t> (h - 1 - y) * static_cast<size_t> (w) + static_cast<size_t> (x)) * 4;
                    data.setPixelColour (x, y, juce::Colour (p[0], p[1], p[2]));
                }
        }
        captureFile.deleteFile();
        juce::FileOutputStream stream (captureFile);
        if (stream.openedOk())
            juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    juce::OpenGLHelpers::clear (Theme::get().background);

    juce::Rectangle<float> grains, output;
    {
        const juce::SpinLock::ScopedLockType lock (areaLock);
        grains = grainArea;
        output = outputArea;
    }
    const float scale = static_cast<float> (glContext.getRenderingScale()) * zoomForRender.load();

    canvas.begin (viewport[2], viewport[3], scale);
    engine.drawGrains (canvas, grains);
    engine.drawOutput (canvas, output, visualMode.load());
    canvas.end();
}

void GranulrEditor::onVBlank (double timestamp)
{
    const double now = nowSeconds();
    if (lastVBlankStamp > 0.0)
    {
        const double delta = timestamp - lastVBlankStamp;
        if (delta > 0.0005 && delta < 0.2)
            vblankPeriod += 0.1 * (delta - vblankPeriod);
    }
    lastVBlankStamp = timestamp;

    panel.refresh (now);
    visualMode.store (panel.getVisualMode());

    // Fall back to software drawing when the GPU path fails or never starts.
    if (gpuActive && (glFailed.load() || (! glReady.load() && now - glAttachTime > 4.0)))
    {
        glFailed.store (true);
        applyRenderMode();
    }

    // Frame pacing. Display sync draws one frame per vertical blank, whatever the refresh rate.
    // Capped skips blanks. Unlimited (GPU only) lets the render thread run freely.
    const double minInterval = frameMode == frameCapped ? 1.0 / static_cast<double> (frameCap) : 0.0;
    const bool due = now - lastFrameTime >= minInterval - 0.0005;
    // When nothing moves, a slow heartbeat is enough to notice when sound comes back.
    const bool wanted = engine.isActive() || now - lastFrameTime > 0.1;

    if (gpuActive)
    {
        if (frameMode != frameUnlimited && due && wanted)
        {
            glContext.triggerRepaint();
            lastFrameTime = now;
        }
    }
    else if (due && wanted)
    {
        const double dt = lastFrameTime > 0.0 ? now - lastFrameTime : vblankPeriod;
        engine.update (dt);
        frameStats.addFrame (dt);
        panel.repaintVisuals();
        lastFrameTime = now;
    }

    if (diagnosticsOn && now - lastDiagnostics > 0.25)
    {
        lastDiagnostics = now;
        panel.setDiagnostics (true, diagnosticsText());
    }
}

juce::String GranulrEditor::diagnosticsText()
{
    double displayHz = vblankPeriod > 0.0 ? 1.0 / vblankPeriod : 0.0;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        if (display->verticalFrequencyHz.has_value())
            displayHz = *display->verticalFrequencyHz;

    float fps = 0.0f, meanMs = 0.0f, worstMs = 0.0f;
    frameStats.summarise (fps, meanMs, worstMs);
    auto& bridge = processor.getBridge();
    const float cpuPeak = bridge.cpuPeak.exchange (0.0f) * 100.0f;
    static const char* modeNames[] { "Display sync", "Unlimited", "Capped" };

    juce::String text;
    text << "Display: " << juce::String (displayHz, 1) << " Hz\n"
         << "Frames: " << juce::String (fps, 1) << " fps, mean " << juce::String (meanMs, 2) << " ms\n"
         << "Worst 1 % of frames: " << juce::String (worstMs, 2) << " ms\n"
         << "Renderer: " << (gpuActive ? "OpenGL (GPU)" : "Software") << ", " << modeNames[juce::jlimit (0, 2, frameMode)];
    if (frameMode == frameCapped)
        text << " " << frameCap << " fps";
    text << "\nAudio CPU: " << juce::String (bridge.cpuAverage.load() * 100.0f, 2) << " % avg, " << juce::String (cpuPeak, 2) << " % peak\n"
         << "Grains: " << bridge.activeGrains.load() << ", " << juce::String (bridge.sampleRate.load(), 0) << " Hz, zoom "
         << juce::roundToInt (zoom * 100.0f) << " %";
    if (! engine.isActive())
        text << "\nIdle: drawing paused";
    return text;
}

void GranulrEditor::showSettingsMenu (juce::Component& target)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Frame rate");
    menu.addItem (1, "Display sync (follows the screen)", true, frameMode == frameDisplaySync);
    menu.addItem (2, "Unlimited (GPU only)", true, frameMode == frameUnlimited);
    juce::PopupMenu caps;
    for (int i = 0; i < static_cast<int> (std::size (kFrameCaps)); ++i)
        caps.addItem (10 + i, juce::String (kFrameCaps[i]) + " fps", true, frameMode == frameCapped && frameCap == kFrameCaps[i]);
    menu.addSubMenu ("Capped", caps, true, nullptr, frameMode == frameCapped);
    menu.addSeparator();
    menu.addItem (3, "GPU rendering", true, settings->getGpuRendering());
    menu.addItem (4, "Show diagnostics (Ctrl + D)", true, diagnosticsOn);
    menu.addSeparator();
    menu.addItem (5, "Open preset folder");
    menu.addItem (6, "About granulr");

    juce::Component::SafePointer<GranulrEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& self = *safe;
        if (result == 1 || result == 2 || result >= 10)
        {
            self.frameMode = result == 1 ? frameDisplaySync : (result == 2 ? frameUnlimited : frameCapped);
            if (result >= 10)
                self.frameCap = kFrameCaps[juce::jlimit (0, static_cast<int> (std::size (kFrameCaps)) - 1, result - 10)];
            self.settings->setFrameMode (self.frameMode);
            self.settings->setFrameCap (self.frameCap);
            self.applyRenderMode();
        }
        else if (result == 3)
        {
            self.settings->setGpuRendering (! self.settings->getGpuRendering());
            self.glFailed.store (false);
            self.applyRenderMode();
        }
        else if (result == 4)
        {
            self.diagnosticsOn = ! self.diagnosticsOn;
            self.frameStats.clear();
            self.panel.setDiagnostics (self.diagnosticsOn, self.diagnosticsText());
        }
        else if (result == 5)
        {
            const auto directory = PresetManager::getUserDirectory();
            directory.createDirectory();
            directory.revealToUser();
        }
        else if (result == 6)
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::InfoIcon)
                                              .withTitle ("granulr " GRANULR_VERSION_STRING)
                                              .withMessage ("Granular effect plugin.\n\nFree software under the GNU Affero General Public "
                                                            "License version 3. Built with the JUCE framework (AGPLv3).\n"
                                                            "Interface font: Inter (SIL Open Font License).")
                                              .withButton ("OK", 1)
                                              .withAssociatedComponent (&self),
                                          nullptr);
        }
    });
}

// ---------------------------------------------------------------------------------------------
void GranulrEditor::prepareForCapture (int selectedSource, bool showBrowser, bool showDiagnostics)
{
    if (selectedSource >= 0)
        context.selectSource (selectedSource);
    panel.showBrowser (showBrowser);
    diagnosticsOn = showDiagnostics;
}

void GranulrEditor::advanceFrameForCapture (double dtSeconds)
{
    captureClock += dtSeconds;
    engine.update (dtSeconds);
    frameStats.addFrame (dtSeconds);
    panel.refresh (captureClock);
    visualMode.store (panel.getVisualMode());
    panel.repaintVisuals();
    if (diagnosticsOn)
        panel.setDiagnostics (true, diagnosticsText());
}
