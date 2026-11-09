// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace grn
{

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;

/** Small, fast pseudo random generator (xorshift32). Not for cryptography. */
struct Rng
{
    uint32_t state = 0x9e3779b9u;

    void seed (uint32_t s) noexcept { state = s != 0 ? s : 0x9e3779b9u; }

    uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** Uniform in [0, 1). */
    float uni() noexcept { return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f); }

    /** Uniform in [-1, 1). */
    float bi() noexcept { return uni() * 2.0f - 1.0f; }
};

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float semitonesToRatio (float semitones) noexcept { return std::exp2 (semitones * (1.0f / 12.0f)); }

/** One-pole coefficient for a time constant in seconds at a given update rate. */
inline float onePoleCoef (float seconds, float rate) noexcept
{
    return seconds <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (seconds * rate));
}

/** Cheap bounded saturation, close to tanh, output strictly within [-1, 1]. */
inline float softClip (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline int nextPowerOfTwo (int value) noexcept
{
    int result = 1;
    while (result < value)
        result <<= 1;
    return result;
}

} // namespace grn
