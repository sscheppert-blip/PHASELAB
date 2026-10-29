#include "shared/AnalyzerProcessor.h"

#include "shared/AnalyzerEditor.h"

namespace phaselab
{

AnalyzerProcessor::AnalyzerProcessor (Role r)
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      role (r),
      state (*this, nullptr, "PHASELAB", createParameterLayout (r))
{
    cutoff = state.getRawParameterValue (ids::bandCutoff);
    if (role == Role::channel)
        mode = state.getRawParameterValue (ids::mode);
}

ChannelMode AnalyzerProcessor::getMode() const noexcept
{
    if (mode == nullptr)
        return ChannelMode::analyse;
    return static_cast<ChannelMode> (juce::jlimit (0, 2, juce::roundToInt (mode->load())));
}

void AnalyzerProcessor::prepareToPlay (double sampleRate, int)
{
    meter.setCutoff (cutoff->load());
    meter.prepare (sampleRate);
    wasAnalysing = isAnalysing();
}

bool AnalyzerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void AnalyzerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    if (numSamples == 0 || channels == 0)
        return;

    const float* left = buffer.getReadPointer (0);
    const float* right = buffer.getReadPointer (channels > 1 ? 1 : 0);

    // Analysis always reads the unchanged input, before the mono monitor (D-30).
    const bool analysing = isAnalysing();
    if (analysing)
    {
        if (! wasAnalysing)
            meter.reset();   // values from before "Nur Senden" are stale
        meter.setCutoff (cutoff->load());
        meter.process (left, right, numSamples);
    }
    wasAnalysing = analysing;

    float peak = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
    float previous = inputPeak.load();
    while (peak > previous && ! inputPeak.compare_exchange_weak (previous, peak)) {}

    // The audio stays untouched unless the mono monitor is switched on.
    if (monoMonitor.load() && channels > 1)
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            l[i] = m;
            r[i] = m;
        }
    }
}

juce::AudioProcessorEditor* AnalyzerProcessor::createEditor()
{
    return new AnalyzerEditor (*this);
}

const juce::String AnalyzerProcessor::getName() const
{
    return role == Role::channel ? "PhaseLab Channel Analyzer" : "PhaseLab Bus Analyzer";
}

void AnalyzerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AnalyzerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));

    // Loading a project never brings back the mono monitor, so no export turns mono by accident.
    monoMonitor.store (false);
}

} // namespace phaselab
