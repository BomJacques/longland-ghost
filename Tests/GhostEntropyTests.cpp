#include "../Source/Core/SessionAudioStore.h"
#include "../Source/Core/SpscQueue.h"
#include "../Source/Engine/GhostEntropyEngine.h"
#include "../Source/Recall/GhostVoicePool.h"
#include "../Source/analysis/AnalysisInputFifo.h"
#include "../Source/analysis/GhostAnalysisWorker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <thread>

namespace ghost = longland::ghost;

namespace
{
int failures = 0;

void require(const bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool sameMutation(const ghost::GhostMutationState& left,
                  const ghost::GhostMutationState& right)
{
    return left.pitchSemitones == right.pitchSemitones
        && left.blur == right.blur && left.spatial == right.spatial;
}

void testHereditaryMutation()
{
    const auto root = ghost::GhostEntropyEngine::original(10);
    const auto child1 = ghost::GhostEntropyEngine::deriveChild(root, 10, 1.0f, 111);
    const auto child1Repeat = ghost::GhostEntropyEngine::deriveChild(root, 10, 1.0f, 111);
    const auto child2 = ghost::GhostEntropyEngine::deriveChild(child1, 11, 1.0f, 222);
    const auto inheritedOnly = ghost::GhostEntropyEngine::deriveChild(child2, 12, 0.0f, 333);

    require(root.rootId == 10 && root.parentId == 0 && root.generation == 0,
            "original memory must establish Generation 0");
    require(child1.rootId == 10 && child1.parentId == 10 && child1.generation == 1,
            "first child must retain root and name its parent");
    require(child2.rootId == 10 && child2.parentId == 11 && child2.generation == 2,
            "second child must retain family root and increment generation");
    require(sameMutation(child1.mutation, child1Repeat.mutation),
            "the same parent and seed must produce deterministic mutation");
    require(sameMutation(child2.mutation, inheritedOnly.mutation),
            "zero Entropy child must inherit parent mutation exactly");
    require(std::abs(child2.mutation.pitchSemitones - child1.mutation.pitchSemitones)
                <= ghost::GhostEntropyEngine::pitchStepPerGeneration + 1.0e-6f
            && std::abs(child2.mutation.blur - child1.mutation.blur)
                <= ghost::GhostEntropyEngine::blurStepPerGeneration + 1.0e-6f
            && std::abs(child2.mutation.spatial - child1.mutation.spatial)
                <= ghost::GhostEntropyEngine::spatialStepPerGeneration + 1.0e-6f,
            "child state must be parent state plus one bounded delta");
}

void testManyGenerationsRemainMusical()
{
    auto lineage = ghost::GhostEntropyEngine::original(100);
    for (std::uint32_t generation = 1; generation <= 128; ++generation)
    {
        lineage = ghost::GhostEntropyEngine::deriveChild(
            lineage, 100 + generation - 1, 100.0f, 0xabc000ULL + generation);
        require(lineage.generation == generation, "generation counter must be monotonic");
        require(std::abs(lineage.mutation.pitchSemitones)
                    <= ghost::GhostEntropyEngine::maximumPitchSemitones
                && std::abs(lineage.mutation.blur)
                    <= ghost::GhostEntropyEngine::maximumBlur
                && std::abs(lineage.mutation.spatial)
                    <= ghost::GhostEntropyEngine::maximumSpatial,
                "long lineage mutation must remain clamped");
        const auto rate = ghost::GhostEntropyEngine::playbackRate(lineage.mutation);
        require(std::isfinite(rate) && rate >= 0.5f && rate <= 2.0f,
                "inherited pitch mutation must map to a safe playback rate");
    }
}

void testVoicePublishesPotentialChild()
{
    ghost::SessionAudioStore store;
    store.prepare(4096, 1);
    std::array<float, 512> source {};
    for (std::size_t frame = 0; frame < source.size(); ++frame)
        source[frame] = 0.4f * std::sin(0.08f * static_cast<float>(frame));
    const float* input[] { source.data() };
    store.write(input, 1, static_cast<std::uint32_t>(source.size()));

    ghost::RecallCommand command;
    command.commandId = 5;
    command.memory = { 50, 0, static_cast<std::uint32_t>(source.size()), store.epoch() };
    command.similarity.total = 1.0f;
    command.fadeInSeconds = 0.0f;
    command.fadeOutSeconds = 0.0f;
    const auto parent = ghost::GhostEntropyEngine::original(command.memory.id);
    command.lineage = ghost::GhostEntropyEngine::deriveChild(parent, command.memory.id,
                                                              1.0f, 99);
    command.playbackRate = ghost::GhostEntropyEngine::playbackRate(command.lineage.mutation);

    ghost::GhostVoicePool pool;
    pool.prepare(8000.0);
    require(pool.schedule(command, store) == ghost::GhostVoicePool::ScheduleResult::started,
            "lineage-bearing recall must schedule");
    std::array<float, 128> output {};
    float* channels[] { output.data() };
    pool.renderAdditive(channels, 1, static_cast<std::uint32_t>(output.size()), store,
                        std::abs(command.lineage.mutation.blur), 0.0f);
    const auto provenance = pool.captureProvenance();
    require(provenance.isValid() && provenance.lineage.rootId == command.lineage.rootId
                && provenance.lineage.parentId == command.lineage.parentId
                && provenance.lineage.generation == 1,
            "rendered recall must publish its potential child lineage");
}

void testDescendantsAudiblyDiverge()
{
    ghost::SessionAudioStore store;
    store.prepare(8192, 1);
    std::array<float, 2048> source {};
    for (std::size_t frame = 0; frame < source.size(); ++frame)
        source[frame] = 0.42f * std::sin(0.11f * static_cast<float>(frame))
                      + 0.18f * std::sin(0.53f * static_cast<float>(frame));
    const float* input[] { source.data() };
    store.write(input, 1, static_cast<std::uint32_t>(source.size()));
    const ghost::MemoryRef memory { 90, 0, static_cast<std::uint32_t>(source.size()),
                                    store.epoch() };

    auto descendant = ghost::GhostEntropyEngine::original(memory.id);
    for (std::uint32_t generation = 1; generation <= 12; ++generation)
        descendant = ghost::GhostEntropyEngine::deriveChild(
            descendant, memory.id + generation - 1, 1.0f, 900 + generation);

    const auto render = [&store, &memory](const ghost::GhostLineage& lineage)
    {
        ghost::RecallCommand command;
        command.commandId = 44;
        command.memory = memory;
        command.similarity.total = 1.0f;
        command.gain = 1.0f;
        command.fadeInSeconds = 0.0f;
        command.fadeOutSeconds = 0.0f;
        command.lineage = lineage;
        command.playbackRate = ghost::GhostEntropyEngine::playbackRate(lineage.mutation);
        command.dream = 0.70f * std::abs(lineage.mutation.blur);
        ghost::GhostVoice voice;
        voice.prepare(8000.0);
        std::array<float, 1024> result {};
        if (!voice.start(command, store))
            return result;
        float* channels[] { result.data() };
        voice.renderAdditive(channels, 1, static_cast<std::uint32_t>(result.size()), store);
        return result;
    };

    const auto original = render(ghost::GhostEntropyEngine::original(memory.id));
    const auto evolved = render(descendant);
    float meanDifference = 0.0f;
    for (std::size_t frame = 0; frame < original.size(); ++frame)
        meanDifference += std::abs(original[frame] - evolved[frame]);
    meanDifference /= static_cast<float>(original.size());
    require(meanDifference > 0.025f,
            "multi-generation inherited mutation must audibly diverge from Generation 0");
}

void testFeedbackProvenanceMaterializesDescendant()
{
    constexpr double sampleRate = 8000.0;
    constexpr std::uint32_t blockFrames = 256;
    ghost::AnalysisInputFifo input;
    ghost::SpscQueue<ghost::RecallCommand, 65> output;
    ghost::GhostAnalysisWorker worker(input, output);
    worker.prepare(sampleRate, static_cast<std::uint32_t>(sampleRate * 20.0));
    worker.updatePolicy(20.0f, 0.0f, 0.72f, 0.5f);
    worker.start();

    auto lineage = ghost::GhostEntropyEngine::original(77);
    lineage = ghost::GhostEntropyEngine::deriveChild(lineage, 77, 1.0f, 1);
    lineage = ghost::GhostEntropyEngine::deriveChild(lineage, 78, 1.0f, 2);
    lineage = ghost::GhostEntropyEngine::deriveChild(lineage, 79, 1.0f, 3);
    const ghost::GhostCaptureProvenance provenance { lineage, 0.65f };

    std::array<float, blockFrames> samples {};
    std::uint64_t absoluteFrame = 0;
    const auto totalFrames = static_cast<std::uint64_t>(sampleRate * 1.4);
    const auto toneFrames = static_cast<std::uint64_t>(sampleRate * 0.65);
    while (absoluteFrame < totalFrames)
    {
        const auto frames = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(blockFrames, totalFrames - absoluteFrame));
        for (std::uint32_t frame = 0; frame < frames; ++frame)
        {
            const auto position = absoluteFrame + frame;
            samples[frame] = position < toneFrames
                ? 0.35f * std::sin(0.14f * static_cast<float>(position)) : 0.0f;
        }
        const float* channels[] { samples.data() };
        input.pushAudio(channels, 1, frames, absoluteFrame, 1,
                        absoluteFrame < toneFrames ? provenance
                                                   : ghost::GhostCaptureProvenance {});
        absoluteFrame += frames;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (worker.diagnostics().completedEventCount == 0
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto diagnostics = worker.diagnostics();
    worker.stop();

    require(diagnostics.completedEventCount > 0,
            "feedback-tagged audio must complete a detected event");
    require(diagnostics.lastLineageRootId == lineage.rootId
                && diagnostics.lastLineageParentId == lineage.parentId
                && diagnostics.lastGeneration == lineage.generation,
            "feedback event must materialize the supplied descendant metadata");
    require(sameMutation(diagnostics.lastMutation, lineage.mutation),
            "materialized descendant must preserve its inherited mutation recipe");
}
}

int main()
{
    testHereditaryMutation();
    testManyGenerationsRemainMusical();
    testVoicePublishesPotentialChild();
    testDescendantsAudiblyDiverge();
    testFeedbackProvenanceMaterializesDescendant();
    if (failures == 0)
        std::cout << "Entropy lineage tests passed\n";
    return failures == 0 ? 0 : 1;
}
