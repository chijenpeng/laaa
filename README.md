# LAAA

Inspired by the Teletronix LA-2A.<br>
Part of our legacy collection: recreations built from schematics where available, with extremely high CPU use in the heaviest models. They're legacy now, but we still think they sound pretty great.

LAAA is an AU and VST3 optical leveling amplifier built with JUCE. Its signal path is a schematic-derived, calibrated hybrid: documented resistor networks and load-line tube stages are modelled, while optical response, some tube stages and transformer behavior use empirical approximations. It is not a component-complete simulation of a measured unit.

This release has its own plug-in identity (`com.c0dypeng.laaa`) so it can install alongside the original LA-2A. Existing DAW sessions continue to resolve their LA-2A instances to that original product. LAAA retains the parameter IDs, order, ranges, defaults and `La2a` saved-state root for compatible parameter data.

## Downloads

[Latest GitHub release](https://github.com/chijenpeng/laaa/releases/latest)

- macOS Universal AU and VST3 for Apple silicon and Intel; ad-hoc signed and not notarized.
- Windows x64 VST3; unsigned.

The DSP follows the Teletronix schematic where the available electrical data
supports it. The T4 audio photocell is a changing conductance in the actual
R6/R7–R1/R8 divider, ahead of the Gain potentiometer. V1's two 12AX7 sections,
V2's coupled 12BH7 follower, and V3's parallel 12AX7 sidechain sections use
load-line tables prepared from their schematic loads and fitted tube curves.
C2 coupling, C8 cathode bypass, and R37/C12 sidechain emphasis are
active filter stages. C6/R30 shunts the stereo parallel bus on the original
schematic; it is not a series filter in the standalone signal path. The T4
conductance has fast and slow recovering populations with exposure memory.
Peak Reduction, Compress/Limit, stereo link, and metering
follow the corresponding signal and control paths.

The electrical drawing does **not** specify the electroluminescent panel's
voltage-to-light curve, the T4 cell's light-to-resistance and memory functions,
or the UTC transformer winding parasitics. Those parts are calibrated models,
as are the interface level and Gain pot taper. The V4 6AQ5/EL driver, complete
C1/C3/C5 and supply networks, and second T4 metering cell are not
node-by-node simulations. This is a schematic-derived, calibrated hybrid,
**not a component-complete simulation of a measured hardware unit**. Gain,
Peak Reduction, detector response, and T4 recovery use empirical calibration
curves where the schematic does not provide enough data. These curves describe
the chosen model and do not claim unit-specific hardware measurements.

## Electrical model inventory

| Section | Implementation | Evidence / limit |
| --- | --- | --- |
| HA-100X input | Calibrated level and passband filter | No original winding equivalent parameters |
| T4 attenuator | R6 68 kΩ + R7 2.7 kΩ into R1 100 kΩ, R8 2.2 MΩ, variable T4 shunt | Resistors from Teletronix schematic; lamp/CdS law fitted |
| V1 audio amplifier | Two 12AX7 load-line stages, 220 kΩ plates and 1.5/2.7 kΩ cathodes | C1/R11/R12 coupling is unresolved; tube curve is a fit, not a digitization of every RCA plate curve |
| V2 output stage | Coupled 12BH7 load-line table with R17/R18/R20/R21 | RCA operating point; simplified AC coupling |
| Sidechain | Peak Reduction, parallel V3 12AX7, C8, R37/C12; C6/R30 on stereo parallel bus | V4 6AQ5/EL drive calibrated rather than plate-solved |
| A-24 output | Calibrated output scale and passband filter | No original winding/load equivalent parameters |

The resistor-divider tests exercise specific photocell resistance values
independently of the optical-transfer fit. Audio processing is allocation-free;
the tube load-line iterations run during preparation and are tabulated for the
callback.

## Model checks

The regression suite covers 19 Gain/Peak Reduction operating points, detector
response from 50 Hz to 16 kHz, 800 Hz burst and re-attack behavior, and 220 Hz
harmonics. These checks keep the selected empirical model consistent; they do
not establish physical-unit specifications. The T4 model has a prompt
conductance response and a slower memory population, with fitted attack and
recovery curves rather than measured component values. The audio tubes use
bounded load-line tables; large-signal color has not been validated against an
original hardware unit.

The schematic labels C1 as 0.02 µF between V1A and V1B, with R11 68 kΩ to
V1A's cathode and R12 470 kΩ to ground. A coupled plate/cathode/C1 prototype
using those nominal values predicts roughly 9 dB loss at 30 Hz. A published
study of six hardware units reports at most 1.34 dB variance across
30 Hz–15 kHz.
The present audio path leaves that isolated coupling model out rather than
adding an unverified compensating equalizer. A measured unit's actual
component values and node responses would resolve this discrepancy.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Laaa_AU Laaa_VST3 laaa_test laaa_benchmark laaa_snapshot -j 2
ctest --test-dir build --output-on-failure
./build/laaa_benchmark
./build/laaa_snapshot_artefacts/Release/laaa_snapshot docs
```

`JUCE_SOURCE_DIR` is optional. Without it, CMake checks this repository's
`third_party/JUCE` for JUCE 8.0.9 and fetches the pinned 8.0.9 release if it is
not available.
Set `-DCOPY_PLUGIN_AFTER_BUILD=ON` only when you want CMake to copy the AU and
VST3 builds into the machine's system plug-in folders; it defaults to `OFF`.

The editor follows the shared codycodie editor style: flat Gain and Peak
Reduction dials, a smaller R37 Emphasis dial, a VU meter with the gain reduction
in its hood and IN / OUT levels (dBFS) beneath it, and one compact lane of text
choices above them for Compress/Limit, GR/+4/+10 metering, stereo linking and
bypass. Host automation and saved sessions control every setting. Shift-drag a
dial for fine adjustment, double-click it to reset, or double-click its number
to type a value. Hover over a control and its explanation appears at the bottom
left. The editor scales (0.75x to 2x) while preserving its aspect ratio. The
snapshot tool writes 2x `docs/editor.png` and `docs/editor-plus4.png` for visual
review.
Embedded Inter and JetBrains Mono font licenses are in `Resources/fonts`.

JUCE supplies the framework and its license notice is available in the JUCE
source checkout. Include that notice with distributed builds, alongside the
font licenses in `Resources/fonts`.

## References

- [Teletronix LA-2A instruction manual (1966)](https://tile.loc.gov/storage-services/master/mbrs/recording_preservation/manuals/Teletronix%20Model%20LA-2A%20Leveling%20Amplifier.pdf), pp. 2–5 and schematic.
- [Universal Audio LA-2A manual](https://media.uaudio.com/assetlibrary/l/a/la-2a_manual.pdf), especially circuit figures 5–7.
- [RCA 12AX7A characteristics](https://frank.pocnet.net/sheets/049/1/12AX7A.pdf) and [12BH7A characteristics](https://frank.pocnet.net/sheets/049/1/12BH7A.pdf).
- [UTC transformer catalog](https://www.ka-electronics.com/images/pdf/UTC_Transformer_Catalog_1974.pdf). Its frequency plots do not supply the specific winding parasitics of a particular LA-2A unit.
- [Moore, “Objective Analysis and Perceptual Evaluation of LA-2A Compressors and Vocal Recordings” (2026)](https://pure.hud.ac.uk/ws/portalfiles/portal/140787498/AAM.pdf). Six measured hardware units varied by 0.50–1.34 dB over 30 Hz–15 kHz under the study's high-impedance output load; none met the manual's ±0.1 dB figure exactly.

The two manuals differ in their wording and release estimates. The release
curves, compression law and tone approximations are deliberately tunable model
choices rather than claims of exact hardware matching.
