#pragma once

#include <array>
#include <atomic>

namespace la2a
{
enum class Mode { compress = 0, limit = 1 };
enum class MeterMode { gainReduction = 0, outputPlus4 = 1, outputPlus10 = 2 };

struct Controls
{
    float peakReduction = 35.0f; // hardware-style 0..100 rotation
    float gain = 50.0f;          // 0..100, approximately unity near 20 in the gain calibration
    float emphasis = 0.0f;      // 0..100; 0 is the flat factory sidechain setting
    Mode mode = Mode::compress;
    MeterMode meterMode = MeterMode::gainReduction;
    bool stereoLink = true;
    bool bypass = false;
};

struct Meters
{
    std::array<float, 2> inputDb { -60.0f, -60.0f };
    std::array<float, 2> outputDb { -60.0f, -60.0f };
    float gainReductionDb = 0.0f;
    float needle = 0.0f; // meter dB: negative GR, or output dB relative to +4/+10 dBu
};

struct ModelDiagnostics
{
    float followerIdleUpperMilliAmps = 0.0f;
    float followerIdleOutputVolts = 0.0f;
    float followerIdleResidualMilliAmps = 0.0f;
    bool followerTableConverged = false;
    bool tubeTableFiniteAndMonotonic = false;
};

// A sample-domain, allocation-free model of one stereo-linked pair of LA-2A units.
// The input divider and V1 load lines use schematic values. Optical conductivity,
// transformer windings, and tube-curve coefficients still need calibration.
class Engine
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setControls (Controls) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;
    Meters getMeters() const noexcept;
    ModelDiagnostics getModelDiagnostics() const noexcept { return modelDiagnostics; }

    float getGainReductionDb() const noexcept { return reductionMeter.load (std::memory_order_relaxed); }
    static float gainDbForKnob (float value) noexcept;
    static float peakWiperFractionForKnob (float value) noexcept;
    static float attenuationForCellOhms (float cellOhms) noexcept;

private:
    struct Channel
    {
        float inputHpX = 0.0f, inputHpY = 0.0f;
        float couplingHpX = 0.0f, couplingHpY = 0.0f;
        float outputHpX = 0.0f, outputHpY = 0.0f;
        float outputLp = 0.0f;
        float sideLp = 0.0f, bassLp = 0.0f, detectorLp = 0.0f;
        float cathodeLp = 0.0f;
        float sideDetector = 0.0f;
        float fastCellMilliSiemens = 0.0f, slowCellMilliSiemens = 0.0f, exposure = 0.0f;
        float currentReduction = 0.0f;
        float inputRms = 0.0f, outputRms = 0.0f;
    };

    static float decibels (float amplitude) noexcept;
    float tubeAmplifier (Channel&, float x) noexcept;
    float sidechainVoltageStage (float gridDrive) const noexcept;
    void prepareTubeTable() noexcept;
    float tick (Channel&, float input, float linkedDetector) noexcept;
    void updateCell (Channel&, float detector) noexcept;

    std::array<Channel, 2> channels {};
    Controls controls;
    float sampleRateHz = 48000.0f;
    float inputHpA = 0.0f, couplingHpA = 0.0f, outputHpA = 0.0f, outputLpA = 0.0f;
    float sideLpA = 0.0f, cathodeLpA = 0.0f;
    float detectorBassA = 0.0f, detectorHighA = 0.0f;
    float detectorAttackA = 0.0f, detectorReleaseA = 0.0f;
    float attackA = 0.0f, slowAttackA = 0.0f;
    float fastReleaseA = 0.0f, exposureA = 0.0f;
    float meterA = 0.0f, parameterA = 0.0f;
    float smoothedWiper = 1.0f, smoothedPeak = 35.0f, smoothedEmphasis = 0.0f;
    float meterNeedle = 0.0f;
    static constexpr int tubeTableSize = 8193;
    static constexpr float tubeTableExtent = 32.0f;
    static constexpr int followerTableSize = 1025;
    static constexpr float followerTableExtent = 30.0f;
    static constexpr int sidechainTableSize = 4097;
    static constexpr float sidechainTableExtent = 128.0f;
    std::array<float, tubeTableSize> tubeTable {};
    std::array<float, followerTableSize> followerTable {};
    std::array<float, sidechainTableSize> sidechainTable {};
    float tubeOutputScale = 1.0f;
    ModelDiagnostics modelDiagnostics;
    std::atomic<float> reductionMeter { 0.0f };
    std::array<std::atomic<float>, 2> inputMeterAtomic {{ -60.0f, -60.0f }};
    std::array<std::atomic<float>, 2> outputMeterAtomic {{ -60.0f, -60.0f }};
    std::atomic<float> needleMeter { 0.0f };
};
}
