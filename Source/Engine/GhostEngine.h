#pragma once

#include "../Core/SessionAudioStore.h"
#include "../Core/SpscQueue.h"
#include "../Recall/GhostVoicePool.h"
#include "../analysis/AnalysisInputFifo.h"
#include "../analysis/GhostAnalysisWorker.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>

namespace longland::ghost
{
struct GhostEngineParameters
{
    float memorySeconds { 60.0f };
    float recall { 0.25f };
    float recognition { 0.72f };
    float age { 0.5f };
    float decay { 0.25f };
    float dream { 0.0f };
    float entropy { 0.0f };
    float presence { 0.25f };
    float repeatVolume { 1.0f };
    float feedback { 0.0f };
    GhostMemoryType memoryType { GhostMemoryType::literal };
};

struct GhostEngineDiagnostics
{
    GhostAnalysisDiagnostics analysis;
    std::uint64_t capturedFrames {};
    std::uint64_t rejectedRecallCommands {};
    std::uint32_t epoch {};
    std::uint32_t activeVoices {};
};

// Audio-thread facade. The worker receives a private copy of input audio and
// never reads SessionAudioStore while its ring is being mutated.
class GhostEngine
{
public:
    GhostEngine();
    ~GhostEngine();

    GhostEngine(const GhostEngine&) = delete;
    GhostEngine& operator=(const GhostEngine&) = delete;

    void prepare(double sampleRate, int maximumBlockSize, int channelCount);
    void release() noexcept;
    void process(juce::AudioBuffer<float>& audio,
                 const GhostEngineParameters& parameters) noexcept;
    void processBypassed(juce::AudioBuffer<float>& audio,
                         const GhostEngineParameters& parameters) noexcept;
    void resetSession() noexcept;

    // Test/manual injection seam. Production has one producer: the analysis worker.
    [[nodiscard]] bool enqueueRecall(const RecallCommand& command) noexcept;
    [[nodiscard]] GhostEngineDiagnostics diagnostics() const noexcept;

private:
    void consumeRecallCommands() noexcept;
    void processChunk(juce::AudioBuffer<float>& audio,
                      int startFrame,
                      int frameCount) noexcept;

    static constexpr double maximumSessionSeconds = 120.0;

    AnalysisInputFifo analysisInput;
    SpscQueue<RecallCommand, 65> recallCommands;
    GhostAnalysisWorker analysisWorker;
    SessionAudioStore audioStore;
    GhostVoicePool voicePool;
    juce::AudioBuffer<float> wetScratch;
    juce::AudioBuffer<float> captureScratch;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> presence;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> repeatVolume;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedback;

    std::atomic<std::uint64_t> rejectedRecallCommands { 0 };
    std::atomic<std::uint64_t> publishedCapturedFrames { 0 };
    std::atomic<std::uint32_t> publishedActiveVoices { 0 };
    std::atomic<std::uint32_t> publishedEpoch { 0 };
    std::uint64_t streamFrame {};
    std::uint32_t streamEpoch {};
    int preparedChannels { 1 };
    int scratchFrames { 1 };
    float currentDecay { 0.25f };
    float currentDream {};
    float currentEntropy {};
    bool prepared {};
};
}
