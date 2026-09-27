#pragma once

#include "../Core/SessionAudioStore.h"

#include <array>
#include <cstdint>

namespace longland::ghost
{
// Per-voice, fixed-state reconstruction processor. Dream is independent of
// chronological age; Decay is multiplied by the memory's normalized age.
// The source audio remains immutable and all degradation is nondestructive.
class GhostDreamProcessor
{
public:
    struct FrameState
    {
        std::array<double, SessionAudioStore::maxChannels> driftFrames {};
        float driftMix {};
        float lowpassCoefficient { 1.0f };
        float stereoWidth { 1.0f };
        float abstraction {};
    };

    void prepare(double sampleRate) noexcept;
    void start(float dream, float decay, float normalizedAge,
               std::uint64_t deterministicSeed,
               float inheritedSpatial = 0.0f) noexcept;
    void setTargets(float dream, float decay) noexcept;

    [[nodiscard]] FrameState advanceFrame() noexcept;
    void processFrame(float* samples, std::uint32_t channels,
                      const FrameState& state) noexcept;

    [[nodiscard]] float effectiveDecay() const noexcept;

private:
    [[nodiscard]] static float bounded(float value) noexcept;

    double sampleRate_ { 48000.0 };
    double phase_ {};
    double phaseIncrement_ {};
    float smoothingCoefficient_ { 1.0f };
    float currentDream_ {};
    float targetDream_ {};
    float currentDecay_ {};
    float targetDecay_ {};
    float normalizedAge_ {};
    float inheritedSpatial_ {};
    std::array<float, SessionAudioStore::maxChannels> lowpassState_ {};
};
} // namespace longland::ghost
