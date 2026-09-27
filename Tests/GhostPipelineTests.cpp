#include "../Source/Core/SessionAudioStore.h"
#include "../Source/Core/SpscQueue.h"
#include "../Source/Recall/GhostVoicePool.h"
#include "../Source/analysis/AnalysisInputFifo.h"
#include "../Source/analysis/GhostAnalysisWorker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <thread>
#include <vector>

namespace ghost = longland::ghost;

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr std::uint32_t blockSize = 256;
    constexpr std::uint32_t epoch = 1;
    const auto totalFrames = static_cast<std::uint32_t>(sampleRate * 2.0);
    std::vector<float> input(totalFrames, 0.0f);

    const auto addTone = [&input](double startSeconds, double durationSeconds)
    {
        const auto start = static_cast<std::size_t>(startSeconds * sampleRate);
        const auto frames = static_cast<std::size_t>(durationSeconds * sampleRate);
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            const auto edge = std::min({ 1.0, static_cast<double>(frame) / 240.0,
                                        static_cast<double>(frames - frame - 1) / 240.0 });
            input[start + frame] = static_cast<float>(0.3 * std::max(0.0, edge)
                * std::sin(2.0 * std::numbers::pi * 220.0
                           * static_cast<double>(frame) / sampleRate));
        }
    };

    // Two genuinely identical musical events separated far enough to pass the
    // recency and cooldown gates.
    addTone(0.10, 0.45);
    addTone(1.15, 0.45);

    ghost::AnalysisInputFifo analysisInput;
    ghost::SpscQueue<ghost::RecallCommand, 65> recallOutput;
    ghost::GhostAnalysisWorker worker(analysisInput, recallOutput);
    ghost::SessionAudioStore store;
    store.prepare(static_cast<std::uint32_t>(sampleRate * 20.0), 1);
    worker.prepare(sampleRate, store.capacityFrames());
    worker.updatePolicy(20.0f, 1.0f, 0.70f, 0.5f);
    worker.start();

    for (std::uint32_t start = 0; start < totalFrames; start += blockSize)
    {
        const auto frames = std::min(blockSize, totalFrames - start);
        const float* channels[] { input.data() + start };
        analysisInput.pushAudio(channels, 1, frames, start, epoch);
        store.write(channels, 1, frames);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ghost::RecallCommand command;
    bool received = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (recallOutput.tryPop(command))
        {
            received = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    worker.stop();

    if (!received)
    {
        const auto diagnostics = worker.diagnostics();
        std::cerr << "No similarity-driven recall command; events="
                  << diagnostics.completedEventCount << " best="
                  << diagnostics.bestSimilarity << '\n';
        return 1;
    }
    if (command.similarity.total < 0.70f || !store.contains(command.memory)
        || command.normalizedAge <= 0.0f || command.normalizedAge > 1.0f)
    {
        std::cerr << "Recall command did not identify a valid, age-tagged historical event\n";
        return 1;
    }

    ghost::GhostVoicePool voices;
    voices.prepare(sampleRate);
    if (voices.schedule(command, store) != ghost::GhostVoicePool::ScheduleResult::started)
    {
        std::cerr << "Recognised memory could not be scheduled\n";
        return 1;
    }

    const auto renderFrames = std::min<std::uint32_t>(command.memory.lengthFrames, 4096);
    std::vector<float> recalled(renderFrames, 0.0f);
    float* output[] { recalled.data() };
    voices.renderAdditive(output, 1, renderFrames, store);
    const auto peak = *std::max_element(recalled.begin(), recalled.end(),
                                        [](float left, float right)
                                        { return std::abs(left) < std::abs(right); });
    if (std::abs(peak) < 0.01f)
    {
        std::cerr << "Scheduled historical audio rendered silence\n";
        return 1;
    }

    std::cout << "Similarity-driven pipeline passed: score=" << command.similarity.total
              << " source=" << command.memory.id << '\n';
    return 0;
}
