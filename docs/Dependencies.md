# PhaseLab – Dependencies and licenses

PhaseLab's own code is released into the public domain under the [Unlicense](../LICENSE). That does
not change the terms of the parts below: a build of the plugins contains them and is bound by their
licenses too. This list is the dependency inventory of the specification (open point O-01); it is
not legal advice.

| Component | Version | Source | Used for | License |
|---|---|---|---|---|
| JUCE | 9.0.3 (tag) | github.com/juce-framework/JUCE | plugin framework, GUI, formats | AGPLv3 or commercial JUCE 9 licence – PhaseLab uses the AGPLv3 route |
| VST3 SDK | bundled with JUCE 9.0.3 | Steinberg, via JUCE | VST3 format | MIT (checked in the copy inside JUCE 9.0.3) |
| clap-juce-extensions | 7adee3a1bd4684d4caa5601100e364abccff4b4d | github.com/free-audio/clap-juce-extensions | CLAP format | MIT |
| CLAP SDK, clap-helpers | as pinned by clap-juce-extensions | github.com/free-audio/clap | CLAP format | MIT |

Fetched by CMake into the build tree at configure time, never committed.

## What the AGPLv3 route means

- Anyone who gets a PhaseLab binary may also get its complete source, under the AGPLv3. As the
  repository's code is public domain and JUCE's source is public, that is already covered once the
  repository is published; a release has to point to the source.
- PhaseLab's own files stay public domain and can be used in other projects without JUCE under any
  terms. Whoever uses them together with JUCE is bound by JUCE's license for that build.
- Fonts, icons and test signals that are added later go into this list with their licenses.

## Before a public release

1. Check the license files of the pinned versions (JUCE `LICENSE.md`, the VST3 SDK inside JUCE,
   clap-juce-extensions and CLAP `LICENSE`) and copy the required notices into the packages.
2. Name JUCE and the AGPLv3 in the About text and in the release notes.
3. Record each new dependency here with version, source, purpose and license.
