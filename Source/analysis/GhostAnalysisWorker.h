#pragma once

#include "AnalysisInputFifo.h"
#include "GhostEventDetector.h"
#include "GhostFeatureExtractor.h"
#include "GhostSimilarityEngine.h"
#include "../Core/MemoryTypes.h"
#include "../Core/SpscQueue.h"
#include "../Recall/RecallContracts.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace longland::ghost
{
struct GhostAnalysisDiagnostics
{
    std::uint64_t completedEventCount {};
    std::uint64_t droppedAnalysisBlocks {};
    std::uint64_t discontinuityCount {};
    std::uint64_t lastEventStartFrame {};
    std::uint64_t lastEventEndFrame {};
    std::uint64_t bestMatchMemoryId {};
    MemoryId lastLineageRootId {};
    MemoryId lastLineageParentId {};
    std::uint32_t lastGeneration {};
    float lastEventPeak {};
    float lastEventRms {};
    float bestSimilarity {};
    float harmonicSimilarity {};
    float spectralSimilarity {};
    float transientSimilarity {};
    float dynamicsSimilarity {};
    float stereoSimilarity {};
    GhostMutationState lastMutation {};
};

class GhostAnalysisWorker
{
public:
    GhostAnalysisWorker(AnalysisInputFifo& input,
                        SpscQueue<RecallCommand, 65>& recallOutput);
    ~GhostAnalysisWorker();

    GhostAnalysisWorker(const GhostAnalysisWorker&) = delete;
    GhostAnalysisWorker& operator=(const GhostAnalysisWorker&) = delete;

    void prepare(double sampleRate, std::uint32_t sessionCapacityFrames);
    void start();
    void stop() noexcept;
    void updatePolicy(float memorySeconds,
                      float recall,
                      float recognition,
                      float age,
                      GhostMemoryType memoryType = GhostMemoryType::literal,
                      float entropy = 0.0f) noexcept;
    [[nodiscard]] GhostAnalysisDiagnostics diagnostics() const noexcept;

private:
    void run(std::stop_token stopToken);
    void consume(const AnalysisAudioBlock& block);
    void beginStream(const AnalysisAudioBlock& block);
    void writeHistory(const AnalysisAudioBlock& block) noexcept;
    void recordProvenance(const AnalysisAudioBlock& block) noexcept;
    void completeEvent(const GhostDetectedEvent& event);

    struct CatalogEntry
    {
        MemoryRef memory;
        std::uint64_t captureEndFrame {};
        float distinctiveness {};
        GhostLineage lineage {};
        GhostDescriptor descriptor;
    };

    struct ProvenanceSpan
    {
        std::uint64_t startFrame {};
        std::uint64_t endFrame {};
        GhostCaptureProvenance provenance {};
    };

    [[nodiscard]] bool copyEventAudio(std::uint64_t start,
                                      std::uint64_t end,
                                      std::size_t& copiedFrames) noexcept;
    void findAndScheduleMatch(const CatalogEntry& query);
    void addToCatalog(const CatalogEntry& event) noexcept;
    [[nodiscard]] GhostLineage lineageForEvent(std::uint64_t start,
                                               std::uint64_t end,
                                               MemoryId eventMemoryId) const noexcept;

    AnalysisInputFifo& inputFifo;
    SpscQueue<RecallCommand, 65>& recallFifo;
    GhostEventDetector detector;
    GhostFeatureExtractor extractor;
    std::jthread thread;
    std::array<float, kAnalysisBlockFrames> monoScratch {};
    std::vector<float> historyLeft;
    std::vector<float> historyRight;
    std::vector<float> eventLeft;
    std::vector<float> eventRight;
    // Four thousand entries preserve the complete 120 s prototype horizon
    // even at a deliberately conservative 32 detected events per second.
    std::vector<CatalogEntry> catalog;
    std::array<ProvenanceSpan, 8192> provenanceSpans {};
    std::size_t catalogSize {};
    std::size_t catalogWriteIndex {};
    std::size_t historyCapacity {};
    std::size_t provenanceWriteIndex {};
    double rate { 48000.0 };

    std::uint64_t expectedNextFrame {};
    std::uint64_t detectorFrameOrigin {};
    std::uint32_t activeEpoch {};
    std::uint64_t nextMemoryId { 1 };
    std::uint64_t nextCommandId { 1 };
    std::uint64_t lastRecallFrame {};
    MemoryId lastRecalledMemory {};
    bool hasStream {};

    std::atomic<float> policyMemorySeconds { 60.0f };
    std::atomic<float> policyRecall { 0.25f };
    std::atomic<float> policyRecognition { 0.72f };
    std::atomic<float> policyAge { 0.5f };
    std::atomic<GhostMemoryType> policyMemoryType { GhostMemoryType::literal };
    std::atomic<float> policyEntropy { 0.0f };

    std::atomic<std::uint64_t> completedEvents { 0 };
    std::atomic<std::uint64_t> discontinuities { 0 };
    std::atomic<std::uint64_t> lastStart { 0 };
    std::atomic<std::uint64_t> lastEnd { 0 };
    std::atomic<std::uint64_t> bestMemoryId { 0 };
    std::atomic<MemoryId> lastLineageRoot { 0 };
    std::atomic<MemoryId> lastLineageParent { 0 };
    std::atomic<std::uint32_t> lastLineageGeneration { 0 };
    std::atomic<float> lastPeak { 0.0f };
    std::atomic<float> lastRms { 0.0f };
    std::atomic<float> bestScore { 0.0f };
    std::atomic<float> bestHarmonic { 0.0f };
    std::atomic<float> bestSpectral { 0.0f };
    std::atomic<float> bestTransient { 0.0f };
    std::atomic<float> bestDynamics { 0.0f };
    std::atomic<float> bestStereo { 0.0f };
    std::atomic<float> lastMutationPitch { 0.0f };
    std::atomic<float> lastMutationBlur { 0.0f };
    std::atomic<float> lastMutationSpatial { 0.0f };
};
}
