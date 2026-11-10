# granulr

A granular effect plugin (VST3, Windows) for live audio: it records the incoming signal into a buffer and replays it as clouds of short grains, with freeze, pitch quantised to a scale, a pattern sequencer, built-in effects and a modulation matrix.

![Main window with the modulation panel open](docs/screenshots/main.png)

| Spectrum view | Preset browser |
| :- | :- |
| ![Spectrum view](docs/screenshots/spectrum.png) | ![Preset browser](docs/screenshots/browser.png) |

The screenshots are produced off screen by `granulr_shot` (software renderer, synthetic test signal). In a host the visualisers are drawn on the GPU.

## What it does

- **Live buffer with freeze.** 8 seconds of stereo audio are always available. Freeze stops recording and lets the grains play the held material for as long as you want.
- **Grains.** Position, size, density, spray, jitter, reverse chance, scan, window shape and skew, pan spread and width. Up to 64, 128 or 256 grains depending on the quality setting (halved above 100 kHz).
- **Three trigger modes.** Free (density in grains per second), Sync (note divisions of the host tempo) and Pattern (16 steps with gate, chance and ratchets).
- **Pitch on a scale.** Transposition, random pitch, four harmony voices, octave jumps, and quantisation to a scale (chromatic, major, minor, pentatonics, dorian, phrygian, whole tone, octaves, fifths and octaves, or a custom 12-note mask).
- **Effects.** Filter, saturation and diffusion, each placed either inside the feedback loop or after it. Feedback, ducking against the dry input, and a safety limiter.
- **Modulation.** Four LFOs, two random sources, an envelope follower and four macros, routed through a 32-slot matrix to 41 destinations. Drag a source chip onto a knob to create a route.
- **Presets.** Factory bank, user presets, favourites, A/B slots with morph, undoable randomisation with group locks.
- **Visualisers.** Grain view over the buffer, plus spectrum, spectrogram, oscilloscope and vectorscope of the output.
- **Interface zoom** from 100 to 200 % (default 125 %).

Stereo in, stereo out. No MIDI input, no sidechain, no added latency.

### Typical uses

The factory bank holds 100 presets in 12 categories:

| Category | Presets | What to expect |
| :- | :- | :- |
| Ambient | 10 | Slow clouds and washes behind the source |
| Glitch | 10 | Tempo-synced chops, ratchets and stutters |
| Shimmer | 9 | Pitched-up feedback through diffusion |
| Frozen | 9 | Held buffer turned into pads (these presets switch Freeze on) |
| Delay | 9 | Grain echoes, synced or loose |
| Chorus | 7 | Thickening, detune and vibrato |
| Reverse | 7 | Backward grains and swells |
| Cascades | 8 | Random pitches quantised to a scale |
| Lo-Fi | 8 | Wobble, band limiting and saturation |
| Ducked | 6 | Textures that step back while the source plays |
| Smear | 8 | Blurred transients, pad-like sustain |
| Drones | 9 | Long feedback beds from short material |

In every preset Macro 1 and Macro 2 are routed to the controls that matter most for that sound, so turning them is the quickest way to adapt it. Presets are defined in `scripts/build-presets.sh`, which regenerates `presets/factory`.

## Install

1. Download `granulr-<version>-windows-vst3.zip` and its `.sha256` file from the Releases page of this repository.
2. Check the archive (PowerShell): `Get-FileHash .\granulr-<version>-windows-vst3.zip -Algorithm SHA256` and compare with the content of the `.sha256` file.
3. Extract `granulr.vst3` into `C:\Program Files\Common Files\VST3`.
4. Rescan plugins in your host.

Requirements: Windows 10 or 11, 64-bit, a processor with AVX2 (any Intel or AMD desktop processor since about 2015), a VST3 host. OpenGL 3.2 is used for the visualisers when available; otherwise the plugin falls back to software drawing.

The binaries are not code signed. Windows SmartScreen or your browser may warn about the download; the checksum above lets you confirm the file is the one built by the release workflow.

## Build from source

Prerequisites: Visual Studio 2022 or newer (or the Build Tools) with the C++ desktop workload, CMake 3.22 or newer, Git. JUCE 9.0.5 is downloaded automatically at configure time.

```
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

By default the build copies `granulr.vst3` into `C:\Program Files\Common Files\VST3`. If that folder is not writable for your account, either run `scripts/install-vst3.ps1` from an elevated PowerShell after the build, or change the behaviour at configure time:

| Option | Default | Effect |
| :- | :- | :- |
| `VST3_INSTALL_DIR` | `C:/Program Files/Common Files/VST3` | Folder the bundle is copied to |
| `GRANULR_COPY_PLUGIN` | `ON` | Set to `OFF` to skip the copy |
| `GRANULR_VERSION` | `0.1.0` | Version written into the plugin |
| `GRANULR_FAST_MATH` | `OFF` | Enables `/fp:fast` |

Build outputs:

- `build/granulr_artefacts/Release/VST3/granulr.vst3`: the plugin
- `build/granulr_artefacts/Release/Standalone/granulr.exe`: standalone application
- `build/Release/granulr_tests.exe`: unit tests (`--verbose` prints the level of every factory preset)
- `build/Release/granulr_bench.exe`: CPU benchmark (`--quick`, `--seconds N`, `--presets`, `--check`)
- `build/Release/granulr_shot.exe`: off-screen screenshots (`--out`, `--zoom`, `--preset`, `--mode`, `--mod`, `--browser`)

## Using it

### Header

Preset name (click to open the browser), previous and next, A and B slots, Copy (copies the active slot to the other one), morph slider between A and B, Randomize, its options menu (amount and group locks), Undo, Redo, Mod (shows the modulation panel), quality, zoom, Settings, CPU and grain count, Bypass.

### Sections

| Section | Controls |
| :- | :- |
| Grains | Position and Size (free in milliseconds or synced to a note division), Density, Spray, Jitter, Reverse, Scan, Window, Skew, Pan, Width, Freeze |
| Output | Feedback, Duck (amount, attack, release), Input, Mix, Output, Limiter |
| Pitch | Scale, root, note strip (custom mask), Pitch, Fine, Random, Oct +, Oct -, four voices (interval and chance) |
| Trigger | Mode, rate, step division, pattern length, step grid (click: gate, drag up or down: chance, number below: ratchets) |
| Effects | Filter (type, cutoff, resonance), Saturation (type, drive), Diffusion (size, decay, damping, mix), each with its placement |
| Modulation | Source chips, editor of the selected source, macros, matrix |

Scan moves the read position through the buffer over time; it is most useful when the buffer is frozen.

### Modulation

- Drag a source chip onto a knob to add a route.
- Alt + drag on a knob changes the depth of the selected source for that knob.
- Right-click a knob for its routes, value entry and reset.
- The coloured ring around a knob shows the modulated value in real time.

### Scale quantisation

When a scale is selected, granulr tracks the pitch of the incoming audio and moves each grain to the nearest note of the scale, so the result lands on absolute scale notes. When the input has no clear pitch (noise, drums, dense chords), it falls back to quantising the transposition interval, which assumes the input is already in the chosen key.

### Shortcuts

| Keys | Action |
| :- | :- |
| Ctrl + plus, Ctrl + minus, Ctrl + wheel | Zoom in and out |
| Ctrl + 0 | Zoom back to 125 % |
| Ctrl + Z, Ctrl + Y | Undo, redo |
| Ctrl + D | Diagnostics overlay (frame rate, frame time, renderer, CPU) |

Some hosts keep keyboard shortcuts for themselves; every action is also available from the header.

### Settings

- **Frame rate:** Display sync (follows the monitor refresh rate), Unlimited (no frame rate limit, GPU only) or a fixed cap.
- **GPU rendering:** on by default. Turn it off to draw everything in software.
- **Show diagnostics**, **Open preset folder** and **About granulr**.

The zoom chosen last becomes the default for new instances.

Changes to the modulation matrix made by hand are not part of undo history; preset loads and randomisation are.

### Where files are stored

| What | Location |
| :- | :- |
| User presets | `Documents\granulr\Presets\*.granulrpreset` |
| Global settings and favourites | `%APPDATA%\granulr\settings.xml` |

Factory presets are embedded in the plugin.

## Performance

Measured with `granulr_bench` on a Ryzen 7 9800X3D, one core, as processing time divided by block duration, blocks of 128 samples. Full output: [docs/benchmark-9800x3d.txt](docs/benchmark-9800x3d.txt).

| Case | 48 kHz | 96 kHz | 192 kHz |
| :- | :- | :- | :- |
| Default settings, mean | 0.30 % | 0.40 % | 0.65 % |
| Worst case (High quality, maximum density, every effect on), mean | about 6 % | about 11 % | about 11.5 % |

Bypassed, the plugin uses about 0.2 % at 48 kHz. Each of the 100 factory presets stays under 1.4 % mean (2.1 % at the 99th percentile) at 96 kHz. Isolated single blocks can take longer than the mean (see the "worst" column of the benchmark output); their cause has not been isolated.

When the input and the grain tail are silent the engine goes idle. With the editor closed, no analysis is done for the visualisers.

## Debugging aid

Setting the environment variable `GRANULR_GL_CAPTURE` to a PNG file path before starting the host or the standalone application makes the editor save one image read back from the OpenGL frame shortly after opening. It is meant for checking the GPU rendering path on machines where normal screen capture does not work.

## Project structure

```
CMakeLists.txt            project, JUCE fetch, plugin and tool targets
src/plugin/               processor and editor
src/params/               parameter IDs and layout
src/dsp/                  grain engine, pitch tracking and quantisation, effects
src/mod/                  modulation sources and matrix
src/state/                presets, global settings
src/shared/               audio to interface bridge
src/gui/                  theme, look and feel, controls, visualisers, panels
presets/factory/          factory presets (generated by scripts/build-presets.sh)
assets/fonts/             Inter (SIL Open Font License)
tests/                    unit tests
bench/                    CPU benchmark
scripts/                  install script, preset generator, screenshot tool
.github/workflows/        release build
docs/                     plan, benchmark output, screenshots
```

## Releases

Publishing a GitHub release runs `.github/workflows/release.yml`, which builds the plugin on Windows, runs the tests and attaches `granulr-<tag>-windows-vst3.zip` and its SHA-256 file to the release. The workflow can also be started by hand; the archive is then kept as a workflow artifact.

## Licence

granulr is free software under the GNU Affero General Public License, version 3 or later. See [LICENSE](LICENSE).

It is built with [JUCE](https://juce.com) 9, used under its AGPLv3 option. The interface uses the Inter typeface under the SIL Open Font License ([assets/fonts/Inter-LICENSE.txt](assets/fonts/Inter-LICENSE.txt)). VST is a registered trademark of Steinberg Media Technologies GmbH.
