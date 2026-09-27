#include "GhostEngine.h"
#include "GhostEntropyEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace longland::ghost
{
namespace
{
float boundedParameter(const float value,
                       const float minimum,
                       const float maximum,
                       const float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
}

GhostEngine::GhostEngine()
    : analysisWorker(analysisInput, recallCommands)
{
}

GhostEngine::~GhostEngine()
{
    release();
}

void GhostEngine::prepare(double sampleRate, int maximumBlockSize, int channelCount)
{
    release();

    preparedChannels = std::clamp(channelCount, 1, static_cast<int>(SessionAudioStore::maxChannels));
    scratchFrames = std::max(1, maximumBlockSize);
    const auto requestedFrames = std::ceil(sampleRate * maximumSessionSeconds);
    const auto boundedFrames = std::min(requestedFrames,
                                        static_cast<double>(std::numeric_limits<std::uint32_t>::max()));

    audioStore.prepare(static_cast<std::uint32_t>(boundedFrames),
                       static_cast<std::uint32_t>(preparedChannels));
    wetScratch.setSize(preparedChannels, scratchFrames, false, true, false);
    captureScratch.setSize(preparedChannels, scratchFrames, false, true, false);
    wetScratch.clear();
    captureScratch.clear();
    voicePool.prepare(sampleRate);
    analysisInput.reset();
    recallCommands.reset();
    analysisWorker.prepare(sampleRate, audioStore.capacityFrames());

    streamFrame = 0;
    streamEpoch = audioStore.epoch();
    rejectedRecallCommands.store(0, std::memory_order_relaxed);
    publishedCapturedFrames.store(0, std::memory_order_relaxed);
    publishedActiveVoices.store(0, std::memory_order_relaxed);
    publishedEpoch.store(streamEpoch, std::memory_order_relaxed);
    presence.reset(sampleRate, 0.02);
    presence.setCurrentAndTargetValue(0.0f);
    repeatVolume.reset(sampleRate, 0.02);
    repeatVolume.setCurrentAndTargetValue(1.0f);
    feedback.reset(sampleRate, 0.02);
    feedback.setCurrentAndTargetValue(0.0f);
    currentDecay = 0.25f;
    currentDream = 0.0f;
    currentEntropy = 0.0f;
    prepared = true;
    analysisWorker.start();
}

void GhostEngine::release() noexcept
{
    analysisWorker.stop();
    voicePool.clear();
    prepared = false;
}

bool GhostEngine::enqueueRecall(const RecallCommand& command) noexcept
{
    if (recallCommands.tryPush(command))
        return true;

    rejectedRecallCommands.fetch_add(1, std::memory_order_relaxed);
    return false;
}

GhostEngineDiagnostics GhostEngine::diagnostics() const noexcept
{
    return {
        analysisWorker.diagnostics(),
        publishedCapturedFrames.load(std::memory_order_relaxed),
        rejectedRecallCommands.load(std::memory_order_relaxed),
        publishedEpoch.load(std::memory_order_relaxed),
        publishedActiveVoices.load(std::memory_order_relaxed)
    };
}

void GhostEngine::resetSession() noexcept
{
    if (!prepared)
        return;

    voicePool.clear();
    audioStore.clear();
    streamFrame = 0;
    streamEpoch = audioStore.epoch();
    presence.setCurrentAndTargetValue(0.0f);
    repeatVolume.setCurrentAndTargetValue(1.0f);
    feedback.setCurrentAndTargetValue(0.0f);
    currentDecay = 0.25f;
    currentDream = 0.0f;
    currentEntropy = 0.0f;
    publishedCapturedFrames.store(0, std::memory_order_relaxed);
    publishedActiveVoices.store(0, std::memory_order_relaxed);
    publishedEpoch.store(streamEpoch, std::memory_order_relaxed);
}

void GhostEngine::consumeRecallCommands() noexcept
{
    RecallCommand command;

    while (recallCommands.tryPop(command))
    {
        if (!command.lineage.isValid())
        {
            const auto parent = GhostEntropyEngine::original(command.memory.id);
            command.lineage = GhostEntropyEngine::deriveChild(
                parent, command.memory.id, currentEntropy, command.commandId);
            command.playbackRate = std::clamp(
                command.playbackRate * GhostEntropyEngine::playbackRate(command.lineage.mutation),
                0.125f, 8.0f);
        }
        command.decay = currentDecay;
        command.dream = std::clamp(currentDream
                                   + 0.70f * std::abs(command.lineage.mutation.blur),
                                   0.0f, 1.0f);
        const auto result = voicePool.schedule(command, audioStore);
        if (result != GhostVoicePool::ScheduleResult::started
            && result != GhostVoicePool::ScheduleResult::queuedAfterVoiceRelease)
            rejectedRecallCommands.fetch_add(1, std::memory_order_relaxed);
    }
}

void GhostEngine::process(juce::AudioBuffer<float>& audio,
                          const GhostEngineParameters& parameters) noexcept
{
    if (!prepared || audio.getNumSamples() == 0)
        return;

    const auto channels = std::min(audio.getNumChannels(), preparedChannels);
    if (channels == 0)
        return;

    presence.setTargetValue(boundedParameter(parameters.presence, 0.0f, 1.0f, 0.0f));
    repeatVolume.setTargetValue(boundedParameter(parameters.repeatVolume, 0.0f, 2.0f, 1.0f));
    // Keeping the recirculation coefficient below unity makes every
    // feedback-only generation decay. The recalled contribution is also
    // soft-bounded in processChunk, so malformed or stacked voices cannot
    // turn the session store into an unstable loop.
    feedback.setTargetValue(boundedParameter(parameters.feedback, 0.0f, 0.85f, 0.0f));
    currentDecay = boundedParameter(parameters.decay, 0.0f, 1.0f, 0.25f);
    currentDream = boundedParameter(parameters.dream, 0.0f, 1.0f, 0.0f);
    currentEntropy = boundedParameter(parameters.entropy, 0.0f, 1.0f, 0.0f);
    analysisWorker.updatePolicy(parameters.memorySeconds, parameters.recall,
                                parameters.recognition, parameters.age,
                                parameters.memoryType, currentEntropy);

    consumeRecallCommands();

    for (int start = 0; start < audio.getNumSamples(); start += scratchFrames)
        processChunk(audio, start, std::min(scratchFrames, audio.getNumSamples() - start));

    streamFrame += static_cast<std::uint64_t>(audio.getNumSamples());
    publishedCapturedFrames.store(audioStore.absoluteWriteFrame(), std::memory_order_relaxed);
    publishedActiveVoices.store(static_cast<std::uint32_t>(voicePool.activeVoiceCount()),
                                std::memory_order_relaxed);
}

void GhostEngine::processBypassed(juce::AudioBuffer<float>& audio,
                                  const GhostEngineParameters& parameters) noexcept
{
    if (!prepared || audio.getNumSamples() == 0)
        return;

    const auto channels = std::min(audio.getNumChannels(), preparedChannels);
    if (channels == 0)
        return;

    presence.setCurrentAndTargetValue(0.0f);
    feedback.setCurrentAndTargetValue(0.0f);
    repeatVolume.setCurrentAndTargetValue(
        boundedParameter(parameters.repeatVolume, 0.0f, 2.0f, 1.0f));
    currentDecay = boundedParameter(parameters.decay, 0.0f, 1.0f, 0.25f);
    currentDream = boundedParameter(parameters.dream, 0.0f, 1.0f, 0.0f);
    currentEntropy = boundedParameter(parameters.entropy, 0.0f, 1.0f, 0.0f);
    analysisWorker.updatePolicy(parameters.memorySeconds, parameters.recall,
                                parameters.recognition, parameters.age,
                                parameters.memoryType, currentEntropy);
    analysisInput.pushAudio(audio.getArrayOfReadPointers(),
                            static_cast<std::uint32_t>(channels),
                            static_cast<std::uint32_t>(audio.getNumSamples()),
                            streamFrame,
                            streamEpoch);
    consumeRecallCommands();

    for (int start = 0; start < audio.getNumSamples(); start += scratchFrames)
    {
        const auto frames = std::min(scratchFrames, audio.getNumSamples() - start);
        wetScratch.clear(0, frames);
        voicePool.renderAdditive(wetScratch.getArrayOfWritePointers(),
                                 static_cast<std::uint32_t>(channels),
                                 static_cast<std::uint32_t>(frames),
                                 audioStore, currentDream, currentDecay);

        std::array<const float*, SessionAudioStore::maxChannels> dryPointers {};
        for (int channel = 0; channel < channels; ++channel)
            dryPointers[static_cast<std::size_t>(channel)] = audio.getReadPointer(channel, start);
        audioStore.write(dryPointers.data(), static_cast<std::uint32_t>(channels),
                         static_cast<std::uint32_t>(frames));
    }

    streamFrame += static_cast<std::uint64_t>(audio.getNumSamples());
    publishedCapturedFrames.store(audioStore.absoluteWriteFrame(), std::memory_order_relaxed);
    publishedActiveVoices.store(static_cast<std::uint32_t>(voicePool.activeVoiceCount()),
                                std::memory_order_relaxed);
}

void GhostEngine::processChunk(juce::AudioBuffer<float>& audio,
                               int startFrame,
                               int frameCount) noexcept
{
    const auto channels = std::min(audio.getNumChannels(), preparedChannels);
    wetScratch.clear(0, frameCount);
    voicePool.renderAdditive(wetScratch.getArrayOfWritePointers(),
                             static_cast<std::uint32_t>(channels),
                             static_cast<std::uint32_t>(frameCount),
                             audioStore, currentDream, currentDecay);

    const auto activeVoices = std::max<std::size_t>(1, voicePool.activeVoiceCount());
    const auto voiceBudget = 1.0f / static_cast<float>(activeVoices);
    float maximumFeedbackGain = 0.0f;

    for (int frame = 0; frame < frameCount; ++frame)
    {
        const auto mix = presence.getNextValue();
        const auto wetGain = mix * repeatVolume.getNextValue();
        const auto feedbackGain = feedback.getNextValue();
        maximumFeedbackGain = std::max(maximumFeedbackGain, feedbackGain);

        for (int channel = 0; channel < channels; ++channel)
        {
            auto* destination = audio.getWritePointer(channel, startFrame);
            const auto wet = std::tanh(wetScratch.getSample(channel, frame) * voiceBudget);
            const auto dry = std::isfinite(destination[frame]) ? destination[frame] : 0.0f;
            captureScratch.setSample(channel, frame, dry + wet * feedbackGain);
            // Presence is an additive ghost contribution: dry passthrough
            // remains unity even when no recall voice is active.
            destination[frame] += wet * wetGain;
        }
    }

    std::array<const float*, SessionAudioStore::maxChannels> capturePointers {};
    for (int channel = 0; channel < channels; ++channel)
        capturePointers[static_cast<std::size_t>(channel)] = captureScratch.getReadPointer(channel);

    // Recall is rendered before advancing the ring, preserving the lease that
    // was validated when a voice was scheduled. Crucially, the exact same
    // feedback-inclusive samples and absolute frame range are published to the
    // analyzer and the store, so a descriptor's MemoryRef names its own audio.
    auto provenance = voicePool.captureProvenance();
    provenance.contribution *= maximumFeedbackGain;
    if (provenance.contribution <= 0.01f)
        provenance = {};
    analysisInput.pushAudio(capturePointers.data(),
                            static_cast<std::uint32_t>(channels),
                            static_cast<std::uint32_t>(frameCount),
                            streamFrame + static_cast<std::uint64_t>(startFrame),
                            streamEpoch, provenance);
    audioStore.write(capturePointers.data(),
                     static_cast<std::uint32_t>(channels),
                     static_cast<std::uint32_t>(frameCount));
}
}
