#include "GhostSimilarityEngine.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace longland::ghost
{
namespace
{
float distributionSimilarity(const auto& first, const auto& second) noexcept
{
    float sumFirst = 0.0f;
    float sumSecond = 0.0f;
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        sumFirst += std::max(0.0f, first[i]);
        sumSecond += std::max(0.0f, second[i]);
    }
    if (sumFirst <= 1.0e-9f || sumSecond <= 1.0e-9f)
        return 0.0f;
    float coefficient = 0.0f;
    for (std::size_t i = 0; i < first.size(); ++i)
        coefficient += std::sqrt(std::max(0.0f, first[i]) / sumFirst
                                 * (std::max(0.0f, second[i]) / sumSecond));
    return std::clamp(coefficient, 0.0f, 1.0f);
}

float scalarSimilarity(float first, float second) noexcept
{
    return std::clamp(1.0f - std::abs(first - second), 0.0f, 1.0f);
}

float average(std::initializer_list<float> values) noexcept
{
    float sum = 0.0f;
    for (float value : values)
        sum += value;
    return sum / static_cast<float>(values.size());
}
}

GhostSimilarityResult GhostSimilarityEngine::compare(const GhostDescriptor& query,
                                                      const GhostDescriptor& candidate) const noexcept
{
    GhostSimilarityResult result;
    if (!query.isFiniteAndNormalised() || !candidate.isFiniteAndNormalised()
        || query.schemaVersion != candidate.schemaVersion)
        return result;

    // Uniform chroma distributions can have a deceptively high Bhattacharyya
    // coefficient against any chord. Tonality compatibility prevents broadband
    // noise from looking harmonically related merely because it covers all bins.
    const auto chromaSimilarity = distributionSimilarity(query.chroma, candidate.chroma);
    const auto tonalityCompatibility = scalarSimilarity(query.harmonicity,
                                                         candidate.harmonicity);
    result.harmonic = chromaSimilarity * (0.25f + 0.75f * tonalityCompatibility);
    result.spectral = 0.58f * distributionSimilarity(query.spectralBands, candidate.spectralBands)
                      + 0.42f * average({ scalarSimilarity(query.centroidMean, candidate.centroidMean),
                                         scalarSimilarity(query.centroidStd, candidate.centroidStd),
                                         scalarSimilarity(query.flatnessMean, candidate.flatnessMean),
                                         scalarSimilarity(query.flatnessStd, candidate.flatnessStd),
                                         scalarSimilarity(query.harmonicity, candidate.harmonicity) });
    result.transient = average({ scalarSimilarity(query.fluxMean, candidate.fluxMean),
                                 scalarSimilarity(query.fluxP90, candidate.fluxP90),
                                 scalarSimilarity(query.crestFactor, candidate.crestFactor) });
    result.dynamics = average({ scalarSimilarity(query.rmsMean, candidate.rmsMean),
                                scalarSimilarity(query.dynamicRange, candidate.dynamicRange) });
    result.stereo = average({ scalarSimilarity(query.stereoWidth, candidate.stereoWidth),
                              scalarSimilarity(0.5f * (query.stereoCorrelation + 1.0f),
                                               0.5f * (candidate.stereoCorrelation + 1.0f)) });
    result.duration = scalarSimilarity(query.logDuration, candidate.logDuration);

    const auto harmonicReliability = std::sqrt(query.harmonicReliability
                                               * candidate.harmonicReliability);
    const auto stereoReliability = std::sqrt(query.stereoReliability
                                             * candidate.stereoReliability);
    std::array rawWeights { weights.harmonic * harmonicReliability, weights.spectral,
                            weights.transient, weights.dynamics,
                            weights.stereo * stereoReliability, weights.duration };
    const auto weightSum = std::max(1.0e-9f, rawWeights[0] + rawWeights[1] + rawWeights[2]
                                             + rawWeights[3] + rawWeights[4] + rawWeights[5]);
    for (std::size_t i = 0; i < rawWeights.size(); ++i)
        result.effectiveWeights[i] = rawWeights[i] / weightSum;

    const std::array groupScores { result.harmonic, result.spectral, result.transient,
                                   result.dynamics, result.stereo, result.duration };
    float weightedLog = 0.0f;
    for (std::size_t i = 0; i < groupScores.size(); ++i)
        weightedLog += result.effectiveWeights[i]
                       * std::log(std::max(1.0e-4f, groupScores[i]));
    result.overall = std::clamp(std::exp(weightedLog), 0.0f, 1.0f);
    return result;
}

std::vector<GhostRankedCandidate> GhostCandidateRanker::rank(
    const GhostDescriptor& query, std::span<const GhostCandidate> candidates,
    std::size_t maximumResults, float minimumSimilarity) const
{
    std::vector<GhostRankedCandidate> ranked;
    ranked.reserve(std::min(maximumResults, candidates.size()));
    for (const auto& candidate : candidates)
    {
        const auto similarity = engine.compare(query, candidate.descriptor);
        if (similarity.overall >= minimumSimilarity)
            ranked.push_back({ candidate.memoryId, candidate.timestampSamples, similarity });
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& first, const auto& second)
    {
        if (std::abs(first.similarity.overall - second.similarity.overall) > 1.0e-7f)
            return first.similarity.overall > second.similarity.overall;
        if (first.timestampSamples != second.timestampSamples)
            return first.timestampSamples > second.timestampSamples;
        return first.memoryId < second.memoryId;
    });
    if (ranked.size() > maximumResults)
        ranked.resize(maximumResults);
    return ranked;
}
}
