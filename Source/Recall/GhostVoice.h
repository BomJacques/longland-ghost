#pragma once

#include "RecallContracts.h"
#include "GhostDreamProcessor.h"
#include "../Core/SessionAudioStore.h"

#include <array>
#include <cstdint>

namespace longland::ghost
{
class GhostVoice
{
public:
    void prepare(double sampleRate) noexcept;
    [[nodiscard]] bool start(const RecallCommand& command,
                             const SessionAudioStore& store) noexcept;
    void stop() noexcept;
    void beginRelease(std::uint32_t releaseFrames) noexcept;
    void setReconstructionTargets(float dream, float decay) noexcept;

    // Adds into output. The caller must render before writing the same block to
    // SessionAudioStore, so a lease measured at entry remains conservative.
    void renderAdditive(float* const* output,
                        std::uint32_t outputChannels,
                        std::uint32_t numFrames,
                        const SessionAudioStore& store) noexcept;

    [[nodiscard]] bool isActive() const noexcept { return active_; }
    [[nodiscard]] MemoryId memoryId() const noexcept { return command_.memory.id; }
    [[nodiscard]] float currentLevel() const noexcept { return currentLevel_; }
    [[nodiscard]] float lastRenderPeak() const noexcept { return lastRenderPeak_; }
    [[nodiscard]] const GhostLineage& lineage() const noexcept { return command_.lineage; }
    [[nodiscard]] std::uint64_t remainingOutputFrames() const noexcept;
    [[nodiscard]] static std::uint64_t plannedOutputFrames(const RecallCommand& command) noexcept;

private:
    [[nodiscard]] float envelopeForFrame(std::uint64_t frame) const noexcept;
    [[nodiscard]] float readForOutputChannel(std::uint32_t outputChannel,
                                             std::uint32_t outputChannels,
                                             double sourceOffset,
                                             const SessionAudioStore& store) const noexcept;
    void renderHeldRelease(float* const* output,
                           std::uint32_t outputChannels,
                           std::uint32_t numFrames) noexcept;

    RecallCommand command_ {};
    double sampleRate_ { 48000.0 };
    double sourceOffset_ {};
    std::uint64_t renderedFrames_ {};
    std::uint64_t totalOutputFrames_ {};
    std::uint32_t attackFrames_ {};
    std::uint32_t releaseFrames_ {};
    std::uint32_t forcedReleaseRemaining_ {};
    std::uint32_t forcedReleaseLength_ {};
    std::array<float, SessionAudioStore::maxChannels> lastOutput_ {};
    GhostDreamProcessor dreamProcessor_;
    float currentLevel_ {};
    float lastRenderPeak_ {};
    bool active_ {};
};
} // namespace longland::ghost
