#pragma once

#include "dsp/La2aEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace ParamID
{
inline constexpr auto peakReduction = "peakReduction";
inline constexpr auto gain = "gain";
inline constexpr auto compressLimit = "compressLimit";
inline constexpr auto meter = "meter";
inline constexpr auto emphasis = "emphasis";
inline constexpr auto stereoLink = "stereoLink";
inline constexpr auto bypass = "bypass";
}

class La2aProcessor final : public juce::AudioProcessor
{
public:
    La2aProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    la2a::Meters getMeters() const noexcept { return engine.getMeters(); }
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    la2a::Engine engine;
    std::atomic<float>* const peakReduction;
    std::atomic<float>* const gain;
    std::atomic<float>* const compressLimit;
    std::atomic<float>* const meter;
    std::atomic<float>* const emphasis;
    std::atomic<float>* const stereoLink;
    std::atomic<float>* const bypass;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (La2aProcessor)
};
