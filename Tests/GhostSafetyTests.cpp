#include "../Source/Core/SpscQueue.h"
#include "../Source/Engine/GhostEngine.h"
#include "../Source/analysis/AnalysisInputFifo.h"
#include "../Source/analysis/GhostAnalysisWorker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>

namespace ghost = longland::ghost;

namespace
{
ghost::RecallCommand recallCommand(const std::uint64_t id,
                                   const std::uint64_t startFrame,
                                   const std::uint32_t lengthFrames,
                                   const std::uint32_t epoch)
{
    ghost::RecallCommand command;
    command.commandId = id;
    command.queryEventId = id + 100;
    command.memory = { id, startFrame, lengthFrames, epoch };
    command.similarity.total = 1.0f;
    command.playbackRate = 1.0f;
    command.gain = 1.0f;
    command.fadeInSeconds = 0.0f;
    command.fadeOutSeconds = 0.0f;
    return command;
}

bool testDryAndBypass()
{
    ghost::GhostEngine engine;
    engine.prepare(48000.0, 256, 1);
    ghost::GhostEngineParameters parameters;
    parameters.recall = 0.0f;
    parameters.presence = 1.0f;

    juce::AudioBuffer<float> block(1, 256);
    block.clear();
    for (int frame = 0; frame < block.getNumSamples(); ++frame)
        block.setSample(0, frame, 0.25f * std::sin(0.03f * static_cast<float>(frame)));
    juce::AudioBuffer<float> reference;
    reference.makeCopyOf(block);

    engine.process(block, parameters);
    for (int frame = 0; frame < block.getNumSamples(); ++frame)
        if (block.getSample(0, frame) != reference.getSample(0, frame))
            return false;

    engine.processBypassed(block, parameters);
    for (int frame = 0; frame < block.getNumSamples(); ++frame)
        if (block.getSample(0, frame) != reference.getSample(0, frame))
            return false;

    return engine.diagnostics().capturedFrames == 512;
}

float recalledTailPeak(const float repeatVolume)
{
    auto engine = std::make_unique<ghost::GhostEngine>();
    engine->prepare(1000.0, 64, 1);
    ghost::GhostEngineParameters parameters;
    parameters.recall = 0.0f;
    parameters.presence = 1.0f;
    parameters.repeatVolume = repeatVolume;

    juce::AudioBuffer<float> source(1, 64);
    source.clear();
    for (int frame = 0; frame < 32; ++frame)
        source.setSample(0, frame, 0.25f);
    engine->process(source, parameters); // Settles both 20 ms gain ramps.

    const auto epoch = engine->diagnostics().epoch;
    if (!engine->enqueueRecall(recallCommand(1, 0, 32, epoch)))
        return -1.0f;

    juce::AudioBuffer<float> recalled(1, 64);
    recalled.clear();
    engine->process(recalled, parameters);

    float peak = 0.0f;
    for (int frame = 24; frame < 32; ++frame)
        peak = std::max(peak, std::abs(recalled.getSample(0, frame)));
    return peak;
}

bool testRepeatVolumeAndFeedbackBounds()
{
    const auto muted = recalledTailPeak(0.0f);
    const auto boosted = recalledTailPeak(2.0f);
    if (muted < 0.0f || boosted < 0.0f || muted > 1.0e-6f || boosted < 0.45f)
        return false;

    auto engine = std::make_unique<ghost::GhostEngine>();
    engine->prepare(1000.0, 64, 1);
    ghost::GhostEngineParameters parameters;
    parameters.recall = 0.0f;
    parameters.presence = 1.0f;
    parameters.repeatVolume = 1.0f;
    parameters.feedback = 100.0f; // Deliberately hostile; the engine clamps it.

    juce::AudioBuffer<float> source(1, 64);
    source.clear();
    for (int frame = 0; frame < 32; ++frame)
        source.setSample(0, frame, 10.0f);
    engine->process(source, parameters);

    const auto epoch = engine->diagnostics().epoch;
    if (!engine->enqueueRecall(recallCommand(2, 0, 32, epoch)))
        return false;

    juce::AudioBuffer<float> firstGeneration(1, 64);
    firstGeneration.clear();
    engine->process(firstGeneration, parameters);

    // The exact feedback-inclusive frames captured above must be addressable
    // through their MemoryRef. Recalling them also exposes the hard stability
    // bound: tanh(0.85) is approximately 0.691.
    if (!engine->enqueueRecall(recallCommand(3, 64, 32, epoch)))
        return false;
    parameters.feedback = 0.0f;
    juce::AudioBuffer<float> secondGeneration(1, 64);
    secondGeneration.clear();
    engine->process(secondGeneration, parameters);

    float tailPeak = 0.0f;
    for (int frame = 24; frame < 32; ++frame)
        tailPeak = std::max(tailPeak, std::abs(secondGeneration.getSample(0, frame)));
    return tailPeak > 0.65f && tailPeak < 0.70f;
}

bool testBypassSuppressesFeedbackCapture()
{
    auto engine = std::make_unique<ghost::GhostEngine>();
    engine->prepare(1000.0, 64, 1);
    ghost::GhostEngineParameters parameters;
    parameters.recall = 0.0f;
    parameters.presence = 1.0f;
    parameters.feedback = 0.85f;

    juce::AudioBuffer<float> source(1, 64);
    source.clear();
    for (int frame = 0; frame < 32; ++frame)
        source.setSample(0, frame, 0.5f);
    engine->process(source, parameters);

    const auto epoch = engine->diagnostics().epoch;
    if (!engine->enqueueRecall(recallCommand(4, 0, 32, epoch)))
        return false;
    juce::AudioBuffer<float> bypassed(1, 64);
    bypassed.clear();
    engine->processBypassed(bypassed, parameters);
    for (int frame = 0; frame < bypassed.getNumSamples(); ++frame)
        if (bypassed.getSample(0, frame) != 0.0f)
            return false;

    if (!engine->enqueueRecall(recallCommand(5, 64, 32, epoch)))
        return false;
    juce::AudioBuffer<float> recalledBypassCapture(1, 64);
    recalledBypassCapture.clear();
    engine->process(recalledBypassCapture, parameters);
    for (int frame = 0; frame < recalledBypassCapture.getNumSamples(); ++frame)
        if (recalledBypassCapture.getSample(0, frame) != 0.0f)
            return false;
    return true;
}

bool testStationaryNoiseDoesNotRecall()
{
    constexpr double sampleRate = 48000.0;
    constexpr std::uint32_t blockSize = 512;
    constexpr std::uint64_t noiseFrames = static_cast<std::uint64_t>(sampleRate * 9.0);
    constexpr std::uint64_t totalFrames = static_cast<std::uint64_t>(sampleRate * 9.5);

    ghost::AnalysisInputFifo input;
    ghost::SpscQueue<ghost::RecallCommand, 65> output;
    ghost::GhostAnalysisWorker worker(input, output);
    worker.prepare(sampleRate, static_cast<std::uint32_t>(sampleRate * 120.0));
    worker.updatePolicy(120.0f, 1.0f, 0.70f, 0.5f);
    worker.start();

    std::uint32_t random = 0x6d2b79f5u;
    std::array<float, blockSize> samples {};
    for (std::uint64_t start = 0; start < totalFrames; start += blockSize)
    {
        const auto frames = static_cast<std::uint32_t>(std::min<std::uint64_t>(blockSize,
                                                                               totalFrames - start));
        for (std::uint32_t frame = 0; frame < frames; ++frame)
        {
            if (start + frame >= noiseFrames)
            {
                samples[frame] = 0.0f;
                continue;
            }
            random ^= random << 13u;
            random ^= random >> 17u;
            random ^= random << 5u;
            samples[frame] = 0.2f * (static_cast<float>(random)
                           / static_cast<float>(UINT32_MAX) * 2.0f - 1.0f);
        }
        const float* channels[] { samples.data() };
        input.pushAudio(channels, 1, frames, start, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (worker.diagnostics().completedEventCount < 2
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    ghost::RecallCommand command;
    const bool recalled = output.tryPop(command);
    const auto diagnostics = worker.diagnostics();
    const auto events = diagnostics.completedEventCount;
    worker.stop();
    if (events < 2 || recalled)
        std::cerr << "noise events=" << events << " recalled=" << recalled
                  << " best=" << diagnostics.bestSimilarity
                  << " harmonic=" << diagnostics.harmonicSimilarity
                  << " spectral=" << diagnostics.spectralSimilarity
                  << " transient=" << diagnostics.transientSimilarity << '\n';
    return events >= 2 && !recalled;
}
}

int main()
{
    if (!testDryAndBypass())
    {
        std::cerr << "Dry passthrough or bypass safety failed\n";
        return 1;
    }
    if (!testStationaryNoiseDoesNotRecall())
    {
        std::cerr << "Stationary broadband material caused recall or did not segment\n";
        return 1;
    }
    if (!testRepeatVolumeAndFeedbackBounds())
    {
        std::cerr << "Repeat volume, feedback recapture, or stability bound failed\n";
        return 1;
    }
    if (!testBypassSuppressesFeedbackCapture())
    {
        std::cerr << "Bypass emitted or recaptured feedback audio\n";
        return 1;
    }
    std::cout << "Ghost safety tests passed\n";
    return 0;
}
