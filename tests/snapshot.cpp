#include "../Source/PluginEditor.h"

#include <cmath>
#include <cstdio>

namespace
{
void set (La2aProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.apvts.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

bool near (float a, float b) { return std::abs (a - b) < 0.001f; }

juce::Component* namedChild (juce::Component& root, const juce::String& name)
{
    if (root.getName() == name) return &root;
    for (auto* child : root.getChildren())
        if (auto* found = namedChild (*child, name)) return found;
    return nullptr;
}
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    La2aProcessor processor;
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    processor.setPlayConfigDetails (2, 2, rate, block);
    processor.prepareToPlay (rate, block);
    set (processor, ParamID::peakReduction, 47.0f);
    set (processor, ParamID::gain, 53.0f);
    set (processor, ParamID::meter, 0.0f);
    set (processor, ParamID::compressLimit, 0.0f);
    juce::AudioBuffer<float> audio (2, block);
    juce::MidiBuffer midi;
    double phase = 0.0;
    for (int iteration = 0; iteration < 160; ++iteration)
    {
        for (int sample = 0; sample < block; ++sample)
        {
            const float v = 0.16f * std::sin ((float) phase);
            phase += juce::MathConstants<double>::twoPi * 1000.0 / rate;
            audio.setSample (0, sample, v);
            audio.setSample (1, sample, v);
        }
        processor.processBlock (audio, midi);
    }
    La2aEditor editor (processor);
    editor.updateMeters();
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "docs");
    directory.createDirectory();
    const auto output = directory.getChildFile ("editor.png");
    output.deleteFile();
    juce::FileOutputStream stream (output);
    const bool wrote = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
    stream.flush();
    set (processor, ParamID::meter, 1.0f);
    set (processor, ParamID::compressLimit, 1.0f);
    for (int iteration = 0; iteration < 30; ++iteration)
    {
        for (int sample = 0; sample < block; ++sample)
        {
            const float v = 0.16f * std::sin ((float) phase);
            phase += juce::MathConstants<double>::twoPi * 1000.0 / rate;
            audio.setSample (0, sample, v);
            audio.setSample (1, sample, v);
        }
        processor.processBlock (audio, midi);
    }
    editor.updateMeters();
    editor.showHelpForParameter (ParamID::meter);
    const auto plusFour = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    const auto secondOutput = directory.getChildFile ("editor-plus4.png");
    secondOutput.deleteFile();
    juce::FileOutputStream secondStream (secondOutput);
    const bool secondWrote = secondStream.openedOk() && juce::PNGImageFormat().writeImageToStream (plusFour, secondStream);
    const bool parametersOk = near (processor.apvts.getRawParameterValue (ParamID::peakReduction)->load(), 47.0f)
                           && near (processor.apvts.getRawParameterValue (ParamID::gain)->load(), 53.0f);
    const bool imageOk = image.getWidth() == EditorView::kWidth * 2
                         && image.getHeight() == EditorView::kHeight * 2
                         && image.getPixelAt (20, 20) != image.getPixelAt (1100, 300);
    set (processor, ParamID::peakReduction, 61.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    auto* peakKnob = dynamic_cast<juce::Slider*> (namedChild (editor, "Peak Reduction"));
    const bool hostToEditor = peakKnob != nullptr && peakKnob->getTextFromValue (peakKnob->getValue()) == "61 %";

    set (processor, ParamID::compressLimit, 0.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    set (processor, ParamID::compressLimit, 1.0f);
    auto* gainKnob = dynamic_cast<juce::Slider*> (namedChild (editor, "Gain"));
    if (gainKnob != nullptr) gainKnob->setValue (70.0, juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    const bool editorToHost = gainKnob != nullptr
                              && near (processor.apvts.getRawParameterValue (ParamID::gain)->load(), 70.0f);

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    La2aProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    const bool stateOk = near (restored.apvts.getRawParameterValue (ParamID::peakReduction)->load(), 61.0f)
                         && near (restored.apvts.getRawParameterValue (ParamID::compressLimit)->load(), 1.0f);
    std::printf ("snapshot %s (%d x %d), parameters %s, painted image %s\n",
                 output.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight(),
                 parametersOk ? "ok" : "FAIL", imageOk ? "ok" : "FAIL");
    std::printf ("host -> editor %s, editor -> host %s, saved state %s\n",
                 hostToEditor ? "ok" : "FAIL", editorToHost ? "ok" : "FAIL", stateOk ? "ok" : "FAIL");
    return wrote && secondWrote && parametersOk && imageOk && hostToEditor && editorToHost && stateOk ? 0 : 1;
}
