# StompBox

A Logic Pro (AU) guitar plugin: a virtual pedalboard into two identical Royal 30
amps (an AC30 Top Boost style circuit model), one per stereo side, each into a
cab IR. Built for worship guitar, built in the cloud.

## Build status

| Step | What | Status |
|---|---|---|
| 1 | Amp/Cab tab: dual Royal 30 + cab IR loader | Done |
| 2a | Pedalboard: board, shelf, drag and drop, cables, close-up | Done |
| 2b | First pedal sounds: Hot Rod, Golden Hour, Afterglow | Done |
| 3 | Overdrives: Blue Highway, Scotland, Nutrient | Done |
| 4 + 6 | Compressors, tuner and EQ: Pacific 76, Silver 76, Fresh Squeeze, True Tone, Sculptor | **In this build** |
| 5 | Time Machine, Open Sky | |

## The pedalboard (2a)

- Small (6 slots), Medium (10, two rows of 5) or Large (14, two rows of 7).
- Drag pedals between the shelf and the board; drop on an occupied slot and the
  others slide over if there's room. 2-slot pedals stay within one row.
- Signal runs right to left, bottom row first. Cables route themselves; after
  a stereo pedal they're doubled (L and R).
- Click a pedal for its close-up; the footswitch toggles it on the board.
- A pedal put back on the shelf returns to its default settings.
- Everything is saved with the Logic project. Pedals pass audio through
  unchanged until their sound arrives (step noted in each close-up).

## Compressors, tuner and EQ (4 + 6)

- **Pacific 76** (Cali76): 1176-style FET compressor. IN drives it, RATIO
  4:1 to 20:1, ATTACK, RELEASE (program-dependent), OUT, and DRY for parallel
  compression. The dots are a live gain-reduction meter (2 dB a dot).
- **Silver 76** (UAFX 1176): INPUT / OUTPUT; ATTACK and RELEASE get faster
  clockwise, as on the hardware; RATIO steps 4, 8, 12, 20 and ALL (all
  buttons in). MODE: Single, Dual (two in series, smoother and deeper) or
  Sustain. MIX blends in the dry signal (the hardware's parallel mode).
- **Fresh Squeeze** (Pulp 'N' Peel V4): smooth OTA-style compression with
  auto makeup; BLEND dry to fully compressed; EQ flat at noon; DIRT adds the
  overdrive; lots of clean VOLUME.
- **True Tone** (TU-3): chromatic tuner, accurate to well under a cent from
  drop D to the high frets. Mutes the output while on, and arrives on the
  board switched off.
- **Sculptor** (GE-7): seven bands at 100 Hz to 6.4 kHz and LEVEL, each
  +-15 dB; centred is flat.

## The first pedals (2b)

- **Hot Rod** (SD-1): op-amp gain stage focused above ~720 Hz, three silicon
  diodes clipping asymmetrically, active Tone, Level. Oversampled.
- **Golden Hour** (Morning Glory V4): Bluesbreaker-family gain stage into
  soft symmetrical diode clipping; Gain switch (up = more gain and low end),
  Hi-Cut switch, high-headroom Volume. Oversampled.
- **Afterglow** (HALO): stereo modulated dual echo, always in the HALO rhythm
  (dotted eighth left, quarter right) and always synced to Logic's tempo
  (incl. tempo changes). Tape-like repeats; TONE makes them darker or brighter
  (1 kHz to 8.75 kHz); trails when switched off.
- **Blue Highway** (BD-2): two cascaded soft-clipping gain stages on one
  GAIN knob, a big pre-clipping bass boost (fuzzy, touch-sensitive), wide TONE.
- **Scotland** (Kilt V2): op-amp drive whose G1 / G2 toggles step from boost
  and overdrive (G1) to fuzz (G2) to gated fuzz (both); TONE, CUT/FLAT, VOLUME.
- **Nutrient** (Protein V4): Green (right, ODR-1 style) into Blue (left,
  Bluesbreaker style), a footswitch each; Blue has the 3-way HI-CUT.
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
- **Cab IR loader.** WAV/AIFF, zero-latency convolution. **Link L/R** on (the
  default): one IR on both amps. Off: a different IR on the left and right
  amps; two different IRs are lined up automatically (start time and
  polarity) so the guitar stays full when the mix is summed to mono. IRs are
  saved inside the plugin state, so a Logic project keeps working if the
  files move. Without an IR, a basic speaker filter is used.
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
