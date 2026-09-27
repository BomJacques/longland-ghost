#include "LonglandGhostAudioProcessorEditor.h"
#include "LonglandGhostAudioProcessor.h"

namespace longland::ghost
{
LonglandGhostAudioProcessorEditor::LonglandGhostAudioProcessorEditor(
    LonglandGhostAudioProcessor& processor)
    : juce::GenericAudioProcessorEditor(processor)
{
    setSize(420, 350);
}
}
