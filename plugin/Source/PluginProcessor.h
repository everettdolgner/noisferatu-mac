// PluginProcessor.h — JUCE AudioProcessor wrapper around the NOISFERATU engine.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"

class NoisferatuProcessor : public juce::AudioProcessor
{
public:
    NoisferatuProcessor();
    ~NoisferatuProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Noisferatu"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Exposed for the editor.
    juce::AudioProcessorValueTreeState apvts;
    noisferatu::Engine& engine() { return engine_; }

    static constexpr const char* kBank     = "bank";
    static constexpr const char* kAlgo     = "algo";
    static constexpr const char* kPot1     = "pot1";
    static constexpr const char* kPot2     = "pot2";
    static constexpr const char* kBitcrush = "bitcrush";
    static constexpr const char* kRate     = "rate";
    static constexpr const char* kVolume   = "volume";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    noisferatu::Engine engine_;

    std::atomic<float>* bankP_     = nullptr;
    std::atomic<float>* algoP_     = nullptr;
    std::atomic<float>* pot1P_     = nullptr;
    std::atomic<float>* pot2P_     = nullptr;
    std::atomic<float>* bitcrushP_ = nullptr;
    std::atomic<float>* rateP_     = nullptr;
    std::atomic<float>* volumeP_   = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoisferatuProcessor)
};
