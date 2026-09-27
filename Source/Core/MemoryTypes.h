#pragma once

#include <cstdint>

namespace longland::ghost
{
using MemoryId = std::uint64_t;

enum class MemoryScope : std::uint8_t
{
    session,
    project,
    archive
};

// A reference uses the store's monotonic frame domain. Ring indices must never
// be persisted in an event because the same slot represents a different sample
// after every wrap.
struct MemoryRef
{
    MemoryId id {};
    std::uint64_t startFrame {};
    std::uint32_t lengthFrames {};
    std::uint32_t epoch {};

    [[nodiscard]] constexpr std::uint64_t endFrame() const noexcept
    {
        return startFrame + static_cast<std::uint64_t> (lengthFrames);
    }

    [[nodiscard]] constexpr bool isStructurallyValid() const noexcept
    {
        return id != 0 && lengthFrames != 0
            && endFrame() >= startFrame; // Also rejects integer overflow.
    }
};

// Realtime-safe event metadata. Heavier descriptors belong to the analysis
// catalog and can be joined by id.
struct MemoryEvent
{
    MemoryRef audio {};
    std::uint64_t captureEndFrame {};
    std::uint64_t lineageId {};
    MemoryId parentId {};
    std::uint32_t generation {};
    std::uint32_t recallCount {};
    float imprintStrength {};
    float familiarity {};
    MemoryScope scope { MemoryScope::session };
};
} // namespace longland::ghost
