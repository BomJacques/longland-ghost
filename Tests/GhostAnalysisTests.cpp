#include "../Source/analysis/GhostEventDetector.h"
#include "../Source/analysis/GhostFeatureExtractor.h"
#include "../Source/analysis/GhostSimilarityEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace longland::ghost;

namespace
{
constexpr double sampleRate = 48000.0;

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<float> sine(float frequency, float seconds, float amplitude = 0.3f)
{
    const auto count = static_cast<std::size_t>(std::llround(seconds * sampleRate));
    std::vector<float> signal(count);
    for (std::size_t i = 0; i < count; ++i)
        signal[i] = amplitude * std::sin(2.0f * std::numbers::pi_v<float> * frequency
                                        * static_cast<float>(i / sampleRate));
    return signal;
}

std::vector<float> chord(std::initializer_list<float> frequencies, float seconds)
{
    auto signal = sine(*frequencies.begin(), seconds, 0.0f);
    const auto scale = 0.35f / static_cast<float>(frequencies.size());
    for (float frequency : frequencies)
        for (std::size_t i = 0; i < signal.size(); ++i)
            signal[i] += scale * std::sin(2.0f * std::numbers::pi_v<float> * frequency
                                         * static_cast<float>(i / sampleRate));
    return signal;
}

std::vector<float> noise(float seconds, std::uint32_t seed = 0x12345678u)
{
    const auto count = static_cast<std::size_t>(std::llround(seconds * sampleRate));
    std::vector<float> signal(count);
    for (auto& value : signal)
    {
        seed ^= seed << 13u;
        seed ^= seed >> 17u;
        seed ^= seed << 5u;
        value = 0.18f * (static_cast<float>(seed) / static_cast<float>(UINT32_MAX) * 2.0f - 1.0f);
    }
    return signal;
}

GhostDescriptor describe(const std::vector<float>& signal, double rate = sampleRate)
{
    GhostFeatureExtractor extractor;
    extractor.prepare(rate);
    return extractor.extract(signal.data(), nullptr, signal.size());
}

std::vector<GhostDetectedEvent> detect(const std::vector<float>& signal, std::size_t blockSize)
{
    GhostEventDetector detector;
    detector.prepare(sampleRate);
    std::vector<GhostDetectedEvent> events;
    for (std::size_t position = 0; position < signal.size(); position += blockSize)
    {
        const auto count = std::min(blockSize, signal.size() - position);
        auto completed = detector.process(signal.data() + position, count);
        events.insert(events.end(), completed.begin(), completed.end());
    }
    auto completed = detector.flush();
    events.insert(events.end(), completed.begin(), completed.end());
    return events;
}

void testSilenceAndBlockInvariance()
{
    std::vector<float> silence(static_cast<std::size_t>(180.0 * sampleRate), 0.0f);
    require(detect(silence, 127).empty(), "silence generated events");

    std::vector<float> signal(static_cast<std::size_t>(2.0 * sampleRate), 0.0f);
    auto tone = sine(220.0f, 0.45f);
    std::copy(tone.begin(), tone.end(), signal.begin() + static_cast<std::ptrdiff_t>(0.25 * sampleRate));
    auto reference = detect(signal, 1);
    require(!reference.empty(), "tone was not detected");
    for (auto block : { 17u, 64u, 127u, 512u, 1024u })
    {
        const auto candidate = detect(signal, block);
        require(candidate.size() == reference.size(), "event count depends on host block size");
        for (std::size_t i = 0; i < candidate.size(); ++i)
        {
            require(candidate[i].startSample == reference[i].startSample,
                    "event start depends on host block size");
            require(candidate[i].endSample == reference[i].endSample,
                    "event end depends on host block size");
        }
    }
}

void testSimilarityRelationships()
{
    GhostSimilarityEngine engine;
    const auto a = describe(chord({ 110.0f, 164.8138f, 220.0f, 261.6256f, 329.6276f }, 0.8f)); // Am9
    const auto repeatedA = describe(chord({ 110.0f, 164.8138f, 220.0f, 261.6256f, 329.6276f }, 0.8f));
    const auto related = describe(chord({ 110.0f, 164.8138f, 220.0f, 261.6256f }, 0.8f)); // Am7
    const auto unrelated = describe(noise(0.8f));
    const auto identicalScore = engine.compare(a, repeatedA);
    const auto relatedScore = engine.compare(a, related);
    const auto unrelatedScore = engine.compare(a, unrelated);
    require(identicalScore.overall > 0.999f, "identical signal did not score approximately one");
    require(relatedScore.overall > unrelatedScore.overall + 0.20f,
            "Am9 did not score materially above deterministic white noise for Am7");
    require(relatedScore.harmonic > unrelatedScore.harmonic + 0.25f,
            "harmonic contribution did not explain the related-chord match (related="
            + std::to_string(relatedScore.harmonic) + ", unrelated="
            + std::to_string(unrelatedScore.harmonic) + ")");

    const auto base = describe(sine(220.0f, 0.8f));
    const auto octave = describe(sine(440.0f, 0.8f));
    const auto tritone = describe(sine(311.127f, 0.8f));
    const auto octaveScore = engine.compare(base, octave);
    const auto tritoneScore = engine.compare(base, tritone);
    require(octaveScore.harmonic > 0.95f, "octave equivalence was lost in chroma (score="
            + std::to_string(octaveScore.harmonic) + ", h="
            + std::to_string(base.harmonicity) + "/" + std::to_string(octave.harmonicity)
            + ", r=" + std::to_string(base.harmonicReliability) + "/"
            + std::to_string(octave.harmonicReliability) + ")");
    require(octaveScore.harmonic > tritoneScore.harmonic + 0.60f,
            "tritone produced an octave-like harmonic score");

    auto quieter = chord({ 110.0f, 164.8138f, 220.0f, 261.6256f, 329.6276f }, 0.8f);
    for (auto& sample : quieter)
        sample *= 0.25f;
    const auto quietScore = engine.compare(a, describe(quieter));
    require(quietScore.harmonic > 0.999f && quietScore.spectral > 0.98f,
            "fixed level change altered harmonic or spectral shape");
    require(quietScore.dynamics < 0.95f, "dynamics contribution ignored a 12 dB level change");
}

GhostDescriptor controlledDescriptor()
{
    GhostDescriptor descriptor;
    descriptor.chroma[0] = 1.0f;
    descriptor.spectralBands[0] = 1.0f;
    descriptor.centroidMean = 0.2f;
    descriptor.centroidStd = 0.2f;
    descriptor.flatnessMean = 0.2f;
    descriptor.flatnessStd = 0.2f;
    descriptor.fluxMean = 0.3f;
    descriptor.fluxP90 = 0.4f;
    descriptor.rmsMean = 0.6f;
    descriptor.dynamicRange = 0.5f;
    descriptor.crestFactor = 0.4f;
    descriptor.harmonicity = 0.8f;
    descriptor.logDuration = 0.5f;
    descriptor.stereoWidth = 0.3f;
    descriptor.stereoCorrelation = 0.7f;
    descriptor.harmonicReliability = 1.0f;
    descriptor.stereoReliability = 1.0f;
    return descriptor;
}

void testMemoryTypeRecognitionProfiles()
{
    const auto literal = recognitionProfile(GhostMemoryType::literal);
    const auto emotional = recognitionProfile(GhostMemoryType::emotional);
    const auto dream = recognitionProfile(GhostMemoryType::dream);

    require(std::abs(literal.thresholdFor(0.72f) - 0.72f) < 1.0e-6f,
            "Literal no longer preserves the established Recognition default");
    require(emotional.thresholdFor(0.72f) < literal.thresholdFor(0.72f)
            && dream.thresholdFor(0.72f) < emotional.thresholdFor(0.72f),
            "profile threshold mapping is not progressively more associative");
    for (const auto& profile : { literal, emotional, dream })
        require(profile.thresholdFor(0.0f) < profile.thresholdFor(0.5f)
                && profile.thresholdFor(0.5f) < profile.thresholdFor(1.0f),
                "Recognition control is not monotonic within a memory type");

    const auto query = controlledDescriptor();
    auto literalEvidence = query;
    literalEvidence.spectralBands.fill(0.0f);
    literalEvidence.spectralBands[7] = 1.0f;
    literalEvidence.centroidMean = 0.8f;
    literalEvidence.centroidStd = 0.8f;
    literalEvidence.flatnessMean = 0.7f;
    literalEvidence.flatnessStd = 0.7f;
    literalEvidence.rmsMean = 0.1f;
    literalEvidence.dynamicRange = 0.0f;

    auto broadCharacterEvidence = query;
    broadCharacterEvidence.chroma.fill(0.0f);
    broadCharacterEvidence.chroma[0] = 0.25f;
    broadCharacterEvidence.chroma[7] = 0.75f;
    broadCharacterEvidence.fluxMean = 0.6f;
    broadCharacterEvidence.fluxP90 = 0.7f;
    broadCharacterEvidence.crestFactor = 0.7f;
    broadCharacterEvidence.logDuration = 1.0f;

    const auto literalExact = GhostSimilarityEngine(literal.weights).compare(query, literalEvidence);
    const auto literalBroad = GhostSimilarityEngine(literal.weights).compare(query, broadCharacterEvidence);
    const auto emotionalExact = GhostSimilarityEngine(emotional.weights).compare(query, literalEvidence);
    const auto emotionalBroad = GhostSimilarityEngine(emotional.weights).compare(query, broadCharacterEvidence);
    const auto dreamExact = GhostSimilarityEngine(dream.weights).compare(query, literalEvidence);
    const auto dreamBroad = GhostSimilarityEngine(dream.weights).compare(query, broadCharacterEvidence);

    require(literalExact.overall > literalBroad.overall,
            "Literal did not prefer pitch identity over broad character evidence");
    require(emotionalBroad.overall > emotionalExact.overall + 0.20f,
            "Emotional did not prefer spectral/dynamic character evidence");
    require(dreamBroad.overall > dreamExact.overall + 0.35f,
            "Dream did not make loose spectral overlap its dominant association");
    require(literalExact.effectiveWeights[0] > literalExact.effectiveWeights[1],
            "Literal contribution readout does not expose harmonic dominance");
    require(emotionalBroad.effectiveWeights[1] + emotionalBroad.effectiveWeights[3] > 0.60f,
            "Emotional contribution readout does not expose spectral/dynamics dominance");
    require(dreamBroad.effectiveWeights[1] > 0.65f,
            "Dream contribution readout does not expose spectral dominance");
}

void testTransientPadAndNoiseBounds()
{
    std::vector<float> impulses(static_cast<std::size_t>(2.2 * sampleRate), 0.0f);
    for (int hit = 0; hit < 8; ++hit)
        impulses[static_cast<std::size_t>((0.1 + hit * 0.25) * sampleRate)] = 1.0f;
    const auto impulseEvents = detect(impulses, 113);
    require(impulseEvents.size() >= 7 && impulseEvents.size() <= 8,
            "separated impulses were missed or retriggered (count="
            + std::to_string(impulseEvents.size()) + ")");

    auto pad = sine(146.832f, 5.0f, 0.2f);
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.5 * sampleRate); ++i)
        pad[i] *= static_cast<float>(i / (0.5 * sampleRate));
    const auto padEvents = detect(pad, 257);
    require(!padEvents.empty(), "slow-attack sustained pad was not segmented");
    require(padEvents.size() <= 2, "sustained pad generated a retrigger storm");

    const auto noiseEvents = detect(noise(30.0f), 511);
    require(noiseEvents.size() <= 10, "stationary noise generated unbounded events");
}

void testBoundsAndStableRanking()
{
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        const auto count = static_cast<std::size_t>(rate * 0.25);
        std::vector<float> signal(count);
        for (std::size_t i = 0; i < count; ++i)
            signal[i] = 0.2f * std::sin(2.0f * std::numbers::pi_v<float> * 330.0f
                                       * static_cast<float>(i / rate));
        require(describe(signal, rate).isFiniteAndNormalised(),
                "descriptor contains non-finite or unbounded data at a supported sample rate");
    }

    const auto descriptor = describe(sine(220.0f, 0.4f));
    const std::array candidates {
        GhostCandidate { 3, 100, descriptor },
        GhostCandidate { 2, 200, descriptor },
        GhostCandidate { 1, 200, descriptor }
    };
    GhostCandidateRanker ranker;
    const auto ranked = ranker.rank(descriptor, candidates, candidates.size(), 0.0f);
    require(ranked.size() == 3 && ranked[0].memoryId == 1 && ranked[1].memoryId == 2
            && ranked[2].memoryId == 3, "candidate tie ordering is not deterministic");

    auto invalid = descriptor;
    invalid.chroma[0] = std::numeric_limits<float>::quiet_NaN();
    require(GhostSimilarityEngine().compare(descriptor, invalid).overall == 0.0f,
            "invalid descriptor was allowed into recognition");
}
}

int main()
{
    try
    {
        testSilenceAndBlockInvariance();
        testSimilarityRelationships();
        testMemoryTypeRecognitionProfiles();
        testTransientPadAndNoiseBounds();
        testBoundsAndStableRanking();
        std::cout << "Ghost analysis tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Ghost analysis test failure: " << error.what() << '\n';
        return 1;
    }
}
