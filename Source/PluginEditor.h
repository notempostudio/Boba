#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class BobaEditor : public juce::AudioProcessorEditor
{
public:
    explicit BobaEditor (BobaAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob { juce::Slider slider; juce::Label label; std::unique_ptr<SA> att; };
    struct Combo { juce::ComboBox box; juce::Label label; std::unique_ptr<CA> att; };
    struct Toggle { juce::ToggleButton button; std::unique_ptr<BA> att; };

    void addKnob (std::vector<std::unique_ptr<Knob>>&, const juce::String& id, const juce::String& name);
    void addCombo (const juce::String& id, const juce::String& name);
    void addToggle (const juce::String& id, const juce::String& name);

    BobaAudioProcessor& proc;
    std::vector<std::unique_ptr<Knob>> mainKnobs, eqKnobs;
    std::vector<std::unique_ptr<Combo>> combos;
    std::vector<std::unique_ptr<Toggle>> toggles;
    juce::Label eqTitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BobaEditor)
};
