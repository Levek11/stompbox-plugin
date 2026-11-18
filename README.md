# StompBox

A Logic Pro (AU) guitar plugin: a virtual pedalboard into two identical Royal 30
amps (an AC30 Top Boost style circuit model), one per stereo side, each into a
cab IR. Built for worship guitar, built in the cloud.

## Build status

| Step | What | Status |
|---|---|---|
| 1 | Amp/Cab tab: dual Royal 30 + cab IR loader | Done |
| 2a | Pedalboard: board, shelf, drag and drop, cables, close-up | Done |
| 2b | First pedal sounds: Hot Rod, Golden Hour, Afterglow | **In this build** |
| 3 | Overdrives: Blue Highway, Scotland, Nutrient | |
| 4 | Compressors: Pacific 76, Silver 76, Fresh Squeeze | |
| 5 | Time Machine, Open Sky | |
| 6 | True Tone, Sculptor | |

## The pedalboard (2a)

- Small (6 slots), Medium (10, two rows of 5) or Large (14, two rows of 7).
- Drag pedals between the shelf and the board; drop on an occupied slot and the
  others slide over if there's room. 2-slot pedals stay within one row.
- Signal runs right to left, bottom row first. Cables route themselves; after
  a stereo pedal they're doubled (L and R).
- Click a pedal for its close-up; the footswitch toggles it on the board.
- Everything is saved with the Logic project. Pedals pass audio through
  unchanged until their sound arrives (step noted in each close-up).

## The first pedals (2b)

- **Hot Rod** (SD-1): op-amp gain stage focused above ~720 Hz, three silicon
  diodes clipping asymmetrically, active Tone, Level. Oversampled.
- **Golden Hour** (Morning Glory V4): Bluesbreaker-family gain stage into
  soft symmetrical diode clipping; Gain switch (up = more gain and low end),
  Hi-Cut switch, high-headroom Volume. Oversampled.
- **Afterglow** (HALO): stereo modulated dual echo with tape-like repeats
  (darker, saturating), RHYTHM 1/4, dotted 1/8 or HALO (both at once), SYNC to
  Logic's tempo (incl. tempo changes) or FREE in ms, trails when switched off.
- The chain is mono until a stereo pedal; after it, mono pedals run as dual
  mono and each side feeds its own Royal 30.

## What's in step 1

- **Royal 30, x2.** Two identical amps, one per side, sharing one set of controls:
  Channel (Normal / Top Boost), Volume, Treble, Bass, Cut, Master.
  The input jack is mono like a guitar cable: on a stereo track with the guitar
  on one side, StompBox picks up that side, and both amps get the same signal.
  Works as Mono, Mono→Stereo or Stereo in Logic.
- **Circuit-based model.** ECC83 preamp stages with grid-conduction bias shift,
  the Top Boost Treble/Bass stack solved from its real component values
  (100k slope, 50 pF / 22 nF / 22 nF, fixed 10k "mid", 1M log pots), cathode
  follower, long-tailed-pair phase inverter, the Cut network across the PI
  outputs, 4 x EL84 push-pull output with rectifier sag, and no negative
  feedback (so the speaker's impedance curve shows in the response).
- **Cab IR loader.** WAV/AIFF, same IR on both sides, zero-latency convolution.
  The IR is saved inside the plugin state, so a Logic project keeps working if
  the file moves. Without an IR, a basic speaker filter is used.
- **Zero latency.** No lookahead anywhere; reports 0 samples to Logic.
- **Live / Studio.** Live runs the amps at 2x oversampling, Studio at 4x.
  Offline bounces always use Studio. Remembered globally, not per project.

## Getting the plugin

Every push builds on GitHub Actions (macOS, universal Apple silicon + Intel).
Open the latest run under **Actions**, download **StompBox-macOS**, and follow
`INSTALL.txt` inside it.

The build also runs the DSP tests (tone stack, gain staging, zero latency,
IR save/restore, levels, CPU) and validates the AU with `auval`.

## Building locally (optional)

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

JUCE 8.0.9 is fetched automatically.

## Layout

```
Source/dsp/Filters.h     filter building blocks
Source/dsp/Tubes.h       triode / phase inverter / EL84 curves and stages
Source/dsp/ToneStack.h   Top Boost tone stack
Source/dsp/Royal30.*     one amp, and the dual-amp pair
Source/dsp/CabIR.*       IR loading, state storage, zero-latency convolution
Source/dsp/Engine.*      full chain: oversampling -> amps -> cab -> output
Source/board/            pedal catalog and board model (slots, order, state)
Source/ui/               look and feel, Amp/Cab tab, Pedalboard tab, close-up
Tests/DspTest.cpp        headless checks, run in CI
```

The Goldman and Archivo Black fonts are bundled under the SIL Open Font License (`assets/fonts`).
