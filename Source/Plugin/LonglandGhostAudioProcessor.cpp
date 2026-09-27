#include "LonglandGhostAudioProcessor.h"
#include "LonglandGhostAudioProcessorEditor.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace longland::ghost
{
namespace
{
constexpr auto stateTreeName = "LONGLAND_GHOST_STATE";
constexpr auto stateSchemaProperty = "schemaVersion";
constexpr int stateSchemaVersion = 3;

GhostMemoryType memoryTypeFromParameter(float value) noexcept
{
    const auto index = std::clamp(static_cast<int>(std::lround(value)), 0, 2);
    return static_cast<GhostMemoryType>(index);
}
}

LonglandGhostAudioProcessor::LonglandGhostAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, stateTreeName, createParameterLayout())
{
    memoryParameter = parameters.getRawParameterValue("memory");
    recallParameter = parameters.getRawParameterValue("recall");
    recognitionParameter = parameters.getRawParameterValue("recognition");
    ageParameter = parameters.getRawParameterValue("age");
    decayParameter = parameters.getRawParameterValue("decay");
    dreamParameter = parameters.getRawParameterValue("dream");
    entropyParameter = parameters.getRawParameterValue("entropy");
    presenceParameter = parameters.getRawParameterValue("presence");
    repeatVolumeParameter = parameters.getRawParameterValue("repeatVolume");
    feedbackParameter = parameters.getRawParameterValue("feedback");
    memoryTypeParameter = parameters.getRawParameterValue("memoryType");
    jassert(memoryParameter != nullptr && recallParameter != nullptr
            && recognitionParameter != nullptr && ageParameter != nullptr
            && decayParameter != nullptr && dreamParameter != nullptr
            && entropyParameter != nullptr
            && presenceParameter != nullptr && repeatVolumeParameter != nullptr
            && feedbackParameter != nullptr && memoryTypeParameter != nullptr);
}

LonglandGhostAudioProcessor::~LonglandGhostAudioProcessor()
{
    engine.release();
}

juce::AudioProcessorValueTreeState::ParameterLayout
LonglandGhostAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
                   juce::ParameterID { "memory", 1 }, "Memory",
                   juce::NormalisableRange<float> { 30.0f, 120.0f, 1.0f, 0.45f }, 60.0f,
                   juce::AudioParameterFloatAttributes().withLabel("s")),
               std::make_unique<juce::AudioParameterFloat>(
                   juce::ParameterID { "recall", 1 }, "Recall",
                   juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.25f),
               std::make_unique<juce::AudioParameterFloat>(
                   juce::ParameterID { "recognition", 1 }, "Recognition",
                   juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.72f),
               std::make_unique<juce::AudioParameterFloat>(
                   juce::ParameterID { "age", 1 }, "Age",
                   juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.5f),
               std::make_unique<juce::AudioParameterFloat>(
                   juce::ParameterID { "decay", 1 }, "Decay",
                   juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.25f),
               std::make_unique<juce::AudioParameterFloat>(
                   juce::ParameterID { "dream", 1 }, "Dream",
                   juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f),
               std::make_unique<juce::AudioParameterFloat>(
                   juce::ParameterID { "entropy", 1 }, "Entropy",
                   juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f),
               std::make_unique<juce::AudioParameterFloat>(
                   juce::ParameterID { "presence", 1 }, "Presence",
                   juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.25f),
               std::make_unique<juce::AudioParameterFloat>(
                   juce::ParameterID { "repeatVolume", 1 }, "Repeat Volume",
                   juce::NormalisableRange<float> { 0.0f, 2.0f, 0.001f }, 1.0f,
                   juce::AudioParameterFloatAttributes().withLabel("x")),
               std::make_unique<juce::AudioParameterFloat>(
                   juce::ParameterID { "feedback", 1 }, "Feedback",
                   juce::NormalisableRange<float> { 0.0f, 0.85f, 0.001f }, 0.0f),
               std::make_unique<juce::AudioParameterChoice>(
                   juce::ParameterID { "memoryType", 1 }, "Memory Type",
                   juce::StringArray { "Literal", "Emotional", "Dream" }, 0));
    return layout;
}

void LonglandGhostAudioProcessor::prepareToPlay(double sampleRate,
                                                 int maximumExpectedSamplesPerBlock)
{
    engine.prepare(sampleRate,
                   maximumExpectedSamplesPerBlock,
                   std::max(1, getTotalNumInputChannels()));
}

void LonglandGhostAudioProcessor::releaseResources()
{
    engine.release();
}

bool LonglandGhostAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

GhostEngineParameters LonglandGhostAudioProcessor::readEngineParameters() const noexcept
{
    return {
        memoryParameter->load(std::memory_order_relaxed),
        recallParameter->load(std::memory_order_relaxed),
        recognitionParameter->load(std::memory_order_relaxed),
        ageParameter->load(std::memory_order_relaxed),
        decayParameter->load(std::memory_order_relaxed),
        dreamParameter->load(std::memory_order_relaxed),
        entropyParameter->load(std::memory_order_relaxed),
        presenceParameter->load(std::memory_order_relaxed),
        repeatVolumeParameter->load(std::memory_order_relaxed),
        feedbackParameter->load(std::memory_order_relaxed),
        memoryTypeFromParameter(memoryTypeParameter->load(std::memory_order_relaxed))
    };
}

void LonglandGhostAudioProcessor::processBlock(juce::AudioBuffer<float>& audio,
                                                juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        audio.clear(channel, 0, audio.getNumSamples());

    if (sessionResetRequested.exchange(false, std::memory_order_acq_rel))
        engine.resetSession();
    engine.process(audio, readEngineParameters());
}

void LonglandGhostAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& audio,
                                                        juce::MidiBuffer& midi)
{
    midi.clear();
    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        audio.clear(channel, 0, audio.getNumSamples());

    if (sessionResetRequested.exchange(false, std::memory_order_acq_rel))
        engine.resetSession();
    engine.processBypassed(audio, readEngineParameters());
}

juce::AudioProcessorEditor* LonglandGhostAudioProcessor::createEditor()
{
    return new LonglandGhostAudioProcessorEditor(*this);
}

void LonglandGhostAudioProcessor::getStateInformation(juce::MemoryBlock& destinationData)
{
    auto state = parameters.copyState();
    state.setProperty(stateSchemaProperty, stateSchemaVersion, nullptr);
    juce::MemoryOutputStream stream(destinationData, false);
    state.writeToStream(stream);
}

void LonglandGhostAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto restored = juce::ValueTree::readFromData(data, static_cast<std::size_t>(sizeInBytes));
    if (!restored.isValid() || restored.getType() != juce::Identifier(stateTreeName))
        return;

    const auto schema = static_cast<int>(restored.getProperty(stateSchemaProperty, 1));
    if (schema <= 0 || schema > stateSchemaVersion)
        return;

    parameters.replaceState(restored);
    // Volatile audio is never part of project state. Defer the session reset
    // to the next audio callback so no worker join/allocation occurs here.
    sessionResetRequested.store(true, std::memory_order_release);
}

GhostEngineDiagnostics LonglandGhostAudioProcessor::engineDiagnostics() const noexcept
{
    return engine.diagnostics();
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new longland::ghost::LonglandGhostAudioProcessor();
}
