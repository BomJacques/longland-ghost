#include "GhostDreamProcessor.h"

#include <algorithm>
#include <cmath>

namespace longland::ghost
{
namespace
{
constexpr double twoPi = 6.28318530717958647692;
}

float GhostDreamProcessor::bounded(const float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

void GhostDreamProcessor::prepare(const double sampleRate) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
    smoothingCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.03 * sampleRate_)));
    phaseIncrement_ = twoPi * 0.23 / sampleRate_;
    start(0.0f, 0.0f, 0.0f, 0);
}

void GhostDreamProcessor::start(const float dream, const float decay,
                                const float normalizedAge,
                                const std::uint64_t deterministicSeed,
                                const float inheritedSpatial) noexcept
{
    currentDream_ = targetDream_ = bounded(dream);
    currentDecay_ = targetDecay_ = bounded(decay);
    normalizedAge_ = bounded(normalizedAge);
    inheritedSpatial_ = std::isfinite(inheritedSpatial)
        ? std::clamp(inheritedSpatial, -1.0f, 1.0f) : 0.0f;
    const auto unitSeed = static_cast<double>(deterministicSeed & 0xffffULL) / 65536.0;
    phase_ = unitSeed * twoPi;
    lowpassState_.fill(0.0f);
}

void GhostDreamProcessor::setTargets(const float dream, const float decay) noexcept
{
    targetDream_ = bounded(dream);
    targetDecay_ = bounded(decay);
}

float GhostDreamProcessor::effectiveDecay() const noexcept
{
    // Nonlinear age scaling preserves young memories while making the oldest
    // material audibly less exact at high Decay settings.
    return currentDecay_ * std::pow(normalizedAge_, 0.65f);
}

GhostDreamProcessor::FrameState GhostDreamProcessor::advanceFrame() noexcept
{
    currentDream_ += smoothingCoefficient_ * (targetDream_ - currentDream_);
    currentDecay_ += smoothingCoefficient_ * (targetDecay_ - currentDecay_);

    const auto agedDecay = effectiveDecay();
    const auto dreamCurve = currentDream_ * currentDream_;
    const auto decayCurve = agedDecay * agedDecay;
    const auto abstraction = std::clamp(currentDream_ * 0.72f + agedDecay * 0.88f,
                                        0.0f, 1.0f);
    const auto maximumDrift = sampleRate_ * (0.020 * dreamCurve + 0.008 * decayCurve);

    FrameState result;
    result.driftFrames[0] = std::sin(phase_) * maximumDrift;
    result.driftFrames[1] = std::sin(phase_ + 1.13) * maximumDrift;
    result.driftMix = std::clamp(currentDream_ * 0.72f + agedDecay * 0.28f,
                                 0.0f, 0.82f);

    // Logarithmic cutoff mapping: literal playback is bypassed exactly, while
    // high age-linked Decay can reduce a memory to broad spectral identity.
    const auto softening = std::clamp(currentDream_ * 0.62f + agedDecay * 0.92f,
                                      0.0f, 1.0f);
    if (softening > 1.0e-6f)
    {
        const auto cutoff = std::clamp(20000.0 * std::pow(0.020, softening),
                                       180.0, sampleRate_ * 0.45);
        result.lowpassCoefficient = static_cast<float>(
            1.0 - std::exp(-twoPi * cutoff / sampleRate_));
    }

    const auto dreamWidthMotion = 0.18f * currentDream_
        * static_cast<float>(std::sin(phase_ * 0.37));
    result.stereoWidth = std::clamp(1.0f - 0.82f * agedDecay + dreamWidthMotion
                                        + 0.32f * inheritedSpatial_,
                                    0.12f, 1.18f);
    result.abstraction = abstraction;

    phase_ += phaseIncrement_ * (1.0 + 1.7 * abstraction);
    if (phase_ >= twoPi)
        phase_ -= twoPi;
    return result;
}

void GhostDreamProcessor::processFrame(float* const samples,
                                       const std::uint32_t channels,
                                       const FrameState& state) noexcept
{
    const auto boundedChannels = std::min<std::uint32_t>(channels,
                                                         SessionAudioStore::maxChannels);
    if (samples == nullptr || boundedChannels == 0)
        return;

    for (std::uint32_t channel = 0; channel < boundedChannels; ++channel)
    {
        auto sample = std::isfinite(samples[channel]) ? samples[channel] : 0.0f;
        if (state.abstraction <= 1.0e-6f)
        {
            // Preserve bit-for-bit literal playback while priming state for
            // smooth automation into transformed playback.
            lowpassState_[channel] = sample;
        }
        else
        {
            lowpassState_[channel] += state.lowpassCoefficient
                                    * (sample - lowpassState_[channel]);
            sample = lowpassState_[channel];
        }
        samples[channel] = sample;
    }

    if (boundedChannels == 2)
    {
        const auto mid = 0.5f * (samples[0] + samples[1]);
        const auto side = 0.5f * (samples[0] - samples[1]) * state.stereoWidth;
        samples[0] = mid + side;
        samples[1] = mid - side;
    }
}
} // namespace longland::ghost
