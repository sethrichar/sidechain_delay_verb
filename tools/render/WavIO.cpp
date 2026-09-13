#include "WavIO.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace clearspace::render
{

std::optional<WavData> readWav(const juce::File& file)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(
        file.createInputStream().release(), /*deleteStreamIfOpeningFails*/ true));
    if (reader == nullptr)
        return std::nullopt;

    const auto numChannels = static_cast<int>(reader->numChannels);
    const auto numSamples = static_cast<int>(reader->lengthInSamples);
    if (numChannels <= 0 || numSamples < 0)
        return std::nullopt;

    WavData data;
    data.sampleRate = reader->sampleRate;
    data.buffer.setSize(numChannels, numSamples);
    data.buffer.clear();
    if (numSamples > 0 && !reader->read(&data.buffer, 0, numSamples, 0, true, true))
        return std::nullopt;
    return data;
}

bool writeWav(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat format;
    const auto options =
        juce::AudioFormatWriterOptions()
            .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)
            .withSampleRate(sampleRate)
            .withNumChannels(buffer.getNumChannels())
            .withBitsPerSample(32);
    std::unique_ptr<juce::OutputStream> streamToPass(std::move(stream));
    auto writer = format.createWriterFor(streamToPass, options); // takes ownership on success
    if (writer == nullptr)
        return false;

    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

} // namespace clearspace::render
