#include "shared/Parameters.h"

#include "core/MonoScore.h"

namespace phaselab
{

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (Role role)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Cutoff of the band mono score (D-22): 40–500 Hz, centre of the travel at the geometric mean.
    juce::NormalisableRange<float> cutoffRange ((float) MonoScoreMeter::minCutoffHz,
                                                (float) MonoScoreMeter::maxCutoffHz, 1.0f);
    cutoffRange.setSkewForCentre (std::sqrt ((float) (MonoScoreMeter::minCutoffHz * MonoScoreMeter::maxCutoffHz)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ids::bandCutoff, 1 }, "Mono-Score Grenzfrequenz", cutoffRange, 120.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    if (role == Role::channel)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids::mode, 1 }, "Betriebsart",
            juce::StringArray { "Analyse", "Analyse + Senden", "Nur Senden" }, 0));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids::slot, 1 }, "Slot", juce::StringArray { "A", "B" }, 0));
    }

    return layout;
}

} // namespace phaselab
