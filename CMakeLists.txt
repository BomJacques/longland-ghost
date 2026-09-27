cmake_minimum_required(VERSION 3.22)

project(LonglandGhost VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

set(GHOST_CORE_SOURCES
    Source/Core/MemoryTypes.h
    Source/Core/GhostLineage.h
    Source/Core/SessionAudioStore.cpp
    Source/Core/SessionAudioStore.h
    Source/Core/SpscQueue.h
    Source/analysis/AnalysisInputFifo.cpp
    Source/analysis/AnalysisInputFifo.h
    Source/analysis/GhostAnalysisWorker.cpp
    Source/analysis/GhostAnalysisWorker.h
    Source/analysis/GhostDescriptor.cpp
    Source/analysis/GhostDescriptor.h
    Source/analysis/GhostEventDetector.cpp
    Source/analysis/GhostEventDetector.h
    Source/analysis/GhostFeatureExtractor.cpp
    Source/analysis/GhostFeatureExtractor.h
    Source/analysis/GhostSimilarityEngine.cpp
    Source/analysis/GhostSimilarityEngine.h
    Source/Recall/GhostVoice.cpp
    Source/Recall/GhostVoice.h
    Source/Recall/GhostDreamProcessor.cpp
    Source/Recall/GhostDreamProcessor.h
    Source/Recall/GhostVoicePool.cpp
    Source/Recall/GhostVoicePool.h
    Source/Recall/RecallContracts.h
    Source/Engine/GhostEngine.cpp
    Source/Engine/GhostEngine.h
    Source/Engine/GhostEntropyEngine.cpp
    Source/Engine/GhostEntropyEngine.h
)

add_library(ghost_core STATIC ${GHOST_CORE_SOURCES})
target_include_directories(ghost_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_link_libraries(ghost_core
    PUBLIC
        juce::juce_audio_basics
        juce::juce_dsp
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
target_compile_definitions(ghost_core
    PUBLIC
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
)

juce_add_plugin(LonglandGhost
    COMPANY_NAME "Longland Audio"
    BUNDLE_ID "com.longland.audio.ghost"
    PLUGIN_MANUFACTURER_CODE LnLd
    PLUGIN_CODE LnGh
    FORMATS VST3
    PRODUCT_NAME "Longland Ghost"
    DESCRIPTION "Similarity-driven musical memory effect"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(LonglandGhost PRIVATE
    Source/Plugin/LonglandGhostAudioProcessor.cpp
    Source/Plugin/LonglandGhostAudioProcessor.h
    Source/Plugin/LonglandGhostAudioProcessorEditor.cpp
    Source/Plugin/LonglandGhostAudioProcessorEditor.h
)
target_link_libraries(LonglandGhost
    PRIVATE
        ghost_core
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
target_compile_definitions(LonglandGhost
    PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0
)

enable_testing()

add_executable(GhostAnalysisTests Tests/GhostAnalysisTests.cpp)
target_link_libraries(GhostAnalysisTests PRIVATE ghost_core)
add_test(NAME GhostAnalysisTests COMMAND GhostAnalysisTests)

add_executable(RecallCoreTests Tests/RecallCoreTests.cpp)
target_link_libraries(RecallCoreTests PRIVATE ghost_core)
add_test(NAME RecallCoreTests COMMAND RecallCoreTests)

add_executable(GhostPipelineTests Tests/GhostPipelineTests.cpp)
target_link_libraries(GhostPipelineTests PRIVATE ghost_core)
add_test(NAME GhostPipelineTests COMMAND GhostPipelineTests)

add_executable(GhostSafetyTests Tests/GhostSafetyTests.cpp)
target_link_libraries(GhostSafetyTests PRIVATE ghost_core)
add_test(NAME GhostSafetyTests COMMAND GhostSafetyTests)

add_executable(GhostDreamTests Tests/GhostDreamTests.cpp)
target_link_libraries(GhostDreamTests PRIVATE ghost_core)
add_test(NAME GhostDreamTests COMMAND GhostDreamTests)

add_executable(GhostEntropyTests Tests/GhostEntropyTests.cpp)
target_link_libraries(GhostEntropyTests PRIVATE ghost_core)
add_test(NAME GhostEntropyTests COMMAND GhostEntropyTests)

# Convenience aggregate: each suite keeps its own main and can be run directly,
# while `cmake --build --target GhostTests` builds the complete deterministic set.
add_custom_target(GhostTests DEPENDS GhostAnalysisTests RecallCoreTests GhostPipelineTests GhostSafetyTests GhostDreamTests GhostEntropyTests)
