#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace longland::ghost
{
class LonglandGhostAudioProcessor;

// JUCE's generic editor is intentionally used for the DSP-first phases. It
// exposes real automatable parameters without introducing GUI-owned DSP state.
class LonglandGhostAudioProcessorEditor final : public juce::GenericAudioProcessorEditor
{
public:
    explicit LonglandGhostAudioProcessorEditor(LonglandGhostAudioProcessor& processor);
};
}
