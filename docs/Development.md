# PhaseLab – Development

## Building

Needs CMake ≥ 3.22, a C++20 compiler and Ninja (optional). JUCE 9.0.3 and clap-juce-extensions are
fetched by CMake (pinned, see [Dependencies.md](Dependencies.md)).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/PhaseLabTests_artefacts/Release/PhaseLabTests
```

The plugins are copied into the user plugin folders after building (on Windows the VST3s go to
`Common Files\VST3`; `-DPHASELAB_COPY_PLUGIN=OFF` to skip). Everything is also in
`build/PhaseLabChannel_artefacts/Release/` and `build/PhaseLabBus_artefacts/Release/` (VST3, CLAP;
AU on macOS).

The test runner checks the mono score formula against its reference cases (identical, inverted, one
channel, uncorrelated noise, silence), the band score, bit-identical pass-through for several block
sizes, the *Send only* mode, the mono monitor, state recall (the mono monitor is never restored),
the supported bus layouts and that the editor opens. The link tests drive Channel Analyzers with
their own playheads and check that known offsets (0, ±1, 20 samples) come out exactly with different
block sizes and callback orders, polarity and sum score, stopped transport, seek, loop passes, a
missing or stale source, group separation, slot conflicts, ring overflow, the offline lock, different
sample rates and a source in a **second process** (the runner starts itself with `--link-child`).
The measurement tests compare against reference cases: 1 kHz at −23 and −33 dBFS reads −23.0/−33.0
LUFS (EBU Tech 3341), 20 s at −20 then −30 dBFS gives LRA 10 LU (EBU Tech 3342), an fs/4 sine at 45°
shows its inter-sample true peak, peak/RMS of a sine, spectrum level, a resonance in noise, band
correlation, mono loss and phase (one sample of delay = −7.5° at 1 kHz), stereo values, history with
bar positions, FIFO overflow and that display analysis only runs with an open editor. The advice and
every panel state (both roles, both languages, send only, mono monitor) are painted once.
`docs/Hosttest.md` lists the checks that need a real DAW.

`PhaseLabTests --snapshot <png> channel|bus [option-option…]` renders the panel at design size after
feeding 4 s of a test signal; options `linked` (a Bus Analyzer with two sources, B 20 samples late),
`send`, `mono`, `en`. The screenshots in `docs/images/` are made this way.

### Build matrix

| Platform | Formats | Built | Tested |
|---|---|---|---|
| Windows 11 x64 (MSVC 14.51, Ninja) | VST3, CLAP | yes, locally | own tests incl. cross-process link, pluginval strictness 10 (VST3) |
| macOS arm64 (Apple Silicon) | AU, VST3, CLAP | not yet (release workflow / `tools/release-mac.sh`) | – |
| Linux x64 (Ubuntu 24.04) | VST3, CLAP | not yet (release workflow) | – |

The minimum OS versions, the Linux distributions and the host versions are still open
(open point O-10 of the specification).

## Code

- `Source/core/` – the analysis core, without JUCE and without allocation after `prepare`.
  `MonoScore.*`: the draft mono score formula of the specification, a 24 dB Linkwitz-Riley low-pass
  for the band score and a meter that publishes both scores every 400 ms.
- `Source/core/Loudness.*` – K-weighting, momentary/short-term/integrated loudness with gating, loudness
  range (histograms, so integration never allocates), sample and true peak (4x windowed-sinc
  interpolator), RMS. `Source/core/Spectrum.*` – own radix-2 FFT (`FFT.*`), 4096-point Hann spectra of a
  pair of signals with mid and side, third-octave correlation, phase and mono loss, resonance search.
  `Source/core/Stereo.*` – L/R correlation, side share, balance, goniometer points, 90 s history.
- `Source/core/Timeline.*` – recent audio of one source by timeline position, with run key and write
  time per sample. `Source/core/ABAnalysis.*` – A/B correlation, offset search (normalised
  cross-correlation, ±10 ms), polarity hint, sum score A+B below the cutoff.
- `Source/transport/` – the link (no JUCE). `SharedRegion` is named shared memory (Windows file
  mapping in `Local\`, POSIX `shm_open`), so it works across plugin binaries and processes. `Link.*`:
  one slot per group (1–4) and source (A/B) with a lock-free ring of blocks stamped with the host
  timeline position, run (epoch) and writer generation; the writer claims a slot by compare-and-swap,
  a second writer is refused. The layout and its names (`pl1g1A` …) are versioned – change
  `layoutVersion` when the layout changes.
- `Source/shared/BusLink.*` – the Bus Analyzer's worker thread: polls slots A and B every 50 ms, joins
  the newest 400 ms both cover from one continuous run each, decides the validity state and runs the
  A/B analysis. Only `valid` shows A/B values.
- `Source/shared/AnalysisEngine.*` – the worker of each plugin's own input: the audio callback only
  copies into a lock-free FIFO; metering, stereo, history and – only while an editor is open –
  spectrum, goniometer and waveform run here. A snapshot for the editor is published about 25 times
  a second. A full FIFO is reported as a gap, never waited for.
- `Source/shared/Parameters.*` – every host parameter. **The IDs are a contract with saved
  projects: never rename or remove one, only add.**
- `Source/shared/AnalyzerProcessor.*` – the processor of both plugins (`Role::channel` or
  `Role::bus`): unchanged pass-through, 0 samples latency, mono or stereo in = out. The analysis reads
  the input before the mono monitor. The mono monitor is not a host parameter and is switched off
  whenever a project is loaded, so no export turns mono by accident.
- `Source/shared/AnalyzerEditor.*` – hosts the panel at its design size (1100 × 780) and scales it as a
  whole with the window (60–150 %).
- `Source/ui/` – `Panel.*` the instrument view of both plugins (after the UI preview of the
  specification), `Advice.*` the plain-language hints, `Look.*` colours, controls and the German/English
  text helper. The language is stored in the plugin state, not as a host parameter.
- `Source/channel/`, `Source/bus/` – the plugin entry points.

The audio callback only accumulates the mono score (a few multiplications per sample), copies the
block into the analysis FIFO and, in the Channel Analyzer, into the shared ring – no allocation, no
locks, no system calls.
Shared memory is opened on the message thread (timer, prepare, state load), never on the audio
thread. The A/B analysis runs in the Bus Analyzer's worker thread.

## Releases

Builds run only on demand (pushes don't trigger them, to save GitHub Actions minutes). To publish a
new version, bump `VERSION` in `CMakeLists.txt`, commit and push, then:

- **On a Mac:** `tools/release-mac.sh 0.1.0` builds, tests and validates the macOS plugins locally as
  Apple Silicon builds, creates a draft release with them, then lets GitHub build only Linux and
  Windows and publish. `--dry-run` stops after packaging.
- **On a PC:** `gh workflow run build -f version=0.1.0` (or Actions → build → Run workflow) builds
  all three platforms on GitHub.

Each run checks the version, scans for sensitive data, builds both plugins as AU (macOS), VST3 and
CLAP, runs the tests (on Linux in a virtual display, since they open the editor) and publishes
release `v<version>`. macOS builds for Apple Silicon only (D-01).

## Validation

```bash
pluginval --strictness-level 10 --validate-in-process --validate "PhaseLab Channel Analyzer.vst3"
pluginval --strictness-level 10 --validate-in-process --validate "PhaseLab Bus Analyzer.vst3"
```

Both passed on Windows. On macOS also `auval -v aufx PlCh Sbli`, `auval -v aufx PlBs Sbli` and
`clap-validator validate` for both CLAPs.

`tools/check-sensitive-data.sh` (needs ripgrep) scans the tracked files for credentials and local
paths; the release workflow runs it too. `.githooks/pre-push` runs it before every push once enabled
with `git config core.hooksPath .githooks`.

## Identifiers

| | Channel Analyzer | Bus Analyzer |
|---|---|---|
| Product name | PhaseLab Channel Analyzer | PhaseLab Bus Analyzer |
| Manufacturer code | Sbli | Sbli |
| Plugin code | PlCh | PlBs |
| Bundle / CLAP ID | io.github.sscheppert-blip.phaselab.channel | io.github.sscheppert-blip.phaselab.bus |

These identify the plugins in saved projects: never change them after the first release.
