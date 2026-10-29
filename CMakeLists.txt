cmake_minimum_required(VERSION 3.22)

project(PhaseLab VERSION 0.0.1 LANGUAGES C CXX)

option(PHASELAB_BUILD_TESTS "Build the offline tests" ON)
option(PHASELAB_COPY_PLUGIN "Copy the built plugins into the user's plugin folders" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# Dependencies are fetched into the build tree, never committed (inventory: docs/Dependencies.md).
include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.3
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL)

FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
    EXCLUDE_FROM_ALL)

FetchContent_MakeAvailable(JUCE clap_juce_extensions)

# ---------------------------------------------------------------------------
# Analysis core and link transport (no JUCE): shared by both plugins and the tests
# ---------------------------------------------------------------------------
add_library(PhaseLabCore STATIC
    Source/core/MonoScore.cpp
    Source/core/Timeline.cpp
    Source/core/ABAnalysis.cpp
    Source/transport/SharedRegion.cpp
    Source/transport/Link.cpp)
target_include_directories(PhaseLabCore PUBLIC Source)
target_compile_features(PhaseLabCore PUBLIC cxx_std_20)
set_target_properties(PhaseLabCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(UNIX AND NOT APPLE)
    target_link_libraries(PhaseLabCore PUBLIC rt)   # shm_open on older glibc
endif()
if(MSVC)
    target_compile_options(PhaseLabCore PRIVATE /W4)
else()
    target_compile_options(PhaseLabCore PRIVATE -Wall -Wextra)
endif()

# Sources both plugins share (processor base, editor, parameters)
set(PHASELAB_SHARED_SOURCES
    Source/shared/Parameters.cpp
    Source/shared/AnalyzerProcessor.cpp
    Source/shared/AnalyzerEditor.cpp
    Source/shared/BusLink.cpp)

set(PHASELAB_PLUGIN_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    PHASELAB_VERSION_STRING="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Plugins: Channel Analyzer and Bus Analyzer (docs/spec/09, D-03/D-21)
# ---------------------------------------------------------------------------
function(phaselab_add_plugin target)
    cmake_parse_arguments(ARG "" "PRODUCT_NAME;PLUGIN_CODE;BUNDLE_SUFFIX;DESCRIPTION;ENTRY" "" ${ARGN})

    juce_add_plugin(${target}
        PRODUCT_NAME             "${ARG_PRODUCT_NAME}"
        COMPANY_NAME             "sscheppert-blip"
        COMPANY_WEBSITE          "https://github.com/sscheppert-blip"
        BUNDLE_ID                "io.github.sscheppert-blip.phaselab.${ARG_BUNDLE_SUFFIX}"
        PLUGIN_MANUFACTURER_CODE Sbli
        PLUGIN_CODE              ${ARG_PLUGIN_CODE}
        FORMATS                  AU VST3
        IS_SYNTH                 FALSE
        NEEDS_MIDI_INPUT         FALSE
        NEEDS_MIDI_OUTPUT        FALSE
        IS_MIDI_EFFECT           FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD  ${PHASELAB_COPY_PLUGIN}
        VST3_CATEGORIES          Fx Analyzer
        AU_MAIN_TYPE             kAudioUnitType_Effect
        DESCRIPTION              "${ARG_DESCRIPTION}")

    target_sources(${target} PRIVATE ${PHASELAB_SHARED_SOURCES} ${ARG_ENTRY})
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PUBLIC ${PHASELAB_PLUGIN_DEFINITIONS})

    target_link_libraries(${target}
        PRIVATE
            PhaseLabCore
            clap_juce_extensions
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    clap_juce_extensions_plugin(TARGET ${target}
        CLAP_ID       "io.github.sscheppert-blip.phaselab.${ARG_BUNDLE_SUFFIX}"
        CLAP_FEATURES audio-effect analyzer stereo mono)

    # JUCE writes moduleinfo.json into the VST3 after signing, and the CLAP and AU bundles are
    # not signed at all; Apple Silicon hosts refuse to load bundles without a valid signature.
    if(APPLE)
        foreach(format VST3 CLAP AU)
            set(folder ${format})
            if(format STREQUAL "AU")
                set(folder Components)
            endif()
            set(installed "$ENV{HOME}/Library/Audio/Plug-Ins/${folder}/$<TARGET_BUNDLE_DIR_NAME:${target}_${format}>")
            add_custom_command(TARGET ${target}_${format} POST_BUILD
                COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${target}_${format}>"
                COMMAND test ! -d "${installed}" || codesign --force --deep --sign - "${installed}"
                VERBATIM)
        endforeach()
    endif()
endfunction()

phaselab_add_plugin(PhaseLabChannel
    PRODUCT_NAME  "PhaseLab Channel Analyzer"
    PLUGIN_CODE   PlCh
    BUNDLE_SUFFIX channel
    DESCRIPTION   "Phase and mono analysis of one track, can send it to the Bus Analyzer"
    ENTRY         Source/channel/ChannelEntry.cpp)

phaselab_add_plugin(PhaseLabBus
    PRODUCT_NAME  "PhaseLab Bus Analyzer"
    PLUGIN_CODE   PlBs
    BUNDLE_SUFFIX bus
    DESCRIPTION   "Phase and mono analysis of a bus, compares two Channel Analyzer sources"
    ENTRY         Source/bus/BusEntry.cpp)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PHASELAB_BUILD_TESTS)
    juce_add_console_app(PhaseLabTests PRODUCT_NAME "PhaseLabTests")

    target_sources(PhaseLabTests PRIVATE ${PHASELAB_SHARED_SOURCES} tests/Tests.cpp)
    target_include_directories(PhaseLabTests PRIVATE Source)
    target_compile_definitions(PhaseLabTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1
            PHASELAB_VERSION_STRING="${PROJECT_VERSION}")

    target_link_libraries(PhaseLabTests
        PRIVATE
            PhaseLabCore
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    enable_testing()
    add_test(NAME PhaseLabTests COMMAND PhaseLabTests)
endif()
