#include "shared/AnalyzerProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new phaselab::AnalyzerProcessor (phaselab::Role::bus);
}
