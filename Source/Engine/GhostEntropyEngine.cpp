#include "GhostEntropyEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace longland::ghost
{
namespace
{
std::uint64_t splitMix64(std::uint64_t& state) noexcept
{
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

float signedUnit(std::uint64_t& state) noexcept
{
    const auto bits = splitMix64(state) & 0x00ffffffULL;
    return static_cast<float>(bits) / static_cast<float>(0x00ffffffULL) * 2.0f - 1.0f;
}

float boundedEntropy(const float entropy) noexcept
{
    return std::isfinite(entropy) ? std::clamp(entropy, 0.0f, 1.0f) : 0.0f;
}
}

GhostLineage GhostEntropyEngine::original(const MemoryId memoryId) noexcept
{
    GhostLineage lineage;
    lineage.rootId = memoryId;
    return lineage;
}

GhostLineage GhostEntropyEngine::deriveChild(const GhostLineage& parent,
                                             const MemoryId parentMemoryId,
                                             const float entropy,
                                             const std::uint64_t seed) noexcept
{
    const auto amount = boundedEntropy(entropy);
    GhostLineage child;
    child.rootId = parent.rootId != 0 ? parent.rootId : parentMemoryId;
    child.parentId = parentMemoryId;
    child.generation = parent.generation == std::numeric_limits<std::uint32_t>::max()
        ? parent.generation : parent.generation + 1;
    child.mutation = parent.mutation;

    auto randomState = seed ^ child.rootId * 0x9e3779b97f4a7c15ULL
                            ^ parentMemoryId * 0xbf58476d1ce4e5b9ULL
                            ^ static_cast<std::uint64_t>(child.generation) * 0x94d049bb133111ebULL;
    child.mutation.pitchSemitones = std::clamp(
        child.mutation.pitchSemitones + signedUnit(randomState)
            * pitchStepPerGeneration * amount,
        -maximumPitchSemitones, maximumPitchSemitones);
    child.mutation.blur = std::clamp(
        child.mutation.blur + signedUnit(randomState) * blurStepPerGeneration * amount,
        -maximumBlur, maximumBlur);
    child.mutation.spatial = std::clamp(
        child.mutation.spatial + signedUnit(randomState) * spatialStepPerGeneration * amount,
        -maximumSpatial, maximumSpatial);
    return child;
}

float GhostEntropyEngine::playbackRate(const GhostMutationState& mutation) noexcept
{
    const auto semitones = std::isfinite(mutation.pitchSemitones)
        ? std::clamp(mutation.pitchSemitones, -maximumPitchSemitones, maximumPitchSemitones)
        : 0.0f;
    return std::clamp(std::exp2(semitones / 12.0f), 0.5f, 2.0f);
}
} // namespace longland::ghost
