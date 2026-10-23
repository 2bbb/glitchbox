# EHL damage: CPU on the FM-1 (2026-10-23, 2026-10-24)

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
- Headroom comes from the second CPU core: merged from Felucca 1.6.1 (v1.6.1-ehl3), below.

## MASTER 0

The FX layer runs after the MASTER level. BITFLIP sets bits in silent words, and ADPCM's knocked decoder drifts on
its own, so both made sound with MASTER at 0. Since v1.5.1-ehl2, what they add is scaled by MASTER as well
(`perform.c dmg_master`). `glitch_test` checks that MASTER 0 with all four latched adds nothing: its largest sample
step over a held note was 18375 before the fix and 1 after. Without the FX layer it is 15, from the stock path.

## The second core (v1.6.1-ehl3, Felucca 1.6.1 merged)

Same scenarios, same device. MENU > SYSTEM > 2ND CORE is OFF by default (BETA upstream); the measurement build's
console turns it on (`cpu2 1`, from the next power-on).

| Scenario | 2ND CORE OFF: cpu / peak / late / shed | 2ND CORE ON: cpu / peak / late / shed |
|---|---|---|
| mixed EHL INSERTs, no FX | 66 % / 85.9 % / 0 / 1 | 42 % / 73.1 % / 0 / 0 |
| + GATE (stock) | 73 % / 95.1 % / 6 / 19 | 50 % / 78.8 % / 0 / 0 |
| + REPEAT 1/16 (stock) | 72 % / 102.8 % / 6 / 50 | 53 % / 84.1 % / 0 / 0 |
| + WRAP | 71 % / 97.3 % / 3 / 49 | 51 % / 81.4 % / 0 / 0 |
| + BITFLIP | 69 % / 97.7 % / 1 / 48 | 51 % / 80.0 % / 0 / 0 |
| + UNDERRUN | 71 % / 97.1 % / 1 / 49 | 51 % / 82.3 % / 0 / 0 |
| + ADPCM | 75 % / 99.8 % / 6 / 60 | 55 % / 84.2 % / 0 / 0 |
| INSERTs OFF + all 4 damage FX | 67 % / 90.6 % / 0 / 11 | 49 % / 76.5 % / 0 / 0 |
| mixed EHL INSERTs + all 4 damage FX | 73 % / 100.8 % / 7 / 60 | 55 % / 86.9 % / 0 / 0 |

With 2ND CORE ON, the INSERT table (power-on parts playing 37 % / 60.9 %, each EHL type on all four tracks 40–42 % /
69–72 %, BASS ×4 43 % / 74 %) and every FX case run without a shed voice or a late half. The second core took about
11,000 blocks per 8 s scenario, and the first core waited for it 155 µs at most. It did not give up, and no job was
misread (`cpu2_gave_up 0`, `cpu2_bad 0`).

The worst case still peaks at 87 % now and then, but Felucca only sheds after two halves in a row above 85 %, and none
did. **Recommendation: turn MENU > SYSTEM > 2ND CORE ON.** The default stays OFF, as upstream, because it is BETA
there. The setting is kept across firmware installs.
