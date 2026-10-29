// Offline tests for the analysis core and the shared processor.
//   PhaseLabTests                               run all tests
//   PhaseLabTests --snapshot <png> channel|bus [send|mono]
//                                               render the panel after feeding a test signal

#include "core/MonoScore.h"
#include "shared/AnalyzerProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <vector>

using namespace phaselab;

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok)
        ++failures;
}

constexpr double rate = 48000.0;
constexpr double pi = 3.14159265358979323846;

using Signal = std::function<void (int index, float& left, float& right)>;

ChannelEnergies energiesOf (const Signal& signal, int length)
{
    ChannelEnergies e;
    for (int i = 0; i < length; ++i)
    {
        float l = 0, r = 0;
        signal (i, l, r);
        e.add (l, r);
    }
    return e;
}

float sine (double hz, int i) { return (float) std::sin (2.0 * pi * hz * i / rate); }

void testScoreFormula()
{
    const int n = 48000;
    check (monoScore (energiesOf ([] (int i, float& l, float& r) { l = r = 0.5f * sine (440, i); }, n)) == 100,
           "identical channels give 100");
    check (monoScore (energiesOf ([] (int i, float& l, float& r) { l = 0.5f * sine (440, i); r = -l; }, n)) == 0,
           "opposite polarity gives 0");
    check (monoScore (energiesOf ([] (int i, float& l, float& r) { l = 0.5f * sine (440, i); r = 0; }, n)) == 100,
           "one channel only gives 100");

    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.0f, 0.2f);
    const int uncorrelated = monoScore (energiesOf ([&] (int, float& l, float& r) { l = noise (rng); r = noise (rng); }, n));
    check (uncorrelated >= 47 && uncorrelated <= 53, "equal uncorrelated noise gives about 50");

    check (monoScore (energiesOf ([] (int, float& l, float& r) { l = r = 0.0f; }, n)) < 0, "silence gives no value");
    check (monoScore (energiesOf ([] (int i, float& l, float& r) { l = r = 1.0e-5f * sine (440, i); }, n)) < 0,
           "signal below -80 dBFS gives no value");
}

void runMeter (MonoScoreMeter& meter, const Signal& signal, int length)
{
    std::vector<float> l (512), r (512);
    for (int pos = 0; pos < length; pos += 512)
    {
        const int count = std::min (512, length - pos);
        for (int i = 0; i < count; ++i)
            signal (pos + i, l[(size_t) i], r[(size_t) i]);
        meter.process (l.data(), r.data(), count);
    }
}

void testBandScore()
{
    // Low end identical, highs in opposite polarity: full band 50, the band below 120 Hz stays mono.
    const Signal lowMonoHighWide = [] (int i, float& l, float& r)
    {
        const float low = 0.4f * sine (60, i), high = 0.4f * sine (5000, i);
        l = low + high;
        r = low - high;
    };

    MonoScoreMeter meter;
    meter.setCutoff (120.0);
    meter.prepare (rate);
    runMeter (meter, lowMonoHighWide, 96000);
    check (std::abs (meter.fullScore() - 50) <= 1, "full-band score sees the wide highs (about 50)");
    check (meter.bandScore() >= 98, "band score below 120 Hz ignores the wide highs");

    // Wide low end: the band score drops, the cutoff limits what counts.
    const Signal wideLow = [] (int i, float& l, float& r)
    {
        l = 0.4f * sine (80, i);
        r = 0.4f * sine (80, i + 150);   // ~90° at 80 Hz
    };
    MonoScoreMeter wide;
    wide.setCutoff (120.0);
    wide.prepare (rate);
    runMeter (wide, wideLow, 96000);
    check (wide.bandScore() >= 0 && wide.bandScore() < 60, "phase-shifted bass lowers the band score");

    MonoScoreMeter clamped;
    clamped.setCutoff (5.0);
    check (clamped.cutoff() == MonoScoreMeter::minCutoffHz, "cutoff is limited to 40 Hz");
    clamped.setCutoff (2000.0);
    check (clamped.cutoff() == MonoScoreMeter::maxCutoffHz, "cutoff is limited to 500 Hz");

    MonoScoreMeter fresh;
    fresh.prepare (rate);
    check (fresh.fullScore() < 0 && fresh.bandScore() < 0, "no value before the first window");
}

juce::AudioBuffer<float> randomStereo (int numSamples, unsigned seed)
{
    juce::AudioBuffer<float> buffer (2, numSamples);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (ch, i, dist (rng));
    return buffer;
}

bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;
    return true;
}

void setParameter (AnalyzerProcessor& p, const char* id, float plainValue)
{
    auto* param = p.getState().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

void testProcessor (Role role, const char* name)
{
    AnalyzerProcessor p (role);
    p.setPlayConfigDetails (2, 2, rate, 256);
    p.prepareToPlay (rate, 256);
    juce::MidiBuffer midi;
    const juce::String prefix = juce::String (name) + ": ";

    // Neutral pass-through for several block sizes (A-02, T-03)
    bool neutral = true;
    for (int size : { 1, 17, 256, 1024 })
    {
        auto buffer = randomStereo (size, (unsigned) size);
        const auto original = buffer;
        p.processBlock (buffer, midi);
        neutral = neutral && identical (buffer, original);
    }
    check (neutral, (prefix + "audio passes bit-identical").toRawUTF8());
    check (p.getLatencySamples() == 0, (prefix + "reports 0 samples latency").toRawUTF8());

    if (role == Role::channel)
    {
        setParameter (p, ids::mode, (float) ChannelMode::sendOnly);
        auto buffer = randomStereo (512, 3);
        const auto original = buffer;
        p.processBlock (buffer, midi);
        check (identical (buffer, original) && ! p.isAnalysing(), (prefix + "send-only passes bit-identical without analysis").toRawUTF8());
        setParameter (p, ids::mode, (float) ChannelMode::analyse);
    }

    // Mono monitor (D-30): sums the output, the analysis still reads the original.
    p.setMonoMonitor (true);
    auto buffer = randomStereo (256, 11);
    const auto original = buffer;
    p.processBlock (buffer, midi);
    bool summed = true;
    for (int i = 0; i < 256; ++i)
    {
        const float expected = 0.5f * (original.getSample (0, i) + original.getSample (1, i));
        summed = summed && buffer.getSample (0, i) == expected && buffer.getSample (1, i) == expected;
    }
    check (summed, (prefix + "mono monitor sums both channels").toRawUTF8());

    // State: parameters come back, the mono monitor never does.
    setParameter (p, ids::bandCutoff, 200.0f);
    if (role == Role::channel)
    {
        setParameter (p, ids::mode, (float) ChannelMode::analyseAndSend);
        setParameter (p, ids::slot, 1.0f);
    }
    juce::MemoryBlock saved;
    p.getStateInformation (saved);

    AnalyzerProcessor restored (role);
    restored.setMonoMonitor (true);
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    const float cutoff = restored.getState().getRawParameterValue (ids::bandCutoff)->load();
    check (std::abs (cutoff - 200.0f) < 1.0f, (prefix + "cutoff is restored").toRawUTF8());
    if (role == Role::channel)
        check (restored.getMode() == ChannelMode::analyseAndSend
                   && juce::roundToInt (restored.getState().getRawParameterValue (ids::slot)->load()) == 1,
               (prefix + "mode and slot are restored").toRawUTF8());
    check (! restored.isMonoMonitor(), (prefix + "loading a project switches the mono monitor off").toRawUTF8());

    // Bus layouts: mono and stereo in = out, nothing else.
    auto layout = [&p] (const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (in);
        l.outputBuses.add (out);
        return p.checkBusesLayoutSupported (l);
    };
    check (layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())
               && layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())
               && ! layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo())
               && ! layout (juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1()),
           (prefix + "mono and stereo layouts only").toRawUTF8());

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    check (editor != nullptr && editor->getWidth() > 0, (prefix + "editor opens").toRawUTF8());
}

int snapshot (const juce::String& path, const juce::String& roleName, const juce::String& option)
{
    const Role role = roleName == "bus" ? Role::bus : Role::channel;
    AnalyzerProcessor p (role);
    p.setPlayConfigDetails (2, 2, rate, 256);
    p.prepareToPlay (rate, 256);
    if (role == Role::channel && option == "send")
        setParameter (p, ids::mode, (float) ChannelMode::sendOnly);
    if (option == "mono")
        p.setMonoMonitor (true);

    // A wide pad over a mono bass, so both scores have something to show.
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 200; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            const int n = block * 256 + i;
            const float bass = 0.4f * sine (55, n);
            buffer.setSample (0, i, bass + 0.2f * sine (660, n));
            buffer.setSample (1, i, bass + 0.2f * sine (660, n + 30));
        }
        p.processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    for (int i = 0; i < 3; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, out))
    {
        std::printf ("could not write %s\n", path.toRawUTF8());
        return 1;
    }
    std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc >= 4 && juce::String (argv[1]) == "--snapshot")
        return snapshot (argv[2], argv[3], argc >= 5 ? juce::String (argv[4]) : juce::String());

    testScoreFormula();
    testBandScore();
    testProcessor (Role::channel, "Channel Analyzer");
    testProcessor (Role::bus, "Bus Analyzer");

    if (failures == 0)
        std::printf ("\nall tests passed\n");
    else
        std::printf ("\n%d test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
