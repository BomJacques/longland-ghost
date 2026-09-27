#include "GhostVoicePool.h"

#include <algorithm>
#include <cmath>

namespace longland::ghost
{
void GhostVoicePool::prepare(const double sampleRate) noexcept
{
    for (auto& voice : voices_)
        voice.prepare(sampleRate);
    pendingValid_ = false;
    lastCaptureProvenance_ = {};
}

void GhostVoicePool::clear() noexcept
{
    for (auto& voice : voices_)
        voice.stop();
    pendingValid_ = false;
    lastCaptureProvenance_ = {};
}

GhostVoice* GhostVoicePool::findInactiveVoice() noexcept
{
    const auto iterator = std::find_if(voices_.begin(), voices_.end(),
                                       [] (const GhostVoice& voice) { return ! voice.isActive(); });
    return iterator != voices_.end() ? &*iterator : nullptr;
}

GhostVoice& GhostVoicePool::quietestVoice() noexcept
{
    return *std::min_element(voices_.begin(), voices_.end(),
                             [] (const GhostVoice& left, const GhostVoice& right)
                             {
                                 return left.currentLevel() < right.currentLevel();
                             });
}

GhostVoicePool::ScheduleResult GhostVoicePool::schedule(const RecallCommand& command,
                                                        const SessionAudioStore& store) noexcept
{
    const auto duration = GhostVoice::plannedOutputFrames(command);
    if (duration == 0 || ! std::isfinite(command.similarity.total)
        || ! std::isfinite(command.gain) || ! store.contains(command.memory))
        return ScheduleResult::rejectedInvalidCommand;

    if (command.validUntilFrame != 0 && store.absoluteWriteFrame() > command.validUntilFrame)
        return ScheduleResult::rejectedExpired;

    if (! store.hasPlaybackLease(command.memory, duration, leaseSafetyFrames))
        return ScheduleResult::rejectedInsufficientLease;

    if (auto* inactive = findInactiveVoice())
        return inactive->start(command, store) ? ScheduleResult::started
                                               : ScheduleResult::rejectedInvalidCommand;

    // Voice stealing is deliberately two-stage: ramp an existing voice down,
    // then start the pending command on a later render boundary. Hard resets
    // here would click. Retain only the strongest one-deep pending request.
    if (pendingValid_ && command.similarity.total <= pending_.similarity.total)
        return ScheduleResult::rejectedInvalidCommand;

    pending_ = command;
    pendingValid_ = true;
    quietestVoice().beginRelease(stealReleaseFrames);
    return ScheduleResult::queuedAfterVoiceRelease;
}

void GhostVoicePool::tryStartPending(const SessionAudioStore& store) noexcept
{
    if (! pendingValid_)
        return;

    auto* inactive = findInactiveVoice();
    if (inactive == nullptr)
        return;

    const auto duration = GhostVoice::plannedOutputFrames(pending_);
    const bool unexpired = pending_.validUntilFrame == 0
        || store.absoluteWriteFrame() <= pending_.validUntilFrame;
    if (unexpired && store.hasPlaybackLease(pending_.memory, duration, leaseSafetyFrames))
        static_cast<void> (inactive->start(pending_, store));
    pendingValid_ = false;
}

void GhostVoicePool::renderAdditive(float* const* output,
                                    const std::uint32_t outputChannels,
                                    const std::uint32_t numFrames,
                                    const SessionAudioStore& store,
                                    const float dream,
                                    const float decay) noexcept
{
    tryStartPending(store);
    lastCaptureProvenance_ = {};
    for (auto& voice : voices_)
    {
        voice.setReconstructionTargets(dream, decay);
        voice.renderAdditive(output, outputChannels, numFrames, store);
        if (voice.lastRenderPeak() > lastCaptureProvenance_.contribution
            && voice.lineage().isValid())
            lastCaptureProvenance_ = { voice.lineage(), voice.lastRenderPeak() };
    }
    tryStartPending(store);
}

std::size_t GhostVoicePool::activeVoiceCount() const noexcept
{
    return static_cast<std::size_t> (std::count_if(voices_.begin(), voices_.end(),
                                                   [] (const GhostVoice& voice)
                                                   {
                                                       return voice.isActive();
                                                   }));
}
} // namespace longland::ghost
