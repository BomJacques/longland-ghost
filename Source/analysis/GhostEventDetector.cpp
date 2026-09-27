#include "GhostEventDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace longland::ghost
{
namespace
{
constexpr float powerAtMinus48Db = 1.5848932e-5f;
constexpr float powerAtMinus50Db = 1.0e-5f;
constexpr float powerAtMinus55Db = 3.1622777e-6f;
constexpr float powerAtMinus60Db = 1.0e-6f;

float powerToDb(float power) noexcept
{
    return 10.0f * std::log10(std::max(power, 1.0e-12f));
}
}

void GhostEventDetector::prepare(double sampleRate)
{
    rate = std::max(8000.0, sampleRate);
    hopSamples = static_cast<std::size_t>(std::max(32.0, std::round(rate * (256.0 / 48000.0))));
    hopBuffer.assign(hopSamples, 0.0f);
    reset();
}

void GhostEventDetector::reset() noexcept
{
    hopFill = 0;
    samplePosition = 0;
    eventStart = 0;
    lastSplit = 0;
    previousSample = 0.0f;
    previousDifferenceEnergy = 0.0f;
    fastPower = 0.0f;
    slowPower = 0.0f;
    noisePower = 1.0e-10f;
    eventPeak = 0.0f;
    eventPowerSum = 0.0f;
    eventOnset = 0.0f;
    eventHops = 0;
    silentHops = 0;
    sustainedHops = 0;
    active = false;
    std::fill(hopBuffer.begin(), hopBuffer.end(), 0.0f);
}

std::vector<GhostDetectedEvent> GhostEventDetector::process(const float* mono,
                                                            std::size_t sampleCount)
{
    std::vector<GhostDetectedEvent> completed;
    if (mono == nullptr || sampleCount == 0 || hopBuffer.empty())
        return completed;

    for (std::size_t i = 0; i < sampleCount; ++i)
    {
        hopBuffer[hopFill++] = std::isfinite(mono[i]) ? mono[i] : 0.0f;
        ++samplePosition;
        if (hopFill == hopSamples)
        {
            analyseHop(completed);
            hopFill = 0;
        }
    }
    return completed;
}

std::vector<GhostDetectedEvent> GhostEventDetector::flush()
{
    std::vector<GhostDetectedEvent> completed;
    if (active)
        finishEvent(samplePosition, completed);
    hopFill = 0;
    return completed;
}

void GhostEventDetector::analyseHop(std::vector<GhostDetectedEvent>& completed)
{
    float power = 0.0f;
    float differencePower = 0.0f;
    float peak = 0.0f;
    for (float sample : hopBuffer)
    {
        power += sample * sample;
        const auto difference = sample - previousSample;
        differencePower += difference * difference;
        previousSample = sample;
        peak = std::max(peak, std::abs(sample));
    }
    const auto inverseCount = 1.0f / static_cast<float>(hopSamples);
    power *= inverseCount;
    differencePower *= inverseCount;

    const auto hopSeconds = static_cast<float>(hopSamples / rate);
    const auto fastCoefficient = std::exp(-hopSeconds / 0.005f);
    const auto slowCoefficient = std::exp(-hopSeconds / 0.080f);
    fastPower = fastCoefficient * fastPower + (1.0f - fastCoefficient) * power;
    slowPower = slowCoefficient * slowPower + (1.0f - slowCoefficient) * power;

    const auto energyRise = std::clamp((powerToDb(fastPower) - powerToDb(slowPower)) / 18.0f,
                                       0.0f, 1.0f);
    const auto fluxDenominator = std::max({ differencePower, previousDifferenceEnergy, 1.0e-9f });
    const auto differenceFlux = std::clamp((differencePower - previousDifferenceEnergy)
                                           / fluxDenominator, 0.0f, 1.0f);
    previousDifferenceEnergy = differencePower;
    const auto onset = 0.55f * energyRise + 0.45f * differenceFlux;

    // The learned floor moves only while idle and only for quiet material. This
    // prevents a fade-in pad becoming its own ever-rising threshold.
    if (!active && power < powerAtMinus48Db)
        noisePower = 0.995f * noisePower + 0.005f * power;

    const auto onsetGate = std::max(powerAtMinus55Db, noisePower * 15.848932f);
    const auto sustainedGate = std::max(powerAtMinus48Db, noisePower * 15.848932f);
    const auto silenceGate = std::max(powerAtMinus60Db, noisePower * 3.9810717f);
    sustainedHops = power >= sustainedGate ? sustainedHops + 1u : 0u;

    const auto hopEnd = samplePosition;
    const auto preRoll = static_cast<std::uint64_t>(std::llround(rate * 0.020));
    const auto minimumLength = static_cast<std::uint64_t>(std::llround(rate * 0.080));
    const auto splitRefractory = static_cast<std::uint64_t>(std::llround(rate * 0.100));
    const auto maximumLength = static_cast<std::uint64_t>(std::llround(rate * 4.0));
    const auto silenceHopsNeeded = static_cast<std::uint32_t>(
        std::max(1.0, std::ceil(0.120 * rate / static_cast<double>(hopSamples))));
    const auto sustainedHopsNeeded = static_cast<std::uint32_t>(
        std::max(1.0, std::ceil(0.080 * rate / static_cast<double>(hopSamples))));

    if (!active)
    {
        const bool transientStart = power >= onsetGate && onset >= 0.18f;
        const bool sustainedStart = sustainedHops >= sustainedHopsNeeded;
        if (transientStart || sustainedStart)
        {
            active = true;
            eventStart = hopEnd > hopSamples + preRoll ? hopEnd - hopSamples - preRoll : 0;
            lastSplit = eventStart;
            eventPeak = peak;
            eventPowerSum = power;
            eventOnset = onset;
            eventHops = 1;
            silentHops = 0;
        }
        return;
    }

    eventPeak = std::max(eventPeak, peak);
    eventPowerSum += power;
    eventOnset = std::max(eventOnset, onset);
    ++eventHops;
    silentHops = power < silenceGate ? silentHops + 1u : 0u;

    const auto eventLength = hopEnd - eventStart;
    const bool split = onset >= 0.38f && hopEnd - lastSplit >= splitRefractory
                       && eventLength >= minimumLength;
    if (split)
    {
        const auto boundary = hopEnd > hopSamples + preRoll ? hopEnd - hopSamples - preRoll : hopEnd;
        finishEvent(std::max(boundary, eventStart + minimumLength), completed);
        active = true;
        eventStart = boundary;
        lastSplit = boundary;
        eventPeak = peak;
        eventPowerSum = power;
        eventOnset = onset;
        eventHops = 1;
        silentHops = 0;
    }
    else if (silentHops >= silenceHopsNeeded)
    {
        const auto trailingSilence = static_cast<std::uint64_t>(silentHops) * hopSamples;
        // A one-sample drum impulse is still a musically useful fragment. Keep
        // enough release context to satisfy the minimum event length instead of
        // subtracting the entire silence-confirmation window and rejecting it.
        const auto silenceBoundary = hopEnd > trailingSilence ? hopEnd - trailingSilence : hopEnd;
        finishEvent(std::max(eventStart + minimumLength, silenceBoundary), completed);
    }
    else if (eventLength >= maximumLength)
    {
        finishEvent(eventStart + maximumLength, completed);
    }
}

void GhostEventDetector::finishEvent(std::uint64_t end,
                                     std::vector<GhostDetectedEvent>& completed)
{
    const auto minimumLength = static_cast<std::uint64_t>(std::llround(rate * 0.080));
    if (end > eventStart && end - eventStart >= minimumLength && eventPeak >= std::sqrt(powerAtMinus50Db))
    {
        const auto meanPower = eventPowerSum / static_cast<float>(std::max(1u, eventHops));
        const auto level = std::clamp((powerToDb(meanPower) + 60.0f) / 60.0f, 0.0f, 1.0f);
        const auto distinctiveness = std::clamp(0.65f * eventOnset + 0.35f * level, 0.0f, 1.0f);
        completed.push_back({ eventStart, end, eventPeak, std::sqrt(std::max(0.0f, meanPower)),
                              eventOnset, distinctiveness });
    }
    active = false;
    eventPeak = 0.0f;
    eventPowerSum = 0.0f;
    eventOnset = 0.0f;
    eventHops = 0;
    silentHops = 0;
    sustainedHops = 0;
}
}
