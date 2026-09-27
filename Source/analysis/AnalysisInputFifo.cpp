#include "AnalysisInputFifo.h"

#include <algorithm>

namespace longland::ghost
{
void AnalysisInputFifo::reset() noexcept
{
    queue.reset();
    droppedBlocks.store(0, std::memory_order_relaxed);
}

void AnalysisInputFifo::pushAudio(const float* const* channels,
                                  std::uint32_t channelCount,
                                  std::uint32_t frameCount,
                                  std::uint64_t absoluteStartFrame,
                                  std::uint32_t epoch,
                                  const GhostCaptureProvenance& provenance) noexcept
{
    const auto channelsToCopy = std::min(channelCount, kAnalysisChannels);
    std::uint32_t offset = 0;

    while (offset < frameCount)
    {
        AnalysisAudioBlock block;
        const auto frames = std::min(kAnalysisBlockFrames, frameCount - offset);
        block.absoluteStartFrame = absoluteStartFrame + offset;
        block.epoch = epoch;
        block.frameCount = static_cast<std::uint16_t>(frames);
        block.channelCount = static_cast<std::uint8_t>(channelsToCopy);
        block.provenance = provenance;

        for (std::uint32_t channel = 0; channel < channelsToCopy; ++channel)
            std::copy_n(channels[channel] + offset, frames, block.samples[channel].begin());

        if (!queue.tryPush(block))
            droppedBlocks.fetch_add(1, std::memory_order_relaxed);

        offset += frames;
    }
}

bool AnalysisInputFifo::tryPop(AnalysisAudioBlock& block) noexcept
{
    return queue.tryPop(block);
}

std::uint64_t AnalysisInputFifo::droppedBlockCount() const noexcept
{
    return droppedBlocks.load(std::memory_order_relaxed);
}
}
