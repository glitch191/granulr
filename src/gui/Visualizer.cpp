// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Visualizer.h"

namespace grn
{
namespace
{
    /** Packs a colour as R, G, B, A bytes in memory (the layout OpenGL expects for GL_RGBA). */
    uint32_t packRgba (juce::Colour c)
    {
        return static_cast<uint32_t> (c.getRed()) | (static_cast<uint32_t> (c.getGreen()) << 8)
             | (static_cast<uint32_t> (c.getBlue()) << 16) | (static_cast<uint32_t> (c.getAlpha()) << 24);
    }

    float decibels (float gain) { return 20.0f * std::log10 (std::max (gain, 1.0e-9f)); }

    constexpr float kMinFrequency = 20.0f;
    const char* const kModeNames[numVisualModes] { "Spectrum", "Spectrogram", "Scope", "Vector" };
    constexpr int kStripHeight = 20;
}

// ---------------------------------------------------------------------------------------------
VisualEngine::VisualEngine (GranulrProcessor& p)
    : processor (p), bridge (p.getBridge()),
      ringL (ringSize, 0.0f), ringR (ringSize, 0.0f), popL (4096, 0.0f), popR (4096, 0.0f),
      fftData (static_cast<size_t> (2 << maxOrder), 0.0f), window (static_cast<size_t> (1 << maxOrder), 0.0f),
      scratch (8192)
{
    const auto& t = Theme::get();
    juce::ColourGradient gradient (t.visualBack, 0.0f, 0.0f, juce::Colours::white, 1.0f, 0.0f, false);
    gradient.addColour (0.35, t.accentDim);
    gradient.addColour (0.65, t.accent);
    gradient.addColour (0.86, t.warning);
    for (int i = 0; i < 256; ++i)
        colourMap[static_cast<size_t> (i)] = packRgba (gradient.getColourAtPosition (static_cast<double> (i) / 255.0).withAlpha (1.0f));
    for (int c = 0; c < SpectrogramImage::width; ++c)
        std::fill_n (spectrogramImage.column (c), SpectrogramImage::height, colourMap[0]);
}

void VisualEngine::update (double dt)
{
    dt = juce::jlimit (0.0, 0.25, dt);
    const double fs = bridge.sampleRate.load();
    if (std::abs (fs - sampleRate) > 0.5 || window[1] == 0.0f)
    {
        sampleRate = fs;
        fftOrder = fs <= 50000.0 ? 12 : (fs <= 100000.0 ? 13 : 14);
        const int size = 1 << fftOrder;
        hopSize = size / 4;
        columnPeriod = static_cast<double> (hopSize) / sampleRate;
        for (int i = 0; i < size; ++i)
            window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (size - 1));
    }

    // Pull the new output samples into the local ring.
    int received = 0;
    float newPeak = 0.0f;
    for (;;)
    {
        const int n = bridge.popOutput (popL.data(), popR.data(), static_cast<int> (popL.size()));
        if (n <= 0)
            break;
        for (int i = 0; i < n; ++i)
        {
            ringL[static_cast<size_t> (ringWrite)] = popL[static_cast<size_t> (i)];
            ringR[static_cast<size_t> (ringWrite)] = popR[static_cast<size_t> (i)];
            ringWrite = (ringWrite + 1) & (ringSize - 1);
            newPeak = std::max (newPeak, std::max (std::abs (popL[static_cast<size_t> (i)]), std::abs (popR[static_cast<size_t> (i)])));
        }
        received += n;
        if (received >= ringSize)
            break;
    }

    sinceHop += received;
    if (sinceHop >= hopSize)
    {
        // At most a few columns per frame: if the display is slower than the hop rate, skip ahead.
        const int columns = std::min (4, sinceHop / hopSize);
        sinceHop %= hopSize;
        for (int c = 0; c < columns; ++c)
            analyse();
        secondsSinceColumn = 0.0;
    }
    else
    {
        secondsSinceColumn += dt;
    }

    // Time-based smoothing: identical look at 60, 144 or 360 Hz.
    const auto rise = static_cast<float> (1.0 - std::exp (-dt / 0.02));
    const auto fall = static_cast<float> (1.0 - std::exp (-dt / 0.12));
    const auto peakFall = static_cast<float> (dt * 0.15);
    for (size_t i = 0; i < static_cast<size_t> (spectrumBins); ++i)
    {
        shown[i] += (target[i] > shown[i] ? rise : fall) * (target[i] - shown[i]);
        peak[i] = std::max (shown[i], peak[i] - peakFall);
    }

    // Meters: input L/R, output L/R, with a 36 dB per second fall.
    std::atomic<float>* sources[4] { &bridge.inputPeak[0], &bridge.inputPeak[1], &bridge.outputPeak[0], &bridge.outputPeak[1] };
    const auto meterFall = static_cast<float> (std::pow (10.0, -36.0 * dt / 20.0));
    for (int m = 0; m < 4; ++m)
    {
        const float latest = sources[m]->exchange (0.0f, std::memory_order_relaxed);
        meterLevel[m] = std::max (latest, meterLevel[m] * meterFall);
        meterHold[m] = std::max (latest, meterHold[m] * static_cast<float> (std::pow (10.0, -9.0 * dt / 20.0)));
    }

    if (bridge.grains.update())
        grainFrame = bridge.grains.read();

    const bool moving = newPeak > 1.0e-5f || grainFrame.count > 0 || meterLevel[2] > 1.0e-4f || meterLevel[0] > 1.0e-4f;
    idleSeconds = moving ? 0.0 : idleSeconds + dt;
    active.store (idleSeconds < 2.0, std::memory_order_relaxed);
}

void VisualEngine::analyse()
{
    const int size = 1 << fftOrder;
    const int startIndex = ringWrite - size;
    float sumLR = 0.0f, sumLL = 0.0f, sumRR = 0.0f;
    for (int i = 0; i < size; ++i)
    {
        const auto index = static_cast<size_t> ((startIndex + i) & (ringSize - 1));
        const float l = ringL[index], r = ringR[index];
        fftData[static_cast<size_t> (i)] = 0.5f * (l + r) * window[static_cast<size_t> (i)];
        sumLR += l * r;
        sumLL += l * l;
        sumRR += r * r;
    }
    std::fill (fftData.begin() + size, fftData.begin() + 2 * size, 0.0f);
    const float denominator = std::sqrt (sumLL * sumRR);
    correlation += 0.3f * ((denominator > 1.0e-9f ? sumLR / denominator : 0.0f) - correlation);

    auto& fft = fftOrder == 12 ? fft12 : (fftOrder == 13 ? fft13 : fft14);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    // Log-frequency bins from 20 Hz to the top of the audible range.
    const float maxFrequency = std::min (20000.0f, static_cast<float> (sampleRate) * 0.5f);
    const float ratio = maxFrequency / kMinFrequency;
    const float binsPerHz = static_cast<float> (size) / static_cast<float> (sampleRate);
    const float scale = 4.0f / static_cast<float> (size);
    uint32_t* column = spectrogramImage.column (spectrogramImage.writeColumn);
    for (int b = 0; b < spectrumBins; ++b)
    {
        const float f0 = kMinFrequency * std::pow (ratio, static_cast<float> (b) / static_cast<float> (spectrumBins));
        const float f1 = kMinFrequency * std::pow (ratio, static_cast<float> (b + 1) / static_cast<float> (spectrumBins));
        const float x0 = f0 * binsPerHz, x1 = f1 * binsPerHz;
        const int i0 = static_cast<int> (x0);
        const int i1 = std::min (size / 2 - 1, static_cast<int> (x1));
        float magnitude;
        if (i1 <= i0)
        {
            const float frac = x0 - static_cast<float> (i0);
            magnitude = fftData[static_cast<size_t> (i0)] + frac * (fftData[static_cast<size_t> (std::min (i0 + 1, size / 2 - 1))] - fftData[static_cast<size_t> (i0)]);
        }
        else
        {
            magnitude = 0.0f;
            for (int i = i0; i <= i1; ++i)
                magnitude = std::max (magnitude, fftData[static_cast<size_t> (i)]);
        }
        const float value = juce::jlimit (0.0f, 1.0f, (decibels (magnitude * scale) - minDb) / -minDb);
        target[static_cast<size_t> (b)] = value;
        column[b] = colourMap[static_cast<size_t> (juce::jlimit (0, 255, static_cast<int> (value * 255.0f)))];
    }
    spectrogramImage.writeColumn = (spectrogramImage.writeColumn + 1) % SpectrogramImage::width;
    ++spectrogramImage.columnCounter;
}

// ---------------------------------------------------------------------------------------------
void VisualEngine::drawGrains (Canvas& canvas, juce::Rectangle<float> area)
{
    const auto& t = Theme::get();
    canvas.setClip (area);
    canvas.fillRect (area, t.visualBack);

    const float w = area.getWidth(), h = area.getHeight();
    const float mid = area.getCentreY();
    const bool frozen = grainFrame.frozen;

    for (int s = 1; s < 8; ++s)
    {
        const float x = area.getRight() - w * static_cast<float> (s) / 8.0f;
        canvas.fillRect ({ x, area.getY(), 1.0f, h }, t.grid);
    }
    canvas.fillRect ({ area.getX(), mid, w, 1.0f }, t.grid);

    // Buffer waveform: newest audio on the right.
    auto& engine = processor.getEngine();
    constexpr int bars = 512;
    const float barWidth = w / static_cast<float> (bars);
    const auto waveColour = frozen ? t.frozen.withMultipliedAlpha (0.55f) : t.waveform;
    for (int i = 0; i < bars; ++i)
    {
        const float level = std::max (engine.getOverviewPeak (i * 2), engine.getOverviewPeak (i * 2 + 1));
        if (level < 1.0e-4f)
            continue;
        const float mapped = juce::jlimit (0.0f, 1.0f, 1.0f + decibels (level) / 54.0f);
        const float half = std::max (0.5f, mapped * h * 0.46f);
        canvas.fillRect ({ area.getRight() - static_cast<float> (i + 1) * barWidth, mid - half, barWidth + 0.35f, half * 2.0f }, waveColour);
    }

    // Position and spray range.
    auto& state = processor.getState();
    if (state.getRawParameterValue (pid::positionSync)->load() < 0.5f)
    {
        const float position = state.getRawParameterValue (pid::position)->load() / (GrainEngine::bufferSeconds * 1000.0f);
        const float spray = state.getRawParameterValue (pid::spray)->load() / (GrainEngine::bufferSeconds * 1000.0f);
        const float x = area.getRight() - position * w;
        canvas.fillRect ({ x - spray * w, area.getY(), spray * w, h }, t.accent.withAlpha (0.07f));
        canvas.fillRect ({ x - 0.5f, area.getY(), 1.0f, h }, t.accent.withAlpha (0.55f));
    }

    // Grains: horizontal position is the read point, vertical position the pitch, colour the pan.
    for (int i = 0; i < grainFrame.count; ++i)
    {
        const auto& view = grainFrame.views[static_cast<size_t> (i)];
        if (view.level <= 0.001f)
            continue;
        const float x = area.getRight() - juce::jlimit (0.0f, 1.0f, view.delayNorm) * w;
        const float y = mid - juce::jlimit (-1.0f, 1.0f, view.pitch / 24.0f) * (h * 0.5f - 8.0f);
        // Far grains (Distance) are drawn smaller and fainter.
        const float nearness = 1.0f - 0.6f * juce::jlimit (0.0f, 1.0f, view.depth);
        const float size = (3.0f + 9.0f * view.level) * nearness;
        const auto colour = t.grain.interpolatedWith (t.warning, juce::jlimit (0.0f, 1.0f, view.pan))
                                .withAlpha ((0.25f + 0.75f * view.level) * nearness);
        canvas.fillRect ({ x - 1.5f, y - size * 0.5f, 3.0f, size }, colour);
    }

    // Recorded gesture: its path, and a marker where the playback is now.
    {
        GestureData local;
        bool recording = false;
        {
            const juce::SpinLock::ScopedLockType lock (overlayLock);
            local.count = overlay.count;
            recording = overlayRecording;
            if (local.count > 1)
                local = overlay;
        }
        if (local.count > 1)
        {
            auto toPoint = [&] (float gx, float gy)
            {
                return juce::Point<float> (area.getCentreX() + gx * w * 0.5f, mid - gy * (h * 0.5f - 8.0f));
            };
            if (static_cast<int> (scratch.size()) < local.count)
                scratch.resize (static_cast<size_t> (local.count));
            for (int i = 0; i < local.count; ++i)
                scratch[static_cast<size_t> (i)] = toPoint (local.x[static_cast<size_t> (i)], local.y[static_cast<size_t> (i)]);
            const auto colour = recording ? t.meterClip : t.sourceColour (srcGestureX);
            canvas.polyline (scratch.data(), local.count, 1.5f, colour.withAlpha (recording ? 0.9f : 0.55f));
            if (! recording)
            {
                const auto now = toPoint (bridge.sourceValue[srcGestureX].load (std::memory_order_relaxed),
                                          bridge.sourceValue[srcGestureY].load (std::memory_order_relaxed));
                canvas.fillRect ({ now.x - 3.0f, now.y - 3.0f, 6.0f, 6.0f }, colour);
            }
        }
    }

    canvas.fillRect ({ area.getRight() - 2.0f, area.getY(), 2.0f, h }, frozen ? t.frozen : t.accent);
}

void VisualEngine::setGestureOverlay (const GestureData& data, bool recording)
{
    const juce::SpinLock::ScopedLockType lock (overlayLock);
    overlay = data;
    overlayRecording = recording;
}

void VisualEngine::drawOutput (Canvas& canvas, juce::Rectangle<float> area, int mode)
{
    const auto& t = Theme::get();
    canvas.setClip (area);
    canvas.fillRect (area, t.visualBack);
    const auto meters = area.removeFromRight (static_cast<float> (meterWidth));
    drawMeters (canvas, meters);
    canvas.setClip (area);

    switch (mode)
    {
        case visSpectrogram:
            canvas.spectrogram (area, spectrogramImage,
                                static_cast<float> (juce::jlimit (0.0, 1.0, secondsSinceColumn / columnPeriod)));
            break;
        case visScope:  drawScope (canvas, area); break;
        case visVector: drawVector (canvas, area); break;
        default:        drawSpectrum (canvas, area); break;
    }
}

void VisualEngine::drawSpectrum (Canvas& canvas, juce::Rectangle<float> area)
{
    const auto& t = Theme::get();
    const float maxFrequency = std::min (20000.0f, static_cast<float> (sampleRate) * 0.5f);
    const float logRange = std::log (maxFrequency / kMinFrequency);

    for (float frequency : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = area.getX() + area.getWidth() * std::log (frequency / kMinFrequency) / logRange;
        canvas.fillRect ({ x, area.getY(), 1.0f, area.getHeight() }, t.grid);
    }
    for (float db = -18.0f; db > minDb; db -= 18.0f)
        canvas.fillRect ({ area.getX(), area.getY() + area.getHeight() * (db / minDb), area.getWidth(), 1.0f }, t.grid);

    auto* points = scratch.data();
    for (int b = 0; b < spectrumBins; ++b)
    {
        const float x = area.getX() + area.getWidth() * (static_cast<float> (b) + 0.5f) / static_cast<float> (spectrumBins);
        points[b] = { x, area.getBottom() - shown[static_cast<size_t> (b)] * area.getHeight() };
    }
    canvas.fillUnder (points, spectrumBins, area.getBottom(), t.accent.withAlpha (0.16f));
    canvas.polyline (points, spectrumBins, 1.5f, t.accent);

    for (int b = 0; b < spectrumBins; ++b)
        points[b].y = area.getBottom() - peak[static_cast<size_t> (b)] * area.getHeight();
    canvas.polyline (points, spectrumBins, 1.0f, t.textFaint.withAlpha (0.8f));
}

void VisualEngine::drawScope (Canvas& canvas, juce::Rectangle<float> area)
{
    const auto& t = Theme::get();
    canvas.fillRect ({ area.getX(), area.getCentreY(), area.getWidth(), 1.0f }, t.grid);

    const int length = juce::jlimit (256, ringSize / 4, static_cast<int> (sampleRate * 0.03));
    // Stable picture: start on a rising zero crossing when there is one.
    int start = ringWrite - length;
    for (int back = 0; back < length; ++back)
    {
        const auto i0 = static_cast<size_t> ((ringWrite - length - back - 1) & (ringSize - 1));
        const auto i1 = static_cast<size_t> ((ringWrite - length - back) & (ringSize - 1));
        if (ringL[i0] + ringR[i0] <= 0.0f && ringL[i1] + ringR[i1] > 0.0f)
        {
            start = ringWrite - length - back;
            break;
        }
    }

    const int numPoints = std::min (length, std::min (static_cast<int> (scratch.size()), 2048));
    auto* points = scratch.data();
    const float half = area.getHeight() * 0.48f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& ring = ch == 0 ? ringL : ringR;
        for (int p = 0; p < numPoints; ++p)
        {
            const int offset = static_cast<int> (static_cast<int64_t> (p) * (length - 1) / std::max (1, numPoints - 1));
            const float value = ring[static_cast<size_t> ((start + offset) & (ringSize - 1))];
            points[p] = { area.getX() + area.getWidth() * static_cast<float> (p) / static_cast<float> (numPoints - 1),
                          area.getCentreY() - juce::jlimit (-1.0f, 1.0f, value) * half };
        }
        canvas.polyline (points, numPoints, 1.3f, ch == 0 ? t.accent : t.warning.withAlpha (0.7f));
    }
}

void VisualEngine::drawVector (Canvas& canvas, juce::Rectangle<float> area)
{
    const auto& t = Theme::get();
    const auto centre = area.getCentre();
    const float radius = std::min (area.getWidth(), area.getHeight() - 14.0f) * 0.46f;
    canvas.line ({ centre.x - radius, centre.y - radius }, { centre.x + radius, centre.y + radius }, 1.0f, t.grid);
    canvas.line ({ centre.x + radius, centre.y - radius }, { centre.x - radius, centre.y + radius }, 1.0f, t.grid);
    canvas.fillRect ({ centre.x, centre.y - radius, 1.0f, radius * 2.0f }, t.grid);

    const int step = std::max (1, static_cast<int> (sampleRate / 48000.0));
    constexpr int numPoints = 1500;
    for (int p = 0; p < numPoints; ++p)
    {
        const auto index = static_cast<size_t> ((ringWrite - 1 - p * step) & (ringSize - 1));
        const float l = ringL[index], r = ringR[index];
        const float x = juce::jlimit (-1.0f, 1.0f, (r - l) * 0.7071f);
        const float y = juce::jlimit (-1.0f, 1.0f, (l + r) * 0.7071f);
        const float alpha = 0.75f * (1.0f - static_cast<float> (p) / static_cast<float> (numPoints));
        canvas.fillRect ({ centre.x + x * radius - 1.0f, centre.y - y * radius - 1.0f, 2.0f, 2.0f }, t.accent.withAlpha (alpha));
    }

    // Correlation: -1 on the left, +1 on the right.
    const auto bar = juce::Rectangle<float> (area.getX() + 12.0f, area.getBottom() - 9.0f, area.getWidth() - 24.0f, 4.0f);
    canvas.fillRect (bar, t.control);
    const float x = bar.getCentreX() + correlation * bar.getWidth() * 0.5f;
    canvas.fillRect ({ std::min (x, bar.getCentreX()), bar.getY(), std::abs (x - bar.getCentreX()), bar.getHeight() },
                     correlation < 0.0f ? t.danger : t.accent);
}

void VisualEngine::drawMeters (Canvas& canvas, juce::Rectangle<float> area)
{
    const auto& t = Theme::get();
    canvas.fillRect (area, t.panel);
    const auto inner = area.reduced (4.0f, 5.0f);
    const float barWidth = 5.0f;
    const float xs[4] { inner.getX(), inner.getX() + barWidth + 1.0f, inner.getRight() - 2.0f * barWidth - 1.0f, inner.getRight() - barWidth };
    for (int m = 0; m < 4; ++m)
    {
        const juce::Rectangle<float> track (xs[m], inner.getY(), barWidth, inner.getHeight());
        canvas.fillRect (track, t.visualBack);
        const float level = juce::jlimit (0.0f, 1.0f, 1.0f + decibels (meterLevel[m]) / 60.0f);
        const float hold = juce::jlimit (0.0f, 1.0f, 1.0f + decibels (meterHold[m]) / 60.0f);
        const auto colour = meterLevel[m] >= 0.999f ? t.meterClip : (level > 0.85f ? t.meterHigh : (m < 2 ? t.textFaint : t.meterLow));
        canvas.fillRect (track.withTop (track.getBottom() - level * track.getHeight()), colour);
        if (hold > 0.01f)
            canvas.fillRect ({ track.getX(), track.getBottom() - hold * track.getHeight(), barWidth, 1.0f }, t.text.withAlpha (0.8f));
    }
}

// ---------------------------------------------------------------------------------------------
void GlCanvas::initialise (juce::OpenGLContext& c)
{
    using namespace juce::gl;
    context = &c;
    vertices.reserve (600000);

    static const char* colourVertex =
        "attribute vec2 position;\n"
        "attribute vec4 colour;\n"
        "uniform vec2 viewport;\n"
        "varying vec4 vColour;\n"
        "void main()\n"
        "{\n"
        "    vColour = colour;\n"
        "    vec2 p = position / viewport * 2.0 - 1.0;\n"
        "    gl_Position = vec4 (p.x, -p.y, 0.0, 1.0);\n"
        "}\n";
    static const char* colourFragment =
        "varying vec4 vColour;\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = vColour;\n"
        "}\n";
    static const char* textureVertex =
        "attribute vec2 position;\n"
        "attribute vec2 texCoord;\n"
        "uniform vec2 viewport;\n"
        "varying vec2 vTex;\n"
        "void main()\n"
        "{\n"
        "    vTex = texCoord;\n"
        "    vec2 p = position / viewport * 2.0 - 1.0;\n"
        "    gl_Position = vec4 (p.x, -p.y, 0.0, 1.0);\n"
        "}\n";
    static const char* textureFragment =
        "varying vec2 vTex;\n"
        "uniform sampler2D image;\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = texture2D (image, vTex);\n"
        "}\n";

    auto build = [&c] (const char* vertexSource, const char* fragmentSource)
    {
        auto program = std::make_unique<juce::OpenGLShaderProgram> (c);
        if (program->addVertexShader (juce::OpenGLHelpers::translateVertexShaderToV3 (vertexSource))
            && program->addFragmentShader (juce::OpenGLHelpers::translateFragmentShaderToV3 (fragmentSource))
            && program->link())
            return program;
        jassertfalse;
        return std::unique_ptr<juce::OpenGLShaderProgram>();
    };
    colourProgram = build (colourVertex, colourFragment);
    textureProgram = build (textureVertex, textureFragment);

    glGenBuffers (1, &vertexBuffer);
    glGenTextures (1, &texture);
    glBindTexture (GL_TEXTURE_2D, texture);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, SpectrogramImage::width, SpectrogramImage::height, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture (GL_TEXTURE_2D, 0);
    textureReady = false;
    uploadedColumns = 0;
}

void GlCanvas::shutdown()
{
    using namespace juce::gl;
    colourProgram.reset();
    textureProgram.reset();
    if (vertexBuffer != 0)
        glDeleteBuffers (1, &vertexBuffer);
    if (texture != 0)
        glDeleteTextures (1, &texture);
    vertexBuffer = texture = 0;
    context = nullptr;
}

void GlCanvas::begin (int width, int height, float renderScale)
{
    using namespace juce::gl;
    viewportW = std::max (1, width);
    viewportH = std::max (1, height);
    scale = renderScale;
    vertices.clear();
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_CULL_FACE);
    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GlCanvas::end()
{
    using namespace juce::gl;
    flush();
    glDisable (GL_SCISSOR_TEST);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    glUseProgram (0);
}

void GlCanvas::setClip (juce::Rectangle<float> area)
{
    using namespace juce::gl;
    flush();
    const auto physical = (area * scale).getSmallestIntegerContainer();
    glEnable (GL_SCISSOR_TEST);
    glScissor (physical.getX(), viewportH - physical.getBottom(), physical.getWidth(), physical.getHeight());
}

void GlCanvas::flush()
{
    using namespace juce::gl;
    if (vertices.empty() || colourProgram == nullptr)
    {
        vertices.clear();
        return;
    }
    colourProgram->use();
    colourProgram->setUniform ("viewport", static_cast<float> (viewportW), static_cast<float> (viewportH));
    glBindBuffer (GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData (GL_ARRAY_BUFFER, static_cast<GLsizeiptr> (vertices.size() * sizeof (float)), vertices.data(), GL_STREAM_DRAW);
    const auto program = colourProgram->getProgramID();
    const auto positionLocation = static_cast<GLuint> (glGetAttribLocation (program, "position"));
    const auto colourLocation = static_cast<GLuint> (glGetAttribLocation (program, "colour"));
    glVertexAttribPointer (positionLocation, 2, GL_FLOAT, GL_FALSE, 6 * sizeof (float), nullptr);
    glVertexAttribPointer (colourLocation, 4, GL_FLOAT, GL_FALSE, 6 * sizeof (float),
                           reinterpret_cast<const void*> (2 * sizeof (float)));
    glEnableVertexAttribArray (positionLocation);
    glEnableVertexAttribArray (colourLocation);
    glDrawArrays (GL_TRIANGLES, 0, static_cast<GLsizei> (vertices.size() / 6));
    glDisableVertexAttribArray (positionLocation);
    glDisableVertexAttribArray (colourLocation);
    vertices.clear();
}

void GlCanvas::vertex (float x, float y, const float* rgba)
{
    if (vertices.size() + 6 > vertices.capacity())
        flush(); // never grow on the render path
    vertices.insert (vertices.end(), { x * scale, y * scale, rgba[0], rgba[1], rgba[2], rgba[3] });
}

void GlCanvas::quad (juce::Point<float> a, juce::Point<float> b, juce::Point<float> c, juce::Point<float> d, juce::Colour colour)
{
    const float rgba[4] { colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue(), colour.getFloatAlpha() };
    vertex (a.x, a.y, rgba);
    vertex (b.x, b.y, rgba);
    vertex (c.x, c.y, rgba);
    vertex (a.x, a.y, rgba);
    vertex (c.x, c.y, rgba);
    vertex (d.x, d.y, rgba);
}

void GlCanvas::fillRect (juce::Rectangle<float> r, juce::Colour colour)
{
    quad (r.getTopLeft(), r.getTopRight(), r.getBottomRight(), r.getBottomLeft(), colour);
}

void GlCanvas::line (juce::Point<float> a, juce::Point<float> b, float thickness, juce::Colour colour)
{
    const auto delta = b - a;
    const float length = delta.getDistanceFromOrigin();
    if (length < 1.0e-4f)
        return;
    const juce::Point<float> normal (-delta.y / length * thickness * 0.5f, delta.x / length * thickness * 0.5f);
    quad (a + normal, b + normal, b - normal, a - normal, colour);
}

void GlCanvas::polyline (const juce::Point<float>* points, int numPoints, float thickness, juce::Colour colour)
{
    for (int i = 0; i + 1 < numPoints; ++i)
        line (points[i], points[i + 1], thickness, colour);
}

void GlCanvas::fillUnder (const juce::Point<float>* points, int numPoints, float baseY, juce::Colour colour)
{
    for (int i = 0; i + 1 < numPoints; ++i)
        quad (points[i], points[i + 1], { points[i + 1].x, baseY }, { points[i].x, baseY }, colour);
}

void GlCanvas::spectrogram (juce::Rectangle<float> area, const SpectrogramImage& image, float scrollFraction)
{
    using namespace juce::gl;
    flush();
    if (textureProgram == nullptr)
        return;

    glBindTexture (GL_TEXTURE_2D, texture);
    const uint32_t pending = textureReady ? image.columnCounter - uploadedColumns : static_cast<uint32_t> (SpectrogramImage::width);
    const int count = static_cast<int> (std::min<uint32_t> (pending, static_cast<uint32_t> (SpectrogramImage::width)));
    for (int k = 0; k < count; ++k)
    {
        const int column = ((image.writeColumn - count + k) % SpectrogramImage::width + SpectrogramImage::width) % SpectrogramImage::width;
        glTexSubImage2D (GL_TEXTURE_2D, 0, column, 0, 1, SpectrogramImage::height, GL_RGBA, GL_UNSIGNED_BYTE, image.column (column));
    }
    uploadedColumns = image.columnCounter;
    textureReady = true;

    // The newest column sits just left of writeColumn. One column is kept off screen so the
    // picture can slide smoothly between two analysis frames.
    const float w = static_cast<float> (SpectrogramImage::width);
    const float u1 = (static_cast<float> (image.writeColumn) - 1.0f + scrollFraction) / w;
    const float u0 = u1 - (w - 2.0f) / w;
    const float x0 = area.getX() * scale, x1 = area.getRight() * scale;
    const float y0 = area.getY() * scale, y1 = area.getBottom() * scale;
    const float quadVertices[] { x0, y0, u0, 1.0f,  x1, y0, u1, 1.0f,  x1, y1, u1, 0.0f,
                                 x0, y0, u0, 1.0f,  x1, y1, u1, 0.0f,  x0, y1, u0, 0.0f };

    textureProgram->use();
    textureProgram->setUniform ("viewport", static_cast<float> (viewportW), static_cast<float> (viewportH));
    textureProgram->setUniform ("image", 0);
    glActiveTexture (GL_TEXTURE0);
    glBindBuffer (GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (quadVertices), quadVertices, GL_STREAM_DRAW);
    const auto program = textureProgram->getProgramID();
    const auto positionLocation = static_cast<GLuint> (glGetAttribLocation (program, "position"));
    const auto texLocation = static_cast<GLuint> (glGetAttribLocation (program, "texCoord"));
    glVertexAttribPointer (positionLocation, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), nullptr);
    glVertexAttribPointer (texLocation, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), reinterpret_cast<const void*> (2 * sizeof (float)));
    glEnableVertexAttribArray (positionLocation);
    glEnableVertexAttribArray (texLocation);
    glDrawArrays (GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray (positionLocation);
    glDisableVertexAttribArray (texLocation);
    glBindTexture (GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------------------------
void GraphicsCanvas::fillRect (juce::Rectangle<float> area, juce::Colour colour)
{
    g.setColour (colour);
    g.fillRect (area);
}

void GraphicsCanvas::line (juce::Point<float> a, juce::Point<float> b, float thickness, juce::Colour colour)
{
    g.setColour (colour);
    g.drawLine ({ a, b }, thickness);
}

void GraphicsCanvas::polyline (const juce::Point<float>* points, int numPoints, float thickness, juce::Colour colour)
{
    if (numPoints < 2)
        return;
    juce::Path path;
    path.preallocateSpace (numPoints * 3 + 8);
    path.startNewSubPath (points[0]);
    for (int i = 1; i < numPoints; ++i)
        path.lineTo (points[i]);
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (thickness));
}

void GraphicsCanvas::fillUnder (const juce::Point<float>* points, int numPoints, float baseY, juce::Colour colour)
{
    if (numPoints < 2)
        return;
    juce::Path path;
    path.preallocateSpace (numPoints * 3 + 16);
    path.startNewSubPath (points[0].x, baseY);
    for (int i = 0; i < numPoints; ++i)
        path.lineTo (points[i]);
    path.lineTo (points[numPoints - 1].x, baseY);
    path.closeSubPath();
    g.setColour (colour);
    g.fillPath (path);
}

void GraphicsCanvas::spectrogram (juce::Rectangle<float> area, const SpectrogramImage& image, float)
{
    juce::Image picture (juce::Image::ARGB, SpectrogramImage::width, SpectrogramImage::height, false);
    {
        juce::Image::BitmapData data (picture, juce::Image::BitmapData::writeOnly);
        for (int x = 0; x < SpectrogramImage::width; ++x)
        {
            const uint32_t* column = image.column ((image.writeColumn + x) % SpectrogramImage::width);
            for (int y = 0; y < SpectrogramImage::height; ++y)
            {
                const uint32_t rgba = column[SpectrogramImage::height - 1 - y];
                data.setPixelColour (x, y, juce::Colour (static_cast<uint8_t> (rgba & 0xff), static_cast<uint8_t> ((rgba >> 8) & 0xff),
                                                         static_cast<uint8_t> ((rgba >> 16) & 0xff)));
            }
        }
    }
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (picture, area);
}

// ---------------------------------------------------------------------------------------------
VisualView::VisualView (GranulrProcessor& p, VisualEngine& e, Kind k) : processor (p), engine (e), kind (k)
{
    if (kind == grains)
    {
        setTooltip ("Buffer: the last 8 seconds of audio, newest on the right. Each mark is a grain: its height is the pitch, "
                    "its colour the pan. Drag to set Position and Pitch (Shift: position only). Alt + drag to draw the Position "
                    "and Spray range. Rec: the next drag is recorded and replayed in a loop as the Gesture sources "
                    "(right-click Rec to erase it).");
        auto& state = processor.getState();
        positionParam = state.getParameter (pid::position);
        pitchParam = state.getParameter (pid::pitch);
        sprayParam = state.getParameter (pid::spray);
    }
    else
        setTooltip ("Output: what leaves the plugin. Click a name to change the view. Meters show input (grey) and output levels.");
}

juce::Rectangle<int> VisualView::plotArea() const
{
    return getLocalBounds().reduced (1).withTrimmedTop (kStripHeight);
}

void VisualView::setMode (int newMode)
{
    mode = juce::jlimit (0, numVisualModes - 1, newMode);
    repaint();
}

void VisualView::setSoftwareRendering (bool shouldDrawContent)
{
    software = shouldDrawContent;
    repaint();
}

juce::Rectangle<int> VisualView::recButtonArea() const
{
    return { getWidth() - 118, 3, 34, kStripHeight - 5 };
}

void VisualView::endParamGestures()
{
    if (positionGesture) positionParam->endChangeGesture();
    if (pitchGesture)    pitchParam->endChangeGesture();
    if (sprayGesture)    sprayParam->endChangeGesture();
    positionGesture = pitchGesture = sprayGesture = false;
}

void VisualView::applyPointDrag (const juce::MouseEvent& e)
{
    const auto plot = plotArea().toFloat();
    if (positionGesture)
    {
        const float delay = juce::jlimit (0.0f, 1.0f, (plot.getRight() - static_cast<float> (e.x)) / plot.getWidth());
        positionParam->setValueNotifyingHost (positionParam->convertTo0to1 (delay * GrainEngine::bufferSeconds * 1000.0f));
    }
    if (pitchGesture)
    {
        const float y = (plot.getCentreY() - static_cast<float> (e.y)) / (plot.getHeight() * 0.5f - 8.0f);
        pitchParam->setValueNotifyingHost (pitchParam->convertTo0to1 (std::round (juce::jlimit (-1.0f, 1.0f, y) * 24.0f)));
    }
}

void VisualView::applyRegionDrag (const juce::MouseEvent& e)
{
    // The grains read between Position and Position + Spray: the newest edge is the position.
    const auto plot = plotArea().toFloat();
    const float a = juce::jlimit (0.0f, 1.0f, (plot.getRight() - static_cast<float> (regionAnchorX)) / plot.getWidth());
    const float b = juce::jlimit (0.0f, 1.0f, (plot.getRight() - static_cast<float> (e.x)) / plot.getWidth());
    const float seconds = GrainEngine::bufferSeconds * 1000.0f;
    if (positionGesture)
        positionParam->setValueNotifyingHost (positionParam->convertTo0to1 (std::min (a, b) * seconds));
    if (sprayGesture)
        sprayParam->setValueNotifyingHost (sprayParam->convertTo0to1 (std::min (2000.0f, std::abs (a - b) * seconds)));
}

void VisualView::addGesturePoint (const juce::MouseEvent& e)
{
    const auto plot = plotArea().toFloat();
    GesturePoint point;
    point.seconds = juce::Time::getMillisecondCounterHiRes() * 0.001 - recordStart;
    point.x = juce::jlimit (-1.0f, 1.0f, (static_cast<float> (e.x) - plot.getCentreX()) / (plot.getWidth() * 0.5f));
    point.y = juce::jlimit (-1.0f, 1.0f, (plot.getCentreY() - static_cast<float> (e.y)) / (plot.getHeight() * 0.5f - 8.0f));
    if (recorded.size() < 20000)
        recorded.push_back (point);
    engine.setGestureOverlay (buildGesture(), true);
}

/** Resamples the recorded path to evenly spaced points in time. */
GestureData VisualView::buildGesture() const
{
    GestureData data;
    if (recorded.size() < 2)
        return data;
    const double duration = recorded.back().seconds;
    data.seconds = static_cast<float> (std::max (0.2, duration));
    data.count = juce::jlimit (2, GestureData::maxPoints, static_cast<int> (duration * 60.0) + 2);
    size_t index = 0;
    for (int i = 0; i < data.count; ++i)
    {
        const double time = duration * static_cast<double> (i) / static_cast<double> (data.count - 1);
        while (index + 2 < recorded.size() && recorded[index + 1].seconds < time)
            ++index;
        const auto& p0 = recorded[index];
        const auto& p1 = recorded[index + 1];
        const double span = p1.seconds - p0.seconds;
        const auto t = static_cast<float> (span > 1.0e-9 ? juce::jlimit (0.0, 1.0, (time - p0.seconds) / span) : 0.0);
        data.x[static_cast<size_t> (i)] = p0.x + t * (p1.x - p0.x);
        data.y[static_cast<size_t> (i)] = p0.y + t * (p1.y - p0.y);
    }
    return data;
}

void VisualView::refresh()
{
    if (kind != grains || dragMode == dragRecord)
        return;
    const uint32_t version = processor.getGestureVersion();
    if (version != shownGestureVersion)
    {
        shownGestureVersion = version;
        engine.setGestureOverlay (processor.getGesture(), false);
        repaint();
    }
}

void VisualView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == dragPoint)
        applyPointDrag (e);
    else if (dragMode == dragRegion)
        applyRegionDrag (e);
    else if (dragMode == dragRecord)
        addGesturePoint (e);
}

void VisualView::mouseUp (const juce::MouseEvent&)
{
    if (dragMode == dragRecord)
    {
        const auto gesture = buildGesture();
        dragMode = dragNone;
        recArmed = false;
        if (gesture.count > 1 && recorded.back().seconds >= 0.15)
        {
            processor.setGesture (gesture);
            // What was drawn is what plays: X moves the position, Y the pitch.
            auto& matrix = processor.getMatrix();
            if (matrix.findSlot (srcGestureX, dstPosition) < 0)
                matrix.setRouting (srcGestureX, dstPosition, -0.5f);
            if (matrix.findSlot (srcGestureY, dstPitch) < 0)
                matrix.setRouting (srcGestureY, dstPitch, 0.5f);
        }
        shownGestureVersion = 0xffffffffu;
        recorded.clear();
        repaint();
    }
    endParamGestures();
    dragMode = dragNone;
}

void VisualView::mouseDown (const juce::MouseEvent& e)
{
    if (kind == grains)
    {
        if (recButtonArea().contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu())
            {
                processor.setGesture ({});
                recArmed = false;
            }
            else
            {
                recArmed = ! recArmed;
            }
            repaint();
            return;
        }
        if (! plotArea().contains (e.getPosition()) || e.mods.isPopupMenu())
            return;

        const bool positionFree = processor.getState().getRawParameterValue (pid::positionSync)->load() < 0.5f;
        if (recArmed)
        {
            dragMode = dragRecord;
            recorded.clear();
            recordStart = juce::Time::getMillisecondCounterHiRes() * 0.001;
            addGesturePoint (e);
        }
        else if (e.mods.isAltDown())
        {
            dragMode = dragRegion;
            regionAnchorX = e.x;
            positionGesture = positionFree;
            sprayGesture = true;
            if (positionGesture) positionParam->beginChangeGesture();
            sprayParam->beginChangeGesture();
            applyRegionDrag (e);
        }
        else
        {
            dragMode = dragPoint;
            positionGesture = positionFree;
            pitchGesture = ! e.mods.isShiftDown();
            if (positionGesture) positionParam->beginChangeGesture();
            if (pitchGesture)    pitchParam->beginChangeGesture();
            applyPointDrag (e);
        }
        return;
    }

    if (e.y > kStripHeight)
        return;
    const int tabWidth = 78;
    const int index = (e.x - 6) / tabWidth;
    if (index >= 0 && index < numVisualModes)
    {
        setMode (index);
        if (onModeChanged)
            onModeChanged (index);
    }
}

void VisualView::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    const auto plot = plotArea();

    if (software)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot);
        GraphicsCanvas canvas (g);
        if (kind == grains)
            engine.drawGrains (canvas, plot.toFloat());
        else
            engine.drawOutput (canvas, plot.toFloat(), mode);
    }

    // Title strip and frame (the plot itself is drawn underneath by the renderer).
    auto strip = getLocalBounds().removeFromTop (kStripHeight + 1);
    g.setColour (t.panel);
    g.fillRect (strip);
    g.setColour (t.border);
    g.drawRect (getLocalBounds(), 1);

    g.setFont (Fonts::title());
    if (kind == grains)
    {
        const bool frozen = processor.getState().getRawParameterValue (pid::freeze)->load() > 0.5f;
        g.setColour (t.textDim);
        g.drawText ("BUFFER", strip.withTrimmedLeft (8), juce::Justification::centredLeft, false);
        g.setFont (Fonts::small());
        g.setColour (frozen ? t.frozen : t.textFaint);
        g.drawText (frozen ? "Frozen" : "Recording", strip.withTrimmedRight (8), juce::Justification::centredRight, false);

        // Gesture recording button.
        const auto rec = recButtonArea().toFloat();
        const bool hasGesture = processor.getGesture().count > 1;
        g.setColour (recArmed ? t.meterClip : t.control);
        g.fillRoundedRectangle (rec, 3.0f);
        g.setColour (recArmed ? t.meterClip : (hasGesture ? t.sourceColour (srcGestureX) : t.border));
        g.drawRoundedRectangle (rec, 3.0f, 1.0f);
        g.setColour (recArmed ? t.background : t.text);
        g.drawText ("Rec", rec, juce::Justification::centred, false);

        g.setColour (t.textFaint);
        g.drawText ("8 s", plot.reduced (5, 3), juce::Justification::bottomLeft, false);
        g.drawText ("now", plot.reduced (5, 3), juce::Justification::bottomRight, false);
        g.drawText ("+24 st", plot.reduced (5, 3), juce::Justification::topLeft, false);
    }
    else
    {
        const int tabWidth = 78;
        for (int m = 0; m < numVisualModes; ++m)
        {
            const auto tab = juce::Rectangle<int> (6 + m * tabWidth, 0, tabWidth, kStripHeight);
            g.setColour (m == mode ? t.text : t.textFaint);
            g.setFont (m == mode ? Fonts::title() : Fonts::small());
            g.drawText (kModeNames[m], tab, juce::Justification::centredLeft, false);
        }
        g.setFont (Fonts::small());
        g.setColour (t.textFaint);
        const auto inner = plot.withTrimmedRight (VisualEngine::meterWidth).reduced (5, 3);
        if (mode == visSpectrum || mode == visSpectrogram)
        {
            g.drawText ("20 Hz", inner, juce::Justification::bottomLeft, false);
            g.drawText ("20 kHz", inner, juce::Justification::bottomRight, false);
        }
        else if (mode == visVector)
        {
            g.drawText ("L", inner, juce::Justification::topLeft, false);
            g.drawText ("R", inner, juce::Justification::topRight, false);
        }
    }
}

} // namespace grn
