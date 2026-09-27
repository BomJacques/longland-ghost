#pragma once

#include "../Core/GhostLineage.h"

#include <cstdint>

namespace longland::ghost
{
// Stateless deterministic lineage evolution. A child always starts from its
// parent's complete state and receives one small Entropy-scaled delta.
class GhostEntropyEngine
{
public:
    [[nodiscard]] static GhostLineage original(MemoryId memoryId) noexcept;
    [[nodiscard]] static GhostLineage deriveChild(const GhostLineage& parent,
                                                  MemoryId parentMemoryId,
                                                  float entropy,
                                                  std::uint64_t seed) noexcept;
    [[nodiscard]] static float playbackRate(const GhostMutationState& mutation) noexcept;

    static constexpr float maximumPitchSemitones = 5.0f;
    static constexpr float maximumBlur = 1.0f;
    static constexpr float maximumSpatial = 1.0f;
    static constexpr float pitchStepPerGeneration = 0.45f;
    static constexpr float blurStepPerGeneration = 0.10f;
    static constexpr float spatialStepPerGeneration = 0.12f;
};
} // namespace longland::ghost
