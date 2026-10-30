# PhaseLab

Phase and mono analysis for music production, as two plugins: the **Channel Analyzer** goes on a
track, the **Bus Analyzer** on a bus and compares two tracks – kick and bass, two mics on one drum,
DI and amp. PhaseLab shows phase relations, cancellations, mono compatibility, narrow resonances,
the stereo image and loudness, and explains in plain words what to check. It only analyses – the
audio passes through unchanged. VST3, CLAP and AU, built with JUCE.

> **Status: version 0.1.1.** Everything described here is built and passes its own tests (including
> EBU loudness reference cases and a link across two processes) and pluginval at strictness 10 on
> Windows. Not yet tested in a DAW, macOS and Linux builds come from the release workflow
> ([host test](docs/Hosttest.md)).

**Manual:** [English](docs/Manual.md) · [Deutsch](docs/Handbuch.md)

![Bus Analyzer comparing kick and bass](docs/images/bus_en.png)

## Features

- **Two plugins:** the Channel Analyzer analyses one track and can send it to the Bus Analyzer as
  source A or B of a group (*Analyse*, *Analyse + send* or *Send only* – the last one switches the
  analysis off to save CPU). The Bus Analyzer is the main instance and compares A with B.
- **A/B comparison, sample-accurate where it can be proven:** both sources are matched by host
  timeline position – also across separate plugin processes. Correlation, time offset in samples with
  a hint like "move track B 20 samples earlier", polarity hint, phase of B against A per third
  octave, spectra of A and B and how well they add up in the low end. Comparisons that cannot be
  synchronised reliably are shown as invalid instead of showing wrong values.
- **Mono compatibility:** mono score 0–100 for the full band and below a cutoff you can move
  (40–500 Hz, default 120 Hz), correlation per third octave (green in phase, red cancelling) and a
  spectrum of what the mono sum loses
- **Spectrum** with the most striking narrow resonance marked, mid/side view, dual waveform with a
  zoom from 2 to 400 ms, an oscilloscope-style trigger and switches to hide A or B
- **Stereo:** goniometer, L/R correlation, side share, balance
- **Loudness after BS.1770 / EBU R128:** peak, true peak, RMS, LUFS short-term and integrated, LRA
- **Loudness timeline** like a loudness meter: short-term loudness as a filled area with momentary and
  integrated loudness, or level (peak envelope and RMS), or correlation – over the last 30/60/90 s or
  the whole song (up to two hours), labelled with minutes and bars
- **Clipping** in red: clip LEDs, a counter of clips and clipped samples, red marks in the history
- **Hints in plain words** – what was measured, where, and what to check or listen to; never an
  automatic change
- **Mono monitor:** the only switch that changes the audio – sums the output to mono for listening,
  off by default and never saved with the project
- German and English user interface, window scales from 60 to 150 %

![Channel Analyzer](docs/images/channel_en.png)

## Platforms

Windows 10/11 x64, Linux x64, macOS on Apple Silicon. Planned test hosts: Ableton Live, FL Studio,
Bitwig Studio, REAPER, Waveform Free.

**macOS:** the builds are not notarized by Apple, so macOS may block them; the manual explains the
one-line fix ([English](docs/Manual.md#macos) · [Deutsch](docs/Handbuch.md#macos)).

## License

Public domain ([Unlicense](LICENSE)) – do whatever you like with it, no credit needed.
The plugins are built with [JUCE](https://juce.com), which has its own license terms
([dependencies](docs/Dependencies.md)).

<sub>Building from source: [docs/Development.md](docs/Development.md)</sub>
