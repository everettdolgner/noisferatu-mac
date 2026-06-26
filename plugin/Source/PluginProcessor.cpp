// PluginProcessor.cpp — JUCE AudioProcessor wrapper around the NOISFERATU engine.
#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

NoisferatuProcessor::NoisferatuProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    bankP_     = apvts.getRawParameterValue (kBank);
    algoP_     = apvts.getRawParameterValue (kAlgo);
    pot1P_     = apvts.getRawParameterValue (kPot1);
    pot2P_     = apvts.getRawParameterValue (kPot2);
    bitcrushP_ = apvts.getRawParameterValue (kBitcrush);
    rateP_     = apvts.getRawParameterValue (kRate);
    volumeP_   = apvts.getRawParameterValue (kVolume);
}

APVTS::ParameterLayout NoisferatuProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;

    // Bank / algorithm as integer choices (5 banks x 9 algorithms).
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { kBank, 1 }, "Bank", 0, 4, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { kAlgo, 1 }, "Algorithm", 0, 8, 0));

    auto unit = NormalisableRange<float> (0.0f, 1.0f, 0.0001f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kPot1, 1 },     "Pot 1",    unit, 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kPot2, 1 },     "Pot 2",    unit, 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kBitcrush, 1 }, "Bitcrush", unit, 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kRate, 1 },     "Rate",     unit, 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kVolume, 1 },   "Volume",   unit, 0.8f));
    return layout;
}

void NoisferatuProcessor::prepareToPlay (double sampleRate, int)
{
    engine_.prepare (sampleRate);
}

bool NoisferatuProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void NoisferatuProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Push the current control values into the engine (once per block, like the firmware).
    engine_.setBankAlgo (static_cast<int> (*bankP_), static_cast<int> (*algoP_));
    engine_.setControls (*pot1P_, *pot2P_, *bitcrushP_, *rateP_, *volumeP_);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    auto* ch0 = buffer.getWritePointer (0);
    for (int i = 0; i < numSamples; ++i)
        ch0[i] = engine_.nextHostSample();

    // Mono engine -> copy to any further output channels.
    for (int c = 1; c < numChannels; ++c)
        buffer.copyFrom (c, 0, ch0, numSamples);
}

juce::AudioProcessorEditor* NoisferatuProcessor::createEditor()
{
    return new NoisferatuEditor (*this);
}

void NoisferatuProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void NoisferatuProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NoisferatuProcessor();
}
