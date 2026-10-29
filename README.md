# PhaseLab

Phase and mono analysis for music production, as two plugins: the **Channel Analyzer** goes on a
track, the **Bus Analyzer** on a bus and compares two tracks. PhaseLab shows phase relations,
possible cancellations, mono compatibility, striking resonances and the stereo image, and explains
in plain words what to check. It only analyses – the audio passes through unchanged. VST3, CLAP and
AU, built with JUCE.

> **Status: P1 – feasibility prototype, version 0.0.1.** Both plugins build, load and pass audio
> through bit-identically (own tests, pluginval at strictness 10 on Windows). Channel Analyzers send
> to the Bus Analyzer through shared memory – also across separate plugin processes – and the Bus
> Analyzer joins A and B on the host timeline: correlation, time offset in samples with a hint,
> polarity hint and how well A and B add up in the low end, plus the mono scores and the mono
> monitor. The spectrum, waveforms, goniometer, metering and history are not built yet. Not tested in
> a DAW so far ([host test](docs/Hosttest.md)).

## Planned features

- **Two plugins:** the Channel Analyzer analyses one track and can send it to the Bus Analyzer as
  source A or B (*Analyse*, *Analyse + Send* or *Send only* – the last one switches the analysis off
  to save CPU). The Bus Analyzer is the main instance and compares A with B.
- **Kick vs. bass and more:** dual waveform, phase relation, correlation per frequency band, time
  offset in samples and milliseconds with a hint like "move track B 20 samples earlier"
- **Mono compatibility:** mono score 0–100 for the full band and below a cutoff you can move
  (default 120 Hz), a spectrum of what the mono sum loses, and how well A and B add up in the low end
- **Stereo:** stereo imager, goniometer, L/R correlation
- **Metering:** peak, true peak, RMS, LUFS, LRA
- **History:** 30/60/90 seconds, labelled with bar and beat where the host provides them
- **Mono monitor:** the only switch that changes the audio – sums the output to mono for listening,
  off by default and never saved with the project
- **Sample-accurate where it can be proven:** comparisons that cannot be synchronised reliably are
  marked invalid instead of showing wrong values
- German and English user interface

## Platforms

Windows 10/11 x64, Linux x64, macOS on Apple Silicon. Planned test hosts: Ableton Live, FL Studio,
Bitwig Studio, REAPER, Waveform Free.

## License

Public domain ([Unlicense](LICENSE)) – do whatever you like with it, no credit needed.
The plugins are built with [JUCE](https://juce.com), which has its own license terms
([dependencies](docs/Dependencies.md)).

<sub>Building from source: [docs/Development.md](docs/Development.md)</sub>
