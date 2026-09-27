#pragma once

#include "MemoryTypes.h"

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace longland::ghost
{
struct GhostMutationState
{
    float pitchSemitones {};
    float blur {};
    float spatial {};
};

struct GhostLineage
{
    MemoryId rootId {};
    MemoryId parentId {};
    std::uint32_t generation {};
    GhostMutationState mutation {};

    [[nodiscard]] bool isValid() const noexcept
    {
        return rootId != 0 && std::isfinite(mutation.pitchSemitones)
            && std::isfinite(mutation.blur) && std::isfinite(mutation.spatial);
    }
};

struct GhostCaptureProvenance
{
    GhostLineage lineage {};
    float contribution {};

    [[nodiscard]] bool isValid() const noexcept
    {
        return lineage.isValid() && std::isfinite(contribution) && contribution > 0.0f;
    }
};

static_assert(std::is_trivially_copyable_v<GhostMutationState>);
static_assert(std::is_trivially_copyable_v<GhostLineage>);
static_assert(std::is_trivially_copyable_v<GhostCaptureProvenance>);
} // namespace longland::ghost
