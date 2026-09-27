#include "GhostDescriptor.h"

#include <algorithm>
#include <cmath>

namespace longland::ghost
{
float GhostRecognitionProfile::thresholdFor(float recognition) const noexcept
{
    // Keep 0.72 as the semantic centre of the existing Recognition control.
    // This preserves Literal's historical default while allowing Emotional and
    // Dream to expose deliberately broader, but still deterministic, matching.
    constexpr float centre = 0.72f;
    const auto control = std::clamp(recognition, 0.0f, 1.0f);
    if (control <= centre)
        return minimumThreshold
             + (defaultThreshold - minimumThreshold) * (control / centre);

    return defaultThreshold
         + (maximumThreshold - defaultThreshold) * ((control - centre) / (1.0f - centre));
}

GhostRecognitionProfile recognitionProfile(GhostMemoryType type) noexcept
{
    switch (type)
    {
        case GhostMemoryType::emotional:
            // Broad spectral density, level/dynamic contour, and harmonic
            // tension (harmonicity is part of the spectral group) dominate.
            return { { 0.12f, 0.36f, 0.08f, 0.30f, 0.08f, 0.06f },
                     0.40f, 0.62f, 0.88f };

        case GhostMemoryType::dream:
            // Spectral overlap is intentionally dominant; pitch identity,
            // duration, and loudness become weak supporting evidence.
            return { { 0.03f, 0.70f, 0.12f, 0.03f, 0.07f, 0.05f },
                     0.24f, 0.50f, 0.78f };

        case GhostMemoryType::literal:
        default:
            // Pitch-class identity, spectral shape, and event duration make
            // this the strict profile for melodic and instrumental repetition.
            return { { 0.44f, 0.22f, 0.11f, 0.06f, 0.04f, 0.13f },
                     0.55f, 0.72f, 0.95f };
    }
}

bool GhostDescriptor::isFiniteAndNormalised() const noexcept
{
    const auto boundedArray = [](const auto& values)
    {
        return std::all_of(values.begin(), values.end(), [](float value)
        {
            return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
        });
    };
    const std::array scalars { centroidMean, centroidStd, flatnessMean, flatnessStd,
                               fluxMean, fluxP90, rmsMean, dynamicRange, crestFactor,
                               harmonicity, logDuration, stereoWidth, f0Confidence,
                               harmonicReliability, stereoReliability };
    return schemaVersion == kGhostDescriptorSchemaVersion
           && boundedArray(chroma) && boundedArray(spectralBands) && boundedArray(scalars)
           && std::isfinite(stereoCorrelation) && stereoCorrelation >= -1.0f
           && stereoCorrelation <= 1.0f
           && (f0Confidence <= 0.0f || (std::isfinite(f0Hz) && f0Hz > 0.0f));
}
}
