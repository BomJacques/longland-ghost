#pragma once

#include "../Core/MemoryTypes.h"
#include "../Core/GhostLineage.h"

#include <cstdint>
#include <type_traits>

namespace longland::ghost
{
struct SimilarityBreakdown
{
    float total {};
    float harmonic {};
    float spectral {};
    float dynamics {};
    float transient {};
    float stereo {};
};

// Fixed-size worker-to-audio-thread command. It is safe to copy through an
// SPSC FIFO and owns no memory. The audio thread must revalidate memory before
// starting a voice; worker validation alone is never sufficient.
struct RecallCommand
{
    std::uint64_t commandId {};
    std::uint64_t queryEventId {};
    std::uint64_t validUntilFrame {}; // Zero means no explicit expiry.
    MemoryRef memory {};
    SimilarityBreakdown similarity {};
    float playbackRate { 1.0f };
    float gain { 1.0f };
    float fadeInSeconds { 0.01f };
    float fadeOutSeconds { 0.02f };
    float normalizedAge {};
    float dream {};
    float decay {};
    GhostLineage lineage {};
    bool reverse {};
};

static_assert(std::is_trivially_copyable_v<RecallCommand>);
} // namespace longland::ghost
