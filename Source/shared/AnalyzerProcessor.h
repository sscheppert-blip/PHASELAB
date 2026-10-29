#pragma once

#include "core/MonoScore.h"
#include "shared/AnalysisEngine.h"
#include "shared/BusLink.h"
#include "shared/Parameters.h"
#include "transport/Link.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace phaselab
{

// What the Channel Analyzer's link is doing, for the editor.
enum class SendState { off, sending, busy, unavailable };

// Shared processor of both plugins. Audio passes through unchanged; the only exception is the
// mono monitor switch (D-30), which is not a host parameter and is never saved with the project.
class AnalyzerProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    explicit AnalyzerProcessor (Role role);
    ~AnalyzerProcessor() override;

    Role getRole() const noexcept { return role; }
    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    const MonoScoreMeter& getMeter() const noexcept { return meter; }

    ChannelMode getMode() const noexcept;
    bool isAnalysing() const noexcept { return getMode() != ChannelMode::sendOnly; }
    bool isSending() const noexcept { return role == Role::channel && getMode() != ChannelMode::analyse; }
    int getGroup() const noexcept;
    int getSlot() const noexcept;

    void setMonoMonitor (bool on) noexcept { monoMonitor.store (on); }
    bool isMonoMonitor() const noexcept { return monoMonitor.load(); }

    // Peak of the input since the last call, for the editor (0 when nothing arrived).
    float takeInputPeak() noexcept { return inputPeak.exchange (0.0f); }

    SendState getSendState() const noexcept { return sendState.load(); }
    juce::String getTrackName() const;
    BusLinkStatus getLinkStatus() const;
    BusLink* getBusLink() noexcept { return busLink.get(); }
    AnalysisEngine& getEngine() noexcept { return engine; }

    // Display-only analysis runs while at least one editor is open (D-29).
    void setDisplayActive (bool active) noexcept;

    // UI language, stored with the project but not a host parameter (D-06).
    bool isEnglish() const;
    void setEnglish (bool english);

    // Opens the shared slot the current settings need and publishes the track name. Runs on the
    // message thread (timer, prepare, state load); the tests call it directly.
    void maintainLink();

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
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
    void updateTrackProperties (const TrackProperties& properties) override;

private:
    void timerCallback() override { maintainLink(); }
    void sendBlock (const float* left, const float* right, int numSamples);
    link::BlockTiming readTiming() const;

    const Role role;
    juce::AudioProcessorValueTreeState state;
    std::atomic<float>* cutoff = nullptr;
    std::atomic<float>* mode = nullptr;
    std::atomic<float>* slot = nullptr;
    std::atomic<float>* group = nullptr;

    MonoScoreMeter meter;
    std::atomic<bool> monoMonitor { false };
    std::atomic<float> inputPeak { 0.0f };
    bool wasAnalysing = true;

    // Channel Analyzer: writer side of the link
    link::SlotTable slots;
    link::Writer writer;
    link::SlotLayout* sendingTo = nullptr;   // audio thread only
    std::atomic<SendState> sendState { SendState::off };
    mutable juce::SpinLock nameLock;
    juce::String trackName;

    // Bus Analyzer: reader side of the link
    std::unique_ptr<BusLink> busLink;

    AnalysisEngine engine { meter };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerProcessor)
};

} // namespace phaselab
