#pragma once

#include "../Core/SpscQueue.h"
#include "../Core/GhostLineage.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace longland::ghost
{
constexpr std::uint32_t kAnalysisBlockFrames = 512;
constexpr std::uint32_t kAnalysisChannels = 2;

struct AnalysisAudioBlock
{
    std::uint64_t absoluteStartFrame {};
    std::uint32_t epoch {};
    std::uint16_t frameCount {};
    std::uint8_t channelCount {};
    GhostCaptureProvenance provenance {};
    std::array<std::array<float, kAnalysisBlockFrames>, kAnalysisChannels> samples {};
};

// Audio callback producer, analysis worker consumer. Overflow deliberately drops
// analysis blocks while leaving the audio path untouched.
class AnalysisInputFifo
{
public:
    void reset() noexcept;

    void pushAudio(const float* const* channels,
                   std::uint32_t channelCount,
                   std::uint32_t frameCount,
                   std::uint64_t absoluteStartFrame,
                   std::uint32_t epoch,
                   const GhostCaptureProvenance& provenance = {}) noexcept;

    [[nodiscard]] bool tryPop(AnalysisAudioBlock& block) noexcept;
    [[nodiscard]] std::uint64_t droppedBlockCount() const noexcept;

private:
    SpscQueue<AnalysisAudioBlock, 129> queue;
    std::atomic<std::uint64_t> droppedBlocks { 0 };
};
}
