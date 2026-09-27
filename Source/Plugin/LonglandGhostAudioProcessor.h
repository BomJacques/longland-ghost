#pragma once

#include "../Engine/GhostEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace longland::ghost
{
class LonglandGhostAudioProcessor final : public juce::AudioProcessor
{
public:
    LonglandGhostAudioProcessor();
    ~LonglandGhostAudioProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    [[nodiscard]] juce::AudioProcessorEditor* createEditor() override;
    [[nodiscard]] bool hasEditor() const override { return true; }
    [[nodiscard]] const juce::String getName() const override { return JucePlugin_Name; }
    [[nodiscard]] bool acceptsMidi() const override { return false; }
    [[nodiscard]] bool producesMidi() const override { return false; }
    [[nodiscard]] bool isMidiEffect() const override { return false; }
    [[nodiscard]] double getTailLengthSeconds() const override { return 12.0; }

    [[nodiscard]] int getNumPrograms() override { return 1; }
    [[nodiscard]] int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    [[nodiscard]] const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destinationData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    [[nodiscard]] GhostEngineDiagnostics engineDiagnostics() const noexcept;
    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    [[nodiscard]] GhostEngineParameters readEngineParameters() const noexcept;

    GhostEngine engine;
    std::atomic<float>* memoryParameter {};
    std::atomic<float>* recallParameter {};
    std::atomic<float>* recognitionParameter {};
    std::atomic<float>* ageParameter {};
    std::atomic<float>* decayParameter {};
    std::atomic<float>* dreamParameter {};
    std::atomic<float>* entropyParameter {};
    std::atomic<float>* presenceParameter {};
    std::atomic<float>* repeatVolumeParameter {};
    std::atomic<float>* feedbackParameter {};
    std::atomic<float>* memoryTypeParameter {};
    std::atomic<bool> sessionResetRequested { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LonglandGhostAudioProcessor)
};
}
