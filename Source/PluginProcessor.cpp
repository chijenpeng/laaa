#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout La2aProcessor::createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Bool = juce::AudioParameterBool;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<Float> (juce::ParameterID { ParamID::peakReduction, 1 },
                                        "Peak Reduction", juce::NormalisableRange<float> (0.0f, 100.0f), 35.0f));
    layout.add (std::make_unique<Float> (juce::ParameterID { ParamID::gain, 1 },
                                        "Gain", juce::NormalisableRange<float> (0.0f, 100.0f), 50.0f));
    layout.add (std::make_unique<Choice> (juce::ParameterID { ParamID::compressLimit, 1 },
                                         "Compress / Limit", juce::StringArray { "Compress", "Limit" }, 0));
    layout.add (std::make_unique<Choice> (juce::ParameterID { ParamID::meter, 1 },
                                         "Meter", juce::StringArray { "Gain Reduction", "Output +4", "Output +10" }, 0));
    layout.add (std::make_unique<Float> (juce::ParameterID { ParamID::emphasis, 1 },
                                        "R37 Emphasis", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f));
    layout.add (std::make_unique<Bool> (juce::ParameterID { ParamID::stereoLink, 1 }, "Stereo Link", true));
    layout.add (std::make_unique<Bool> (juce::ParameterID { ParamID::bypass, 1 }, "Bypass", false));
    return layout;
}

La2aProcessor::La2aProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "La2a", createLayout()),
      peakReduction (apvts.getRawParameterValue (ParamID::peakReduction)),
      gain (apvts.getRawParameterValue (ParamID::gain)),
      compressLimit (apvts.getRawParameterValue (ParamID::compressLimit)),
      meter (apvts.getRawParameterValue (ParamID::meter)),
      emphasis (apvts.getRawParameterValue (ParamID::emphasis)),
      stereoLink (apvts.getRawParameterValue (ParamID::stereoLink)),
      bypass (apvts.getRawParameterValue (ParamID::bypass))
{
}

void La2aProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    setLatencySamples (0);
}

bool La2aProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto output = layout.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
           && output == layout.getMainInputChannelSet();
}

void La2aProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int count = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, count);

    la2a::Controls c;
    c.peakReduction = peakReduction->load (std::memory_order_relaxed);
    c.gain = gain->load (std::memory_order_relaxed);
    c.mode = compressLimit->load (std::memory_order_relaxed) >= 0.5f ? la2a::Mode::limit : la2a::Mode::compress;
    c.meterMode = static_cast<la2a::MeterMode> (juce::jlimit (0, 2, juce::roundToInt (meter->load (std::memory_order_relaxed))));
    c.emphasis = emphasis->load (std::memory_order_relaxed);
    c.stereoLink = stereoLink->load (std::memory_order_relaxed) >= 0.5f;
    c.bypass = bypass->load (std::memory_order_relaxed) >= 0.5f;
    engine.setControls (c);
    engine.process (buffer.getWritePointer (0), getTotalNumInputChannels() > 1 ? buffer.getWritePointer (1) : nullptr, count);
}

void La2aProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, data);
}

void La2aProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* La2aProcessor::createEditor()
{
    return new La2aEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new La2aProcessor();
}
