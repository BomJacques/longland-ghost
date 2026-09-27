#include "SessionAudioStore.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace longland::ghost
{
void SessionAudioStore::prepare(const std::uint32_t capacityFrames,
                                const std::uint32_t numChannels)
{
    if (capacityFrames == 0)
        throw std::invalid_argument("SessionAudioStore capacity must be non-zero");
    if (numChannels == 0 || numChannels > maxChannels)
        throw std::invalid_argument("SessionAudioStore supports one or two channels");

    capacityFrames_ = capacityFrames;
    numChannels_ = numChannels;
    samples_.assign(static_cast<std::size_t> (capacityFrames_) * numChannels_, 0.0f);
    absoluteWriteFrame_ = 0;

    // Epoch zero is reserved for unprepared/default references.
    if (++epoch_ == 0)
        ++epoch_;
}

void SessionAudioStore::clear() noexcept
{
    absoluteWriteFrame_ = 0;
    if (++epoch_ == 0)
        ++epoch_;
}

void SessionAudioStore::write(const float* const* source,
                              const std::uint32_t sourceChannels,
                              const std::uint32_t numFrames) noexcept
{
    if (capacityFrames_ == 0 || numFrames == 0)
        return;

    for (std::uint32_t frame = 0; frame < numFrames; ++frame)
    {
        const auto ringFrame = static_cast<std::uint32_t> (absoluteWriteFrame_ % capacityFrames_);
        for (std::uint32_t channel = 0; channel < numChannels_; ++channel)
        {
            float sample = 0.0f;
            if (source != nullptr && channel < sourceChannels && source[channel] != nullptr)
                sample = source[channel][frame];
            else if (source != nullptr && sourceChannels == 1 && source[0] != nullptr)
                sample = source[0][frame];

            samples_[static_cast<std::size_t> (channel) * capacityFrames_ + ringFrame]
                = std::isfinite(sample) ? sample : 0.0f;
        }
        ++absoluteWriteFrame_;
    }
}

bool SessionAudioStore::contains(const MemoryRef& ref) const noexcept
{
    if (capacityFrames_ == 0 || ref.epoch != epoch_ || ! ref.isStructurallyValid()
        || ref.lengthFrames > capacityFrames_ || ref.startFrame > absoluteWriteFrame_)
        return false;

    const auto available = absoluteWriteFrame_ - ref.startFrame;
    if (static_cast<std::uint64_t> (ref.lengthFrames) > available)
        return false; // The event has not been completely captured yet.

    const auto oldestFrame = absoluteWriteFrame_ > capacityFrames_
        ? absoluteWriteFrame_ - capacityFrames_
        : 0;
    return ref.startFrame >= oldestFrame;
}

std::uint64_t SessionAudioStore::framesUntilOverwrite(const MemoryRef& ref) const noexcept
{
    if (! contains(ref))
        return 0;

    // The first frame of the event is overwritten after this many future
    // writes. This also accounts for the not-yet-full initial pass.
    return ref.startFrame + static_cast<std::uint64_t> (capacityFrames_)
        - absoluteWriteFrame_;
}

bool SessionAudioStore::hasPlaybackLease(const MemoryRef& ref,
                                         const std::uint64_t outputFrames,
                                         const std::uint32_t safetyFrames) const noexcept
{
    if (! contains(ref))
        return false;

    const auto requested = outputFrames + static_cast<std::uint64_t> (safetyFrames);
    if (requested < outputFrames) // Overflow.
        return false;
    return framesUntilOverwrite(ref) >= requested;
}

float SessionAudioStore::readAbsoluteUnchecked(const std::uint32_t channel,
                                               const std::uint64_t frame) const noexcept
{
    const auto ringFrame = static_cast<std::uint32_t> (frame % capacityFrames_);
    return samples_[static_cast<std::size_t> (channel) * capacityFrames_ + ringFrame];
}

float SessionAudioStore::readSample(const std::uint32_t channel,
                                    const MemoryRef& ref,
                                    const std::uint32_t offset) const noexcept
{
    if (channel >= numChannels_ || offset >= ref.lengthFrames || ! contains(ref))
        return 0.0f;
    return readAbsoluteUnchecked(channel, ref.startFrame + offset);
}

float SessionAudioStore::readHermite(const std::uint32_t channel,
                                     const MemoryRef& ref,
                                     const double offset) const noexcept
{
    if (channel >= numChannels_ || ! contains(ref) || ! std::isfinite(offset)
        || offset < 0.0 || offset > static_cast<double> (ref.lengthFrames - 1))
        return 0.0f;

    const auto center = static_cast<std::int64_t> (std::floor(offset));
    const auto maximum = static_cast<std::int64_t> (ref.lengthFrames - 1);
    const auto clampedRead = [this, channel, &ref, maximum] (std::int64_t index) noexcept
    {
        index = std::clamp<std::int64_t> (index, 0, maximum);
        return readAbsoluteUnchecked(channel, ref.startFrame + static_cast<std::uint64_t> (index));
    };

    const float y0 = clampedRead(center - 1);
    const float y1 = clampedRead(center);
    const float y2 = clampedRead(center + 1);
    const float y3 = clampedRead(center + 2);
    const float fraction = static_cast<float> (offset - static_cast<double> (center));

    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * fraction + c2) * fraction + c1) * fraction + y1;
}
} // namespace longland::ghost
