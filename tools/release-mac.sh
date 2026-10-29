#!/usr/bin/env bash
# Release from a Mac. The macOS packages are built and validated here (free), then GitHub
# Actions builds only Linux + Windows, adds them to the release and publishes it.
#
#   tools/release-mac.sh 0.1.0             build, draft release, start Linux/Windows, publish
#   tools/release-mac.sh 0.1.0 --dry-run   build, test and package only; nothing goes to GitHub
#
# Needs: Xcode command line tools, cmake, ninja, gh (logged in), ripgrep.
set -euo pipefail

die() { echo "release-mac: $*" >&2; exit 1; }

version="${1:-}"
dry_run=false
[[ "${2:-}" == "--dry-run" ]] && dry_run=true
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "usage: tools/release-mac.sh <version, e.g. 0.1.0>"
[[ "$(uname)" == "Darwin" ]] || die "macOS only; on other systems run: gh workflow run build -f version=$version"
tag="v$version"

cd "$(git rev-parse --show-toplevel)"

# ── preconditions ───────────────────────────────────────────────────────────
git fetch -q origin
[[ -z "$(git status --porcelain)" ]] || die "uncommitted changes – commit or stash first"
[[ "$(git rev-parse --abbrev-ref HEAD)" == "main" ]] || die "switch to main first"
if [[ "$dry_run" == false ]]; then
    [[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/main)" ]] || die "local main differs from origin/main – push or pull first"
    gh release view "$tag" >/dev/null 2>&1 && die "release $tag already exists"
fi
grep -Eq "^project\(PhaseLab VERSION ${version//./\\.} " CMakeLists.txt \
    || die "version $version does not match VERSION in CMakeLists.txt – bump it, commit and push first"
./tools/check-sensitive-data.sh

sha=$(git rev-parse HEAD)
build=build-release
dist=$build/dist

# ── build + test (universal binary) ─────────────────────────────────────────
echo "▸ building macOS packages for $tag ($sha)"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DPHASELAB_COPY_PLUGIN=OFF >/dev/null
cmake --build "$build" --target PhaseLabChannel_AU PhaseLabChannel_VST3 PhaseLabChannel_CLAP \
                                 PhaseLabBus_AU PhaseLabBus_VST3 PhaseLabBus_CLAP PhaseLabTests
"$build/PhaseLabTests_artefacts/Release/PhaseLabTests"

rm -rf "$dist" && mkdir -p "$dist"
mkdir -p ~/Library/Audio/Plug-Ins/Components

# target | product name | AU subtype
for plugin in "PhaseLabChannel|PhaseLab Channel Analyzer|PlCh" "PhaseLabBus|PhaseLab Bus Analyzer|PlBs"; do
    IFS='|' read -r target name code <<< "$plugin"
    release="$build/${target}_artefacts/Release"
    file="${name// /-}"

    for bundle in "VST3/$name.vst3" "CLAP/$name.clap" "AU/$name.component"; do
        codesign -v "$release/$bundle"
        archs="$(lipo -archs "$release/$bundle/Contents/MacOS/$name")"
        [[ "$archs" == *arm64* && "$archs" == *x86_64* ]] || die "$bundle is not universal"
    done

    # auval needs the component installed; this replaces any installed copy.
    echo "▸ validating AU ($name)"
    rm -rf ~/Library/Audio/Plug-Ins/Components/"$name.component"
    cp -R "$release/AU/$name.component" ~/Library/Audio/Plug-Ins/Components/
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    auval -v aufx "$code" Sbli | tail -3

    if command -v pluginval >/dev/null 2>&1; then
        echo "▸ pluginval ($name)"
        pluginval --strictness-level 10 --validate-in-process --validate "$release/VST3/$name.vst3" | tail -2
    fi
    if command -v clap-validator >/dev/null 2>&1; then
        echo "▸ clap-validator ($name)"
        clap-validator validate "$release/CLAP/$name.clap" | tail -2
    fi

    ditto -c -k --keepParent "$release/VST3/$name.vst3"      "$dist/$file-macOS-VST3.zip"
    ditto -c -k --keepParent "$release/CLAP/$name.clap"      "$dist/$file-macOS-CLAP.zip"
    ditto -c -k --keepParent "$release/AU/$name.component"   "$dist/$file-macOS-AU.zip"
done

if [[ "$dry_run" == true ]]; then
    echo "✓ dry run done – packages in $dist:"
    ls -lh "$dist" | awk 'NR > 1 { print "  " $5 "  " $9 }'
    exit 0
fi

# ── draft release, start the Linux/Windows build ────────────────────────────
echo "▸ creating draft release $tag with the macOS packages"
gh release create "$tag" "$dist"/* --draft --target "$sha" --title "PhaseLab $version" --generate-notes >/dev/null

echo "▸ starting the Linux + Windows build on GitHub"
started=$(date -u +%Y-%m-%dT%H:%M:%SZ)
gh workflow run build --ref main -f version="$version" -f macos=false

run_id=""
for _ in $(seq 1 30); do
    run_id=$(gh run list --workflow build --event workflow_dispatch --limit 5 \
                 --json databaseId,createdAt,headSha \
                 --jq "[.[] | select(.createdAt >= \"$started\" and .headSha == \"$sha\")][0].databaseId // empty")
    [[ -n "$run_id" ]] && break
    sleep 2
done
[[ -n "$run_id" ]] || die "could not find the started workflow run – check the Actions tab"

echo "▸ waiting for run $run_id (Linux + Windows, usually 15–25 min)"
if gh run watch "$run_id" --exit-status --interval 30 >/dev/null; then
    echo "✓ released: $(gh release view "$tag" --json url --jq .url)"
else
    die "the GitHub build failed. The draft $tag keeps the macOS packages. A flaky failure: gh run rerun $run_id. Needs a code fix: delete the draft (gh release delete $tag), fix, push and run this again"
fi
