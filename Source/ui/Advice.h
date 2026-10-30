#pragma once

// Plain-language observations and manual hints (R-14, R-15, D-23): what is measured, where, and what
// to check by ear. Never a verdict on quality and never an automatic change to the audio.

#include "shared/AnalysisEngine.h"
#include "shared/BusLink.h"

#include <juce_core/juce_core.h>

namespace phaselab::ui
{

struct Advice
{
    juce::String title;
    juce::String text;
    bool warning = false;
    bool status = false;   // a state message (no signal, link state), not an observation: may change at once
};

struct OwnInput
{
    const EngineSnapshot& snapshot;
    int fullScore = -1, bandScore = -1;
    double cutoff = 120.0;
};

Advice adviseOwnInput (const OwnInput&, bool english);
Advice adviseBus (const OwnInput&, const BusLinkStatus&, bool english);

juce::String linkStateText (LinkState, bool english);
juce::String offsetText (const ABResult&, double sampleRate, bool english);

} // namespace phaselab::ui
