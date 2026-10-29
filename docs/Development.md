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
the supported bus layouts and that the editor opens.
`PhaseLabTests --snapshot <png> channel|bus [send|mono]` renders a panel after feeding a test signal.

### Build matrix

| Platform | Formats | Built | Tested |
|---|---|---|---|
| Windows 11 x64 (MSVC 14.51, Ninja) | VST3, CLAP | yes, locally | own tests, pluginval strictness 10 (VST3) |
| macOS arm64 + x86_64 (universal) | AU, VST3, CLAP | not yet (release workflow / `tools/release-mac.sh`) | – |
| Linux x64 (Ubuntu 24.04) | VST3, CLAP | not yet (release workflow) | – |

The minimum OS versions, the Linux distributions and the host versions are still open
(open point O-10 of the specification).

## Code

- `Source/core/` – the analysis core, without JUCE and without allocation after `prepare`.
  `MonoScore.*`: the draft mono score formula of the specification, a 24 dB Linkwitz-Riley low-pass
  for the band score and a meter that publishes both scores every 400 ms.
- `Source/shared/Parameters.*` – every host parameter. **The IDs are a contract with saved
  projects: never rename or remove one, only add.**
- `Source/shared/AnalyzerProcessor.*` – the processor of both plugins (`Role::channel` or
  `Role::bus`): unchanged pass-through, 0 samples latency, mono or stereo in = out. The analysis reads
  the input before the mono monitor. The mono monitor is not a host parameter and is switched off
  whenever a project is loaded, so no export turns mono by accident.
- `Source/shared/AnalyzerEditor.*` – the P0 panel (scores, cutoff, mode/slot, mono monitor). The full
  instrument view follows the UI preview of the specification.
- `Source/channel/`, `Source/bus/` – the plugin entry points.

For P0 the score is accumulated in the audio callback (a few multiplications per sample, no
allocation, no locks). The link transport and the worker thread for the heavier analysis come with
the P1 prototype.

## Releases

Builds run only on demand (pushes don't trigger them, to save GitHub Actions minutes). To publish a
new version, bump `VERSION` in `CMakeLists.txt`, commit and push, then:

- **On a Mac:** `tools/release-mac.sh 0.1.0` builds, tests and validates the macOS plugins locally as
  universal binaries, creates a draft release with them, then lets GitHub build only Linux and
  Windows and publish. `--dry-run` stops after packaging.
- **On a PC:** `gh workflow run build -f version=0.1.0` (or Actions → build → Run workflow) builds
  all three platforms on GitHub.

Each run checks the version, scans for sensitive data, builds both plugins as AU (macOS), VST3 and
CLAP, runs the tests (on Linux in a virtual display, since they open the editor) and publishes
release `v<version>`. The workflow has not been run yet.

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
