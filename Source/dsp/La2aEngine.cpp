#include "La2aEngine.h"

#include <algorithm>
#include <cmath>

namespace la2a
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
// Teletronix 1966 schematic / UA figure 5. R5 loads the HA-100X secondary;
// R6 and R7 are in series with the T4A shunt and the 100 k Gain pot.
constexpr float inputSeriesOhms = 68000.0f + 2700.0f; // R6 + R7
constexpr float gainPotOhms = 100000.0f;                // R1
constexpr float cellDarkShuntOhms = 2200000.0f;         // R8
constexpr float darkLoadOhms = 1.0f / (1.0f / gainPotOhms + 1.0f / cellDarkShuntOhms);
constexpr float darkDivider = darkLoadOhms / (inputSeriesOhms + darkLoadOhms);
constexpr float maximumAmplifierGain = 76.736f; // 37.7 dB at R1 top from gain calibration
constexpr float maximumSidechainScale = 602.5596f; // fitted V3 drive at R2's upper stop

float clamp (float x, float low, float high) noexcept { return std::min (high, std::max (low, x)); }
float coefficient (float seconds, float sampleRate) noexcept { return std::exp (-1.0f / (seconds * sampleRate)); }
float dbToAmp (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

float opticalDivider (float cellMilliSiemens) noexcept
{
    // T4A is the variable grounded leg of R6/R7 : (R1 || R8 || T4A).
    const float effectiveLoad = 1.0f / (1.0f / gainPotOhms
                                    + 1.0f / cellDarkShuntOhms
                                    + std::max (0.0f, cellMilliSiemens) * 0.001f);
    return (effectiveLoad / (inputSeriesOhms + effectiveLoad)) / darkDivider;
}

float opticalConductanceForDriveDb (float driveDb) noexcept
{
    // Empirical lamp/T4 light-to-conductance calibration. The electrical
    // schematic specifies the divider, but not the electro-optic response.
    // Map the calibrated steady drive into millisiemens; conductance is
    // then the state that attacks and releases, not an output-dB state.
    const float targetDivider = darkDivider * dbToAmp (-driveDb);
    const float loadOhms = inputSeriesOhms * targetDivider / (1.0f - targetDivider);
    return std::max (0.0f, 1000.0f * (1.0f / loadOhms
                        - 1.0f / gainPotOhms - 1.0f / cellDarkShuntOhms));
}

float highpass (float input, float& oldInput, float& oldOutput, float a) noexcept
{
    const float output = a * (oldOutput + input - oldInput);
    oldInput = input;
    oldOutput = output;
    return output;
}

template <size_t N>
float interpolateTable (const std::array<float, N>& table, float value, float extent) noexcept
{
    const float position = (clamp (value, -extent, extent) / extent + 1.0f) * (N - 1) * 0.5f;
    const int index = std::min (static_cast<int> (N) - 2, static_cast<int> (position));
    const float fraction = position - static_cast<float> (index);
    return table[static_cast<size_t> (index)] * (1.0f - fraction)
         + table[static_cast<size_t> (index + 1)] * fraction;
}

template <size_t N>
bool isFiniteAndMonotonic (const std::array<float, N>& table) noexcept
{
    bool valid = true;
    for (int index = 0; index < static_cast<int> (N); ++index)
        valid &= std::isfinite (table[static_cast<size_t> (index)])
            && (index == 0 || table[static_cast<size_t> (index)]
                >= table[static_cast<size_t> (index - 1)] - 1.0e-5f);
    return valid;
}

// V1A/V1B: 12AX7A common-cathode stages with R9/R13=220 k plate loads,
// R10=1.5 k and R14=2.7 k cathode resistors. The 12AX7 curve is a Koren fit
// (not a value printed on the Teletronix schematic). The load-line solution
// is tabulated at prepare time; the audio callback only interpolates it.
double triodeCurrent (double plateToCathode, double gridToCathode) noexcept
{
    if (plateToCathode <= 0.0) return 0.0;
    constexpr double mu = 100.0, exponent = 1.4, kg1 = 1060.0;
    constexpr double kp = 600.0, kvb = 300.0;
    const double root = std::sqrt (kvb + plateToCathode * plateToCathode);
    const double argument = kp * (1.0 / mu + gridToCathode / root);
    const double softplus = argument > 40.0 ? argument : std::log1p (std::exp (argument));
    const double e1 = plateToCathode * softplus / kp;
    return 2.0 * std::pow (std::max (0.0, e1), exponent) / kg1;
}

double solveTriodePlate (double gridVolts, double supplyVolts, double plateOhms,
                         double cathodeOhms, bool parallelSections) noexcept
{
    double lowAmps = 0.0, highAmps = supplyVolts / plateOhms;
    for (int iteration = 0; iteration < 24; ++iteration)
    {
        const double amps = 0.5 * (lowAmps + highAmps);
        const double cathode = amps * cathodeOhms;
        const double plate = supplyVolts - amps * plateOhms;
        const double current = triodeCurrent (plate - cathode, gridVolts - cathode);
        const bool aboveLoadLine = parallelSections ? 2.0 * current > amps : current > amps;
        if (aboveLoadLine)
            lowAmps = amps;
        else
            highAmps = amps;
    }
    return supplyVolts - 0.5 * (lowAmps + highAmps) * plateOhms;
}

double triodePlate (double gridVolts, double cathodeOhms) noexcept
{
    constexpr double supplyVolts = 205.0; // B+ node marked on original schematic
    constexpr double plateOhms = 220000.0;
    return solveTriodePlate (gridVolts, supplyVolts, plateOhms, cathodeOhms, false);
}

double sidechainTriodePlate (double gridVolts) noexcept
{
    // V3A/V3B are connected in parallel in UA fig. 6: common R33=220 k
    // plate and R31=1 k cathode. R32=1 k is the inter-grid stopper.
    constexpr double supplyVolts = 275.0, plateOhms = 220000.0, cathodeOhms = 1000.0;
    return solveTriodePlate (gridVolts, supplyVolts, plateOhms, cathodeOhms, true);
}

// RCA 12BH7A sheet: mu 16.5, rp about 5.3 k, gm 3.1 mS and 11.5 mA
// at 250 V plate / -10.5 V grid. A 1.25-power law matches that point and
// its local slope; it is not a digitization of the full RCA curve family.
double bh7CurrentMilliAmps (double plateToCathode, double gridToCathode) noexcept
{
    const double effective = gridToCathode + std::max (0.0, plateToCathode) / 16.5;
    return 1.680 * std::pow (std::max (0.0, effective), 1.25);
}

struct FollowerPoint { double upperMilliAmps, outputVolts; bool converged = false; };

FollowerPoint solveFollower (double gridAc, FollowerPoint idle,
                             double idleUpperPlate, bool dc) noexcept
{
    // UA fig. 7: V2A plate through R17 10k, cathode through R18 1k to
    // output; R21 100k also feeds output; V2B plate sinks that node and
    // cathode returns through R20 1k. C3 makes V2B grid follow V2A plate
    // AC while R19 470k fixes its DC grid at ground.
    constexpr double rail = 205.0;
    auto residual = [&] (double upperMa, double outputVolts)
    {
        const double upperPlate = rail - 10.0 * upperMa;
        const double upperCathode = outputVolts + upperMa;
        const double lowerMa = upperMa + (rail - outputVolts) / 100.0;
        const double lowerCathode = lowerMa;
        const double upperGrid = dc ? outputVolts : idle.outputVolts + gridAc;
        const double lowerGrid = dc ? 0.0 : upperPlate - idleUpperPlate;
        return FollowerPoint {
            upperMa - bh7CurrentMilliAmps (upperPlate - upperCathode,
                                           upperGrid - upperCathode),
            lowerMa - bh7CurrentMilliAmps (outputVolts - lowerCathode,
                                           lowerGrid - lowerCathode) };
    };

    FollowerPoint result = idle;
    for (int iteration = 0; iteration < 18; ++iteration)
    {
        const auto f = residual (result.upperMilliAmps, result.outputVolts);
        const double norm = std::max (std::abs (f.upperMilliAmps), std::abs (f.outputVolts));
        if (norm < 1.0e-9) break;
        constexpr double currentStep = 1.0e-4, voltageStep = 1.0e-3;
        const auto fi = residual (result.upperMilliAmps + currentStep, result.outputVolts);
        const auto fv = residual (result.upperMilliAmps, result.outputVolts + voltageStep);
        const double a = (fi.upperMilliAmps - f.upperMilliAmps) / currentStep;
        const double b = (fv.upperMilliAmps - f.upperMilliAmps) / voltageStep;
        const double c = (fi.outputVolts - f.outputVolts) / currentStep;
        const double d = (fv.outputVolts - f.outputVolts) / voltageStep;
        const double determinant = a * d - b * c;
        if (std::abs (determinant) < 1.0e-12) break;
        const double currentDelta = (d * f.upperMilliAmps - b * f.outputVolts) / determinant;
        const double voltageDelta = (a * f.outputVolts - c * f.upperMilliAmps) / determinant;
        double scale = 1.0;
        for (int trial = 0; trial < 12; ++trial, scale *= 0.5)
        {
            const FollowerPoint next { result.upperMilliAmps - scale * currentDelta,
                                       result.outputVolts - scale * voltageDelta };
            if (next.upperMilliAmps >= 0.0 && next.upperMilliAmps <= 20.0
                && next.outputVolts >= 0.0 && next.outputVolts <= rail)
            {
                const auto check = residual (next.upperMilliAmps, next.outputVolts);
                if (std::max (std::abs (check.upperMilliAmps), std::abs (check.outputVolts)) < norm)
                {
                    result = next;
                    break;
                }
            }
        }
    }
    const auto last = residual (result.upperMilliAmps, result.outputVolts);
    result.converged = std::isfinite (result.upperMilliAmps) && std::isfinite (result.outputVolts)
        && std::max (std::abs (last.upperMilliAmps), std::abs (last.outputVolts)) < 1.0e-5;
    if (! result.converged)
    {
        // Preparation-time fallback for an out-of-range operating point.
        // Keep a monotone, bounded follower response rather than exposing a
        // failed Newton iterate to the audio callback.
        result.upperMilliAmps = idle.upperMilliAmps;
        result.outputVolts = std::clamp (idle.outputVolts + 0.93 * gridAc, 0.0, rail);
    }
    return result;
}
}

float Engine::decibels (float amplitude) noexcept
{
    return 20.0f * std::log10 (std::max (amplitude, 1.0e-6f));
}

float Engine::attenuationForCellOhms (float cellOhms) noexcept
{
    return opticalDivider (cellOhms > 0.0f ? 1000.0f / cellOhms : 1.0e6f);
}

float Engine::gainDbForKnob (float value) noexcept
{
    const float x = clamp (value, 0.0f, 100.0f);
    if (x <= 0.0f) return -120.0f;
    // Low-signal, Peak Reduction=0 gain calibration at 800 Hz. The
    // hardware-style pot is approximately unity
    // around 20, then reaches 37.7 dB of gain at the end stop.
    if (x < 5.0f) return -22.6f + 20.0f * std::log10 (x / 5.0f);
    struct Point { float knob, gainDb; };
    constexpr Point curve[] = {{ 5.0f, -22.6f }, { 10.0f, -9.6f },
                               { 15.0f, -4.1f }, { 20.0f, -0.1f },
                               { 25.0f, 2.7f }, { 50.0f, 14.5f },
                               { 75.0f, 28.6f }, { 100.0f, 37.7f }};
    for (size_t i = 1; i < sizeof (curve) / sizeof (curve[0]); ++i)
        if (x <= curve[i].knob)
        {
            const float t = (x - curve[i - 1].knob) / (curve[i].knob - curve[i - 1].knob);
            return curve[i - 1].gainDb + t * (curve[i].gainDb - curve[i - 1].gainDb);
        }
    return 37.7f;
}

float Engine::peakWiperFractionForKnob (float value) noexcept
{
    const float x = clamp (value, 0.0f, 100.0f);
    if (x <= 0.0f) return 0.0f;
    // R2 is a grounded 100 k pot feeding V3. Its electrical wiper voltage
    // cannot exceed the top voltage. This taper and the fixed top-level
    // calibration reproduce the Peak Reduction response targets;
    // the schematic does not specify its mechanical taper.
    const float calibratedDriveDb = 2.2f + 0.318f * x + 0.00216f * x * x;
    return clamp (dbToAmp (calibratedDriveDb) / maximumSidechainScale, 0.0f, 1.0f);
}

void Engine::prepareTubeTable() noexcept
{
    // Calibrated volts per full-scale at R1's wiper. This is an interface
    // calibration, not a claimed HA-100X turns ratio. The V2 12BH7A follower
    // is solved below; A-24 turns ratio/loading remain a measured output scale.
    constexpr double gridVoltsPerDigital = 0.005;
    const double firstIdlePlate = triodePlate (0.0, 1500.0);
    const double secondIdlePlate = triodePlate (0.0, 2700.0);
    const auto stage = [&] (double digital)
    {
        const double firstPlateAc = triodePlate (digital * gridVoltsPerDigital, 1500.0)
                                  - firstIdlePlate;
        return triodePlate (firstPlateAc, 2700.0) - secondIdlePlate;
    };
    const auto followerIdle = solveFollower (0.0, { 2.8, 100.0 }, 176.0, true);
    const double idleUpperPlate = 205.0 - 10.0 * followerIdle.upperMilliAmps;
    const double idleLowerMa = followerIdle.upperMilliAmps
        + (205.0 - followerIdle.outputVolts) / 100.0;
    const double idleUpperCathode = followerIdle.outputVolts + followerIdle.upperMilliAmps;
    const double upperResidual = followerIdle.upperMilliAmps
        - bh7CurrentMilliAmps (idleUpperPlate - idleUpperCathode,
                               followerIdle.outputVolts - idleUpperCathode);
    const double lowerResidual = idleLowerMa
        - bh7CurrentMilliAmps (followerIdle.outputVolts - idleLowerMa, -idleLowerMa);
    modelDiagnostics.followerIdleUpperMilliAmps = static_cast<float> (followerIdle.upperMilliAmps);
    modelDiagnostics.followerIdleOutputVolts = static_cast<float> (followerIdle.outputVolts);
    modelDiagnostics.followerIdleResidualMilliAmps = static_cast<float> (
        std::max (std::abs (upperResidual), std::abs (lowerResidual)));
    modelDiagnostics.followerTableConverged = followerIdle.converged;
    for (int index = 0; index < followerTableSize; ++index)
    {
        const double grid = (2.0 * index / (followerTableSize - 1) - 1.0) * followerTableExtent;
        const auto solution = solveFollower (grid, followerIdle, idleUpperPlate, false);
        modelDiagnostics.followerTableConverged &= solution.converged;
        followerTable[static_cast<size_t> (index)] = static_cast<float> (
            solution.outputVolts - followerIdle.outputVolts);
    }
    const auto wholeStage = [&] (double digital)
    {
        // The measured V2 supply is decoupled from the upstream 68 k R16.
        // C2 and C3 pass AC and R15/R19 set the two grid DC references.
        const double grid = std::clamp (stage (digital),
                                        -static_cast<double> (followerTableExtent),
                                        static_cast<double> (followerTableExtent));
        const double position = (grid / followerTableExtent + 1.0) * (followerTableSize - 1) * 0.5;
        const int index = std::min (followerTableSize - 2, static_cast<int> (position));
        const double fraction = position - index;
        return followerTable[static_cast<size_t> (index)] * (1.0 - fraction)
             + followerTable[static_cast<size_t> (index + 1)] * fraction;
    };
    const double smallSignalGain = (wholeStage (0.001) - wholeStage (-0.001)) / 0.002;
    tubeOutputScale = static_cast<float> (1.0 / smallSignalGain);
    for (int index = 0; index < tubeTableSize; ++index)
    {
        const double x = (2.0 * index / (tubeTableSize - 1) - 1.0) * tubeTableExtent;
        tubeTable[static_cast<size_t> (index)] = static_cast<float> (stage (x));
    }
    // The V3 grid voltage per digital input unit cannot be inferred from
    // the schematic alone. Its signed AC transfer uses 1 mV per normalized
    // grid-drive unit, then is calibrated to unit small-signal gain. The
    // V4/EL/T4 transfer after V3 remains fitted to the calibration target.
    constexpr double sidechainGridVoltsPerDrive = 0.001;
    const double sidechainIdle = sidechainTriodePlate (0.0);
    const double sidechainSlope = -(sidechainTriodePlate (sidechainGridVoltsPerDrive)
                                - sidechainTriodePlate (-sidechainGridVoltsPerDrive))
                                  / (2.0 * sidechainGridVoltsPerDrive);
    for (int index = 0; index < sidechainTableSize; ++index)
    {
        const double drive = (2.0 * index / (sidechainTableSize - 1) - 1.0)
                             * sidechainTableExtent;
        sidechainTable[static_cast<size_t> (index)] = static_cast<float> (
            (sidechainIdle - sidechainTriodePlate (drive * sidechainGridVoltsPerDrive))
             / (sidechainSlope * sidechainGridVoltsPerDrive));
    }
    modelDiagnostics.tubeTableFiniteAndMonotonic = std::isfinite (smallSignalGain)
        && smallSignalGain > 0.0;
    modelDiagnostics.tubeTableFiniteAndMonotonic &= isFiniteAndMonotonic (tubeTable);
    modelDiagnostics.tubeTableFiniteAndMonotonic &= isFiniteAndMonotonic (followerTable);
    modelDiagnostics.tubeTableFiniteAndMonotonic &= isFiniteAndMonotonic (sidechainTable);
}

float Engine::sidechainVoltageStage (float gridDrive) const noexcept
{
    return interpolateTable (sidechainTable, gridDrive, sidechainTableExtent);
}

float Engine::tubeAmplifier (Channel& channel, float x) noexcept
{
    const float firstStages = interpolateTable (tubeTable, x, tubeTableExtent);
    // C2 is physically between V1B's plate and V2A's grid.
    const float coupled = highpass (firstStages, channel.couplingHpX,
                                    channel.couplingHpY, couplingHpA);
    return interpolateTable (followerTable, coupled, followerTableExtent) * tubeOutputScale;
}

void Engine::prepare (double sampleRate) noexcept
{
    sampleRateHz = clamp (static_cast<float> (sampleRate), 8000.0f, 384000.0f);
    // The published unit is flat within ±0.1 dB over 30 Hz–15 kHz. Keep
    // surrogate coupling filters below the passband and transformer rolloff
    // sufficiently above it at digital sample rates.
    inputHpA = std::exp (-2.0f * pi * 1.0f / sampleRateHz);
    // C2=0.1 uF into R15=470 k: 3.39 Hz, between the V1 and V2 tables.
    // C1=.02 uF with R11/R12 joins the V1A plate/cathode to V1B's grid.
    // A coupled V1A plate/cathode and C1/R11/R12 prototype loses about 9 dB
    // at 30 Hz and differs from the calibrated passband target. The schematic
    // values alone do not explain that behavior, so C1 is not inserted
    // as an isolated pole into the calibrated audio path.
    couplingHpA = std::exp (-1.0f / (470000.0f * 0.0000001f * sampleRateHz));
    outputHpA = std::exp (-2.0f * pi * 1.0f / sampleRateHz);
    outputLpA = 0.005f;
    // C6/R30 shunts the stereo-parallel bus in UA fig. 6. It is not a
    // series audio feed to V3, so no detector high-pass is applied.
    // R37=1 M/C12=1 nF set a 159 Hz sidechain shelving time constant.
    sideLpA = std::exp (-1.0f / (1000000.0f * 0.000000001f * sampleRateHz));
    cathodeLpA = std::exp (-1.0f / (1000.0f * 0.00000003f * sampleRateHz));
    // These detector bandwidth terms are calibrated to level/frequency
    // response targets. The drawing does not specify enough transformer and lamp data to
    // assign them to one exact hardware component.
    detectorBassA = std::exp (-2.0f * pi * 250.0f / sampleRateHz);
    detectorHighA = std::exp (-2.0f * pi * 15000.0f / sampleRateHz);
    // V4/lamp drive builds more slowly than the illuminated T4 cell. This
    // 0.5 ms fitted rectifier smoothing is distinct from the cell's 10 us
    // prompt conductivity time below.
    detectorAttackA = coefficient (0.0005f, sampleRateHz);
    detectorReleaseA = coefficient (0.010f, sampleRateHz);
    // Published attack is approximately 10 us, shorter than one sample at
    // 48 kHz. Its sampled exponential reaches 87.5% in the first 20.8 us.
    attackA = coefficient (0.000010f, sampleRateHz);
    // A second T4 conductance population charges under sustained light.
    // Its calibrated charge time is separate from the published prompt attack.
    slowAttackA = coefficient (0.100f, sampleRateHz);
    fastReleaseA = coefficient (0.030f, sampleRateHz);
    exposureA = coefficient (2.0f, sampleRateHz);
    meterA = coefficient (0.300f, sampleRateHz); // VU ballistics
    parameterA = coefficient (0.010f, sampleRateHz);
    prepareTubeTable();
    reset();
}

void Engine::reset() noexcept
{
    channels = {};
    smoothedWiper = dbToAmp (gainDbForKnob (controls.gain)) / maximumAmplifierGain;
    smoothedPeak = controls.peakReduction;
    smoothedEmphasis = controls.emphasis;
    meterNeedle = 0.0f;
    reductionMeter.store (0.0f, std::memory_order_relaxed);
    for (int i = 0; i < 2; ++i)
    {
        inputMeterAtomic[i].store (-60.0f, std::memory_order_relaxed);
        outputMeterAtomic[i].store (-60.0f, std::memory_order_relaxed);
    }
    needleMeter.store (0.0f, std::memory_order_relaxed);
}

void Engine::setControls (Controls c) noexcept
{
    controls.peakReduction = clamp (c.peakReduction, 0.0f, 100.0f);
    controls.gain = clamp (c.gain, 0.0f, 100.0f);
    controls.emphasis = clamp (c.emphasis, 0.0f, 100.0f);
    controls.mode = c.mode;
    controls.meterMode = c.meterMode;
    controls.stereoLink = c.stereoLink;
    controls.bypass = c.bypass;
}

void Engine::updateCell (Channel& channel, float detector) noexcept
{
    // Feedback optical transfer: the knee begins late and light/cell
    // response steepens with post-cell level. The EL-to-T4 curve was fitted
    // to 800 Hz, 0.5 s calibration targets at Peak Reduction 25/50/75.
    const float above = std::max (0.0f, decibels (detector));
    const float amount = controls.mode == Mode::limit ? 1.4f : 0.44f;
    const float driveDb = smoothedPeak <= 0.001f ? 0.0f
                          : clamp (amount * above * above, 0.0f, 40.0f);
    const float target = opticalConductanceForDriveDb (driveDb);
    // Cell memory accumulates throughout illumination, including held tones.
    const float exposureTarget = clamp (driveDb / 20.0f, 0.0f, 1.0f);
    channel.exposure = exposureA * channel.exposure + (1.0f - exposureA) * exposureTarget;
    // The prompt population responds at the published approximately 10 us.
    // The CdS memory population charges over a fitted 100 ms. Giving both
    // populations the full target would make the total GR instantaneous;
    // separate 20/80% targets give the model a fast onset and slow build.
    const float fastTarget = 0.20f * target;
    const float slowTarget = 0.80f * target;
    if (fastTarget > channel.fastCellMilliSiemens)
        channel.fastCellMilliSiemens += (1.0f - attackA)
            * (fastTarget - channel.fastCellMilliSiemens);
    else
        channel.fastCellMilliSiemens *= fastReleaseA;

    if (slowTarget > channel.slowCellMilliSiemens)
        channel.slowCellMilliSiemens += (1.0f - slowAttackA)
            * (slowTarget - channel.slowCellMilliSiemens);
    else
    {
        // Calibrated memory-tail time varies from 0.3 to 1.8 s. This is the
        // late conductance population, not the manual's initial 60 ms release.
        const float slowTime = 0.3f + 1.5f * channel.exposure;
        // exp(-1/x) = 1 - 1/x to better than 1e-8 here; no transcendental
        // function in this varying per-sample release path.
        channel.slowCellMilliSiemens *= 1.0f - 1.0f / (slowTime * sampleRateHz);
    }

    channel.currentReduction = -decibels (opticalDivider (channel.fastCellMilliSiemens
                                                         + channel.slowCellMilliSiemens));
}

float Engine::tick (Channel& channel, float input, float linkedDetector) noexcept
{
    channel.inputRms = meterA * channel.inputRms + (1.0f - meterA) * input * input;
    float signal = highpass (input, channel.inputHpX, channel.inputHpY, inputHpA);

    const float cellGain = opticalDivider (channel.fastCellMilliSiemens
                                          + channel.slowCellMilliSiemens);
    const float afterCell = signal * cellGain;

    // Gain is downstream of the T4 and its detector tap.
    // R1 is a 100 k grounded potentiometer. Its audio wiper fraction follows
    // a fitted taper because its mechanical law is not marked on the drawing.
    signal = afterCell * (smoothedWiper * maximumAmplifierGain);
    signal = tubeAmplifier (channel, signal);
    signal = highpass (signal, channel.outputHpX, channel.outputHpY, outputHpA);
    channel.outputLp = outputLpA * channel.outputLp + (1.0f - outputLpA) * signal;
    signal = channel.outputLp;

    // The detector is driven from the gain-reduced path. The shelf is used only
    // by the sidechain, so R37 has no direct frequency effect on the audio.
    // R2 feeds V3 on the AC sidechain. C8 changes V3 cathode degeneration;
    // R37/C12 acts on its plate output before EL-drive rectification.
    const float v3Input = afterCell * maximumSidechainScale
                          * peakWiperFractionForKnob (smoothedPeak);
    channel.cathodeLp = cathodeLpA * channel.cathodeLp
                       + (1.0f - cathodeLpA) * v3Input;
    const float v3Drive = channel.cathodeLp
                          + 1.39f * (v3Input - channel.cathodeLp);
    const float v3Plate = sidechainVoltageStage (v3Drive);
    channel.sideLp = sideLpA * channel.sideLp + (1.0f - sideLpA) * v3Plate;
    const float high = v3Plate - channel.sideLp;
    const float shelf = dbToAmp (10.0f * smoothedEmphasis / 100.0f);
    const float r37Output = channel.sideLp + high * shelf;
    channel.bassLp = detectorBassA * channel.bassLp
                     + (1.0f - detectorBassA) * r37Output;
    const float detectorInput = r37Output + 0.70f * channel.bassLp;
    channel.detectorLp = detectorHighA * channel.detectorLp
                         + (1.0f - detectorHighA) * detectorInput;
    const float sideSignal = 1.05f * std::abs (channel.detectorLp);
    if (sideSignal > channel.sideDetector)
        channel.sideDetector = detectorAttackA * channel.sideDetector
                               + (1.0f - detectorAttackA) * sideSignal;
    else
        channel.sideDetector *= detectorReleaseA;
    updateCell (channel, std::max (channel.sideDetector, linkedDetector));

    const float delivered = controls.bypass ? input : signal;
    channel.outputRms = meterA * channel.outputRms + (1.0f - meterA) * delivered * delivered;
    return delivered;
}

void Engine::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || numSamples <= 0) return;

    const float targetWiper = dbToAmp (gainDbForKnob (controls.gain)) / maximumAmplifierGain;
    for (int n = 0; n < numSamples; ++n)
    {
        smoothedWiper = parameterA * smoothedWiper + (1.0f - parameterA) * targetWiper;
        smoothedPeak = parameterA * smoothedPeak + (1.0f - parameterA) * controls.peakReduction;
        smoothedEmphasis = parameterA * smoothedEmphasis + (1.0f - parameterA) * controls.emphasis;

        // Update both optical cells with the same sidechain in linked stereo.
        // Previous-sample detector state avoids inserting a block-sized delay.
        const float linked = (right != nullptr && controls.stereoLink)
                           ? std::max (channels[0].sideDetector, channels[1].sideDetector) : 0.0f;
        const float inL = left[n];
        const float inR = right != nullptr ? right[n] : 0.0f;
        left[n] = tick (channels[0], inL, linked);
        if (right != nullptr)
            right[n] = tick (channels[1], inR, linked);

        const float gr = std::max (channels[0].currentReduction,
                                  right != nullptr ? channels[1].currentReduction : 0.0f);
        const float outputRms = std::sqrt (std::max (channels[0].outputRms,
                                  right != nullptr ? channels[1].outputRms : 0.0f));
        // 0 VU at +4 dBu ≈ -18 dBFS, at +10 dBu ≈ -12 dBFS, by digital calibration.
        const float needleTarget = controls.meterMode == MeterMode::gainReduction ? -gr
             : decibels (outputRms) + (controls.meterMode == MeterMode::outputPlus4 ? 18.0f : 12.0f);
        meterNeedle = meterA * meterNeedle + (1.0f - meterA) * needleTarget;
    }

    const float gr = std::max (channels[0].currentReduction,
                              right != nullptr ? channels[1].currentReduction : 0.0f);
    reductionMeter.store (gr, std::memory_order_relaxed);
    needleMeter.store (meterNeedle, std::memory_order_relaxed);
    for (int i = 0; i < (right != nullptr ? 2 : 1); ++i)
    {
        inputMeterAtomic[i].store (decibels (std::sqrt (channels[i].inputRms)), std::memory_order_relaxed);
        outputMeterAtomic[i].store (decibels (std::sqrt (channels[i].outputRms)), std::memory_order_relaxed);
    }
}

Meters Engine::getMeters() const noexcept
{
    Meters result;
    for (int i = 0; i < 2; ++i)
    {
        result.inputDb[i] = inputMeterAtomic[i].load (std::memory_order_relaxed);
        result.outputDb[i] = outputMeterAtomic[i].load (std::memory_order_relaxed);
    }
    result.gainReductionDb = reductionMeter.load (std::memory_order_relaxed);
    result.needle = needleMeter.load (std::memory_order_relaxed);
    return result;
}
}
