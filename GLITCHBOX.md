# Glitchbox

Glitchbox is a glitch/industrial edition of [Felucca](https://github.com/hugelton/Felucca) for the M-VAVE FM-1. Think Year Zero and "The Great Destroyer": harsh resonant synths that chop themselves up, crushed drums, and stutter/crush effects you play live. All the sounds are synthesised; there are no samples to load.

The base is Felucca v1.5.1 (GPL-3.0-only). Everything Felucca does still works: four tracks, live loop recording with overdub, step entry, parameter locks, song mode and MIDI. Glitchbox adds the following.

## What's new

### GLITCH engine (shown as "GLCH", engine 7)

A harsh voice that chops itself up, with up to 2 voices per track. The signal runs: oscillator, then DRV, then a resonant low-pass filter, then CRSH, then the micro-stutter, then a DC block.

| Knob (HOME) | Parameter | What it does |
|---|---|---|
| K1 | CUT | Filter cutoff (ENV/LFO/matrix move it, as on ANALOG) |
| K2 | RES | Resonance; it nearly self-oscillates at 127 |
| K3 | DRV | Hard clipping, 2x to 32x before the filter and 2x to 16x after it; from 64 up a wavefolder as well. Also deepens FM |
| K4 | GLCH | Chance per segment that the voice loops a slice of itself |

The EDIT pages add:

- **WAVE**: SAW, SQR, DUAL (two detuned saws), RING (saw x sine, metallic), NOIS, FAT (two saws + sub square), FM (1:1, index follows the envelope) or FMX (inharmonic FM with feedback).
- **CRSH**: fewer bits and sample-and-hold together (up to 13 bits off, 8x hold).
- **SIZE**: the stutter segment length, 6-70 ms.
- **PJMP**: tape-speed pitch jumps for the loops: +7, -5, +12, -12, +5 and -7 semitones, unlocked in that order as PJMP rises.

Loops can also play reversed (1 in 4) or as 1/2 and 1/4 rolls. The first segment after a note-on always plays clean, so the attack stays intact.

Presets: DESTROYER, FUZZ BASS, BROKEN, SCREAM, NOIZ HIT, HUM, NASTY FM, CRUSH LEAD, SQR FUZZ, FM BASS, MELTDOWN and RUST SAW, plus the bass family's FUZZ BASS, REESE, 808 DIST, GRIT BASS, GROWL, ACID FUZZ and OCTA SMASH (waves FUZZ, REES, 808, GRIT and GROW: a Big-Muff-style fuzz that keeps the fundamental, with a clean SUB under it; for these waves EDIT 3/5/6/7 are FUZZ, SUB, TONE and OCTV, and KNOB 4 is SUB). The defaults are dirty too: FAT wave, DRV 110, CRSH 70, RES 95.

Note: GLITCH took WHEEL's engine slot. Felucca's saved data only has room for 14 engine slots. A Felucca project or preset that used WHEEL will load as GLITCH.

### INSERT per preset

A preset can now carry its own INSERT (TYPE, A, B, C and MIX), applied whenever it loads:

| Preset | INSERT |
|---|---|
| DESTROYER, SQR FUZZ, REESE | MUFF |
| FUZZ BASS, 808 DIST, GRIT BASS | BASS (clean low band kept, highs destroyed) |
| NOIZ HIT, OCTA SMASH, DRUM DESTROY | SMASH (compressor + hard clip) |
| SCREAM | FUZZ |
| CRUSH LEAD | CRUSH |
| MELTDOWN | FOLD |

Each INSERT's LEVEL is set so the preset stays near its level without it. Turn INSERT to OFF on the track for the raw sound.

### Knobs and the emulator

- KNOB ACCEL is ON by default: fast turns of wide values move 2x to 8x (MENU > CONTROL > KNOB ACCEL to switch it off).
- In the browser emulator a knob moves twice as fast per pixel of drag or trackpad scroll, and REC on shows a red outline on REC plus a "REC ARMED" / "RECORDING" badge above the panel.

### DESTROY drum kit

In the DRUM engine, KIT = DSTR, and the preset is DRUM > DESTROY. It uses Felucca's standard drum voices, pushed harder:

- **Kick:** lower, longer and driven.
- **Snare and clap:** snappier and brighter.
- **Hats:** brighter, with more sizzle.

For this kit, **DRV becomes the destroyer**: it drives each hit into a soft clip at 2-8x, then removes 0-10 bits and holds samples for 1-4 samples. The drum grid, lane mutes and live recording work exactly as on the other kits. The DESTROY beat is in SAVE > PHRASES as pattern 14.

### FX layer: four new effects (hold FX + a white key)

| Key | Effect |
|---|---|
| F3 G3 A3 B3 | REPEAT 1/8, 1/16, 1/32, REVERSE (as in Felucca) |
| C4 D4 | LPF, HPF sweeps |
| E4 F4 | TAPE STOP, FREEZE |
| G4 A4 | OCT UP, OCT DN |
| B4 | FLANGER |
| **C5** | **GATE**: mutes the second half of every 1/32 |
| **D5** | **CRSH SWP**: CRUSH builds from nothing to full over one bar |
| E5 | ECHO THROW |
| **F5** | **ROLL**: a repeat that speeds up: 1/8 for a beat, then 1/16, 1/32, 1/64 |
| **G5** | **RANDOM**: on every 1/16, a dice roll between nothing, REPEAT 1/16, REPEAT 1/32, REVERSE or GATE |

PHASER and REV THROW are still available: hold the key in the FX layer and turn PRESETS to remap any key.

### Power-on

At power-on, T1 is GLITCH DESTROYER, T2 is GLITCH FUZZ BASS, T3 is GLITCH HUM and T4 is DRUM DESTROY. All sequencers start empty, so you play them in yourself.

The metronome clicks **while recording** by default (MENU > AUDIO > CLICK REC; OFF and ON are still there). The build switch `GLITCHBOX_DEFAULTS=0` restores Felucca's start sounds and CLICK OFF.

## EHL damage (this fork, v1.5.1-ehl1)

Failure models from the EsionHsrahLatigid plug-ins, running on the FM-1's real integer audio path: the sound breaks
the way a broken codec, a fixed-point chip or an audio driver breaks it.

### Five more INSERT types (per track: INSERT > TYPE)

| TYPE | A | B | C | What it does |
|---|---|---|---|---|
| **ADPCM** | BITS | ERR | RATE | IMA ADPCM in the loop: 2 to 5 bits a code, bit errors and a knocked step index that make the decoder drift away from the encoder, codes held 1 to 16 samples |
| **OVRFL** | FREQ | RES | DRIVE | a resonant low-pass in 16-bit words that wrap instead of clipping: past the edge it folds over and falls into limit cycles of its own |
| **MISRD** | MODE | PROB | SEG | segments read as another format: SWAP (bytes), ULAW, SIGN, HALF (float), NIBL, SHIFT (a byte late), BREV (bits reversed) or MIX |
| **SHEAR** | BITS | FAULT | CLOCK | a word corrupter: 16 to 2 bits, a jittering sample-and-hold, bursts that XOR, rotate or swap the words |
| **UNDRN** | LOAD | MODE | BUF | buffers that miss their deadline: the last buffer again (MODE 0) or silence and stale memory (MODE up), 32 to 256 samples |

### Four more FX keys (hold FX + a white key)

| Key | Effect |
|---|---|
| **G4** | **WRAP**: the master's words wrap around at -12 dBFS (two's complement overflow) instead of clipping |
| **A4** | **BITFLIP**: bit errors in the master's 16-bit words |
| **B4** | **UNDERRUN**: late 32-sample blocks repeat or drop out |
| **E5** | **ADPCM**: the master through 3-bit ADPCM with a knocked step index |

KNOB 2 (CRUSH) with FX makes BITFLIP, UNDERRUN and ADPCM's errors denser. OCT UP, OCT DN, FLANGER and ECHO THROW left
the default map; hold a key in the FX layer and turn PRESETS to put them back.

### Presets and power-on

New GLCH presets: CODEC ROT, OVERFLOW, MISREAD, SHEARED, UNDERRUN and BIT ROT BASS, each with its damage INSERT.
At power-on T1 is CODEC ROT, T2 BIT ROT BASS, T3 UNDERRUN and T4 DRUM DESTROY.

## Volca-style live programming

Felucca already has this:

1. Press PLAY, then REC. The metronome clicks while you record.
2. Play keys on the selected track. Notes are written in, quantised by the track's QUANTIZE setting, and overdub on every pass.
3. On a DRUM track, each key hits a lane: C is the kick, D the snare, F# the closed hat, and so on.
4. To lock a parameter on one step, hold that step and turn a knob (parameter locks).
5. Turn knobs while recording to record knob movements (motion).

## Building and testing (macOS, no hardware needed)

```bash
python3 -m venv .venv && .venv/bin/pip install Pillow fonttools mido python-rtmidi
```

```bash
brew install fribidi
```

```bash
tools/gen_only.sh
```

```bash
tests/quick_tests.sh
```

- `tools/gen_only.sh` generates `build/gen` (fonts, tables and samples) without the JieLi toolchain or Docker.
- `tests/quick_tests.sh` runs the host tests: the Glitchbox sounds and FX, the drums, the FX layer, the UI, MIDI, projects, engine switches, every screen's layout and the regression renders.
- `tests/glitch_test.c` also writes WAV demos to `build/demo/glitch/`: each GLITCH preset, the DESTROY beat, a jam, and the jam under ROLL, GATE, CRSH SWP and RANDOM.

**Known on this Mac:** `perform_test` reports the FLANGER/PHASER cost over its 2.5 % device estimate. Unmodified Felucca fails the same check on this host, because the host compiler counts instructions differently. Treat it as information, not as a Glitchbox regression. For the other CPU budgets, `regress` uses a baseline measured on this Mac (`build/host/cpu_local.txt`).

### Browser emulator (needs Emscripten)

```bash
brew install emscripten
```

```bash
web/emu/build.sh
```

```bash
python3 -m http.server -d build/emu 8790
```

Then open http://localhost:8790: the full panel, playable with mouse, computer keyboard or Web MIDI. `web/emu/build.sh` only needs `build/gen` (from `tools/gen_only.sh`), not a firmware build.

### Firmware (needs Docker Desktop + the JieLi toolchain)

See BUILDING.md: `tools/get_toolchain.sh`, the AC79 SDK, then `./build.sh` produces `build/glitchbox.fwsc`. On Apple Silicon, the Linux x86-64 compiler runs in Docker through Rosetta. `tests/run_tests.sh` is the full suite and needs that build.

## Flashing safely (once you have an FM-1)

1. Read Felucca's README on installing, and **back up the flash first**.
2. Install with `tools/fm1_install.py build/glitchbox.fwsc`, or a local copy of the web installer, from Chrome or Edge.
3. **Do not unplug during an update.** The reverse-engineering notes (AL-255/FM-1-RE) say interrupted writes are not proven safe.
4. To go back: the installer's "Return to official V15", or M-VAVE's own updater.
5. Last resort for a unit that stays black: the WL80UBOOT boot mode with a Seeed XIAO RP2040 ("FM-1 Transporter") and the official FM-1.fwsc.

The firmware runs fixed-point at 44.1 kHz on the FM-1's pi32v2 CPU. CPU headroom on the device has not been measured yet, so keep an eye out for dropouts when all four tracks play heavy GLITCH patches.

## License

GPL-3.0-only, as Felucca. If you share builds, share the source. Glitchbox is not affiliated with M-VAVE or Hügelton Instruments.
