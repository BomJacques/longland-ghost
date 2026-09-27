#include "GhostFeatureExtractor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>

namespace longland::ghost
{
namespace
{
using Complex = std::complex<float>;

void fft(std::vector<Complex>& values)
{
    const auto count = values.size();
    for (std::size_t i = 1, j = 0; i < count; ++i)
    {
        std::size_t bit = count >> 1u;
        for (; (j & bit) != 0u; bit >>= 1u)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(values[i], values[j]);
    }
    for (std::size_t length = 2; length <= count; length <<= 1u)
    {
        const auto angle = -2.0f * std::numbers::pi_v<float> / static_cast<float>(length);
        const Complex step(std::cos(angle), std::sin(angle));
        for (std::size_t base = 0; base < count; base += length)
        {
            Complex rotation(1.0f, 0.0f);
            for (std::size_t offset = 0; offset < length / 2; ++offset)
            {
                const auto even = values[base + offset];
                const auto odd = values[base + offset + length / 2] * rotation;
                values[base + offset] = even + odd;
                values[base + offset + length / 2] = even - odd;
                rotation *= step;
            }
        }
    }
}

float clampUnit(float value) noexcept
{
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

template <std::size_t Size>
void normalise(std::array<float, Size>& values)
{
    const auto sum = std::accumulate(values.begin(), values.end(), 0.0f);
    if (sum > 1.0e-20f)
        for (auto& value : values)
            value = clampUnit(value / sum);
}

std::pair<float, float> meanAndStd(const std::vector<float>& values)
{
    if (values.empty())
        return {};
    const auto mean = std::accumulate(values.begin(), values.end(), 0.0f)
                      / static_cast<float>(values.size());
    float variance = 0.0f;
    for (float value : values)
        variance += (value - mean) * (value - mean);
    variance /= static_cast<float>(values.size());
    return { clampUnit(mean), clampUnit(std::sqrt(std::max(0.0f, variance))) };
}

float percentile90(std::vector<float> values)
{
    if (values.empty())
        return 0.0f;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(std::floor(0.9 * static_cast<double>(values.size() - 1)));
    return clampUnit(values[index]);
}

float normalisedEntropy(const auto& distribution)
{
    float sum = 0.0f;
    for (float value : distribution)
        sum += std::max(0.0f, value);
    if (sum <= 1.0e-20f)
        return 1.0f;
    float entropy = 0.0f;
    std::size_t nonzero = 0;
    for (float value : distribution)
    {
        const auto probability = std::max(0.0f, value) / sum;
        if (probability > 1.0e-12f)
        {
            entropy -= probability * std::log(probability);
            ++nonzero;
        }
    }
    return nonzero > 1 ? clampUnit(entropy / std::log(static_cast<float>(nonzero))) : 0.0f;
}
}

void GhostFeatureExtractor::prepare(double sampleRate)
{
    rate = std::max(8000.0, sampleRate);
}

GhostDescriptor GhostFeatureExtractor::extract(const float* left, const float* right,
                                                std::size_t sampleCount) const
{
    GhostDescriptor descriptor;
    if (left == nullptr || sampleCount == 0)
        return descriptor;

    const auto nyquist = static_cast<float>(rate * 0.5);
    const auto binHz = static_cast<float>(rate / static_cast<double>(fftSize));
    std::vector<Complex> spectrum(fftSize);
    std::vector<float> previousNormalisedMagnitude(fftSize / 2 + 1, 0.0f);
    std::vector<float> centroidFrames;
    std::vector<float> flatnessFrames;
    std::vector<float> fluxFrames;
    std::vector<float> rmsFrames;
    std::vector<float> allBinPower(fftSize / 2 + 1, 0.0f);
    float peak = 0.0f;
    double totalPower = 0.0;
    double leftPower = 0.0;
    double rightPower = 0.0;
    double crossPower = 0.0;
    double midPower = 0.0;
    double sidePower = 0.0;

    for (std::size_t i = 0; i < sampleCount; ++i)
    {
        const auto l = std::isfinite(left[i]) ? left[i] : 0.0f;
        const auto r = right != nullptr && std::isfinite(right[i]) ? right[i] : l;
        const auto mid = 0.5f * (l + r);
        const auto side = 0.5f * (l - r);
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
        totalPower += static_cast<double>(mid) * mid;
        leftPower += static_cast<double>(l) * l;
        rightPower += static_cast<double>(r) * r;
        crossPower += static_cast<double>(l) * r;
        midPower += static_cast<double>(mid) * mid;
        sidePower += static_cast<double>(side) * side;
    }

    for (std::size_t frameStart = 0; frameStart < sampleCount; frameStart += frameHop)
    {
        float framePower = 0.0f;
        for (std::size_t i = 0; i < fftSize; ++i)
        {
            float sample = 0.0f;
            if (frameStart + i < sampleCount)
            {
                const auto l = std::isfinite(left[frameStart + i]) ? left[frameStart + i] : 0.0f;
                const auto r = right != nullptr && std::isfinite(right[frameStart + i])
                                   ? right[frameStart + i] : l;
                sample = 0.5f * (l + r);
            }
            framePower += sample * sample;
            const auto window = 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float>
                                                       * static_cast<float>(i)
                                                       / static_cast<float>(fftSize - 1));
            spectrum[i] = Complex(sample * window, 0.0f);
        }
        framePower /= static_cast<float>(fftSize);
        const auto frameDb = 10.0f * std::log10(std::max(framePower, 1.0e-8f));
        rmsFrames.push_back(clampUnit((frameDb + 80.0f) / 80.0f));
        fft(spectrum);

        std::vector<float> magnitude(fftSize / 2 + 1);
        float magnitudeSum = 0.0f;
        float weightedFrequency = 0.0f;
        float analysedPower = 0.0f;
        float logPower = 0.0f;
        std::size_t analysedBins = 0;
        for (std::size_t bin = 1; bin < magnitude.size(); ++bin)
        {
            const auto frequency = static_cast<float>(bin) * binHz;
            const auto power = std::norm(spectrum[bin]);
            magnitude[bin] = std::sqrt(power);
            allBinPower[bin] += power;
            magnitudeSum += magnitude[bin];
            if (frequency >= 20.0f)
            {
                weightedFrequency += frequency * power;
                analysedPower += power;
                logPower += std::log(std::max(power, 1.0e-20f));
                ++analysedBins;
            }
            if (frequency >= 40.0f)
            {
                const auto position = std::log(frequency / 40.0f)
                                      / std::log(std::max(1.0001f, nyquist / 40.0f));
                const auto band = std::min<std::size_t>(7, static_cast<std::size_t>(
                    std::max(0.0f, std::floor(position * 8.0f))));
                descriptor.spectralBands[band] += power;
            }
        }

        // Chroma is accumulated from interpolated spectral peaks, rather than
        // every leakage bin. This keeps pitch-class estimates octave-consistent
        // at the deliberately compact 2048-sample analysis resolution.
        for (std::size_t bin = 2; bin + 1 < magnitude.size(); ++bin)
        {
            const auto centrePower = std::norm(spectrum[bin]);
            const auto lowerPower = std::norm(spectrum[bin - 1]);
            const auto upperPower = std::norm(spectrum[bin + 1]);
            if (centrePower < lowerPower || centrePower < upperPower || centrePower <= 1.0e-16f)
                continue;
            const auto alpha = std::log(std::max(lowerPower, 1.0e-20f));
            const auto beta = std::log(std::max(centrePower, 1.0e-20f));
            const auto gamma = std::log(std::max(upperPower, 1.0e-20f));
            const auto denominator = alpha - 2.0f * beta + gamma;
            const auto offset = std::abs(denominator) > 1.0e-8f
                                    ? std::clamp(0.5f * (alpha - gamma) / denominator, -0.5f, 0.5f)
                                    : 0.0f;
            const auto peakFrequency = (static_cast<float>(bin) + offset) * binHz;
            if (peakFrequency < 60.0f || peakFrequency > std::min(5000.0f, nyquist))
                continue;
            const auto midi = 69.0f + 12.0f * std::log2(peakFrequency / 440.0f);
            const auto lowerNote = static_cast<int>(std::floor(midi));
            const auto fraction = midi - static_cast<float>(lowerNote);
            const auto lowerClass = ((lowerNote % 12) + 12) % 12;
            descriptor.chroma[static_cast<std::size_t>(lowerClass)] += centrePower * (1.0f - fraction);
            descriptor.chroma[static_cast<std::size_t>((lowerClass + 1) % 12)] += centrePower * fraction;
        }

        const auto centroidHz = analysedPower > 1.0e-20f ? weightedFrequency / analysedPower : 20.0f;
        centroidFrames.push_back(clampUnit(std::log(std::max(20.0f, centroidHz) / 20.0f)
                                           / std::log(std::max(1.0001f, nyquist / 20.0f))));
        const auto arithmeticPower = analysedBins > 0 ? analysedPower / static_cast<float>(analysedBins) : 0.0f;
        const auto geometricPower = analysedBins > 0 ? std::exp(logPower / static_cast<float>(analysedBins)) : 0.0f;
        flatnessFrames.push_back(clampUnit(geometricPower / std::max(arithmeticPower, 1.0e-20f)));

        float flux = 0.0f;
        if (magnitudeSum > 1.0e-20f)
            for (std::size_t bin = 1; bin < magnitude.size(); ++bin)
            {
                magnitude[bin] /= magnitudeSum;
                flux += std::max(0.0f, magnitude[bin] - previousNormalisedMagnitude[bin]);
            }
        fluxFrames.push_back(clampUnit(flux));
        previousNormalisedMagnitude = std::move(magnitude);
    }

    normalise(descriptor.chroma);
    normalise(descriptor.spectralBands);
    const auto [centroidMean, centroidStd] = meanAndStd(centroidFrames);
    const auto [flatnessMean, flatnessStd] = meanAndStd(flatnessFrames);
    const auto [fluxMean, unusedFluxStd] = meanAndStd(fluxFrames);
    (void) unusedFluxStd;
    descriptor.centroidMean = centroidMean;
    descriptor.centroidStd = centroidStd;
    descriptor.flatnessMean = flatnessMean;
    descriptor.flatnessStd = flatnessStd;
    descriptor.fluxMean = fluxMean;
    descriptor.fluxP90 = percentile90(fluxFrames);

    const auto eventRms = std::sqrt(totalPower / static_cast<double>(sampleCount));
    const auto eventDb = 20.0 * std::log10(std::max(eventRms, 1.0e-4));
    descriptor.rmsMean = clampUnit(static_cast<float>((eventDb + 80.0) / 80.0));
    const auto [minimumRms, maximumRms] = std::minmax_element(rmsFrames.begin(), rmsFrames.end());
    descriptor.dynamicRange = rmsFrames.empty() ? 0.0f : clampUnit(*maximumRms - *minimumRms);
    descriptor.crestFactor = clampUnit(static_cast<float>((peak / std::max(eventRms, 1.0e-6) - 1.0) / 15.0));
    descriptor.harmonicity = clampUnit(1.0f - normalisedEntropy(allBinPower));
    descriptor.logDuration = clampUnit(static_cast<float>(
        std::log(std::clamp(static_cast<double>(sampleCount) / rate, 0.08, 4.0) / 0.08)
        / std::log(4.0 / 0.08)));

    if (right != nullptr)
    {
        descriptor.stereoReliability = clampUnit(static_cast<float>(
            std::sqrt(leftPower * rightPower) / std::max(1.0e-12, leftPower + rightPower) * 2.0));
        descriptor.stereoCorrelation = static_cast<float>(crossPower
            / std::max(1.0e-12, std::sqrt(leftPower * rightPower)));
        descriptor.stereoCorrelation = std::clamp(descriptor.stereoCorrelation, -1.0f, 1.0f);
        descriptor.stereoWidth = clampUnit(static_cast<float>(sidePower
            / std::max(1.0e-12, sidePower + midPower)));
    }

    const auto chromaDistinctiveness = 1.0f - normalisedEntropy(descriptor.chroma);
    descriptor.harmonicReliability = clampUnit(std::sqrt(descriptor.harmonicity
                                                         * std::max(0.0f, chromaDistinctiveness)));
    descriptor.f0Confidence = 0.0f;
    return descriptor;
}
}
