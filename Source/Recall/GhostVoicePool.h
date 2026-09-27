#pragma once

#include "GhostVoice.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace longland::ghost
{
class GhostVoicePool
{
public:
    static constexpr std::size_t voiceCount = 8;
    static constexpr std::uint32_t leaseSafetyFrames = 64;
    static constexpr std::uint32_t stealReleaseFrames = 64;

    enum class ScheduleResult
    {
        started,
        queuedAfterVoiceRelease,
        rejectedInvalidCommand,
        rejectedExpired,
        rejectedInsufficientLease
    };

    void prepare(double sampleRate) noexcept;
    void clear() noexcept;

    [[nodiscard]] ScheduleResult schedule(const RecallCommand& command,
                                          const SessionAudioStore& store) noexcept;
    void renderAdditive(float* const* output,
                        std::uint32_t outputChannels,
                        std::uint32_t numFrames,
                        const SessionAudioStore& store,
                        float dream = 0.0f,
                        float decay = 0.0f) noexcept;

    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;
    [[nodiscard]] GhostCaptureProvenance captureProvenance() const noexcept
    {
        return lastCaptureProvenance_;
    }
    [[nodiscard]] bool hasPendingCommand() const noexcept { return pendingValid_; }

private:
    [[nodiscard]] GhostVoice* findInactiveVoice() noexcept;
    [[nodiscard]] GhostVoice& quietestVoice() noexcept;
    void tryStartPending(const SessionAudioStore& store) noexcept;

    std::array<GhostVoice, voiceCount> voices_ {};
    RecallCommand pending_ {};
    bool pendingValid_ {};
    GhostCaptureProvenance lastCaptureProvenance_ {};
};
} // namespace longland::ghost
