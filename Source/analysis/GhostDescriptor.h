#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace longland::ghost
{
constexpr std::uint32_t kGhostDescriptorSchemaVersion = 1;

// The comparison payload is deliberately small and versioned.  f0Hz is metadata:
// it is useful for diagnostics but is never compared unless its confidence is valid.
struct GhostDescriptor
{
    std::uint32_t schemaVersion { kGhostDescriptorSchemaVersion };
    std::array<float, 12> chroma {};
    std::array<float, 8> spectralBands {};
    float centroidMean {};
    float centroidStd {};
    float flatnessMean {};
    float flatnessStd {};
    float fluxMean {};
    float fluxP90 {};
    float rmsMean {};
    float dynamicRange {};
    float crestFactor {};
    float harmonicity {};
    float logDuration {};
    float stereoWidth {};
    float stereoCorrelation { 1.0f };
    float f0Confidence {};
    float f0Hz { std::numeric_limits<float>::quiet_NaN() };
    float harmonicReliability {};
    float stereoReliability {};

    [[nodiscard]] bool isFiniteAndNormalised() const noexcept;
};

struct GhostSimilarityWeights
{
    float harmonic { 0.34f };
    float spectral { 0.28f };
    float transient { 0.16f };
    float dynamics { 0.10f };
    float stereo { 0.05f };
    float duration { 0.07f };
};

// A stable, host-facing choice. Values are persisted as the AudioParameterChoice
// index, so append future recognition modes rather than reordering these.
enum class GhostMemoryType : std::uint8_t
{
    literal = 0,
    emotional = 1,
    dream = 2
};

// Recognition profiles only alter explainable descriptor-group weights and the
// interpretation of the Recognition control. They never bypass eligibility or
// inject randomness into candidate selection.
struct GhostRecognitionProfile
{
    GhostSimilarityWeights weights;
    float minimumThreshold {};
    float defaultThreshold {};
    float maximumThreshold {};

    [[nodiscard]] float thresholdFor(float recognition) const noexcept;
};

[[nodiscard]] GhostRecognitionProfile recognitionProfile(GhostMemoryType type) noexcept;

struct GhostSimilarityResult
{
    float overall {};
    float harmonic {};
    float spectral {};
    float transient {};
    float dynamics {};
    float stereo {};
    float duration {};
    std::array<float, 6> effectiveWeights {};
};
}
