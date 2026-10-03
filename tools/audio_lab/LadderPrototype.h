#pragma once

#include "NonlinearTptLadderAliases.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace vekt::audio_lab
{
struct LadderPrototypeResult
{
	juce::AudioBuffer<float> audio;
	juce::var report;
};

[[nodiscard]] LadderPrototypeResult renderLadderPrototype(double sampleRate, int blockSize);
[[nodiscard]] bool writeLadderPrototypeWav(const juce::File& file,
	const LadderPrototypeResult& result, double sampleRate);
[[nodiscard]] bool writeLadderPrototypeReport(const juce::File& file,
	const LadderPrototypeResult& result);
}