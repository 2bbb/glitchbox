# EHL damage: CPU on the FM-1 (2026-10-23)

Measured on a real FM-1 with an `EHL_MEASURE=1` build and `tools/ehl_measure.py`. The script plays re-struck chords on
MIDI channels 1-3 (8 notes each, more than the 8 shared voices) and a drum pattern on channel 4, then reads the
console's `status` while the notes still sound. Each scenario ran for 8 s.

Columns:

- **cpu**: Felucca's load meter, a ~93 ms average.
- **peak**: the longest audio half rendered, against the 2902 µs half.
- **shed**: voices Felucca faded out. It sheds after two halves in a row above 85 %.
- **late**: halves the DMA had moved past, which are audible.

## INSERTs (v1.5.1-ehl1 measure build)

| Scenario | cpu | peak | late | shed |
|---|---|---|---|---|
| idle, power-on parts | 12 % | 15.6 % | 0 | 0 |
| power-on parts playing | 59 % | 86.6 % | 0 | 0 |
| all INSERTs OFF playing | 52 % | 80.5 % | 0 | 0 |
| ADPCM ×4 | 60 % | 83.1 % | 0 | 0 |
| OVRFL ×4 | 56 % | 80.0 % | 0 | 0 |
| MISRD ×4 | 57 % | 82.4 % | 0 | 0 |
| SHEAR ×4 | 57 % | 78.4 % | 0 | 0 |
| UNDRN ×4 | 57 % | 78.0 % | 0 | 0 |
| BASS ×4 (the heaviest stock INSERT) | 62 % | 90.0 % | 0 | 0 |

The EHL INSERTs cost 4–8 points of the average on all four tracks, less than the stock BASS, and never shed a voice.
The peak is set by the GLITCH voices, not by the INSERTs.

## FX layer (v1.5.1-ehl2 measure build, INSERTs ADPCM / OVRFL / MISRD / UNDRN)

| Scenario | cpu | peak | late | shed |
|---|---|---|---|---|
| no FX | 61 % | 83.5 % | 0 | 0 |
| GATE (stock) | 67 % | 90.7 % | 0 | 5 |
| REPEAT 1/16 (stock) | 70 % | 93.1 % | 0 | 13 |
| WRAP | 71 % | 92.7 % | 0 | 33 |
| BITFLIP | 71 % | 99.8 % | 2 | 44 |
| UNDERRUN | 68 % | 97.0 % | 4 | 51 |
| ADPCM | 74 % | 92.5 % | 0 | 51 |
| INSERTs OFF + all 4 damage FX | 67 % | 88.6 % | 0 | 0 |
| all 4 damage FX | 71 % | 95.3 % | 5 | 61 |

Holding any FX key costs 6 to 13 points, the stock GATE and REPEAT included. WRAP does almost no arithmetic and costs
as much as REPEAT, so most of that cost is the FX layer's own path turning on, not the damage math.

Processing the damage block at a time instead of a call per sample changed nothing measurable. The two runs before
and after that change are within each other's run-to-run spread (cpu ±3, peak ±4 points).

## Reading

- With eight GLITCH voices sounding, the base already peaks at 80–87 % of a half. That is right at Felucca's 85 %
  shedding line, so anything on the master pushes it over and Felucca fades voices out. It degrades on purpose
  (`shed`), and only rarely misses a half (`late`, a few per 8 s, in the heaviest cases).
- In normal playing (fewer than 8 voices at once, one FX key at a time) the load stays below the line. The worst case
  above stacks three tracks of eight-note chords, a drum pattern, four damage INSERTs and every damage FX at once.
- Headroom would come from the second CPU core. Upstream Felucca 1.6 builds it in (`FELUCCA_CPU2`, MENU > SYSTEM >
  2ND CORE). Glitchbox is based on Felucca 1.5.1, which does not have it.

## MASTER 0

The FX layer runs after the MASTER level. BITFLIP sets bits in silent words, and ADPCM's knocked decoder drifts on
its own, so both made sound with MASTER at 0. Since v1.5.1-ehl2, what they add is scaled by MASTER as well
(`perform.c dmg_master`). `glitch_test` checks that MASTER 0 with all four latched adds nothing: its largest sample
step over a held note was 18375 before the fix and 1 after. Without the FX layer it is 15, from the stock path.
