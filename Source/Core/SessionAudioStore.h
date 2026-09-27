#pragma once

#include "MemoryTypes.h"

#include <cstdint>
#include <vector>

namespace longland::ghost
{
// Single-writer, audio-thread-owned circular store. prepare() is deliberately
// the only allocating operation and must be called away from processBlock().
class SessionAudioStore
{
public:
    static constexpr std::uint32_t maxChannels = 2;

    void prepare(std::uint32_t capacityFrames, std::uint32_t numChannels);
    void clear() noexcept;

    void write(const float* const* source,
               std::uint32_t sourceChannels,
               std::uint32_t numFrames) noexcept;

    [[nodiscard]] std::uint32_t capacityFrames() const noexcept { return capacityFrames_; }
    [[nodiscard]] std::uint32_t numChannels() const noexcept { return numChannels_; }
    [[nodiscard]] std::uint32_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] std::uint64_t absoluteWriteFrame() const noexcept { return absoluteWriteFrame_; }

    [[nodiscard]] bool contains(const MemoryRef& ref) const noexcept;
    [[nodiscard]] std::uint64_t framesUntilOverwrite(const MemoryRef& ref) const noexcept;
    [[nodiscard]] bool hasPlaybackLease(const MemoryRef& ref,
                                        std::uint64_t outputFrames,
                                        std::uint32_t safetyFrames = 0) const noexcept;

    // Offset is relative to ref.startFrame. Out-of-range or stale reads return
    // silence. Hermite neighbours are clamped to the event, not allowed to
    // bleed samples from the events on either side.
    [[nodiscard]] float readSample(std::uint32_t channel,
                                   const MemoryRef& ref,
                                   std::uint32_t offset) const noexcept;
    [[nodiscard]] float readHermite(std::uint32_t channel,
                                    const MemoryRef& ref,
                                    double offset) const noexcept;

private:
    [[nodiscard]] float readAbsoluteUnchecked(std::uint32_t channel,
                                              std::uint64_t frame) const noexcept;

    std::vector<float> samples_;
    std::uint32_t capacityFrames_ {};
    std::uint32_t numChannels_ {};
    std::uint32_t epoch_ {};
    std::uint64_t absoluteWriteFrame_ {};
};
} // namespace longland::ghost
