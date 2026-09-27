#pragma once

#include "GhostDescriptor.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace longland::ghost
{
class GhostSimilarityEngine
{
public:
    explicit GhostSimilarityEngine(GhostSimilarityWeights weights = {}) : weights(weights) {}
    [[nodiscard]] GhostSimilarityResult compare(const GhostDescriptor& query,
                                                const GhostDescriptor& candidate) const noexcept;

private:
    GhostSimilarityWeights weights;
};

struct GhostCandidate
{
    std::uint64_t memoryId {};
    std::uint64_t timestampSamples {};
    GhostDescriptor descriptor;
};

struct GhostRankedCandidate
{
    std::uint64_t memoryId {};
    std::uint64_t timestampSamples {};
    GhostSimilarityResult similarity;
};

class GhostCandidateRanker
{
public:
    explicit GhostCandidateRanker(GhostSimilarityEngine engine = GhostSimilarityEngine {})
        : engine(engine) {}
    [[nodiscard]] std::vector<GhostRankedCandidate> rank(const GhostDescriptor& query,
                                                        std::span<const GhostCandidate> candidates,
                                                        std::size_t maximumResults,
                                                        float minimumSimilarity) const;

private:
    GhostSimilarityEngine engine;
};
}
