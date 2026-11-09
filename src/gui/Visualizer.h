// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Visualisers: grain view and output view (spectrum, spectrogram, oscilloscope, vectorscope,
// meters). The analysis runs on the render thread, never on the audio thread. Drawing goes
// through a small Canvas interface with two back ends: OpenGL (default) and juce::Graphics
// (software fallback).
#pragma once

#include "LookAndFeel.h"
#include "../plugin/PluginProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_opengl/juce_opengl.h>

namespace grn
{

enum VisualMode { visSpectrum = 0, visSpectrogram, visScope, visVector, numVisualModes };

/** Scrolling spectrogram pixels, stored as a ring of columns. */
struct SpectrogramImage
{
    static constexpr int width = 512, height = 256;
    std::vector<uint32_t> pixels = std::vector<uint32_t> (static_cast<size_t> (width * height), 0xff000000u); // column major
    int writeColumn = 0;
    uint32_t columnCounter = 0; // total number of columns written

    const uint32_t* column (int index) const { return pixels.data() + static_cast<size_t> (index) * height; }
    uint32_t* column (int index) { return pixels.data() + static_cast<size_t> (index) * height; }
};

class Canvas
{
public:
    virtual ~Canvas() = default;
    virtual void fillRect (juce::Rectangle<float> area, juce::Colour colour) = 0;
    virtual void line (juce::Point<float> a, juce::Point<float> b, float thickness, juce::Colour colour) = 0;
    virtual void polyline (const juce::Point<float>* points, int numPoints, float thickness, juce::Colour colour) = 0;
    /** Fills the area between a curve and a horizontal base line. */
    virtual void fillUnder (const juce::Point<float>* points, int numPoints, float baseY, juce::Colour colour) = 0;
    virtual void spectrogram (juce::Rectangle<float> area, const SpectrogramImage& image, float scrollFraction) = 0;
    virtual void setClip (juce::Rectangle<float> area) = 0;
};

// ---------------------------------------------------------------------------------------------
/** Pulls data from the audio bridge, analyses it and draws the views. One thread at a time. */
class VisualEngine
{
public:
    explicit VisualEngine (GranulrProcessor& processor);

    /** Consumes new audio and advances every animation by dt seconds. */
    void update (double dtSeconds);

    void drawGrains (Canvas& canvas, juce::Rectangle<float> area);
    void drawOutput (Canvas& canvas, juce::Rectangle<float> area, int mode);

    /** True while something is moving and frames are worth drawing. */
    bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }

    static constexpr int meterWidth = 34;
    static constexpr float minDb = -90.0f;

private:
    static constexpr int ringSize = 1 << 16;
    static constexpr int spectrumBins = SpectrogramImage::height;
    static constexpr int maxOrder = 14;

    void analyse();
    void drawSpectrum (Canvas&, juce::Rectangle<float>);
    void drawScope (Canvas&, juce::Rectangle<float>);
    void drawVector (Canvas&, juce::Rectangle<float>);
    void drawMeters (Canvas&, juce::Rectangle<float>);

    GranulrProcessor& processor;
    GuiBridge& bridge;

    std::vector<float> ringL, ringR, popL, popR;
    int ringWrite = 0;
    double sampleRate = 48000.0;

    juce::dsp::FFT fft12 { 12 }, fft13 { 13 }, fft14 { 14 };
    std::vector<float> fftData, window;
    int fftOrder = 12, hopSize = 1024, sinceHop = 0;
    double secondsSinceColumn = 0.0, columnPeriod = 0.02;

    std::array<float, spectrumBins> target {}, shown {}, peak {};
    SpectrogramImage spectrogramImage;
    std::array<uint32_t, 256> colourMap {};

    float meterLevel[4] {}, meterHold[4] {};
    float correlation = 0.0f;
    GrainFrame grainFrame;
    std::atomic<bool> active { true };
    double idleSeconds = 0.0;

    std::vector<juce::Point<float>> scratch;
};

// ---------------------------------------------------------------------------------------------
/** OpenGL back end: batches coloured triangles, plus one textured quad for the spectrogram. */
class GlCanvas final : public Canvas
{
public:
    void initialise (juce::OpenGLContext& context);
    void shutdown();

    void begin (int viewportWidth, int viewportHeight, float scale);
    void end();

    void fillRect (juce::Rectangle<float> area, juce::Colour colour) override;
    void line (juce::Point<float> a, juce::Point<float> b, float thickness, juce::Colour colour) override;
    void polyline (const juce::Point<float>* points, int numPoints, float thickness, juce::Colour colour) override;
    void fillUnder (const juce::Point<float>* points, int numPoints, float baseY, juce::Colour colour) override;
    void spectrogram (juce::Rectangle<float> area, const SpectrogramImage& image, float scrollFraction) override;
    void setClip (juce::Rectangle<float> area) override;

private:
    void flush();
    void vertex (float x, float y, const float* rgba);
    void quad (juce::Point<float> a, juce::Point<float> b, juce::Point<float> c, juce::Point<float> d, juce::Colour colour);

    juce::OpenGLContext* context = nullptr;
    std::unique_ptr<juce::OpenGLShaderProgram> colourProgram, textureProgram;
    unsigned int vertexBuffer = 0, texture = 0;
    std::vector<float> vertices;
    int viewportW = 1, viewportH = 1;
    float scale = 1.0f;
    uint32_t uploadedColumns = 0;
    bool textureReady = false;
};

/** Software back end, used when GPU rendering is off or unavailable. */
class GraphicsCanvas final : public Canvas
{
public:
    explicit GraphicsCanvas (juce::Graphics& graphics) : g (graphics) {}

    void fillRect (juce::Rectangle<float> area, juce::Colour colour) override;
    void line (juce::Point<float> a, juce::Point<float> b, float thickness, juce::Colour colour) override;
    void polyline (const juce::Point<float>* points, int numPoints, float thickness, juce::Colour colour) override;
    void fillUnder (const juce::Point<float>* points, int numPoints, float baseY, juce::Colour colour) override;
    void spectrogram (juce::Rectangle<float> area, const SpectrogramImage& image, float scrollFraction) override;
    void setClip (juce::Rectangle<float>) override {}

private:
    juce::Graphics& g;
};

// ---------------------------------------------------------------------------------------------
/** Frame time statistics for the diagnostics overlay. Written by the render thread. */
struct FrameStats
{
    void addFrame (double seconds) noexcept
    {
        frames[static_cast<size_t> (writeIndex)] = static_cast<float> (seconds);
        writeIndex = (writeIndex + 1) % size;
        count.store (std::min (size, count.load (std::memory_order_relaxed) + 1), std::memory_order_relaxed);
    }

    /** Average frames per second, mean frame time and the mean of the worst 1 % (milliseconds). */
    void summarise (float& fps, float& meanMs, float& worstMs) const
    {
        const int n = count.load (std::memory_order_relaxed);
        fps = meanMs = worstMs = 0.0f;
        if (n < 2)
            return;
        std::array<float, size> copy;
        std::copy_n (frames.begin(), n, copy.begin());
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += copy[static_cast<size_t> (i)];
        meanMs = static_cast<float> (1000.0 * sum / n);
        fps = meanMs > 0.0f ? 1000.0f / meanMs : 0.0f;
        std::sort (copy.begin(), copy.begin() + n);
        const int worstCount = std::max (1, n / 100);
        double worst = 0.0;
        for (int i = n - worstCount; i < n; ++i)
            worst += copy[static_cast<size_t> (i)];
        worstMs = static_cast<float> (1000.0 * worst / worstCount);
    }

    void clear() noexcept { count.store (0); writeIndex = 0; }

    static constexpr int size = 600;
    std::array<float, size> frames {};
    int writeIndex = 0;
    std::atomic<int> count { 0 };
};

// ---------------------------------------------------------------------------------------------
/** Overlay component for a visualiser area: labels on top, and the software drawing path. */
class VisualView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Kind { grains, output };

    VisualView (GranulrProcessor& processor, VisualEngine& engine, Kind kind);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void setMode (int newMode);
    int getMode() const noexcept { return mode; }
    void setSoftwareRendering (bool shouldDrawContent);
    /** Area where the visualiser itself is drawn, in this component's coordinates. */
    juce::Rectangle<int> plotArea() const;

    std::function<void (int)> onModeChanged;

private:
    GranulrProcessor& processor;
    VisualEngine& engine;
    Kind kind;
    int mode = visSpectrum;
    bool software = false;
};

} // namespace grn
