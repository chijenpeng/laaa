#include "dsp/La2aEngine.h"

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>

int main()
{
    constexpr int sampleRate = 48000;
    constexpr int blockSize = 256;
    constexpr int blocks = sampleRate * 30 / blockSize;
    constexpr float pi = 3.14159265358979323846f;
    la2a::Engine engine;
    engine.prepare (sampleRate);
    la2a::Controls controls;
    controls.peakReduction = 65.0f;
    engine.setControls (controls);
    std::array<float, blockSize> left {}, right {};
    float phase = 0.0f;

    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < blocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            left[i] = 0.18f * std::sin (phase);
            right[i] = 0.13f * std::sin (phase * 1.011f);
            phase += 2.0f * pi * 997.0f / sampleRate;
            if (phase >= 2.0f * pi) phase -= 2.0f * pi;
        }
        engine.process (left.data(), right.data(), blockSize);
    }
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    std::cout << "Stereo 48 kHz, 30 s audio: " << elapsed << " s wall, "
              << 100.0 * elapsed / 30.0 << "% of one real-time core\n";
}
