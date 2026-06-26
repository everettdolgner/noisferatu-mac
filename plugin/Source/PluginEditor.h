// PluginEditor.h — functional editor: 5 rotary controls, hardware-style bank/algo
// stepping, a bank.algo readout, and a live oscilloscope tap.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class NoisferatuEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit NoisferatuEditor (NoisferatuProcessor&);
    ~NoisferatuEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void stepBank (int delta);
    void stepAlgo (int delta);
    void refreshReadout();

    // A tiny scope that polls the engine's 16 kHz post-effects tap.
    struct Scope : juce::Component
    {
        explicit Scope (noisferatu::Engine& e) : engine (e) {}
        void paint (juce::Graphics&) override;
        noisferatu::Engine& engine;
        std::array<float, noisferatu::kEngineBlock> buf {};
    };

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;

    NoisferatuProcessor& proc;

    juce::Slider pot1, pot2, bitcrush, rate, volume;
    juce::Label  pot1L, pot2L, bitcrushL, rateL, volumeL;
    std::unique_ptr<SliderAttach> aPot1, aPot2, aBitcrush, aRate, aVolume;

    juce::TextButton bankBtn { "BANK" }, prevBtn { "PREV" }, nextBtn { "ALGO >" };
    juce::Label  readout;     // "1.03"
    juce::Label  algoName;    // current algorithm name
    juce::Label  title;

    Scope scope { proc.engine() };

    void styleKnob (juce::Slider&, juce::Label&, const juce::String&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoisferatuEditor)
};
