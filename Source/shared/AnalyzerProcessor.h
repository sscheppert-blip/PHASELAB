#pragma once

#include "core/MonoScore.h"
#include "shared/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace phaselab
{

// Shared processor of both plugins. Audio passes through unchanged; the only exception is the
// mono monitor switch (D-30), which is not a host parameter and is never saved with the project.
class AnalyzerProcessor final : public juce::AudioProcessor
{
public:
    explicit AnalyzerProcessor (Role role);

    Role getRole() const noexcept { return role; }
    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    const MonoScoreMeter& getMeter() const noexcept { return meter; }

    ChannelMode getMode() const noexcept;
    bool isAnalysing() const noexcept { return getMode() != ChannelMode::sendOnly; }

    void setMonoMonitor (bool on) noexcept { monoMonitor.store (on); }
    bool isMonoMonitor() const noexcept { return monoMonitor.load(); }

    // Peak of the input since the last call, for the editor (0 when nothing arrived).
    float takeInputPeak() noexcept { return inputPeak.exchange (0.0f); }

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
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

private:
    const Role role;
    juce::AudioProcessorValueTreeState state;
    std::atomic<float>* cutoff = nullptr;
    std::atomic<float>* mode = nullptr;

    MonoScoreMeter meter;
    std::atomic<bool> monoMonitor { false };
    std::atomic<float> inputPeak { 0.0f };
    bool wasAnalysing = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerProcessor)
};

} // namespace phaselab
