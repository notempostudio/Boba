#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using namespace juce;
using Float = AudioParameterFloat;

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };
    auto flt = [&] (const char* i, const char* name, NormalisableRange<float> r, float def, const char* unit)
    { p.push_back (std::make_unique<Float> (id (i), name, r, def, AudioParameterFloatAttributes().withLabel (unit))); };

    // Same ranges / defaults as the Max for Live device.
    flt ("threshold", "Threshold",    { -70.f, 30.f, 0.1f },          0.f,      "dB");
    flt ("depth",     "Depth",        { 0.f, 100.f, 0.1f },           30.7f,    "%");
    flt ("attack",    "Attack",       { 0.f, 1200.f, 0.01f, 0.4f },   9.62f,    "ms");   // skew 0.4 == exponent 2.5
    flt ("release",   "Release",      { 0.f, 1200.f, 0.01f, 0.4f },   26.67f,   "ms");
    flt ("link",      "Stereo Link",  { 0.f, 100.f, 1.f },            80.f,     "%");

    p.push_back (std::make_unique<AudioParameterChoice> (id ("stereoMode"), "Stereo Mode", StringArray { "L/R", "M/S" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (id ("sidechain"),  "Side Chain",  StringArray { "Internal", "External" }, 0));
    p.push_back (std::make_unique<AudioParameterBool>   (id ("solo"),  "Solo Side Chain", false));
    p.push_back (std::make_unique<AudioParameterBool>   (id ("delta"), "Delta", false));
    p.push_back (std::make_unique<AudioParameterChoice> (id ("fftSize"), "FFT Size", StringArray { "128", "256", "512", "1024", "2048", "4096" }, 4));
    p.push_back (std::make_unique<AudioParameterChoice> (id ("overlap"), "Overlap",  StringArray { "2", "4", "8", "16", "32" }, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (id ("timeMode"), "Time Mode", StringArray { "Original", "Accurate" }, 0));

    // Emphasis EQ (original: filtergraph~ with 5 filters)
    flt ("eqHp",    "EQ High-pass", { 20.f, 500.f, 0.f, 0.4f },     40.f,      "Hz");
    flt ("eqLp",    "EQ Low-pass",  { 2000.f, 20000.f, 0.f, 0.4f }, 18464.8f,  "Hz");
    const char* names[3] = { "Low", "Mid", "High" };
    const NormalisableRange<float> fr[3] = { { 20.f, 1000.f, 0.f, 0.4f }, { 100.f, 5000.f, 0.f, 0.4f }, { 1000.f, 20000.f, 0.f, 0.4f } };
    const float defF[3] = { 121.87f, 548.f, 8307.35f }, defG[3] = { 0.78f, 0.f, 10.18f }, defQ[3] = { 0.7547f, 0.7284f, 0.5211f };
    static String keep[9];
    for (int i = 0; i < 3; ++i)
    {
        keep[i * 3 + 0] = "eq" + String (i + 1) + "Freq"; keep[i * 3 + 1] = "eq" + String (i + 1) + "Gain"; keep[i * 3 + 2] = "eq" + String (i + 1) + "Q";
        flt (keep[i * 3].toRawUTF8(),     ("EQ " + String (names[i]) + " Freq").toRawUTF8(), fr[i], defF[i], "Hz");
        flt (keep[i * 3 + 1].toRawUTF8(), ("EQ " + String (names[i]) + " Gain").toRawUTF8(), { -18.f, 18.f, 0.01f }, defG[i], "dB");
        flt (keep[i * 3 + 2].toRawUTF8(), ("EQ " + String (names[i]) + " Q").toRawUTF8(),    { 0.1f, 10.f, 0.01f, 0.5f }, defQ[i], "");
    }
    return { p.begin(), p.end() };
}
} // namespace

BobaAudioProcessor::BobaAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "BobaState", createLayout())
{
}

void BobaAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    core.prepare (sr);
    core.setFFT (128 << (int) get ("fftSize"), 2 << (int) get ("overlap"));
    setLatencySamples (core.latencySamples());
}

bool BobaAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainInputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::stereo();
}

boba::Settings BobaAudioProcessor::readSettings() const
{
    boba::Settings s;
    s.thresholdDb = get ("threshold");
    s.depth = get ("depth") * 0.01f;
    s.attackMs = get ("attack");
    s.releaseMs = get ("release");
    s.linkPercent = get ("link");
    s.midSide = get ("stereoMode") > 0.5f;
    s.externalSidechain = get ("sidechain") > 0.5f;
    s.solo = get ("solo") > 0.5f;
    s.delta = get ("delta") > 0.5f;
    s.accurateTime = get ("timeMode") > 0.5f;
    s.eq.hpFreq = get ("eqHp"); s.eq.lpFreq = get ("eqLp");
    const char* f[3] = { "eq1Freq", "eq2Freq", "eq3Freq" }; const char* g[3] = { "eq1Gain", "eq2Gain", "eq3Gain" }; const char* q[3] = { "eq1Q", "eq2Q", "eq3Q" };
    for (int i = 0; i < 3; ++i) { s.eq.peakFreq[i] = get (f[i]); s.eq.peakGainDb[i] = get (g[i]); s.eq.peakQ[i] = get (q[i]); }
    return s;
}

void BobaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;

    // FFT size / overlap changed -> rebuild the engine and tell the host the new latency.
    // (Allocates once per change; see README.)
    const int N = 128 << (int) get ("fftSize"), ov = 2 << (int) get ("overlap");
    if (N != core.fftSize() || ov != core.overlap())
    {
        core.setFFT (N, ov);
        setLatencySamples (core.latencySamples());
    }

    const float *scL = nullptr, *scR = nullptr;
    if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() >= 2) { scL = sc.getReadPointer (0); scR = sc.getReadPointer (1); }
    }

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);
    core.process (l, r, scL, scR, l, r, buffer.getNumSamples(), readSettings());
}

juce::AudioProcessorEditor* BobaAudioProcessor::createEditor() { return new BobaEditor (*this); }

void BobaAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void BobaAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BobaAudioProcessor(); }
