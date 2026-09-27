#include "../Source/Core/SessionAudioStore.h"
#include "../Source/Recall/GhostDreamProcessor.h"
#include "../Source/Recall/GhostVoice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace ghost = longland::ghost;

namespace
{
int failures = 0;

void require(const bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

ghost::RecallCommand commandFor(const ghost::MemoryRef& memory,
                                const float dream,
                                const float decay,
                                const float normalizedAge)
{
    ghost::RecallCommand command;
    command.commandId = 37;
    command.memory = memory;
    command.similarity.total = 1.0f;
    command.gain = 1.0f;
    command.fadeInSeconds = 0.0f;
    command.fadeOutSeconds = 0.0f;
    command.dream = dream;
    command.decay = decay;
    command.normalizedAge = normalizedAge;
    return command;
}

std::vector<float> render(const std::vector<float>& source,
                          const float dream,
                          const float decay,
                          const float normalizedAge)
{
    ghost::SessionAudioStore store;
    store.prepare(static_cast<std::uint32_t>(source.size() * 4), 1);
    const float* input[] { source.data() };
    store.write(input, 1, static_cast<std::uint32_t>(source.size()));
    const ghost::MemoryRef memory { 9, 0, static_cast<std::uint32_t>(source.size()),
                                    store.epoch() };

    ghost::GhostVoice voice;
    voice.prepare(8000.0);
    require(voice.start(commandFor(memory, dream, decay, normalizedAge), store),
            "reconstruction voice should start");
    std::vector<float> output(source.size(), 0.0f);
    float* channels[] { output.data() };
    voice.renderAdditive(channels, 1, static_cast<std::uint32_t>(output.size()), store);
    return output;
}

float differenceEnergy(const std::vector<float>& left, const std::vector<float>& right)
{
    float total = 0.0f;
    for (std::size_t index = 0; index < left.size(); ++index)
        total += std::abs(left[index] - right[index]);
    return total / static_cast<float>(left.size());
}

float edgeEnergy(const std::vector<float>& samples)
{
    float total = 0.0f;
    for (std::size_t index = 1; index < samples.size(); ++index)
        total += std::abs(samples[index] - samples[index - 1]);
    return total / static_cast<float>(samples.size() - 1);
}

void testLiteralPathIsExact()
{
    std::vector<float> source(2048);
    for (std::size_t index = 0; index < source.size(); ++index)
        source[index] = 0.55f * std::sin(0.071f * static_cast<float>(index));
    const auto output = render(source, 0.0f, 1.0f, 0.0f);
    require(differenceEnergy(source, output) < 1.0e-7f,
            "Dream 0 and zero effective age must preserve literal playback");
}

void testDreamProducesControlledAbstraction()
{
    std::vector<float> source(4096);
    for (std::size_t index = 0; index < source.size(); ++index)
        source[index] = 0.45f * std::sin(0.19f * static_cast<float>(index))
                      + 0.2f * std::sin(0.71f * static_cast<float>(index));
    const auto output = render(source, 1.0f, 0.0f, 0.0f);
    const auto difference = differenceEnergy(source, output);
    float peak = 0.0f;
    bool finite = true;
    for (const auto sample : output)
    {
        finite = finite && std::isfinite(sample);
        peak = std::max(peak, std::abs(sample));
    }
    require(difference > 0.08f,
            "high Dream must audibly diverge from literal playback");
    require(finite && peak < 1.0f,
            "Dream reconstruction must remain finite and bounded");
}

void testDecayUsesMemoryAge()
{
    std::vector<float> source(4096);
    for (std::size_t index = 0; index < source.size(); ++index)
        source[index] = (index & 1U) == 0U ? 0.7f : -0.7f;

    const auto recent = render(source, 0.0f, 1.0f, 0.0f);
    const auto old = render(source, 0.0f, 1.0f, 1.0f);
    require(differenceEnergy(source, recent) < 1.0e-7f,
            "Decay must not degrade a zero-age memory");
    require(differenceEnergy(source, old) > 0.45f,
            "an old memory must lose fidelity at high Decay");
    require(edgeEnergy(old) < edgeEnergy(recent) * 0.2f,
            "old-memory Decay must remove transient/high-frequency detail");
}

void testParameterSmoothing()
{
    ghost::GhostDreamProcessor processor;
    processor.prepare(48000.0);
    processor.start(0.0f, 0.0f, 1.0f, 1);
    processor.setTargets(0.0f, 1.0f);
    static_cast<void>(processor.advanceFrame());
    const auto first = processor.effectiveDecay();
    for (int frame = 0; frame < 12000; ++frame)
        static_cast<void>(processor.advanceFrame());
    require(first > 0.0f && first < 0.01f,
            "Decay automation must not jump at the first sample");
    require(processor.effectiveDecay() > 0.99f,
            "smoothed Decay must converge to its target");
}
}

int main()
{
    testLiteralPathIsExact();
    testDreamProducesControlledAbstraction();
    testDecayUsesMemoryAge();
    testParameterSmoothing();
    if (failures == 0)
        std::cout << "Dream/Decay tests passed\n";
    return failures == 0 ? 0 : 1;
}
