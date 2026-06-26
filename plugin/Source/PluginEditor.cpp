// PluginEditor.cpp — see PluginEditor.h.
#include "PluginEditor.h"

using juce::Colour;

namespace
{
    const Colour kBg    { 0xff14110f };
    const Colour kPanel { 0xff1f1b18 };
    const Colour kInk   { 0xffd8c8a8 };
    const Colour kAmber { 0xffe0a040 };
}

NoisferatuEditor::NoisferatuEditor (NoisferatuProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    title.setText ("NOISFERATU", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
    title.setColour (juce::Label::textColourId, kAmber);
    title.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (title);

    styleKnob (pot1,     pot1L,     "POT 1");
    styleKnob (pot2,     pot2L,     "POT 2");
    styleKnob (bitcrush, bitcrushL, "CRUSH");
    styleKnob (rate,     rateL,     "RATE");
    styleKnob (volume,   volumeL,   "VOL");

    aPot1     = std::make_unique<SliderAttach> (proc.apvts, NoisferatuProcessor::kPot1,     pot1);
    aPot2     = std::make_unique<SliderAttach> (proc.apvts, NoisferatuProcessor::kPot2,     pot2);
    aBitcrush = std::make_unique<SliderAttach> (proc.apvts, NoisferatuProcessor::kBitcrush, bitcrush);
    aRate     = std::make_unique<SliderAttach> (proc.apvts, NoisferatuProcessor::kRate,     rate);
    aVolume   = std::make_unique<SliderAttach> (proc.apvts, NoisferatuProcessor::kVolume,   volume);

    for (auto* b : { &bankBtn, &prevBtn, &nextBtn })
    {
        b->setColour (juce::TextButton::buttonColourId, kPanel);
        b->setColour (juce::TextButton::textColourOffId, kInk);
        addAndMakeVisible (*b);
    }
    bankBtn.onClick = [this] { stepBank (1); };
    prevBtn.onClick = [this] { stepAlgo (-1); };
    nextBtn.onClick = [this] { stepAlgo (1); };

    readout.setFont (juce::Font (juce::FontOptions (40.0f).withStyle ("Bold")));
    readout.setColour (juce::Label::textColourId, kAmber);
    readout.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (readout);

    algoName.setFont (juce::Font (juce::FontOptions (15.0f)));
    algoName.setColour (juce::Label::textColourId, kInk);
    algoName.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (algoName);

    addAndMakeVisible (scope);

    refreshReadout();
    setSize (560, 360);
    startTimerHz (30);
}

NoisferatuEditor::~NoisferatuEditor() { stopTimer(); }

void NoisferatuEditor::styleKnob (juce::Slider& s, juce::Label& l, const juce::String& text)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
    s.setColour (juce::Slider::rotarySliderFillColourId, kAmber);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, kPanel);
    s.setColour (juce::Slider::thumbColourId, kInk);
    s.setColour (juce::Slider::textBoxTextColourId, kInk);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (s);

    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::Font (juce::FontOptions (12.0f)));
    l.setColour (juce::Label::textColourId, kInk);
    l.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (l);
}

void NoisferatuEditor::stepBank (int delta)
{
    if (auto* p = proc.apvts.getParameter (NoisferatuProcessor::kBank))
    {
        int cur = static_cast<int> (p->convertFrom0to1 (p->getValue()));
        int next = (cur + delta + 5) % 5;
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (next)));
    }
    // Reset to first algorithm in the new bank (mirrors the hardware's next_bank()).
    if (auto* a = proc.apvts.getParameter (NoisferatuProcessor::kAlgo))
        a->setValueNotifyingHost (a->convertTo0to1 (0.0f));
    refreshReadout();
}

void NoisferatuEditor::stepAlgo (int delta)
{
    if (auto* p = proc.apvts.getParameter (NoisferatuProcessor::kAlgo))
    {
        int cur = static_cast<int> (p->convertFrom0to1 (p->getValue()));
        int next = (cur + delta + 9) % 9;
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (next)));
    }
    refreshReadout();
}

void NoisferatuEditor::refreshReadout()
{
    auto& e = proc.engine();
    readout.setText (juce::String (e.displayText().data()), juce::dontSendNotification);
    algoName.setText (juce::String (e.bankNameStr()) + "  |  " + e.algoNameStr(),
                      juce::dontSendNotification);
}

void NoisferatuEditor::timerCallback()
{
    refreshReadout();   // engine reflects host param changes; keep the readout in sync
    scope.repaint();
}

void NoisferatuEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().reduced (8).toFloat(), 8.0f);
}

void NoisferatuEditor::resized()
{
    auto r = getLocalBounds().reduced (18);

    auto top = r.removeFromTop (40);
    title.setBounds (top.removeFromLeft (200));

    // Readout + transport on the right of the title row.
    auto disp = top;
    readout.setBounds (disp.removeFromRight (110));

    auto btnRow = r.removeFromTop (32).reduced (0, 4);
    bankBtn.setBounds (btnRow.removeFromLeft (90));
    btnRow.removeFromLeft (8);
    prevBtn.setBounds (btnRow.removeFromLeft (90));
    btnRow.removeFromLeft (8);
    nextBtn.setBounds (btnRow.removeFromLeft (90));
    algoName.setBounds (btnRow.removeFromLeft (220).reduced (6, 0));

    r.removeFromTop (6);
    scope.setBounds (r.removeFromTop (90));
    r.removeFromTop (10);

    // Five knobs across the bottom.
    auto knobs = r.removeFromTop (120);
    const int w = knobs.getWidth() / 5;
    auto place = [&] (juce::Slider& s, juce::Label& l)
    {
        auto cell = knobs.removeFromLeft (w);
        l.setBounds (cell.removeFromTop (16));
        s.setBounds (cell.reduced (4));
    };
    place (pot1, pot1L);
    place (pot2, pot2L);
    place (bitcrush, bitcrushL);
    place (rate, rateL);
    place (volume, volumeL);
}

// ---------------------------------------------------------------- Scope
void NoisferatuEditor::Scope::paint (juce::Graphics& g)
{
    g.setColour (Colour (0xff0c0a08));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    engine.copyScope (buf.data(), (int) buf.size());

    auto b = getLocalBounds().toFloat().reduced (4.0f);
    const float midY = b.getCentreY();
    const float amp  = b.getHeight() * 0.45f;

    g.setColour (Colour (0xff3a322a));
    g.drawHorizontalLine ((int) midY, b.getX(), b.getRight());

    juce::Path path;
    const int n = (int) buf.size();
    for (int i = 0; i < n; ++i)
    {
        const float x = b.getX() + b.getWidth() * (i / (float) (n - 1));
        const float y = midY - juce::jlimit (-amp, amp, buf[(size_t) i] * amp);
        if (i == 0) path.startNewSubPath (x, y);
        else        path.lineTo (x, y);
    }
    g.setColour (kAmber);
    g.strokePath (path, juce::PathStrokeType (1.2f));
}
