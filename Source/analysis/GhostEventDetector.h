#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace longland::ghost
{
struct GhostDetectedEvent
{
    std::uint64_t startSample {};
    std::uint64_t endSample {};
    float peak {};
    float meanRms {};
    float onsetStrength {};
    float distinctiveness {};
};

class GhostEventDetector
{
public:
    void prepare(double sampleRate);
    void reset() noexcept;

    // Intended for the analysis worker, not the realtime callback. Results are
    // block-size invariant because frames are accumulated on an internal hop grid.
    [[nodiscard]] std::vector<GhostDetectedEvent> process(const float* mono,
                                                          std::size_t sampleCount);
    [[nodiscard]] std::vector<GhostDetectedEvent> flush();

    [[nodiscard]] std::size_t hopSize() const noexcept { return hopSamples; }

private:
    void analyseHop(std::vector<GhostDetectedEvent>& completed);
    void finishEvent(std::uint64_t end, std::vector<GhostDetectedEvent>& completed);

    double rate { 48000.0 };
    std::size_t hopSamples { 256 };
    std::vector<float> hopBuffer;
    std::size_t hopFill {};
    std::uint64_t samplePosition {};
    std::uint64_t eventStart {};
    std::uint64_t lastSplit {};
    float previousSample {};
    float previousDifferenceEnergy {};
    float fastPower {};
    float slowPower {};
    float noisePower { 1.0e-10f };
    float eventPeak {};
    float eventPowerSum {};
    float eventOnset {};
    std::uint32_t eventHops {};
    std::uint32_t silentHops {};
    std::uint32_t sustainedHops {};
    bool active {};
};
}
