#pragma once

#include "GhostDescriptor.h"

#include <cstddef>
#include <vector>

namespace longland::ghost
{
class GhostFeatureExtractor
{
public:
    void prepare(double sampleRate);

    // right may be null for mono. This function is worker/offline-thread only.
    [[nodiscard]] GhostDescriptor extract(const float* left,
                                          const float* right,
                                          std::size_t sampleCount) const;

private:
    double rate { 48000.0 };
    std::size_t fftSize { 2048 };
    std::size_t frameHop { 512 };
};
}
