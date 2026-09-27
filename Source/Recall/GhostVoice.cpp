#include "GhostVoice.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace longland::ghost
{
namespace
{
constexpr double halfPi = 1.57079632679489661923;
constexpr std::uint32_t emergencyReleaseFrames = 64;

float squaredSine(const double phase) noexcept
{
    const auto sine = std::sin(halfPi * std::clamp(phase, 0.0, 1.0));
    return static_cast<float> (sine * sine);
}
}

void GhostVoice::prepare(const double sampleRate) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    dreamProcessor_.prepare(sampleRate_);
    stop();
}

std::uint64_t GhostVoice::plannedOutputFrames(const RecallCommand& command) noexcept
{
    if (! command.memory.isStructurallyValid() || ! std::isfinite(command.playbackRate)
        || command.playbackRate <= 0.0f)
        return 0;

    const auto rate = std::clamp(static_cast<double> (command.playbackRate), 0.125, 8.0);
    const auto lastSourceOffset = static_cast<double> (command.memory.lengthFrames - 1);
    const auto result = std::ceil(lastSourceOffset / rate) + 1.0;
    if (! std::isfinite(result) || result > static_cast<double> (std::numeric_limits<std::uint64_t>::max()))
        return 0;
    return static_cast<std::uint64_t> (result);
}

bool GhostVoice::start(const RecallCommand& command, const SessionAudioStore& store) noexcept
{
    const auto duration = plannedOutputFrames(command);
    if (duration == 0 || ! store.contains(command.memory) || ! std::isfinite(command.gain))
        return false;

    command_ = command;
    command_.playbackRate = std::clamp(command_.playbackRate, 0.125f, 8.0f);
    command_.gain = std::clamp(command_.gain, 0.0f, 2.0f);
    command_.fadeInSeconds = std::clamp(command_.fadeInSeconds, 0.0f, 1.0f);
    command_.fadeOutSeconds = std::clamp(command_.fadeOutSeconds, 0.0f, 1.0f);
    command_.normalizedAge = std::clamp(command_.normalizedAge, 0.0f, 1.0f);
    command_.dream = std::clamp(command_.dream, 0.0f, 1.0f);
    command_.decay = std::clamp(command_.decay, 0.0f, 1.0f);
    totalOutputFrames_ = duration;
    renderedFrames_ = 0;
    sourceOffset_ = command_.reverse
        ? static_cast<double> (command_.memory.lengthFrames - 1)
        : 0.0;

    attackFrames_ = static_cast<std::uint32_t> (std::min (
        static_cast<double> (std::llround(command_.fadeInSeconds * sampleRate_)),
        static_cast<double> (totalOutputFrames_ / 2)));
    releaseFrames_ = static_cast<std::uint32_t> (std::min (
        static_cast<double> (std::llround(command_.fadeOutSeconds * sampleRate_)),
        static_cast<double> (totalOutputFrames_ / 2)));
    forcedReleaseRemaining_ = 0;
    forcedReleaseLength_ = 0;
    lastOutput_.fill(0.0f);
    currentLevel_ = 0.0f;
    dreamProcessor_.start(command_.dream, command_.decay, command_.normalizedAge,
                          command_.memory.id ^ command_.commandId,
                          command_.lineage.mutation.spatial);
    lastRenderPeak_ = 0.0f;
    active_ = true;
    return true;
}

void GhostVoice::setReconstructionTargets(const float dream, const float decay) noexcept
{
    const auto inheritedBlur = std::isfinite(command_.lineage.mutation.blur)
        ? 0.70f * std::abs(command_.lineage.mutation.blur) : 0.0f;
    dreamProcessor_.setTargets(std::clamp(dream + inheritedBlur, 0.0f, 1.0f), decay);
}

void GhostVoice::stop() noexcept
{
    active_ = false;
    renderedFrames_ = 0;
    totalOutputFrames_ = 0;
    forcedReleaseRemaining_ = 0;
    forcedReleaseLength_ = 0;
    currentLevel_ = 0.0f;
    lastOutput_.fill(0.0f);
}

void GhostVoice::beginRelease(const std::uint32_t releaseFrames) noexcept
{
    if (! active_)
        return;
    const auto requested = std::max<std::uint32_t> (1, releaseFrames);
    if (forcedReleaseRemaining_ == 0 || requested < forcedReleaseRemaining_)
    {
        forcedReleaseRemaining_ = requested;
        forcedReleaseLength_ = requested;
    }
}

std::uint64_t GhostVoice::remainingOutputFrames() const noexcept
{
    return active_ && totalOutputFrames_ > renderedFrames_
        ? totalOutputFrames_ - renderedFrames_
        : 0;
}

float GhostVoice::envelopeForFrame(const std::uint64_t frame) const noexcept
{
    float envelope = 1.0f;
    if (attackFrames_ != 0 && frame < attackFrames_)
        envelope *= squaredSine(static_cast<double> (frame) / attackFrames_);

    const auto remaining = totalOutputFrames_ - frame;
    if (releaseFrames_ != 0 && remaining <= releaseFrames_)
        envelope *= squaredSine(static_cast<double> (remaining - 1) / releaseFrames_);

    if (forcedReleaseRemaining_ != 0)
        envelope *= forcedReleaseLength_ <= 1 ? 0.0f
            : static_cast<float> (forcedReleaseRemaining_ - 1)
              / static_cast<float> (forcedReleaseLength_ - 1);
    return envelope;
}

float GhostVoice::readForOutputChannel(const std::uint32_t outputChannel,
                                       const std::uint32_t outputChannels,
                                       const double sourceOffset,
                                       const SessionAudioStore& store) const noexcept
{
    if (outputChannels == 1 && store.numChannels() == 2)
        return 0.5f * (store.readHermite(0, command_.memory, sourceOffset)
                     + store.readHermite(1, command_.memory, sourceOffset));

    const auto sourceChannel = std::min(outputChannel, store.numChannels() - 1);
    return store.readHermite(sourceChannel, command_.memory, sourceOffset);
}

void GhostVoice::renderHeldRelease(float* const* output,
                                   const std::uint32_t outputChannels,
                                   const std::uint32_t numFrames) noexcept
{
    if (forcedReleaseRemaining_ == 0)
    {
        forcedReleaseRemaining_ = emergencyReleaseFrames;
        forcedReleaseLength_ = emergencyReleaseFrames;
    }

    for (std::uint32_t frame = 0; frame < numFrames && active_; ++frame)
    {
        const auto gain = forcedReleaseLength_ <= 1 ? 0.0f
            : static_cast<float> (forcedReleaseRemaining_ - 1)
              / static_cast<float> (forcedReleaseLength_ - 1);
        for (std::uint32_t channel = 0; channel < outputChannels; ++channel)
        {
            if (output != nullptr && output[channel] != nullptr)
                output[channel][frame] += lastOutput_[std::min(channel, SessionAudioStore::maxChannels - 1)] * gain;
        }

        currentLevel_ *= gain;
        if (--forcedReleaseRemaining_ == 0)
            stop();
    }
}

void GhostVoice::renderAdditive(float* const* output,
                                const std::uint32_t outputChannels,
                                const std::uint32_t numFrames,
                                const SessionAudioStore& store) noexcept
{
    lastRenderPeak_ = 0.0f;
    if (! active_ || numFrames == 0 || outputChannels == 0)
        return;

    if (! store.contains(command_.memory))
    {
        renderHeldRelease(output, outputChannels, numFrames);
        return;
    }

    // Retire before the next block write can invalidate our source. This is
    // conservative for forward playback and required for slow/reverse reads.
    const auto overwriteMargin = store.framesUntilOverwrite(command_.memory);
    const auto naturalRemaining = remainingOutputFrames();
    if (overwriteMargin < naturalRemaining + emergencyReleaseFrames)
        beginRelease(static_cast<std::uint32_t> (std::min<std::uint64_t> (
            emergencyReleaseFrames, std::max<std::uint64_t> (1, overwriteMargin))));

    for (std::uint32_t frame = 0; frame < numFrames && active_; ++frame)
    {
        if (renderedFrames_ >= totalOutputFrames_)
        {
            stop();
            break;
        }

        const auto envelope = envelopeForFrame(renderedFrames_);
        currentLevel_ = envelope * command_.gain;
        const auto reconstruction = dreamProcessor_.advanceFrame();
        std::array<float, SessionAudioStore::maxChannels> reconstructed {};
        for (std::uint32_t channel = 0; channel < outputChannels; ++channel)
        {
            const auto literal = readForOutputChannel(channel, outputChannels, sourceOffset_, store);
            const auto diffused = readForOutputChannel(
                channel, outputChannels,
                sourceOffset_ + reconstruction.driftFrames[std::min<std::uint32_t>(
                    channel, SessionAudioStore::maxChannels - 1)], store);
            reconstructed[std::min<std::uint32_t>(
                channel, SessionAudioStore::maxChannels - 1)]
                = literal + reconstruction.driftMix * (diffused - literal);
        }
        dreamProcessor_.processFrame(reconstructed.data(), outputChannels, reconstruction);
        for (std::uint32_t channel = 0; channel < outputChannels; ++channel)
        {
            const auto sample = reconstructed[std::min<std::uint32_t>(
                channel, SessionAudioStore::maxChannels - 1)];
            const auto rendered = std::isfinite(sample) ? sample * currentLevel_ : 0.0f;
            lastRenderPeak_ = std::max(lastRenderPeak_, std::abs(rendered));
            if (output != nullptr && output[channel] != nullptr)
                output[channel][frame] += rendered;
            lastOutput_[std::min(channel, SessionAudioStore::maxChannels - 1)] = rendered;
        }

        ++renderedFrames_;
        sourceOffset_ += command_.reverse ? -command_.playbackRate : command_.playbackRate;
        if (forcedReleaseRemaining_ != 0 && --forcedReleaseRemaining_ == 0)
        {
            stop();
            break;
        }
        if (renderedFrames_ >= totalOutputFrames_)
            stop();
    }
}
} // namespace longland::ghost
