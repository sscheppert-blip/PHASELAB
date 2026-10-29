#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace phaselab
{

enum class Role { channel, bus };

// Channel Analyzer operating modes (docs/spec/09, D-21)
enum class ChannelMode { analyse = 0, analyseAndSend = 1, sendOnly = 2 };

namespace ids
{
    // The IDs are a contract with saved projects: never rename or remove one, only add.
    inline constexpr const char* bandCutoff = "band_cutoff";
    inline constexpr const char* mode = "mode";
    inline constexpr const char* slot = "slot";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (Role role);

} // namespace phaselab
