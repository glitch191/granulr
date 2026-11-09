# granulr: implementation plan (phase A)

Status: approved plan pending. No code written yet. This document is the reference for phase B.

## 1. Environment findings

| Item | Finding | Consequence |
| :- | :- | :- |
| Folder | `granulr`, empty, no Git repository | Project name is `granulr`. Git is initialised in milestone B1. |
| Compiler | Visual Studio Build Tools 2026 (v18) and 2022 (v17) | Local builds use the newest Visual Studio generator, x64. |
| CMake | 4.4.4 | JUCE requires 3.22 or newer: fine. No Ninja installed, so the Visual Studio generator is used. |
| JUCE | Latest tags: 8.0.17-security and 9.0.5 | See decision D1. |
| Tools | `gh` and `actionlint` installed, `pluginval` absent | `pluginval` is downloaded into an ignored `tools/` folder in B1. |
| VST3 folder | `C:\Program Files\Common Files\VST3` is writable from this session | `COPY_PLUGIN_AFTER_BUILD` works directly. `scripts/install-vst3.ps1` is still provided for other setups. |
| Hardware | Ryzen 7 9800X3D, NVIDIA RTX 5070 Ti (plus AMD integrated graphics) | Reference machine for CPU budgets. Code never assumes this hardware. |

## 2. Key decisions

- **D1, JUCE version.** The prompt says JUCE 8, but JUCE 9 is now the current major version (9.0.5), with the same AGPLv3 or commercial dual licence and several Direct2D and VST3 scaling fixes. Plan: pin JUCE **9.0.5** with `FetchContent`. Fallback: 8.0.17-security if a JUCE 9 regression blocks us.
- **D2, names.** `PRODUCT_NAME`, bundle and displayed name: `granulr` (lowercase, as the folder). `COMPANY_NAME`: `granulr` (placeholder, a single CMake variable to change later). Plugin codes: manufacturer `Grnr`, plugin `Gr01`.
- **D3, rendering.** Primary path: a `juce::OpenGLContext` attached to the editor (component painting enabled for widgets, cached by JUCE; visualisers drawn with custom shaders in `renderOpenGL`). This is the only JUCE path that supports all three frame modes: swap interval 1 for "Display sync" (follows any refresh rate, 360 Hz included), swap interval 0 for "Unlimited", and a paced render loop for "Capped". Fallback path: JUCE native renderer (Direct2D) driven by `juce::VBlankAttachment`, display sync only. The fallback is used automatically if GL initialisation fails, or when the user turns off "GPU rendering" in settings.
- **D4, latency.** Zero added latency. Nothing in the chain needs lookahead: the output limiter is a fast soft-knee limiter without lookahead, backed by a final soft clipper.
- **D5, threading.** Audio processing stays on the host thread, single-threaded. FFT, spectrogram and all drawing happen off the audio thread.
- **D6, anti-aliasing.** Saturation uses first-order antiderivative anti-aliasing (ADAA) instead of oversampling, which is cheaper and fits the budget.
- **D7, instruction set.** Release builds use `/O2 /arch:AVX2 /GL` plus `/LTCG`. `/fp:fast` is enabled only if every quality test still passes. No AVX-512 requirement.
- **D8, modulation matrix storage.** The 32 matrix slots (source, destination, depth) live in the plugin state `ValueTree`, not as host parameters. This keeps the host parameter list readable and the IDs stable. The four macros are the automatable entry point. The audio thread reads an immutable matrix snapshot swapped in lock-free.
- **D9, A/B morph.** The morph slider is a UI control (message thread) that writes interpolated values of continuous parameters between snapshots A and B, inside one undo transaction. It is not a host-automatable parameter, because an audio-rate morph of 150 parameters would cost CPU for little benefit.
- **D10, "speed inside the grain".** Interpreted as **Scan**: how fast the spawn position travels through the buffer (from -2x to +2x, 0 = fixed). It matters most when the buffer is frozen. Pitch already sets the playback rate inside each grain.
- **D11, scale quantisation.** Quantises the *transposition interval* (relative to the root note), which is the convention of commercial pitch-quantised granular effects. The input is assumed to be in the chosen key. Documented in the README.

## 3. Signal flow

```
input -> input gain -> [ducking detector] ----------------------------------------+
   |                                                                              |
   +--> sum --> circular buffer write (paused while frozen, seam crossfade)        |
         ^                 |                                                      |
         |                 v                                                      |
         |        grain scheduler (Free / Sync / Pattern, sample accurate)        |
         |                 |                                                      |
         |                 v                                                      |
         |        grain voices (pool, SoA, Hermite interpolation, window table)   |
         |                 |                                                      |
         |                 v                                                      |
         |        wet bus --> effects placed "In feedback": filter -> saturation -> diffusion
         |                 |                                                      |
         +-- feedback <----+-- loop soft clip + DC blocker                        |
                           |                                                      |
                           v                                                      |
                 effects placed "Post": filter -> saturation -> diffusion         |
                           |                                                      |
                           v                                                      |
                 wet gain x (1 - ducking amount x input envelope) <---------------+
                           |
                           v
                 dry/wet mix -> output gain -> safety limiter -> output
```

Each effect has its own placement switch (In feedback or Post). Within a placement the order is fixed: filter, saturation, diffusion.

## 4. Grain engine design

- **Buffer.** Stereo, length rounded up to a power of two covering at least 8 s at the current sample rate (allocated in `prepareToPlay`). Index wrapping uses a bit mask. Read positions use a 64-bit integer part plus a float fraction to stay precise across 1.5 million samples at 192 kHz.
- **Freeze.** The write head stops. The last 10 ms written before the freeze are crossfaded with the oldest region so a grain crossing the seam never clicks. Unfreezing resumes writing with a short fade-in.
- **Grain pool.** Structure of arrays, preallocated: read position, increment (signed, so reverse is a negative increment), window phase and increment, left and right gains, active flag. Capacity per quality: Eco 64, Normal 128, High 256 (final values set by measurement in B8). When the pool is full, the oldest grain is stolen with a 2 ms fade.
- **Rendering.** Grain-major loop: for each active grain, render its span within the current block into the wet accumulator. The inner loop (window lookup plus 4-point Hermite) has no branches and no virtual calls, so MSVC can vectorise it. Spawns are sample accurate (the scheduler gives sample offsets inside the block).
- **Window table.** 2048 points per shape (Hann, Tukey, Trapezoid, Perc), linear interpolation, skew applied by warping the phase.
- **Scheduler.** Free mode: Poisson-like spacing from density plus jitter. Sync mode: grains on note divisions from the host PPQ position. Pattern mode: 16-step sequencer (gate, probability, ratchet 1 to 4, length 1 to 16, rate division). Fallback tempo 120 BPM when the host gives no play head.
- **Per-grain choices at spawn.** Position = base position + spray random + scan offset; pitch = base + fine + random, then voice choice among the 4 harmony voices by probability, octave jumps up or down by probability, then scale quantisation; reverse by probability; pan from the pan spread.
- **Control rate.** Modulation and parameter targets are recomputed every 32 samples, then linearly interpolated inside the sub-block for gains and filter coefficients.
- **Silence detection.** When input RMS and wet tail both stay below -120 dBFS for 200 ms (and the buffer is not frozen with active grains), the engine skips grain rendering until input returns.

## 5. Effects

- **Filter:** TPT state variable filter (Zavalishin), LP, HP or BP, coefficients updated at control rate.
- **Saturation:** soft (tanh) and hard (clip) with first-order ADAA, automatic level compensation.
- **Diffusion:** 4 series allpass diffusers followed by an 8-line feedback delay network with a Hadamard matrix and light modulation. Eco quality uses 4 lines.
- **Loop protection:** tanh soft clip plus DC blocker in the feedback path, so 100 % feedback stays bounded.
- **Output:** fast peak limiter (no lookahead) plus a final soft clipper, on by default.

## 6. Modulation

- Sources: LFO 1 to 4 (sine, triangle, saw, square, smooth random, sample and hold; free or synced; phase; transport retrigger), Random 1 and 2 (rate, smoothing, free or synced), envelope follower (input or output, attack, release, gain), Macro 1 to 4.
- Matrix: 32 slots (source, destination, bipolar depth). Destinations: every continuous parameter flagged "modulatable" in the parameter table.
- Evaluation: once per 32-sample sub-block. Modulated value = clamp(base normalised value + sum of depth x source). Each destination keeps its own smoother.
- GUI feedback: the current modulated value of every destination is published in an array of atomics, read by the knobs to draw the animated depth ring.

## 7. Audio to GUI bridge

- `editorOpen` atomic flag: when false, the audio thread publishes nothing (zero analysis cost with the editor closed).
- Output samples (stereo) go into a lock-free single-producer, single-consumer FIFO (`juce::AbstractFifo` over a preallocated buffer). Input and output peak levels, CPU load and active grain count are atomics.
- Grain snapshot (position, pitch, pan, amplitude of up to 256 grains, write head, frozen region) goes through a triple buffer.
- GUI side: 4096-point FFT (`juce::dsp::FFT`), Hann window, 75 % overlap, log-frequency mapping, smoothing and peak hold. The spectrogram is a ring texture updated one row per FFT frame. All buffers are preallocated: no allocation per frame.
- Every animation uses the time elapsed between frames, never a frame count.

## 8. Parameters (host-automatable)

IDs are final once the first version is published. "Mod" = available as a modulation destination.

| ID | Name | Range | Unit | Default | Mod |
| :- | :- | :- | :- | :- | :- |
| `inputGain` | Input | -24 to +24 | dB | 0 | yes |
| `outputGain` | Output | -24 to +24 | dB | 0 | yes |
| `mix` | Mix | 0 to 100 | % | 50 | yes |
| `freeze` | Freeze | off, on | | off | no |
| `position` | Position | 0 to 8000 | ms | 250 | yes |
| `positionSync` | Position Sync | off, on | | off | no |
| `positionDiv` | Position Division | 1/64 to 4 bars (straight, dotted, triplet) | | 1/4 | no |
| `scan` | Scan | -2 to +2 | x | 0 | yes |
| `size` | Size | 1 to 2000 | ms | 120 | yes |
| `sizeSync` | Size Sync | off, on | | off | no |
| `sizeDiv` | Size Division | 1/64 to 1 bar | | 1/16 | no |
| `density` | Density | 0.5 to 400 | grains/s | 20 | yes |
| `densityDiv` | Density Division | 1/64 to 1 bar | | 1/16 | no |
| `triggerMode` | Trigger | Free, Sync, Pattern | | Free | no |
| `spray` | Spray | 0 to 2000 | ms | 20 | yes |
| `jitter` | Jitter | 0 to 100 | % | 10 | yes |
| `pitch` | Pitch | -24 to +24 | st | 0 | yes |
| `fine` | Fine | -100 to +100 | ct | 0 | yes |
| `pitchRandom` | Pitch Random | 0 to 24 | st | 0 | yes |
| `reverse` | Reverse | 0 to 100 | % | 0 | yes |
| `windowShape` | Window | Hann, Tukey, Trapezoid, Perc | | Hann | no |
| `windowSkew` | Skew | -100 to +100 | % | 0 | yes |
| `panSpread` | Pan Spread | 0 to 100 | % | 30 | yes |
| `width` | Width | 0 to 200 | % | 100 | yes |
| `feedback` | Feedback | 0 to 100 | % | 0 | yes |
| `duckAmount` | Duck | 0 to 100 | % | 0 | yes |
| `duckAttack` | Duck Attack | 1 to 200 | ms | 10 | no |
| `duckRelease` | Duck Release | 20 to 2000 | ms | 250 | no |
| `limiter` | Limiter | off, on | | on | no |
| `quality` | Quality | Eco, Normal, High | | Normal | no |
| `scale` | Scale | Off, Chromatic, Major, Minor, Major Pent., Minor Pent., Dorian, Phrygian, Whole Tone, Octaves, Fifths + Octaves, Custom | | Off | no |
| `root` | Root | C to B | | C | no |
| `customNote0` to `customNote11` | Custom C to B | off, on | | on | no |
| `voice1Interval` to `voice4Interval` | Voice n Interval | -24 to +24 | st | 0, 7, 12, -12 | yes |
| `voice1Prob` to `voice4Prob` | Voice n Chance | 0 to 100 | % | 100, 0, 0, 0 | yes |
| `octaveUp` | Octave Up | 0 to 100 | % | 0 | yes |
| `octaveDown` | Octave Down | 0 to 100 | % | 0 | yes |
| `patternLength` | Pattern Length | 1 to 16 | steps | 16 | no |
| `patternRate` | Pattern Rate | 1/64 to 1/4 | | 1/16 | no |
| `step1Gate` to `step16Gate` | Step n | off, on | | on | no |
| `step1Prob` to `step16Prob` | Step n Chance | 0 to 100 | % | 100 | no |
| `step1Ratchet` to `step16Ratchet` | Step n Ratchet | 1 to 4 | | 1 | no |
| `filterOn`, `filterType`, `filterCutoff`, `filterReso`, `filterPlace` | Filter | type LP, HP, BP; cutoff 20 to 20000 Hz (default 8000); resonance 0 to 100 %; place In feedback, Post | | off | cutoff, reso |
| `satOn`, `satType`, `satDrive`, `satPlace` | Saturation | Soft, Hard; drive 0 to 36 dB; place | | off | drive |
| `diffOn`, `diffSize`, `diffDecay`, `diffDamp`, `diffMix`, `diffPlace` | Diffusion | size, decay, damping, mix 0 to 100 %; place | | off | size, decay, damp, mix |
| `lfo1Shape` to `lfo4Shape`, `lfoNRate`, `lfoNSync`, `lfoNDiv`, `lfoNPhase`, `lfoNRetrig` | LFO n | rate 0.01 to 40 Hz; phase 0 to 360 degrees | | sine, 0.5 Hz | rate |
| `rand1Rate`, `rand2Rate`, `randNSmooth`, `randNSync`, `randNDiv` | Random n | rate 0.01 to 40 Hz; smooth 0 to 100 % | | 1 Hz | rate |
| `envSource`, `envAttack`, `envRelease`, `envGain` | Envelope | Input, Output; 1 to 500 ms; 10 to 5000 ms; -24 to +24 dB | | Input | no |
| `macro1` to `macro4` | Macro n | 0 to 100 | % | 0 | no (they are sources) |

Total: about 160 host parameters. The exact choice lists for divisions are defined once in `ParameterIds.h`.

Non-host state (in the `ValueTree`): modulation matrix, A and B snapshots, editor zoom, editor size, visualiser mode, state version number.

## 9. File structure

```
CMakeLists.txt                 root project, options (VST3_INSTALL_DIR, GRANULR_VERSION)
cmake/                         JUCE fetch, compiler flags
src/
  plugin/                      PluginProcessor, PluginEditor
  params/                      ParameterIds.h, ParameterLayout.cpp, Divisions.h
  dsp/                         CircularBuffer, WindowTable, Interpolation, GrainPool, GrainScheduler,
                               PatternSequencer, PitchQuantizer, StateVariableFilter, Saturator,
                               Diffuser, Limiter, DcBlocker, EnvelopeFollower, SilenceDetector, Engine
  mod/                         Lfo, RandomSource, ModMatrix, ModulationEngine
  state/                       PresetManager, StateVersion, GlobalSettings
  shared/                      AudioToGuiBridge, TripleBuffer, CpuMeter, Smoothers
  gui/
    theme/                     Theme (design tokens), GranulrLookAndFeel
    components/                Knob, ToggleButton, ChoiceBox, StepGrid, ModSourceChip, ModMatrixView
    sections/                  Header, GrainSection, PitchSection, TriggerSection, EffectsSection,
                               OutputSection, ModulationPanel, PresetBrowser
    visual/                    GlRenderer, FrameClock, GrainView, SpectrumView, SpectrogramView,
                               VectorscopeView, OscilloscopeView, Meters, DiagnosticsOverlay
presets/factory/               factory presets (XML, embedded with juce_add_binary_data)
tests/                         console test runner (juce::UnitTest), allocation tracker
bench/                         console benchmark
scripts/install-vst3.ps1
.github/workflows/release.yml
docs/                          PLAN.md, screenshots/
```

## 10. GUI layout

- Base size at 100 %: 1100 x 720 (1375 x 900 at the default 125 %). Minimum at 100 %: 900 x 600.
- Header: preset browser (name, previous, next, save, favourites), A/B, copy, morph, Randomize (dice icon) with group locks, undo and redo, quality, zoom menu, CPU readout, bypass.
- Centre: grain view and output visualiser side by side when wide enough, otherwise a tab switch. Visualiser modes: Spectrum, Spectrogram, Vectorscope, Oscilloscope.
- Bottom rows: Grains, Pitch and Scale, Trigger and Pattern, Effects, Output. Modulation panel collapsible (sources on the left, matrix on the right). Advanced controls fold away at small sizes instead of shrinking.
- Theme: every colour, size, radius and spacing is a token in `Theme`. One colour per modulation source for the rings.
- Zoom: menu (100, 110, 125, 150, 175, 200 %), Ctrl + plus, Ctrl + minus, Ctrl + wheel, Ctrl + 0 (back to 125 %). Applied with `AudioProcessorEditor::setScaleFactor` combined with the host scale, never multiplied twice. Stored per instance and as the default for new instances in `%APPDATA%\granulr\settings.xml`.

## 11. Test and measurement strategy

- **Unit tests** (`tests/`, run by CTest): no NaN or infinity under random and extreme parameters; mix at 0 % gives a bit-exact dry signal; freeze stays stable for 60 s; 100 % feedback stays bounded; scale quantisation exact on sine input (pitch measured by zero crossings or FFT peak); state and preset round trip; sample rates 44.1 to 192 kHz; block sizes 1 to 4096, including variable sizes; discontinuity measurement on abrupt parameter changes; no denormals in tails (CPU time stable over a long silent tail).
- **Allocation tracker:** the test binary replaces global `operator new` and counts allocations made while a thread-local "in audio callback" flag is set. Any allocation during `processBlock` fails the test.
- **Benchmark** (`bench/`): processes several minutes of synthetic material (noise bursts, chords, pulses) for every factory preset and the worst case, at 44.1, 48, 96 and 192 kHz and block sizes 32, 64, 128, 256 and 1024. Reports mean CPU, worst block and 99th percentile. The 48 kHz, 128-sample worst case must stay at or under 20 %, and the default preset at or under 5 %. Results go into the README.
- **Validation:** `pluginval` at strictness 5 minimum, 10 if time allows, on the installed bundle.
- **Screenshots:** Standalone target at 100, 125 and 200 %, default and minimum sizes, with a synthetic test signal feeding the visualisers.

## 12. Phase B milestones (one local commit each)

1. **B1, skeleton.** Git init, `.gitignore`, `.gitattributes`, `LICENSE` (AGPLv3), CMake with JUCE 9.0.5, empty VST3 and Standalone targets, test and benchmark targets, `install-vst3.ps1`, local install, `pluginval` on the empty plugin.
2. **B2, engine.** Buffer, freeze, scheduler (Free and Sync), grain pool, windows, interpolation, feedback with protection, ducking, limiter, parameter layout, smoothing, first benchmark.
3. **B3, pitch.** Scale quantisation, custom mask, harmony voices, octave jumps.
4. **B4, effects.** Filter, saturation, diffusion, placement switches.
5. **B5, pattern and modulation.** 16-step sequencer, LFOs, random sources, envelope follower, macros, matrix, atomic publishing for the GUI.
6. **B6, GUI.** Theme and look and feel, sections, drag and drop modulation, depth rings, zoom, resizing, GL renderer and fallback, grain view, four visualiser modes, meters, diagnostics overlay.
7. **B7, presets.** Preset manager, browser, favourites, search, user preset files, A/B and morph, grouped randomisation with locks, undo and redo, 12 starter presets (one per scenario).
8. **B8, optimisation and validation.** Benchmark-driven optimisation until every CPU ceiling is met, release workflow, local run of the workflow commands, `actionlint`, final `pluginval`, screenshot checks, acceptance criteria review.

## 13. Assumptions

- JUCE 9.0.5 instead of JUCE 8 (D1). Same licence, more recent fixes.
- "20 % CPU" means one core, processing time divided by block duration (prompt definition).
- Product, bundle and company placeholder names are `granulr` (D2).
- The modulation matrix and A/B snapshots are plugin state, not host parameters (D8, D9).
- "Scan" implements the "speed inside the grain" item (D10).
- The GitHub workflow uses the default Visual Studio generator of the runner (`-A x64`, no fixed generator name), because the Visual Studio version on `windows-latest` may change.
