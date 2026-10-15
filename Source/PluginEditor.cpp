#include "PluginEditor.h"

BobaEditor::BobaEditor (BobaAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    addKnob (mainKnobs, "depth", "Depth");       addKnob (mainKnobs, "threshold", "Threshold");
    addKnob (mainKnobs, "attack", "Attack");     addKnob (mainKnobs, "release", "Release");
    addKnob (mainKnobs, "link", "Stereo Link");

    addCombo ("stereoMode", "Mode");  addCombo ("sidechain", "Side Chain");
    addCombo ("fftSize", "FFT Size"); addCombo ("overlap", "Overlap"); addCombo ("timeMode", "Time Mode");
    addToggle ("solo", "Solo SC");    addToggle ("delta", "Delta");

    eqTitle.setText ("Emphasis EQ (sidechain)", juce::dontSendNotification);
    eqTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    addAndMakeVisible (eqTitle);
    addKnob (eqKnobs, "eqHp", "HP");
    const char* b[3] = { "Low", "Mid", "High" };
    for (int i = 0; i < 3; ++i)
    {
        const auto n = juce::String (i + 1);
        addKnob (eqKnobs, "eq" + n + "Freq", juce::String (b[i]) + " Hz");
        addKnob (eqKnobs, "eq" + n + "Gain", juce::String (b[i]) + " dB");
        addKnob (eqKnobs, "eq" + n + "Q",    juce::String (b[i]) + " Q");
    }
    addKnob (eqKnobs, "eqLp", "LP");
    setSize (640, 430);
}

void BobaEditor::addKnob (std::vector<std::unique_ptr<Knob>>& list, const juce::String& id, const juce::String& name)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->att = std::make_unique<SA> (proc.apvts, id, k->slider);
    addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
    list.push_back (std::move (k));
}

void BobaEditor::addCombo (const juce::String& id, const juce::String& name)
{
    auto c = std::make_unique<Combo>();
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c->box.addItemList (prm->choices, 1);
    c->label.setText (name, juce::dontSendNotification);
    c->label.setJustificationType (juce::Justification::centredRight);
    c->att = std::make_unique<CA> (proc.apvts, id, c->box);
    addAndMakeVisible (c->box); addAndMakeVisible (c->label);
    combos.push_back (std::move (c));
}

void BobaEditor::addToggle (const juce::String& id, const juce::String& name)
{
    auto t = std::make_unique<Toggle>();
    t->button.setButtonText (name);
    t->att = std::make_unique<BA> (proc.apvts, id, t->button);
    addAndMakeVisible (t->button);
    toggles.push_back (std::move (t));
}

void BobaEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1f24));
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Boba", 16, 8, 200, 28, juce::Justification::left);
}

void BobaEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (34);

    auto row = area.removeFromTop (130);
    const int w = row.getWidth() / (int) mainKnobs.size();
    for (auto& k : mainKnobs)
    {
        auto c = row.removeFromLeft (w);
        k->label.setBounds (c.removeFromTop (18));
        k->slider.setBounds (c);
    }

    area.removeFromTop (6);
    auto cr = area.removeFromTop (28);
    const int cw = cr.getWidth() / (int) combos.size();
    for (auto& c : combos)
    {
        auto cell = cr.removeFromLeft (cw);
        c->label.setBounds (cell.removeFromLeft (cw * 40 / 100));
        c->box.setBounds (cell.reduced (2, 0));
    }
    auto tr = area.removeFromTop (28);
    for (auto& t : toggles) t->button.setBounds (tr.removeFromLeft (100));

    area.removeFromTop (6);
    eqTitle.setBounds (area.removeFromTop (22));
    auto er = area.removeFromTop (110);
    const int ew = er.getWidth() / (int) eqKnobs.size();
    for (auto& k : eqKnobs)
    {
        auto c = er.removeFromLeft (ew);
        k->label.setBounds (c.removeFromTop (16));
        k->slider.setBounds (c);
    }
}
