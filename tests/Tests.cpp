// Offline tests for the analysis core, the shared processor and the link (P1 harness).
//   PhaseLabTests                               run all tests
//   PhaseLabTests --snapshot <png> channel|bus [send|mono|linked|en|level|loudness|clip, combinable: linked-en]
//                                               render the panel after feeding a test signal
//   PhaseLabTests --link-child <prefix> <seconds> <delay>
//                                               internal: second process sending source B

#include "core/MonoScore.h"
#include "shared/AnalyzerProcessor.h"
#include "core/Loudness.h"
#include "core/Spectrum.h"
#include "core/Stereo.h"
#include "shared/AnalyzerEditor.h"
#include "transport/Link.h"
#include "ui/Advice.h"

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

//==============================================================================
// Link: Channel Analyzer → Bus Analyzer (P1, docs/spec/08)
//==============================================================================

// Reproducible test signal: the same value for the same timeline position, in every process.
float noiseAt (std::int64_t t)
{
    std::uint64_t z = (std::uint64_t) t + 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z ^= z >> 31;
    return (float) ((double) (z >> 11) / 9007199254740992.0 - 0.5);
}

class TestHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        if (hasTime)
        {
            info.setTimeInSamples (timeline);
            info.setPpqPosition ((double) timeline / (rate / 2.0));   // 120 BPM
            info.setTimeSignature (TimeSignature { 4, 4 });
        }
        info.setIsPlaying (playing);
        return info;
    }

    std::int64_t timeline = 0;
    bool playing = true;
    bool hasTime = true;
};

// A Channel Analyzer with its own playhead, fed with a function of the timeline position.
struct Source
{
    Source (int slot, int groupIndex, std::function<float (std::int64_t)> f) : signal (std::move (f))
    {
        p.setPlayConfigDetails (2, 2, rate, 512);
        p.prepareToPlay (rate, 512);
        p.setPlayHead (&head);
        setParameter (p, ids::mode, (float) ChannelMode::sendOnly);
        setParameter (p, ids::slot, (float) slot);
        setParameter (p, ids::group, (float) groupIndex);
        p.maintainLink();
    }

    // Processes up to timeline position `until` in blocks of `block` samples.
    void runUntil (std::int64_t until, int block)
    {
        juce::MidiBuffer midi;
        while (position < until)
        {
            const int n = (int) std::min<std::int64_t> (block, until - position);
            juce::AudioBuffer<float> buffer (2, n);
            for (int i = 0; i < n; ++i)
            {
                const float v = signal (position + i);
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            head.timeline = position;
            p.processBlock (buffer, midi);
            position += n;
        }
    }

    AnalyzerProcessor p { Role::channel };
    TestHead head;
    std::function<float (std::int64_t)> signal;
    std::int64_t position = 0;
};

struct Bus
{
    Bus()
    {
        p.setPlayConfigDetails (2, 2, rate, 512);   // no prepareToPlay: the tests drive step() themselves
    }
    BusLinkStatus step() { p.getBusLink()->step (link::nowMs()); return p.getLinkStatus(); }
    AnalyzerProcessor p { Role::bus };
};

// Runs A and B to `end` with different block sizes, B first, and returns the bus result.
BusLinkStatus runPair (Source& a, Source& b, Bus& bus, std::int64_t end, int blockA = 256, int blockB = 100)
{
    for (std::int64_t t = a.position; t < end;)
    {
        t = std::min (end, t + 4096);
        b.runUntil (t, blockB);   // inverted callback order: the later source is processed first
        a.runUntil (t, blockA);
        bus.step();
    }
    return bus.step();
}

void testLinkSync()
{
    // Known offsets survive the join exactly, independent of block split and callback order (T-07).
    for (int delay : { 0, 1, -1, 20 })
    {
        Source a (0, 0, [] (std::int64_t t) { return noiseAt (t); });
        Source b (1, 0, [delay] (std::int64_t t) { return noiseAt (t - delay); });
        Bus bus;
        const auto s = runPair (a, b, bus, 48000);
        const auto what = "link: offset of " + juce::String (delay) + " samples is measured exactly (VALID)";
        check (s.state == LinkState::valid && s.result.clearOffset && s.result.offsetSamples == delay
                   && s.result.offsetCorrelation > 0.99,
               what.toRawUTF8());
    }

    {
        Source a (0, 0, [] (std::int64_t t) { return noiseAt (t); });
        Source b (1, 0, [] (std::int64_t t) { return -noiseAt (t); });
        Bus bus;
        const auto s = runPair (a, b, bus, 48000, 512, 64);
        check (s.state == LinkState::valid && s.result.polarityHint && s.result.offsetSamples == 0
                   && s.result.correlation < -0.99 && s.result.sumScore == 0,
               "link: opposite polarity gives a polarity hint and sum score 0");
    }

    {
        Source a (0, 0, [] (std::int64_t t) { return 0.4f * sine (60, (int) t); });
        Source b (1, 0, [] (std::int64_t t) { return 0.4f * sine (60, (int) t); });
        Bus bus;
        const auto s = runPair (a, b, bus, 48000);
        check (s.state == LinkState::valid && s.result.sumScore >= 99, "link: identical bass adds up fully (sum score 100)");
    }

    {
        // Real kick/bass material is often unrelated: that is no error, just no offset to name.
        Source a (0, 0, [] (std::int64_t t) { return noiseAt (t); });
        Source b (1, 0, [] (std::int64_t t) { return noiseAt (t + 1000003); });
        Bus bus;
        const auto s = runPair (a, b, bus, 48000);
        check (s.state == LinkState::valid && ! s.result.clearOffset, "link: unrelated signals name no offset");
    }
}

void testLinkStates()
{
    {
        Source a (0, 0, [] (std::int64_t t) { return noiseAt (t); });
        Bus bus;
        a.runUntil (24000, 256);
        check (bus.step().state == LinkState::disconnected, "link: only A present → disconnected");
    }

    {
        Source a (0, 0, [] (std::int64_t t) { return noiseAt (t); });
        Source b (1, 0, [] (std::int64_t t) { return noiseAt (t); });
        a.head.playing = b.head.playing = false;
        Bus bus;
        check (runPair (a, b, bus, 24000).state == LinkState::noHostTime, "link: stopped transport → no A/B comparison (T-08)");
    }

    {
        Source a (0, 0, [] (std::int64_t t) { return noiseAt (t); });
        Source b (1, 0, [] (std::int64_t t) { return noiseAt (t); });
        Bus bus;
        runPair (a, b, bus, 48000);
        // Seek: both jump to 10 s. The newest window is incomplete until 400 ms of the new run arrived.
        a.position = b.position = 480000;
        const auto afterSeek = runPair (a, b, bus, 480000 + 4800);
        const auto recovered = runPair (a, b, bus, 480000 + 24000);
        check (afterSeek.state == LinkState::gap && recovered.state == LinkState::valid,
               "link: seek invalidates the window, then recovers (T-08)");
    }

    {
        Source a (0, 0, [] (std::int64_t t) { return noiseAt (t); });
        Source b (1, 1, [] (std::int64_t t) { return noiseAt (t); });   // other group
        Bus bus;
        check (runPair (a, b, bus, 24000).state == LinkState::disconnected, "link: a source of another group is never used");
    }

    {
        Source a (0, 0, [] (std::int64_t t) { return noiseAt (t); });
        Source second (0, 0, [] (std::int64_t t) { return noiseAt (t); });   // also wants slot A
        a.runUntil (2048, 256);
        second.runUntil (2048, 256);
        const bool busy = second.p.getSendState() == SendState::busy && a.p.getSendState() == SendState::sending;
        setParameter (a.p, ids::mode, (float) ChannelMode::analyse);   // A stops sending and frees the slot
        a.runUntil (2304, 256);
        second.runUntil (2304, 256);
        check (busy && second.p.getSendState() == SendState::sending,
               "link: a second sender on the same slot is refused, takes over once it is free (T-09)");
    }

    // Raw writers with a fake clock for timing cases the processor cannot fake.
    link::SlotTable table;
    auto* slotA = table.acquire (2, 0);
    auto* slotB = table.acquire (2, 1);
    std::atomic<float> groupValue { 2.0f }, cutoffValue { 120.0f };

    auto writeRun = [] (link::Writer& w, link::SlotLayout& s, std::int64_t from, std::int64_t to, std::int64_t now,
                        double sampleRate = rate, bool offline = false)
    {
        std::vector<float> block (256);
        for (std::int64_t t = from; t < to; t += 256)
        {
            for (int i = 0; i < 256; ++i)
                block[(size_t) i] = noiseAt (t + i);
            link::BlockTiming timing;
            timing.hostTime = true;
            timing.timeline = t;
            timing.sampleRate = sampleRate;
            timing.offline = offline;
            w.write (s, block.data(), block.data(), 256, timing, now);
        }
    };

    {
        link::Writer wa (11), wb (12);
        BusLink bus (groupValue, cutoffValue);
        const auto t0 = link::nowMs();
        writeRun (wa, *slotA, 0, 48000, t0);
        writeRun (wb, *slotB, 0, 48000, t0);
        bus.step (t0);
        const bool validFirst = bus.getStatus().state == LinkState::valid;

        // Loop: A plays the first 0.5 s again 3 s later, B has not got there yet. Same positions,
        // different passes – must not be compared.
        writeRun (wa, *slotA, 0, 24064, t0 + 3000);
        bus.step (t0 + 3000);
        const auto loopState = bus.getStatus().state;

        bus.step (t0 + 3700);   // and then nothing for 700 ms
        const auto staleState = bus.getStatus().state;
        check (validFirst && loopState != LinkState::valid && staleState == LinkState::stale,
               "link: loop passes are never mixed; a silent source turns stale (T-08, T-09)");
        wa.release (*slotA);
        wb.release (*slotB);
    }

    {
        link::Writer wa (21), wb (22);
        BusLink bus (groupValue, cutoffValue);
        const auto t0 = link::nowMs() + 10000;   // past the staleness of the previous writers
        writeRun (wa, *slotA, 0, 24064, t0, rate, true);
        writeRun (wb, *slotB, 0, 24064, t0, rate, true);
        bus.step (t0);
        const bool offline = bus.getStatus().state == LinkState::offline;
        writeRun (wa, *slotA, 24064, 48128, t0, rate);
        writeRun (wb, *slotB, 24064, 48128, t0, 44100.0);
        bus.step (t0);
        check (offline && bus.getStatus().state == LinkState::rateMismatch,
               "link: offline rendering is locked, different sample rates are refused (T-12)");
        wa.release (*slotA);
        wb.release (*slotB);
    }

    {
        // Overflow: 4 s of audio before the bus looks once – more than the ring holds.
        link::Writer wa (31), wb (32);
        BusLink bus (groupValue, cutoffValue);
        const auto t0 = link::nowMs() + 20000;
        writeRun (wa, *slotA, 0, 192000, t0);
        writeRun (wb, *slotB, 0, 192000, t0);
        bus.step (t0);
        const auto s = bus.getStatus();
        check (s.state == LinkState::valid && s.result.offsetSamples == 0,
               "link: ring overflow drops old data safely, the newest window stays valid (T-09)");
        wa.release (*slotA);
        wb.release (*slotB);
    }
}

// Child process: sends source B with a known delay, paced like a realtime host.
// A group no other test uses: POSIX shared memory outlives its users, so an earlier test's blocks
// would still be in the slots of group 1.
constexpr int crossProcessGroup = 3;

int runLinkChild (const juce::String& prefix, int seconds, int delay)
{
    link::setNamespace (prefix.toStdString());
    link::SlotTable table;
    auto* slot = table.acquire (crossProcessGroup, 1);
    if (slot == nullptr)
        return 2;
    link::Writer writer ((std::uint64_t) juce::Random::getSystemRandom().nextInt64() | 1u);
    std::vector<float> block (256);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (std::int64_t t = 0; t < (std::int64_t) seconds * 48000; t += 256)
    {
        for (int i = 0; i < 256; ++i)
            block[(size_t) i] = noiseAt (t + i - delay);
        link::BlockTiming timing;
        timing.hostTime = true;
        timing.timeline = t;
        timing.sampleRate = rate;
        writer.write (*slot, block.data(), block.data(), 256, timing, link::nowMs());
        const double due = start + 1000.0 * (double) (t + 256) / rate;
        const double wait = due - juce::Time::getMillisecondCounterHiRes();
        if (wait > 1.0)
            juce::Thread::sleep ((int) wait);
    }
    writer.release (*slot);
    return 0;
}

void testLinkAcrossProcesses (const juce::String& prefix)
{
    // B comes from a separate process (sandboxed hosts, O-04), A from this one, both on the same timeline.
    constexpr int delay = 7;
    link::SlotTable table;
    auto* slotA = table.acquire (crossProcessGroup, 0);
    auto* slotB = table.acquire (crossProcessGroup, 1);
    const std::uint32_t generationBefore = slotB != nullptr ? slotB->generation.load() : 0;

    juce::ChildProcess child;
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    const bool started = child.start (juce::StringArray { exe, "--link-child", prefix, "3", juce::String (delay) }, 0);

    std::atomic<float> groupValue { (float) crossProcessGroup }, cutoffValue { 120.0f };
    BusLink bus (groupValue, cutoffValue);
    link::Writer writer (41);
    std::vector<float> block (256);
    std::int64_t position = 0;
    BusLinkStatus best;
    bool otherProcess = false;

    const auto until = juce::Time::getMillisecondCounter() + 5000;
    while (started && slotA != nullptr && slotB != nullptr && juce::Time::getMillisecondCounter() < until)
    {
        // Follow B like a host would process both tracks in the same cycle – only blocks of the child.
        const std::uint32_t generation = slotB->generation.load();
        const std::uint64_t records = slotB->recordsWritten.load();
        const auto& last = slotB->records[(records - 1) % link::recordCount];
        if (records > 0 && generation != generationBefore && last.generation.load() == generation)
        {
            otherProcess = true;
            const std::int64_t end = last.timeline.load() + (std::int64_t) last.length.load();
            for (; position + 256 <= end; position += 256)
            {
                for (int i = 0; i < 256; ++i)
                    block[(size_t) i] = noiseAt (position + i);
                link::BlockTiming timing;
                timing.hostTime = true;
                timing.timeline = position;
                timing.sampleRate = rate;
                writer.write (*slotA, block.data(), block.data(), 256, timing, link::nowMs());
            }
        }
        bus.step (link::nowMs());
        const auto s = bus.getStatus();
        if (s.state == LinkState::valid)
            best = s;
        if (best.state == LinkState::valid && position > 72000)
            break;
        juce::Thread::sleep (5);
    }
    writer.release (*slotA);
    child.waitForProcessToFinish (6000);

    const bool ok = started && otherProcess && best.state == LinkState::valid && best.result.offsetSamples == delay;
    if (! ok)
        std::printf ("      started=%d slots=%d/%d otherProcess=%d last state=%d best offset=%d position=%lld child exit=%u\n",
                     (int) started, (int) (slotA != nullptr), (int) (slotB != nullptr), (int) otherProcess,
                     (int) bus.getStatus().state, best.result.offsetSamples, (long long) position,
                     (unsigned) child.getExitCode());
    check (ok, "link: a source in another process is joined sample-accurately (O-04)");
}

//==============================================================================
// Measurement core (P2): references from BS.1770 / EBU 3341 / 3342 and synthetic cases
//==============================================================================

using StereoSignal = std::function<void (int, float&, float&)>;

void runLevels (LevelMeter& m, const StereoSignal& s, int length)
{
    std::vector<float> l (1024), r (1024);
    for (int pos = 0; pos < length; pos += 1024)
    {
        const int n = std::min (1024, length - pos);
        for (int i = 0; i < n; ++i)
            s (pos + i, l[(size_t) i], r[(size_t) i]);
        m.process (l.data(), r.data(), n);
    }
}

SpectrumResult spectrumOf (const StereoSignal& s, int frames = 24)
{
    SpectrumAnalyzer a;
    a.prepare (rate);
    std::vector<float> l ((size_t) spectrumSize), r ((size_t) spectrumSize);
    for (int f = 0; f < frames; ++f)
    {
        for (int i = 0; i < spectrumSize; ++i)
            s (f * spectrumSize / 4 + i, l[(size_t) i], r[(size_t) i]);
        a.addFrame (l.data(), r.data(), -1.0);
    }
    return a.result();
}

int bandOf (double hz)
{
    int best = 0;
    for (int b = 0; b < bandCount; ++b)
        if (std::abs (std::log (bandCentre (b) / hz)) < std::abs (std::log (bandCentre (best) / hz)))
            best = b;
    return best;
}

void testLoudness()
{
    const float a23 = (float) std::pow (10.0, -23.0 / 20.0);
    {
        LevelMeter m;
        m.prepare (rate);
        runLevels (m, [a23] (int i, float& l, float& r) { l = r = a23 * sine (1000, i); }, 20 * 48000);
        const auto v = m.values();
        check (std::abs (v.integrated + 23.0) < 0.1 && std::abs (v.momentary + 23.0) < 0.1 && std::abs (v.shortTerm + 23.0) < 0.1,
               "loudness: 1 kHz at -23 dBFS stereo reads -23.0 LUFS (EBU 3341 case 1)");
    }
    {
        LevelMeter m;
        m.prepare (44100.0);
        const float a33 = (float) std::pow (10.0, -33.0 / 20.0);
        std::vector<float> l (1024), r (1024);
        for (int pos = 0; pos < 20 * 44100; pos += 1024)
        {
            for (int i = 0; i < 1024; ++i)
                l[(size_t) i] = r[(size_t) i] = a33 * (float) std::sin (2.0 * pi * 1000.0 * (pos + i) / 44100.0);
            m.process (l.data(), r.data(), 1024);
        }
        check (std::abs (m.values().integrated + 33.0) < 0.1, "loudness: -33 dBFS at 44.1 kHz reads -33.0 LUFS (EBU 3341 case 2)");
    }
    {
        LevelMeter m;
        m.prepare (rate);
        const float a20 = (float) std::pow (10.0, -20.0 / 20.0), a30 = (float) std::pow (10.0, -30.0 / 20.0);
        runLevels (m, [a20, a30] (int i, float& l, float& r) { l = r = (i < 20 * 48000 ? a20 : a30) * sine (1000, i); }, 40 * 48000);
        check (std::abs (m.values().range - 10.0) < 1.0, "loudness: 20 s at -20 then -30 dBFS gives LRA 10 LU (EBU 3342 case 1)");
    }
    {
        LevelMeter m;
        m.prepare (rate);
        // fs/4 with a 45° phase: every sample is at ±0.707, the waveform between them reaches 1.0.
        runLevels (m, [] (int i, float& l, float& r) { l = r = (float) std::sin (pi / 2.0 * i + pi / 4.0); }, 48000);
        const auto v = m.values();
        check (std::abs (v.peak + 3.01) < 0.05 && std::abs (v.truePeak) < 0.5, "true peak finds the inter-sample peak (sample -3 dB, true ~0 dBTP)");
    }
    {
        LevelMeter m;
        m.prepare (rate);
        runLevels (m, [] (int i, float& l, float& r) { l = r = 0.5f * sine (1000, i); }, 48000);
        const auto v = m.values();
        check (std::abs (v.rms + 9.03) < 0.1 && std::abs (v.peak + 6.02) < 0.05, "peak and RMS of a sine at amplitude 0.5 (-6.0 / -9.0 dBFS)");
    }
    {
        LevelMeter m;
        m.prepare (rate);
        check (m.values().integrated <= noLevel && m.values().peak <= noLevel, "loudness: no value before any audio");
    }
    {
        // Three bursts above full scale on the left, one on the right: 4 clips, every clipped sample counted.
        LevelMeter m;
        m.prepare (rate);
        runLevels (m, [] (int i, float& l, float& r)
        {
            const bool burst = (i >= 1000 && i < 1010) || (i >= 5000 && i < 5005) || (i >= 9000 && i < 9001);
            l = burst ? 1.2f : 0.5f;
            r = (i >= 20000 && i < 20004) ? -1.0f : 0.5f;
        }, 48000);
        const auto v = m.values();
        check (v.clipsLeft == 3 && v.clipsRight == 1 && v.clippedSamples == 20, "clipping: clips and clipped samples are counted per channel");
        m.reset();
        check (m.values().clips() == 0 && m.values().clippedSamples == 0, "clipping: reset clears the count");
    }
}

void testSpectrum()
{
    {
        const auto s = spectrumOf ([] (int i, float& l, float& r) { l = r = 0.5f * sine (1000, i); });
        int nearest = 0;
        for (int p = 0; p < displayPoints; ++p)
            if (std::abs (displayFrequency (p) - 1000.0) < std::abs (displayFrequency (nearest) - 1000.0))
                nearest = p;
        float top = -200.0f;
        for (int p = nearest - 2; p <= nearest + 2; ++p)
            top = std::max (top, s.mid[(size_t) p]);
        check (std::abs (top + 6.02f) < 1.0f, "spectrum: a sine of amplitude 0.5 shows at about -6 dB");
    }
    {
        std::mt19937 rng (3);
        std::normal_distribution<float> noise (0.0f, 0.02f);
        const auto s = spectrumOf ([&] (int i, float& l, float& r) { l = r = noise (rng) + 0.3f * sine (2400, i); });
        check (s.resonanceHz > 2350.0 && s.resonanceHz < 2450.0 && s.resonanceDb >= 6.0, "resonance: a narrow peak at 2.4 kHz is found");
    }
    {
        const auto same = spectrumOf ([] (int i, float& l, float& r) { l = r = noiseAt (i); });
        const auto inverted = spectrumOf ([] (int i, float& l, float& r) { l = noiseAt (i); r = -l; });
        const auto wide = spectrumOf ([] (int i, float& l, float& r) { l = noiseAt (i); r = noiseAt (i + 7777777); });
        bool sameOk = true, invertedOk = true;
        double wideAbs = 0.0;
        int n = 0;
        for (int b = 3; b < bandCount - 2; ++b)
        {
            sameOk = sameOk && same.correlation[(size_t) b] > 0.99 && same.monoLoss[(size_t) b] > -0.1;
            invertedOk = invertedOk && inverted.correlation[(size_t) b] < -0.99 && inverted.monoLoss[(size_t) b] < -30.0;
            wideAbs += std::abs (wide.correlation[(size_t) b]);
            ++n;
        }
        check (sameOk, "bands: identical channels correlate +1 with no mono loss");
        check (invertedOk, "bands: opposite polarity correlates -1 and loses everything in mono");
        check (wideAbs / n < 0.3, "bands: unrelated channels show low correlation");
    }
    {
        // B one sample later: phase of B at 1 kHz is -360° · 1000 / 48000 = -7.5°.
        const auto s = spectrumOf ([] (int i, float& l, float& r) { l = noiseAt (i); r = noiseAt (i - 1); });
        check (std::abs (s.phase[(size_t) bandOf (1000.0)] + 7.5) < 2.0, "bands: one sample delay gives -7.5° phase at 1 kHz");
    }
}

void testStereoAndHistory()
{
    auto stereoOf = [] (const StereoSignal& s)
    {
        StereoMeter m;
        m.prepare (rate);
        std::vector<float> l (48000), r (48000);
        for (int i = 0; i < 48000; ++i)
            s (i, l[(size_t) i], r[(size_t) i]);
        m.process (l.data(), r.data(), 48000);
        return m.values();
    };
    const auto mono = stereoOf ([] (int i, float& l, float& r) { l = r = noiseAt (i); });
    const auto inverted = stereoOf ([] (int i, float& l, float& r) { l = noiseAt (i); r = -l; });
    const auto left = stereoOf ([] (int i, float& l, float& r) { l = noiseAt (i); r = 0.0f; });
    check (mono.correlation > 0.99 && mono.sidePercent < 0.1 && std::abs (mono.balanceDb) < 0.1, "stereo: mono signal → correlation +1, no side, centre");
    check (inverted.correlation < -0.99 && inverted.sidePercent > 99.9, "stereo: opposite polarity → correlation -1, all side");
    check (left.balanceDb < -20.0, "stereo: left only → balance far left");

    // Engine: history points every 100 ms with the bar position, overflow and display gating.
    MonoScoreMeter meter;
    AnalysisEngine engine (meter);
    engine.prepare (rate);
    std::vector<float> l (480), r (480);
    for (int block = 0; block < 100; ++block)   // 1 s
    {
        for (int i = 0; i < 480; ++i)
            l[(size_t) i] = r[(size_t) i] = noiseAt (block * 480 + i);
        BlockInfo info;
        info.playing = true;
        info.hasMusicalPosition = true;
        info.ppq = (block * 480.0) / 24000.0;   // 120 BPM
        engine.push (l.data(), r.data(), 480, info);
        if (block % 10 == 9)
            engine.processPending();
    }
    auto snapshot = std::make_unique<EngineSnapshot>();
    engine.copySnapshot (*snapshot);
    const auto& last = snapshot->history[(size_t) History::points - 1];
    check (last.valid && last.value > 0.99f && last.bar > 1.4 && last.bar < 1.6, "history: points every 100 ms carry correlation and bar position (bar 1.5 after 1 s)");
    check (last.peak > -7.0f && last.peak < -5.9f && last.rms > -20.0f && last.rms < -9.0f && last.momentary > noLevel && ! last.clipped,
           "history: points also carry peak, RMS and loudness");
    check (! snapshot->spectrum.valid && snapshot->wave.bins == 0, "display analysis stays off while no editor is open (D-29)");

    engine.setDisplayActive (true);
    for (int block = 0; block < 40; ++block)
    {
        for (int i = 0; i < 480; ++i)
            l[(size_t) i] = r[(size_t) i] = noiseAt (block * 480 + i);
        engine.push (l.data(), r.data(), 480, {});
    }
    engine.processPending();
    engine.copySnapshot (*snapshot);
    check (snapshot->spectrum.valid && snapshot->wave.bins > 0, "display analysis runs while an editor is open");

    // Waveform zoom: 2 ms shows single samples, 400 ms shows min/max per bin over the whole window.
    engine.setWaveformSeconds (0.002);
    engine.push (l.data(), r.data(), 480, {});
    engine.processPending();
    engine.copySnapshot (*snapshot);
    const bool shortWindow = snapshot->wave.bins == 96 && snapshot->wave.minA[5] == snapshot->wave.maxA[5];
    engine.setWaveformSeconds (0.4);
    engine.push (l.data(), r.data(), 480, {});
    engine.processPending();
    engine.copySnapshot (*snapshot);
    check (shortWindow && snapshot->wave.bins == waveformBins && std::abs (snapshot->wave.seconds - 0.4) < 0.001
               && snapshot->wave.maxA[100] > snapshot->wave.minA[100],
           "waveform zoom: 2 ms shows every sample, 400 ms the min/max of each bin");

    // Trigger: a 100 Hz sine starts every picture at its rising zero crossing, so it stands still.
    engine.setWaveformSeconds (0.004);   // 192 samples: one bin per sample
    engine.setWaveformTrigger (true);
    bool steady = true;
    for (int round = 0; round < 5; ++round)
    {
        for (int block = 0; block < 7; ++block)   // 7 × 480 samples: never a whole number of cycles per round
        {
            const int offset = (round * 7 + block) * 480 + 13;
            for (int i = 0; i < 480; ++i)
                l[(size_t) i] = r[(size_t) i] = 0.8f * sine (100, offset + i);
            engine.push (l.data(), r.data(), 480, {});
        }
        engine.processPending();
        engine.copySnapshot (*snapshot);
        const auto& wv = snapshot->wave;
        steady = steady && wv.triggered && std::abs (wv.maxA[0]) < 0.06f && wv.maxA[10] > wv.maxA[0];
    }
    engine.setWaveformTrigger (false);
    engine.push (l.data(), r.data(), 100, {});
    engine.processPending();
    engine.copySnapshot (*snapshot);
    check (steady && ! snapshot->wave.triggered, "waveform trigger: a periodic signal starts at its rising zero crossing every time");

    for (int block = 0; block < 400; ++block)   // 4 s without the worker: more than the FIFO holds
        engine.push (l.data(), r.data(), 480, {});
    engine.processPending();
    engine.copySnapshot (*snapshot);
    check (snapshot->overflow, "a worker that falls behind marks the gap instead of blocking the audio thread");
    engine.setDisplayActive (false);

    // Whole-song history: one point per second while the host plays, nothing while it is stopped.
    MonoScoreMeter songMeter;
    AnalysisEngine songEngine (songMeter);
    songEngine.prepare (rate);
    for (int block = 0; block < 500; ++block)   // 5 s: 3 s playing, then 2 s stopped and silent
    {
        const bool playing = block < 300;
        for (int i = 0; i < 480; ++i)
            l[(size_t) i] = r[(size_t) i] = playing ? noiseAt (block * 480 + i) : 0.0f;
        BlockInfo info;
        info.playing = playing;
        info.hasMusicalPosition = true;
        songEngine.push (l.data(), r.data(), 480, info);
        songEngine.processPending();
    }
    std::vector<History::Point> song;
    songEngine.copyLongHistory (song);
    check (song.size() == 3 && song.back().peak > -7.0f && song.back().momentary > noLevel,
           "song history: one point per second while playing, none while stopped");
}

void testAdviceAndPanel()
{
    auto snapshot = std::make_unique<EngineSnapshot>();
    snapshot->stereo.valid = true;
    snapshot->stereo.correlation = 0.6;
    const ui::OwnInput wideBass { *snapshot, 80, 40, 120.0 };
    const auto de = ui::adviseOwnInput (wideBass, false), en = ui::adviseOwnInput (wideBass, true);
    check (de.warning && de.title.contains ("monotauglich") && en.title.contains ("mono-safe"),
           "advice: a wide low end is named, in German and English");

    snapshot->levels.truePeak = -0.3;
    check (ui::adviseOwnInput (wideBass, false).title.contains ("True Peak"), "advice: true peak above -1 dBTP comes first");

    {
        auto monoTrack = std::make_unique<EngineSnapshot>();
        monoTrack->stereo.valid = true;
        monoTrack->stereo.correlation = 1.0;
        monoTrack->stereo.sidePercent = 0.0;
        const auto a = ui::adviseOwnInput ({ *monoTrack, 100, 100, 120.0 }, false);
        check (a.title.contains ("mono") && a.text.contains ("Panning"), "advice: an exactly mono track explains that the DAW pans after the plugin");
    }

    BusLinkStatus link;
    link.state = LinkState::valid;
    link.sampleRate = rate;
    link.result.enoughSignal = link.result.clearOffset = true;
    link.result.offsetSamples = 20;
    link.result.offsetCorrelation = 0.97;
    snapshot->levels.truePeak = -6.0;
    const auto bus = ui::adviseBus ({ *snapshot, 80, 90, 120.0 }, link, false);
    check (bus.text.contains ("20 Samples") && bus.text.contains ("vorziehen"), "advice: the bus names the offset and what to do");

    // Every panel state paints without trouble, in both languages.
    bool painted = true;
    for (auto role : { Role::channel, Role::bus })
        for (bool english : { false, true })
            for (int mode : { 0, 2 })
            {
                AnalyzerProcessor p (role);
                p.setPlayConfigDetails (2, 2, rate, 256);
                p.setEnglish (english);
                if (role == Role::channel)
                    setParameter (p, ids::mode, (float) mode);
                p.setMonoMonitor (mode == 2);
                p.getState().state.setProperty ("historyMode", (english ? 1 : 0) + (mode == 2 ? 1 : 0), nullptr);
                std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
                auto& panel = static_cast<AnalyzerEditor*> (editor.get())->getPanel();
                panel.refresh();
                panel.resized();
                const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 0.5f);
                painted = painted && image.isValid() && editor->getWidth() > 0;
            }
    check (painted, "panel paints in every role, mode and language");
}

//==============================================================================
int snapshot (const juce::String& path, const juce::String& roleName, const juce::String& option)
{
    const Role role = roleName == "bus" ? Role::bus : Role::channel;
    AnalyzerProcessor p (role);
    TestHead head;
    p.setPlayHead (&head);
    p.setPlayConfigDetails (2, 2, rate, 256);
    p.prepareToPlay (rate, 256);
    p.getEngine().stop();   // driven by hand below, not concurrently with the worker
    const auto options = juce::StringArray::fromTokens (option, "-", "");
    p.setEnglish (options.contains ("en"));
    if (role == Role::channel && options.contains ("send"))
    {
        setParameter (p, ids::mode, (float) ChannelMode::sendOnly);
        p.maintainLink();   // what the plugin's timer does a moment later
    }
    if (options.contains ("mono"))
        p.setMonoMonitor (true);
    if (options.contains ("level"))
        p.getState().state.setProperty ("historyMode", 1, nullptr);
    if (options.contains ("loudness"))
        p.getState().state.setProperty ("historyMode", 2, nullptr);
    if (options.contains ("corr"))
        p.getState().state.setProperty ("historyMode", 0, nullptr);
    const bool clip = options.contains ("clip");

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());   // display analysis on
    auto& panel = static_cast<AnalyzerEditor*> (editor.get())->getPanel();

    // Bus snapshot with a linked kick/bass pair, B 20 samples late.
    std::unique_ptr<Source> kick, bass;
    if (role == Role::bus && options.contains ("linked"))
    {
        kick = std::make_unique<Source> (0, 0, [] (std::int64_t t) { return 0.25f * noiseAt (t) * (t % 24000 < 3000 ? 1.0f : 0.15f) + 0.5f * sine (55, (int) t); });
        bass = std::make_unique<Source> (1, 0, [] (std::int64_t t) { return 0.12f * noiseAt (t - 20) + 0.45f * sine (55, (int) (t - 20)); });
        kick->runUntil (48000, 256);
        bass->runUntil (48000, 128);
        p.getBusLink()->stop();
        p.getBusLink()->step (link::nowMs());
    }

    // A wide pad with a resonance over a mono bass: 4 s, so loudness and history have something to show.
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    const int blocks = options.contains ("long") ? 3750 : 750;   // 20 s or 4 s
    const bool pulse = options.contains ("pulse");                // kick-like level every half second
    for (int block = 0; block < blocks; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            const int n = block * 256 + i;
            const float low = 0.35f * sine (55, n);
            const float pad = 0.12f * sine (660, n) + 0.05f * noiseAt (n);
            const float padR = 0.12f * sine (660, n + 30) + 0.05f * noiseAt (n + 99991);
            float boost = clip && (n / 24000) % 3 == 2 && n % 24000 < 1200 ? 2.4f : 1.0f;   // overloads now and then
            if (pulse)
                boost *= 0.35f + 0.9f * std::exp (-(float) (n % 24000) / 2400.0f);
            float left = boost * (low + pad + 0.06f * sine (2400, n));
            float right = boost * (low + padR + 0.06f * sine (2400, n));
            if (options.contains ("monotrack"))
                right = left;                       // a mono source: L = R
            if (options.contains ("hardleft"))
                right = 0.0f;                       // panned hard left
            buffer.setSample (0, i, left);
            buffer.setSample (1, i, right);
        }
        head.timeline = block * 256;
        p.processBlock (buffer, midi);
        if (block % 20 == 0)
            p.getEngine().processPending();
    }
    p.getEngine().processPending();

    panel.refresh();
    panel.resized();
    if (options.contains ("phase") || options.contains ("ms"))
        panel.showView (1);
    if (options.contains ("loss"))
        panel.showView (2);
    if (options.contains ("song"))
        panel.showHistory (0);
    if (options.contains ("zoomout") || options.contains ("zoomin"))
    {
        p.setWaveformSeconds (options.contains ("zoomout") ? 0.3 : 0.004);
        if (auto* busLink = p.getBusLink())
            busLink->step (link::nowMs());
        p.getEngine().processPending();
        panel.refresh();
    }
    const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
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
    if (argc >= 5 && juce::String (argv[1]) == "--link-child")
        return runLinkChild (argv[2], juce::String (argv[3]).getIntValue(), juce::String (argv[4]).getIntValue());

    juce::ScopedJuceInitialiser_GUI gui;

    // Own shared-memory names, so the tests never meet a running DAW's PhaseLab instances.
    const juce::String prefix = "t" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30));
    link::setNamespace (prefix.toStdString());

    if (argc >= 4 && juce::String (argv[1]) == "--snapshot")
        return snapshot (argv[2], argv[3], argc >= 5 ? juce::String (argv[4]) : juce::String());

    testScoreFormula();
    testBandScore();
    testProcessor (Role::channel, "Channel Analyzer");
    testProcessor (Role::bus, "Bus Analyzer");
    testLinkSync();
    testLinkStates();
    testLinkAcrossProcesses (prefix);
    testLoudness();
    testSpectrum();
    testStereoAndHistory();
    testAdviceAndPanel();

    if (failures == 0)
        std::printf ("\nall tests passed\n");
    else
        std::printf ("\n%d test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
