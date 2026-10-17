#include "dsp/La2aEngine.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
constexpr float pi = 3.14159265358979323846f;

float renderTone (la2a::Engine& engine, float amplitude, float seconds,
                  float hz = 1000.0f, float sampleRate = 48000.0f)
{
    constexpr int block = 128;
    float phase = 0.0f;
    double energy = 0.0;
    int count = 0;
    const int total = static_cast<int> (seconds * sampleRate);
    std::vector<float> samples (block);
    for (int offset = 0; offset < total; offset += block)
    {
        const int n = std::min (block, total - offset);
        for (int i = 0; i < n; ++i)
        {
            samples[i] = amplitude * std::sin (phase);
            phase += 2.0f * pi * hz / sampleRate;
            if (phase > 2.0f * pi) phase -= 2.0f * pi;
        }
        engine.process (samples.data(), nullptr, n);
        if (offset > total / 2)
            for (int i = 0; i < n; ++i) { energy += samples[i] * samples[i]; ++count; }
    }
    return count > 0 ? std::sqrt (energy / count) : 0.0f;
}

float measureThd (la2a::Engine& engine, float peakAmplitude)
{
    constexpr int sampleRate = 48000;
    constexpr int blockSize = 128;
    std::vector<float> audio (sampleRate);
    std::vector<float> block (blockSize);
    for (int offset = 0; offset < sampleRate * 2; offset += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
            block[i] = peakAmplitude * std::sin (2.0 * pi * 1000.0 * (offset + i) / sampleRate);
        engine.process (block.data(), nullptr, blockSize);
        if (offset >= sampleRate)
            std::copy (block.begin(), block.end(), audio.begin() + offset - sampleRate);
    }

    double fundamental = 0.0, harmonics = 0.0;
    for (int harmonic = 1; harmonic <= 8; ++harmonic)
    {
        double real = 0.0, imaginary = 0.0;
        for (int n = 0; n < sampleRate; ++n)
        {
            const double angle = 2.0 * pi * 1000.0 * harmonic * n / sampleRate;
            real += audio[n] * std::cos (angle);
            imaginary -= audio[n] * std::sin (angle);
        }
        const double power = real * real + imaginary * imaginary;
        if (harmonic == 1) fundamental = power;
        else harmonics += power;
    }
    return static_cast<float> (std::sqrt (harmonics / fundamental));
}

float probeToneGainDb (la2a::Engine& engine, float peakKnob, float gainKnob,
                       float inputPeak, float hz = 800.0f)
{
    constexpr int sampleRate = 48000;
    constexpr int total = sampleRate / 2;
    constexpr int blockSize = 128;
    la2a::Controls controls;
    controls.peakReduction = peakKnob;
    controls.gain = gainKnob;
    engine.setControls (controls);
    engine.reset();
    std::vector<float> block (blockSize);
    double real = 0.0, imaginary = 0.0;
    for (int offset = 0; offset < total; offset += blockSize)
    {
        const int count = std::min (blockSize, total - offset);
        for (int i = 0; i < count; ++i)
            block[i] = inputPeak * std::sin (2.0 * pi * hz * (offset + i) / sampleRate);
        engine.process (block.data(), nullptr, count);
        for (int i = 0; i < count; ++i)
            if (offset + i >= total / 2)
            {
                const double angle = 2.0 * pi * hz * (offset + i) / sampleRate;
                real += block[i] * std::cos (angle);
                imaginary -= block[i] * std::sin (angle);
            }
    }
    const double outputPeak = 2.0 * std::hypot (real, imaginary) / (total / 2);
    return 20.0f * std::log10 (outputPeak / inputPeak);
}

std::array<float, 4> probeBurstReduction (float inputPeak)
{
    // 800 Hz bursts span 0.5–1.5 s and 2.0–2.5 s, with a 0.5 s silent gap.
    // The last window measures optical recovery and re-attack.
    constexpr int sampleRate = 48000;
    constexpr int blockSize = 128;
    constexpr std::array<int, 4> starts { 24240, 28800, 48000, 96240 };
    constexpr int window = 480;
    constexpr int total = starts[3] + window;
    la2a::Engine off, on;
    off.prepare (sampleRate);
    on.prepare (sampleRate);
    la2a::Controls controls;
    controls.gain = 25.0f;
    controls.peakReduction = 0.0f;
    off.setControls (controls);
    controls.peakReduction = 50.0f;
    on.setControls (controls);
    off.reset();
    on.reset();
    std::array<double, 4> offEnergy {}, onEnergy {};
    std::vector<float> offBlock (blockSize), onBlock (blockSize);
    for (int offset = 0; offset < total; offset += blockSize)
    {
        const int count = std::min (blockSize, total - offset);
        for (int i = 0; i < count; ++i)
        {
            const int sample = offset + i;
            const float input = sample < sampleRate / 2
                || (sample >= sampleRate * 3 / 2 && sample < sampleRate * 2) ? 0.0f
                : inputPeak * std::sin (2.0 * pi * 800.0 * (sample - sampleRate / 2)
                                         / sampleRate);
            offBlock[i] = onBlock[i] = input;
        }
        off.process (offBlock.data(), nullptr, count);
        on.process (onBlock.data(), nullptr, count);
        for (int i = 0; i < count; ++i)
        {
            const int sample = offset + i;
            for (int j = 0; j < 4; ++j)
                if (sample >= starts[j] && sample < starts[j] + window)
                {
                    offEnergy[j] += offBlock[i] * offBlock[i];
                    onEnergy[j] += onBlock[i] * onBlock[i];
                }
        }
    }
    std::array<float, 4> reduction {};
    for (int j = 0; j < 4; ++j)
        reduction[j] = 10.0f * std::log10 (offEnergy[j] / onEnergy[j]);
    return reduction;
}

struct Harmonics { float fundamentalPeak, secondDb, thirdDb; };

Harmonics probe220Harmonics (la2a::Engine& engine, float gainKnob, float inputPeak)
{
    constexpr int sampleRate = 48000;
    constexpr int total = 28800; // 0.6 s tone; analyze the last half
    constexpr int blockSize = 128;
    la2a::Controls controls;
    controls.peakReduction = 0.0f;
    controls.gain = gainKnob;
    engine.setControls (controls);
    engine.reset();
    std::vector<float> block (blockSize);
    double real[3] {}, imaginary[3] {};
    for (int offset = 0; offset < total; offset += blockSize)
    {
        const int count = std::min (blockSize, total - offset);
        for (int i = 0; i < count; ++i)
            block[i] = inputPeak * std::sin (2.0 * pi * 220.0 * (offset + i) / sampleRate);
        engine.process (block.data(), nullptr, count);
        for (int i = 0; i < count; ++i)
            if (offset + i >= total / 2)
                for (int h = 1; h <= 3; ++h)
                {
                    const double angle = 2.0 * pi * 220.0 * h * (offset + i) / sampleRate;
                    real[h - 1] += block[i] * std::cos (angle);
                    imaginary[h - 1] -= block[i] * std::sin (angle);
                }
    }
    const double fundamental = 2.0 * std::hypot (real[0], imaginary[0]) / (total / 2);
    const double second = 2.0 * std::hypot (real[1], imaginary[1]) / (total / 2);
    const double third = 2.0 * std::hypot (real[2], imaginary[2]) / (total / 2);
    return { static_cast<float> (fundamental),
             static_cast<float> (20.0 * std::log10 (second / fundamental)),
             static_cast<float> (20.0 * std::log10 (third / fundamental)) };
}

void require (bool condition, const char* message)
{
    if (! condition) { std::cerr << "FAIL: " << message << '\n'; std::abort(); }
}
}

int main()
{
    // Independent analytic values for the 1966 R6=68k, R7=2.7k,
    // R1=100k, R8=2.2M and grounded T4A audio photocell divider.
    require (std::abs (la2a::Engine::attenuationForCellOhms (1.0e30f) - 1.0f) < 1.0e-6f,
             "dark T4 cell should have unity relative divider voltage");
    require (std::abs (la2a::Engine::attenuationForCellOhms (100000.0f) - 0.710973f) < 0.0001f,
             "100k illuminated T4 cell should load the input divider");
    require (std::abs (la2a::Engine::attenuationForCellOhms (10000.0f) - 0.197424f) < 0.0001f,
             "10k illuminated T4 cell should deliver roughly 14.1 dB attenuation");

    la2a::Engine engine;
    engine.prepare (48000.0);
    const auto circuit = engine.getModelDiagnostics();
    std::cout << "V2 12BH7 idle: upper " << circuit.followerIdleUpperMilliAmps
              << " mA, output node " << circuit.followerIdleOutputVolts
              << " V, residual " << circuit.followerIdleResidualMilliAmps << " mA\n";
    require (circuit.followerIdleUpperMilliAmps > 0.1f
             && circuit.followerIdleUpperMilliAmps < 10.0f
             && circuit.followerIdleOutputVolts > 10.0f
             && circuit.followerIdleOutputVolts < 190.0f
             && circuit.followerIdleResidualMilliAmps < 0.0001f,
             "12BH7 follower DC solution should satisfy its coupled load line");
    require (circuit.followerTableConverged && circuit.tubeTableFiniteAndMonotonic,
             "precomputed tube tables should converge, stay finite and be monotonic");
    la2a::Controls c;
    c.peakReduction = 0.0f;
    c.gain = 20.0f;
    engine.setControls (c);
    const float low = renderTone (engine, 0.01f, 0.5f);
    require (low > 0.006f && low < 0.008f, "low level should stay approximately unity");
    require (engine.getGainReductionDb() < 0.1f, "zero peak reduction should not compress a quiet signal");

    engine.reset();
    renderTone (engine, 0.8f, 1.0f);
    require (engine.getGainReductionDb() < 0.01f,
             "full counterclockwise Peak Reduction is off even at high input level");

    engine.reset();
    const float response30 = renderTone (engine, 0.05f, 2.0f, 30.0f);
    engine.reset();
    const float response1000 = renderTone (engine, 0.05f, 2.0f, 1000.0f);
    engine.reset();
    const float response15000 = renderTone (engine, 0.05f, 2.0f, 15000.0f);
    const float lowError = 20.0f * std::log10 (response30 / response1000);
    const float highError = 20.0f * std::log10 (response15000 / response1000);
    std::cout << "Response vs 1 kHz: 30 Hz " << lowError << " dB, 15 kHz " << highError << " dB\n";
    require (std::abs (lowError) < 0.1f && std::abs (highError) < 0.1f,
             "end-to-end response should meet published +/-0.1 dB passband");

    engine.reset();
    const float thd = measureThd (engine, 0.35f);
    std::cout << "1 kHz THD at approximately +10 dBm / -12 dBFS RMS: " << thd * 100.0f << "%\n";
    require (thd < 0.0035f, "moderate-level tube surrogate THD should stay below 0.35%");

    engine.reset();
    c.peakReduction = 70.0f;
    engine.setControls (c);
    const float compressed = renderTone (engine, 0.2f, 1.0f);
    const float reduction = engine.getGainReductionDb();
    require (reduction > 5.0f && reduction < 40.0f, "peak reduction drives optical cell");
    require (compressed < 0.09f, "cell actually attenuates audio");

    engine.reset();
    c.peakReduction = 70.0f;
    c.gain = 20.0f;
    engine.setControls (c);
    const float baseOutput = renderTone (engine, 0.1f, 1.0f);
    const float baseReduction = engine.getGainReductionDb();
    c.gain = 80.0f;
    engine.setControls (c);
    const float louderOutput = renderTone (engine, 0.1f, 0.5f);
    require (louderOutput > baseOutput * 1.2f, "output gain raises level");
    require (std::abs (engine.getGainReductionDb() - baseReduction) < 1.5f,
             "output gain does not drive sidechain");

    engine.reset();
    c.gain = 20.0f;
    c.mode = la2a::Mode::compress;
    engine.setControls (c);
    renderTone (engine, 0.15f, 1.0f);
    const float compressReduction = engine.getGainReductionDb();
    engine.reset();
    c.mode = la2a::Mode::limit;
    engine.setControls (c);
    renderTone (engine, 0.15f, 1.0f);
    require (engine.getGainReductionDb() > compressReduction + 2.0f,
             "limit mode should have steeper reduction");

    auto outputRiseDb = [&] (la2a::Mode mode)
    {
        c.mode = mode;
        c.peakReduction = 70.0f;
        c.gain = 20.0f;
        engine.setControls (c);
        engine.reset();
        const float quiet = renderTone (engine, 0.02f, 2.0f);
        engine.reset();
        const float loud = renderTone (engine, 0.12f, 2.0f);
        return 20.0f * std::log10 (loud / quiet);
    };
    const float compressRise = outputRiseDb (la2a::Mode::compress);
    const float limitRise = outputRiseDb (la2a::Mode::limit);
    std::cout << "15.56 dB input rise: Compress " << compressRise
              << " dB, Limit " << limitRise << " dB output rise\n";
    require (compressRise > 3.0f && compressRise < 8.0f,
             "Compress curve should approximate 3:1 in its active region");
    require (limitRise < 3.0f && limitRise < compressRise * 0.6f,
             "Limit curve should approach a flat output response");
    require (std::abs (la2a::Engine::gainDbForKnob (20.0f) + 0.1f) < 0.01f
             && std::abs (la2a::Engine::gainDbForKnob (100.0f) - 37.7f) < 0.01f,
             "Gain pot follows measured low-level range");
    require (la2a::Engine::peakWiperFractionForKnob (0.0f) == 0.0f
             && std::abs (la2a::Engine::peakWiperFractionForKnob (100.0f) - 1.0f) < 0.0001f
             && la2a::Engine::peakWiperFractionForKnob (25.0f)
                < la2a::Engine::peakWiperFractionForKnob (50.0f)
             && la2a::Engine::peakWiperFractionForKnob (50.0f)
                < la2a::Engine::peakWiperFractionForKnob (75.0f),
             "R2 wiper is bounded, monotonic and off at its lower stop");

    // Regression targets cover calibrated model behavior and are not
    // physical-unit specifications. The T4 has program memory.
    struct ToneCase { float peakKnob, gainKnob, inputPeak, expectedGain; };
    const ToneCase toneCases[] {
        {0, 5, .001f, -22.6f}, {0, 10, .001f, -9.6f},
        {0, 20, .001f, -0.1f}, {0, 25, .001f, 2.7f},
        {0, 50, .001f, 14.5f}, {0, 75, .001f, 28.6f},
        {0, 100, .001f, 37.7f},
        {25, 25, .1f, 2.7f}, {25, 25, .3f, 2.2f}, {25, 25, .5f, -.1f},
        {50, 25, .01f, 2.7f}, {50, 25, .03f, 2.7f},
        {50, 25, .1f, 1.1f}, {50, 25, .2f, -3.0f},
        {50, 25, .3f, -5.8f}, {50, 25, .5f, -9.5f},
        {75, 25, .01f, 2.7f}, {75, 25, .03f, -1.7f},
        {75, 25, .1f, -10.1f}
    };
    float maxError = 0.0f;
    for (const auto& probe : toneCases)
    {
        const float modeled = probeToneGainDb (engine, probe.peakKnob, probe.gainKnob, probe.inputPeak);
        const float error = modeled - probe.expectedGain;
        maxError = std::max (maxError, std::abs (error));
        std::cout << "Probe P" << probe.peakKnob << " G" << probe.gainKnob
                  << " in " << probe.inputPeak << ": model " << modeled
                  << " dB, expected " << probe.expectedGain << " dB, difference " << error << " dB\n";
    }
    require (maxError < 0.5f, "calibration operating points should stay within 0.5 dB of their targets");

    // 48 kHz frequency-response calibration cases at P50/G25 use 0.5 s tones
    // and analyze the final 0.25 s. These exercise the detector's response;
    // P0 audio-path response is checked separately above.
    struct FrequencyProbe { float inputPeak, frequencyHz, expectedGain; };
    constexpr std::array<FrequencyProbe, 10> frequencyProbes {{
        {.1f, 50.0f, -2.1f}, {.1f, 200.0f, -1.6f},
        {.1f, 800.0f, 1.1f}, {.1f, 5000.0f, .9f}, {.1f, 16000.0f, 2.1f},
        {.3f, 50.0f, -9.1f}, {.3f, 200.0f, -9.0f},
        {.3f, 800.0f, -5.8f}, {.3f, 5000.0f, -6.5f}, {.3f, 16000.0f, -5.0f}
    }};
    float maximumFrequencyError = 0.0f;
    for (const auto& probe : frequencyProbes)
    {
        const float modeled = probeToneGainDb (engine, 50.0f, 25.0f,
                                                probe.inputPeak, probe.frequencyHz);
        maximumFrequencyError = std::max (maximumFrequencyError,
                                          std::abs (modeled - probe.expectedGain));
    }
    std::cout << "P50 frequency-gain calibration maximum difference " << maximumFrequencyError << " dB\n";
    require (maximumFrequencyError < 1.25f,
             "detector frequency response should stay within its calibration envelope");

    const auto tone = probe220Harmonics (engine, 50.0f, 0.05f);
    std::cout << "220 Hz calibration case: output peak " << tone.fundamentalPeak
              << ", H2 " << tone.secondDb << " dBc, H3 " << tone.thirdDb << " dBc\n";
    require (std::abs (tone.fundamentalPeak - 0.266f) < 0.01f,
             "high-Gain output level stays within its calibration target");
    require (std::abs (tone.secondDb + 58.0f) < 3.0f && tone.thirdDb < -70.0f,
             "high-Gain low-order harmonics stay within their calibration envelope");

    engine.reset();
    c.mode = la2a::Mode::compress;
    engine.setControls (c);
    renderTone (engine, 0.3f, 3.0f);
    const float held = engine.getGainReductionDb();
    std::vector<float> silence (4800, 0.0f);
    engine.process (silence.data(), nullptr, static_cast<int> (silence.size()));
    const float at100ms = engine.getGainReductionDb();
    require (at100ms < held && at100ms > 1.0f, "initial release is fast but incomplete");
    for (int i = 0; i < 50; ++i)
        engine.process (silence.data(), nullptr, static_cast<int> (silence.size()));
    require (engine.getGainReductionDb() < at100ms,
             "slow optical population eventually releases");

    auto releaseFraction = [&] (float holdSeconds)
    {
        engine.reset();
        renderTone (engine, 0.3f, holdSeconds);
        const float before = engine.getGainReductionDb();
        for (int i = 0; i < 10; ++i)
            engine.process (silence.data(), nullptr, static_cast<int> (silence.size()));
        return engine.getGainReductionDb() / before;
    };
    const float shortTail = releaseFraction (0.2f);
    const float longTail = releaseFraction (4.0f);
    std::cout << "Optical memory: short " << shortTail << ", long " << longTail << "\n";
    require (longTail > shortTail + 0.025f,
             "long illumination must leave a slower relative T4 tail than a short burst");

    // 1966 manual: about 10 us T4 attack, and roughly half of the initial
    // resistance recovery in 40–80 ms. The upstream EL drive has a fitted
    // 0.5 ms charge time, so test the full chain over 0.5 ms rather than
    // expecting a single digital sample to reach high optical reduction.
    engine.reset();
    c.peakReduction = 70.0f;
    c.gain = 20.0f;
    engine.setControls (c);
    std::array<float, 24> firstStep {};
    for (int sample = 0; sample < static_cast<int> (firstStep.size()); ++sample)
        firstStep[sample] = 0.3f * std::sin (2.0f * pi * 800.0f * sample / 48000.0f);
    engine.process (firstStep.data(), nullptr, static_cast<int> (firstStep.size()));
    const float firstHalfMillisecond = engine.getGainReductionDb();
    require (firstHalfMillisecond > 8.0f,
             "fast T4 population should react within the first 0.5 ms");
    engine.reset();
    renderTone (engine, 0.3f, 0.01f, 800.0f);
    const float before60ms = engine.getGainReductionDb();
    std::vector<float> releaseBlock (128, 0.0f);
    for (int sample = 0; sample < 2880; sample += 128)
        engine.process (releaseBlock.data(), nullptr, 128);
    const float releaseRatio = engine.getGainReductionDb() / before60ms;
    std::cout << "Optical transient: 0.5 ms GR " << firstHalfMillisecond << " dB, "
              << "60 ms GR fraction " << releaseRatio << "\n";
    require (releaseRatio > 0.4f && releaseRatio < 0.6f,
             "short-burst optical release should lose roughly half GR in 60 ms");

    // Expected transient calibration targets use 48 kHz, Peak 50, Gain 25,
    // 800 Hz bursts, and 10 ms RMS windows. Target reductions at 5/100/500 ms
    // are 0.93/1.38/1.61 dB at 0.1 peak and 7.27/8.16/8.62 dB at 0.3 peak.
    // After 0.5 s silence, target re-attack at 2.005 s is 1.15/7.58 dB.
    const auto lowBurst = probeBurstReduction (0.1f);
    const auto highBurst = probeBurstReduction (0.3f);
    constexpr std::array<float, 3> lowExpected { 0.93f, 1.38f, 1.61f };
    constexpr std::array<float, 3> highExpected { 7.27f, 8.16f, 8.62f };
    for (int i = 0; i < 3; ++i)
    {
        require (std::abs (lowBurst[i] - lowExpected[i]) < 0.4f,
                 "low-level T4 onset should stay within its calibration target");
        require (std::abs (highBurst[i] - highExpected[i]) < 0.5f,
                 "high-level T4 onset should stay within its calibration target");
    }
    require (std::abs (lowBurst[3] - 1.15f) < 0.35f
             && std::abs (highBurst[3] - 7.58f) < 0.5f,
             "optical tail and re-attack should stay within their calibration targets");
    std::cout << "Transient calibration low " << lowBurst[0] << "/" << lowBurst[1] << "/"
              << lowBurst[2] << " dB, re-attack " << lowBurst[3] << " dB; high "
              << highBurst[0] << "/" << highBurst[1] << "/" << highBurst[2]
              << " dB, re-attack " << highBurst[3] << " dB\n";

    engine.reset();
    c.peakReduction = 80.0f;
    c.emphasis = 0.0f;
    engine.setControls (c);
    renderTone (engine, 0.13f, 1.0f, 8000.0f);
    const float flatHigh = engine.getGainReductionDb();
    engine.reset();
    c.emphasis = 100.0f;
    engine.setControls (c);
    renderTone (engine, 0.13f, 1.0f, 8000.0f);
    require (engine.getGainReductionDb() > flatHigh + 2.0f,
             "R37 makes high-frequency sidechain more sensitive");

    auto quietRightRms = [&] (bool linked)
    {
        engine.reset();
        c.peakReduction = 80.0f;
        c.emphasis = 0.0f;
        c.stereoLink = linked;
        engine.setControls (c);
        std::vector<float> left (128), right (128);
        double power = 0.0;
        int count = 0;
        for (int offset = 0; offset < 48000; offset += 128)
        {
            for (int i = 0; i < 128; ++i)
            {
                const float wave = std::sin (2.0f * pi * 1000.0f * (offset + i) / 48000.0f);
                left[i] = 0.3f * wave;
                right[i] = 0.01f * wave;
            }
            engine.process (left.data(), right.data(), 128);
            if (offset > 24000)
                for (const float sample : right) { power += sample * sample; ++count; }
        }
        return std::sqrt (power / count);
    };
    const float linkedRight = quietRightRms (true);
    const float unlinkedRight = quietRightRms (false);
    require (linkedRight < unlinkedRight * 0.65f,
             "stereo link must let the loud channel control quiet channel attenuation");

    engine.reset();
    c.bypass = true;
    engine.setControls (c);
    float impulse[3] { 0.5f, -0.25f, 0.0f };
    engine.process (impulse, nullptr, 3);
    require (impulse[0] == 0.5f && impulse[1] == -0.25f && impulse[2] == 0.0f,
             "bypass must return bit-identical samples");
    c.gain = 75.0f;
    engine.setControls (c);
    renderTone (engine, 0.05f, 1.0f);
    const auto bypassMeters = engine.getMeters();
    require (std::abs (bypassMeters.outputDb[0] - bypassMeters.inputDb[0]) < 0.01f,
             "bypass output meter should describe delivered audio");

    engine.reset();
    c.bypass = false;
    c.peakReduction = 0.0f;
    c.gain = 100.0f;
    engine.setControls (c);
    float loudImpulse[3] { 0.2f, 0.0f, 0.0f };
    engine.process (loudImpulse, nullptr, 3);
    require (std::isfinite (loudImpulse[0]) && loudImpulse[0] > 0.0f,
             "extreme input and gain must retain polarity and finite output");

    std::cout << "LA-2A DSP tests passed\n";
}
