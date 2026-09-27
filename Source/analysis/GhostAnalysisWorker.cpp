#include "GhostAnalysisWorker.h"
#include "../Engine/GhostEntropyEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace longland::ghost
{
GhostAnalysisWorker::GhostAnalysisWorker(AnalysisInputFifo& input,
                                         SpscQueue<RecallCommand, 65>& recallOutput)
    : inputFifo(input), recallFifo(recallOutput)
{
}

GhostAnalysisWorker::~GhostAnalysisWorker()
{
    stop();
}

void GhostAnalysisWorker::prepare(double sampleRate, std::uint32_t sessionCapacityFrames)
{
    stop();
    rate = std::max(8000.0, sampleRate);
    detector.prepare(sampleRate);
    extractor.prepare(sampleRate);
    detector.reset();
    historyCapacity = static_cast<std::size_t>(std::min<std::uint64_t>(
        sessionCapacityFrames, static_cast<std::uint64_t>(std::ceil(rate * 5.0))));
    historyLeft.assign(historyCapacity, 0.0f);
    historyRight.assign(historyCapacity, 0.0f);
    const auto maximumEventFrames = static_cast<std::size_t>(std::ceil(rate * 4.1));
    eventLeft.assign(maximumEventFrames, 0.0f);
    eventRight.assign(maximumEventFrames, 0.0f);
    catalog.assign(4096, {});
    catalogSize = 0;
    catalogWriteIndex = 0;
    provenanceSpans.fill({});
    provenanceWriteIndex = 0;
    nextMemoryId = 1;
    nextCommandId = 1;
    lastRecallFrame = 0;
    lastRecalledMemory = 0;
    hasStream = false;
    completedEvents.store(0, std::memory_order_relaxed);
    discontinuities.store(0, std::memory_order_relaxed);
    lastStart.store(0, std::memory_order_relaxed);
    lastEnd.store(0, std::memory_order_relaxed);
    lastPeak.store(0.0f, std::memory_order_relaxed);
    lastRms.store(0.0f, std::memory_order_relaxed);
    bestMemoryId.store(0, std::memory_order_relaxed);
    lastLineageRoot.store(0, std::memory_order_relaxed);
    lastLineageParent.store(0, std::memory_order_relaxed);
    lastLineageGeneration.store(0, std::memory_order_relaxed);
    bestScore.store(0.0f, std::memory_order_relaxed);
    bestHarmonic.store(0.0f, std::memory_order_relaxed);
    bestSpectral.store(0.0f, std::memory_order_relaxed);
    bestTransient.store(0.0f, std::memory_order_relaxed);
    bestDynamics.store(0.0f, std::memory_order_relaxed);
    bestStereo.store(0.0f, std::memory_order_relaxed);
    lastMutationPitch.store(0.0f, std::memory_order_relaxed);
    lastMutationBlur.store(0.0f, std::memory_order_relaxed);
    lastMutationSpatial.store(0.0f, std::memory_order_relaxed);
}

void GhostAnalysisWorker::start()
{
    if (!thread.joinable())
        thread = std::jthread([this](std::stop_token token) { run(token); });
}

void GhostAnalysisWorker::stop() noexcept
{
    if (!thread.joinable())
        return;

    thread.request_stop();
    thread.join();
}

void GhostAnalysisWorker::updatePolicy(float memorySeconds,
                                       float recall,
                                       float recognition,
                                       float age,
                                       GhostMemoryType memoryType,
                                       float entropy) noexcept
{
    policyMemorySeconds.store(std::clamp(memorySeconds, 30.0f, 120.0f),
                              std::memory_order_relaxed);
    policyRecall.store(std::clamp(recall, 0.0f, 1.0f), std::memory_order_relaxed);
    policyRecognition.store(std::clamp(recognition, 0.0f, 1.0f),
                            std::memory_order_relaxed);
    policyAge.store(std::clamp(age, 0.0f, 1.0f), std::memory_order_relaxed);
    policyMemoryType.store(memoryType, std::memory_order_relaxed);
    policyEntropy.store(std::isfinite(entropy) ? std::clamp(entropy, 0.0f, 1.0f) : 0.0f,
                        std::memory_order_relaxed);
}

GhostAnalysisDiagnostics GhostAnalysisWorker::diagnostics() const noexcept
{
    return {
        completedEvents.load(std::memory_order_relaxed),
        inputFifo.droppedBlockCount(),
        discontinuities.load(std::memory_order_relaxed),
        lastStart.load(std::memory_order_relaxed),
        lastEnd.load(std::memory_order_relaxed),
        bestMemoryId.load(std::memory_order_relaxed),
        lastLineageRoot.load(std::memory_order_relaxed),
        lastLineageParent.load(std::memory_order_relaxed),
        lastLineageGeneration.load(std::memory_order_relaxed),
        lastPeak.load(std::memory_order_relaxed),
        lastRms.load(std::memory_order_relaxed),
        bestScore.load(std::memory_order_relaxed),
        bestHarmonic.load(std::memory_order_relaxed),
        bestSpectral.load(std::memory_order_relaxed),
        bestTransient.load(std::memory_order_relaxed),
        bestDynamics.load(std::memory_order_relaxed),
        bestStereo.load(std::memory_order_relaxed),
        { lastMutationPitch.load(std::memory_order_relaxed),
          lastMutationBlur.load(std::memory_order_relaxed),
          lastMutationSpatial.load(std::memory_order_relaxed) }
    };
}

void GhostAnalysisWorker::run(std::stop_token stopToken)
{
    AnalysisAudioBlock block;

    while (!stopToken.stop_requested())
    {
        if (inputFifo.tryPop(block))
        {
            consume(block);
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void GhostAnalysisWorker::beginStream(const AnalysisAudioBlock& block)
{
    const bool epochChanged = hasStream && block.epoch != activeEpoch;
    detector.reset();
    if (epochChanged)
    {
        std::fill(catalog.begin(), catalog.end(), CatalogEntry {});
        catalogSize = 0;
        catalogWriteIndex = 0;
        provenanceSpans.fill({});
        provenanceWriteIndex = 0;
        lastRecallFrame = 0;
        lastRecalledMemory = 0;
    }
    activeEpoch = block.epoch;
    detectorFrameOrigin = block.absoluteStartFrame;
    expectedNextFrame = block.absoluteStartFrame;
    hasStream = true;
}

void GhostAnalysisWorker::writeHistory(const AnalysisAudioBlock& block) noexcept
{
    if (historyCapacity == 0)
        return;

    for (std::size_t frame = 0; frame < block.frameCount; ++frame)
    {
        const auto index = static_cast<std::size_t>(
            (block.absoluteStartFrame + frame) % historyCapacity);
        historyLeft[index] = block.channelCount > 0 ? block.samples[0][frame] : 0.0f;
        historyRight[index] = block.channelCount > 1 ? block.samples[1][frame] : historyLeft[index];
    }
}

void GhostAnalysisWorker::recordProvenance(const AnalysisAudioBlock& block) noexcept
{
    if (!block.provenance.isValid())
        return;
    provenanceSpans[provenanceWriteIndex] = {
        block.absoluteStartFrame,
        block.absoluteStartFrame + block.frameCount,
        block.provenance
    };
    provenanceWriteIndex = (provenanceWriteIndex + 1) % provenanceSpans.size();
}

void GhostAnalysisWorker::consume(const AnalysisAudioBlock& block)
{
    if (!hasStream || block.epoch != activeEpoch || block.absoluteStartFrame != expectedNextFrame)
    {
        if (hasStream)
            discontinuities.fetch_add(1, std::memory_order_relaxed);

        beginStream(block);
    }

    const auto frames = static_cast<std::size_t>(block.frameCount);
    const auto channels = static_cast<std::size_t>(block.channelCount);

    if (channels == 0)
        std::fill_n(monoScratch.begin(), frames, 0.0f);
    else if (channels == 1)
        std::copy_n(block.samples[0].begin(), frames, monoScratch.begin());
    else
        for (std::size_t frame = 0; frame < frames; ++frame)
            monoScratch[frame] = 0.5f * (block.samples[0][frame] + block.samples[1][frame]);

    writeHistory(block);
    recordProvenance(block);

    for (const auto& event : detector.process(monoScratch.data(), frames))
        completeEvent(event);

    expectedNextFrame = block.absoluteStartFrame + block.frameCount;
}

bool GhostAnalysisWorker::copyEventAudio(std::uint64_t start,
                                         std::uint64_t end,
                                         std::size_t& copiedFrames) noexcept
{
    copiedFrames = 0;
    if (historyCapacity == 0 || end <= start || end > expectedNextFrame + kAnalysisBlockFrames)
        return false;

    const auto length = end - start;
    if (length > historyCapacity || length > eventLeft.size())
        return false;

    // The block containing an event boundary is already in history. Reject only
    // if the event's oldest sample fell outside the worker-owned history ring.
    const auto latestAvailable = std::max(expectedNextFrame, end);
    const auto oldestAvailable = latestAvailable > historyCapacity
        ? latestAvailable - historyCapacity : 0;
    if (start < oldestAvailable)
        return false;

    copiedFrames = static_cast<std::size_t>(length);
    for (std::size_t frame = 0; frame < copiedFrames; ++frame)
    {
        const auto index = static_cast<std::size_t>((start + frame) % historyCapacity);
        eventLeft[frame] = historyLeft[index];
        eventRight[frame] = historyRight[index];
    }
    return true;
}

GhostLineage GhostAnalysisWorker::lineageForEvent(const std::uint64_t start,
                                                  const std::uint64_t end,
                                                  const MemoryId eventMemoryId) const noexcept
{
    float strongestContribution = 0.0f;
    GhostLineage selected;
    for (const auto& span : provenanceSpans)
    {
        if (!span.provenance.isValid() || span.endFrame <= start || span.startFrame >= end)
            continue;
        const auto overlapStart = std::max(start, span.startFrame);
        const auto overlapEnd = std::min(end, span.endFrame);
        const auto weightedOverlap = static_cast<float>(overlapEnd - overlapStart)
                                   * span.provenance.contribution;
        if (weightedOverlap > strongestContribution)
        {
            strongestContribution = weightedOverlap;
            selected = span.provenance.lineage;
        }
    }
    return selected.isValid() ? selected : GhostEntropyEngine::original(eventMemoryId);
}

void GhostAnalysisWorker::completeEvent(const GhostDetectedEvent& event)
{
    const auto absoluteStart = detectorFrameOrigin + event.startSample;
    const auto absoluteEnd = detectorFrameOrigin + event.endSample;
    std::size_t eventFrames = 0;
    if (!copyEventAudio(absoluteStart, absoluteEnd, eventFrames))
        return;

    CatalogEntry current;
    current.memory = { nextMemoryId++, absoluteStart,
                       static_cast<std::uint32_t>(eventFrames), activeEpoch };
    current.captureEndFrame = absoluteEnd;
    current.distinctiveness = event.distinctiveness;
    current.lineage = lineageForEvent(absoluteStart, absoluteEnd, current.memory.id);
    current.descriptor = extractor.extract(eventLeft.data(), eventRight.data(), eventFrames);

    lastStart.store(absoluteStart, std::memory_order_relaxed);
    lastEnd.store(absoluteEnd, std::memory_order_relaxed);
    lastPeak.store(event.peak, std::memory_order_relaxed);
    lastRms.store(event.meanRms, std::memory_order_relaxed);
    lastLineageRoot.store(current.lineage.rootId, std::memory_order_relaxed);
    lastLineageParent.store(current.lineage.parentId, std::memory_order_relaxed);
    lastLineageGeneration.store(current.lineage.generation, std::memory_order_relaxed);
    lastMutationPitch.store(current.lineage.mutation.pitchSemitones, std::memory_order_relaxed);
    lastMutationBlur.store(current.lineage.mutation.blur, std::memory_order_relaxed);
    lastMutationSpatial.store(current.lineage.mutation.spatial, std::memory_order_relaxed);
    completedEvents.fetch_add(1, std::memory_order_relaxed);

    findAndScheduleMatch(current);
    addToCatalog(current);
}

void GhostAnalysisWorker::findAndScheduleMatch(const CatalogEntry& query)
{
    GhostSimilarityResult strongest;
    GhostSimilarityResult bestObserved;
    const CatalogEntry* selected = nullptr;
    const CatalogEntry* observed = nullptr;
    float selectedNormalizedAge = 0.0f;
    const auto memoryFrames = static_cast<std::uint64_t>(
        std::llround(rate * policyMemorySeconds.load(std::memory_order_relaxed)));
    const auto minimumAge = static_cast<std::uint64_t>(std::llround(rate * 0.35));
    const auto agePreference = policyAge.load(std::memory_order_relaxed);
    const auto memoryType = policyMemoryType.load(std::memory_order_relaxed);
    const auto profile = recognitionProfile(memoryType);
    const auto recognition = profile.thresholdFor(
        policyRecognition.load(std::memory_order_relaxed));
    const GhostSimilarityEngine profileSimilarity(profile.weights);
    float bestSelectionScore = -1.0f;

    for (std::size_t index = 0; index < catalogSize; ++index)
    {
        const auto& candidate = catalog[index];
        if (candidate.memory.epoch != activeEpoch
            || query.memory.startFrame <= candidate.captureEndFrame)
            continue;

        const auto ageFrames = query.memory.startFrame - candidate.captureEndFrame;
        if (ageFrames < minimumAge || ageFrames > memoryFrames)
            continue;

        const auto result = profileSimilarity.compare(query.descriptor, candidate.descriptor);

        if (observed == nullptr || result.overall > bestObserved.overall)
        {
            observed = &candidate;
            bestObserved = result;
        }

        // Repeated stationary broadband chunks are acoustically alike but not
        // meaningful memories. Require a tonal or changing/transient cue when
        // both sides are very flat and harmonically unreliable.
        const bool stationaryBroadband = query.descriptor.flatnessMean > 0.45f
            && candidate.descriptor.flatnessMean > 0.45f
            && query.distinctiveness < 0.45f;
        if (stationaryBroadband || result.overall < recognition)
            continue;

        const auto normalisedAge = std::clamp(static_cast<float>(ageFrames)
                                              / static_cast<float>(std::max<std::uint64_t>(1, memoryFrames)),
                                              0.0f, 1.0f);
        // Age changes selection among legitimate matches, but can alter their
        // rank by at most 15%; descriptor similarity remains dominant.
        const auto ageFit = 1.0f - std::abs(normalisedAge - agePreference);
        const auto selectionScore = result.overall * (0.85f + 0.15f * ageFit);
        if (selectionScore > bestSelectionScore)
        {
            bestSelectionScore = selectionScore;
            strongest = result;
            selected = &candidate;
            selectedNormalizedAge = normalisedAge;
        }
    }

    bestMemoryId.store(observed != nullptr ? observed->memory.id : 0, std::memory_order_relaxed);
    bestScore.store(bestObserved.overall, std::memory_order_relaxed);
    bestHarmonic.store(bestObserved.harmonic, std::memory_order_relaxed);
    bestSpectral.store(bestObserved.spectral, std::memory_order_relaxed);
    bestTransient.store(bestObserved.transient, std::memory_order_relaxed);
    bestDynamics.store(bestObserved.dynamics, std::memory_order_relaxed);
    bestStereo.store(bestObserved.stereo, std::memory_order_relaxed);

    const auto recall = policyRecall.load(std::memory_order_relaxed);
    if (selected == nullptr || recall <= 0.0f)
        return;

    const auto minimumCooldownSeconds = 0.35 + (1.0 - static_cast<double>(recall)) * 5.65;
    const auto cooldownFrames = static_cast<std::uint64_t>(std::llround(rate * minimumCooldownSeconds));
    if (query.memory.startFrame < lastRecallFrame + cooldownFrames
        || (selected->memory.id == lastRecalledMemory
            && query.memory.startFrame < lastRecallFrame + cooldownFrames * 2))
        return;

    // A deterministic hash supplies bounded willingness after recognition. It
    // never promotes an ineligible candidate and makes tests reproducible.
    auto hash = query.memory.id * 0x9e3779b97f4a7c15ULL
              ^ selected->memory.id * 0xbf58476d1ce4e5b9ULL;
    hash ^= hash >> 30U;
    hash *= 0xbf58476d1ce4e5b9ULL;
    hash ^= hash >> 27U;
    const auto draw = static_cast<float>(hash & 0x00ffffffULL)
                    / static_cast<float>(0x01000000ULL);
    if (draw > recall)
        return;

    RecallCommand command;
    command.commandId = nextCommandId++;
    command.queryEventId = query.memory.id;
    command.validUntilFrame = query.captureEndFrame
                            + static_cast<std::uint64_t>(std::llround(rate * 2.0));
    command.memory = selected->memory;
    command.similarity = { strongest.overall, strongest.harmonic, strongest.spectral,
                           strongest.dynamics, strongest.transient, strongest.stereo };
    command.gain = std::clamp(0.2f + 0.45f * strongest.overall, 0.0f, 0.7f);
    command.fadeInSeconds = 0.012f;
    command.fadeOutSeconds = 0.025f;
    command.normalizedAge = selectedNormalizedAge;
    command.lineage = GhostEntropyEngine::deriveChild(
        selected->lineage, selected->memory.id,
        policyEntropy.load(std::memory_order_relaxed), hash ^ command.commandId);
    command.playbackRate = GhostEntropyEngine::playbackRate(command.lineage.mutation);

    if (recallFifo.tryPush(command))
    {
        lastRecallFrame = query.memory.startFrame;
        lastRecalledMemory = selected->memory.id;
    }
}

void GhostAnalysisWorker::addToCatalog(const CatalogEntry& event) noexcept
{
    catalog[catalogWriteIndex] = event;
    catalogWriteIndex = (catalogWriteIndex + 1) % catalog.size();
    catalogSize = std::min(catalogSize + 1, catalog.size());
}
}
