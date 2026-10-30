#include "shared/AnalyzerProcessor.h"

#include "shared/AnalyzerEditor.h"

namespace phaselab
{

namespace
{
    std::uint64_t makeToken()
    {
        // Identifies this instance as the writer of a slot; never stored in the project, so copies
        // of an instance never share it.
        return (std::uint64_t) juce::Random::getSystemRandom().nextInt64() | 1u;
    }
}

AnalyzerProcessor::AnalyzerProcessor (Role r)
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      role (r),
      state (*this, nullptr, "PHASELAB", createParameterLayout (r)),
      writer (makeToken())
{
    cutoff = state.getRawParameterValue (ids::bandCutoff);
    group = state.getRawParameterValue (ids::group);
    if (role == Role::channel)
    {
        mode = state.getRawParameterValue (ids::mode);
        slot = state.getRawParameterValue (ids::slot);
    }
    else
    {
        busLink = std::make_unique<BusLink> (*group, *cutoff);
    }

    if (! state.state.hasProperty ("language"))
        state.state.setProperty ("language", juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("de") ? "de" : "en", nullptr);

    maintainLink();
    startTimerHz (5);
}

void AnalyzerProcessor::setDisplayActive (bool active) noexcept
{
    engine.setDisplayActive (active);
    if (busLink != nullptr)
        busLink->setDisplayActive (active);
}

double AnalyzerProcessor::getWaveformSeconds() const
{
    return juce::jlimit (waveformMinSeconds, waveformMaxSeconds, (double) state.state.getProperty ("waveSeconds", 0.04));
}

void AnalyzerProcessor::setWaveformSeconds (double seconds)
{
    seconds = juce::jlimit (waveformMinSeconds, waveformMaxSeconds, seconds);
    state.state.setProperty ("waveSeconds", seconds, nullptr);
    engine.setWaveformSeconds (seconds);
    if (busLink != nullptr)
        busLink->setWaveformSeconds (seconds);
}

bool AnalyzerProcessor::getWaveformTrigger() const
{
    return (bool) state.state.getProperty ("waveTrigger", true);
}

void AnalyzerProcessor::setWaveformTrigger (bool on)
{
    state.state.setProperty ("waveTrigger", on, nullptr);
    engine.setWaveformTrigger (on);
    if (busLink != nullptr)
        busLink->setWaveformTrigger (on);
}

bool AnalyzerProcessor::isEnglish() const
{
    return state.state.getProperty ("language").toString() == "en";
}

void AnalyzerProcessor::setEnglish (bool english)
{
    state.state.setProperty ("language", english ? "en" : "de", nullptr);
}

AnalyzerProcessor::~AnalyzerProcessor()
{
    stopTimer();
    engine.stop();
    if (busLink != nullptr)
        busLink->stop();
    if (sendingTo != nullptr)
        writer.release (*sendingTo);
}

ChannelMode AnalyzerProcessor::getMode() const noexcept
{
    if (mode == nullptr)
        return ChannelMode::analyse;
    return static_cast<ChannelMode> (juce::jlimit (0, 2, juce::roundToInt (mode->load())));
}

int AnalyzerProcessor::getGroup() const noexcept
{
    return juce::jlimit (0, link::maxGroups - 1, juce::roundToInt (group->load()));
}

int AnalyzerProcessor::getSlot() const noexcept
{
    return slot == nullptr ? 0 : juce::jlimit (0, 1, juce::roundToInt (slot->load()));
}

juce::String AnalyzerProcessor::getTrackName() const
{
    const juce::SpinLock::ScopedLockType lock (nameLock);
    return trackName;
}

BusLinkStatus AnalyzerProcessor::getLinkStatus() const
{
    return busLink != nullptr ? busLink->getStatus() : BusLinkStatus {};
}

void AnalyzerProcessor::maintainLink()
{
    if (role != Role::channel || ! isSending())
        return;

    auto* layout = slots.acquire (getGroup(), getSlot());
    if (layout != nullptr && layout->owner.load() == writer.getToken())
        link::writeName (*layout, getTrackName().toStdString());
}

void AnalyzerProcessor::updateTrackProperties (const TrackProperties& properties)
{
    {
        const juce::SpinLock::ScopedLockType lock (nameLock);
        trackName = properties.name.value_or (juce::String());
    }
    maintainLink();
}

void AnalyzerProcessor::prepareToPlay (double sampleRate, int)
{
    setWaveformSeconds (getWaveformSeconds());   // the stored zoom may come from a loaded project
    setWaveformTrigger (getWaveformTrigger());
    meter.setCutoff (cutoff->load());
    meter.prepare (sampleRate);
    wasAnalysing = isAnalysing();
    engine.prepare (sampleRate);
    engine.start();
    maintainLink();
    if (busLink != nullptr)
        busLink->start();
}

void AnalyzerProcessor::releaseResources()
{
    engine.stop();
    if (busLink != nullptr)
        busLink->stop();
}

bool AnalyzerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

link::BlockTiming AnalyzerProcessor::readTiming() const
{
    link::BlockTiming timing;
    timing.sampleRate = getSampleRate();
    timing.offline = isNonRealtime();

    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto samples = position->getTimeInSamples(); samples.hasValue() && position->getIsPlaying())
            {
                timing.hostTime = true;
                timing.timeline = *samples;
            }

    return timing;
}

void AnalyzerProcessor::sendBlock (const float* left, const float* right, int numSamples)
{
    if (! isSending())
    {
        if (sendingTo != nullptr)
            writer.release (*sendingTo);
        sendingTo = nullptr;
        sendState.store (SendState::off);
        return;
    }

    auto* target = slots.lookup (getGroup(), getSlot());
    if (sendingTo != nullptr && sendingTo != target)
    {
        writer.release (*sendingTo);   // group or slot changed: free the old slot at once
        sendingTo = nullptr;
    }
    if (target == nullptr)
    {
        sendState.store (SendState::unavailable);   // the timer opens it shortly
        return;
    }

    const auto result = writer.write (*target, left, right, numSamples, readTiming(), link::nowMs());
    sendingTo = result == link::WriteStatus::sent ? target : nullptr;
    sendState.store (result == link::WriteStatus::sent ? SendState::sending : SendState::busy);
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

    // Analysis and link always read the unchanged input, before the mono monitor (D-30).
    const bool analysing = isAnalysing();
    if (analysing)
    {
        if (! wasAnalysing)
            meter.reset();   // values from before "Nur Senden" are stale
        meter.setCutoff (cutoff->load());
        meter.process (left, right, numSamples);

        BlockInfo info;
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
            {
                info.playing = position->getIsPlaying();
                if (const auto ppq = position->getPpqPosition())
                {
                    info.hasMusicalPosition = true;
                    info.ppq = *ppq;
                }
                if (const auto sig = position->getTimeSignature())
                {
                    info.numerator = sig->numerator;
                    info.denominator = sig->denominator;
                }
            }
        engine.push (left, right, numSamples, info);
    }
    wasAnalysing = analysing;

    if (role == Role::channel)
        sendBlock (left, right, numSamples);

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
    maintainLink();
}

} // namespace phaselab
