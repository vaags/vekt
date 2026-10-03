#pragma once

#include "KobberVoice.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace vekt::audio_lab
{
enum class KobberEventType { noteOn, noteOff, parameter };

enum class KobberParameter
{
	cutoff,
	resonance,
	envelopeAmount,
	drive,
	ampAttack,
	ampDecay,
	ampSustain,
	ampRelease,
	filterAttack,
	filterDecay,
	filterSustain,
	filterRelease,
	qCompensation,
	noiseLevel
};

struct KobberRenderEvent
{
	std::int64_t sample {};
	KobberEventType type { KobberEventType::parameter };
	KobberParameter parameter { KobberParameter::cutoff };
	float value {};
	int note { 60 };
};

struct KobberMeasurementWindow
{
	juce::String name;
	std::int64_t startSample {};
	std::int64_t endSample {};
};

struct KobberRenderRequest
{
	juce::String fixture;
	double sampleRate { 48'000.0 };
	int blockSize { 128 };
	std::uint32_t seed { 0x4d6f6e6fu };
	std::int64_t totalSamples {};
	kobber::KobberVoiceSettings settings;
	std::vector<KobberRenderEvent> events;
	std::vector<KobberMeasurementWindow> windows;
};

struct KobberRenderResult
{
	juce::AudioBuffer<float> audio;
	juce::var report;
};

[[nodiscard]] kobber::KobberVoiceSettings defaultMonoRenderSettings() noexcept;
[[nodiscard]] bool makeMonoRenderFixture(const juce::String& name, double sampleRate,
	int blockSize, std::uint32_t seed, KobberRenderRequest& destination);
[[nodiscard]] KobberRenderResult renderMono(const KobberRenderRequest& request);
[[nodiscard]] bool writeMonoRenderWav(const juce::File& file, const KobberRenderResult& result,
	double sampleRate);
[[nodiscard]] bool writeMonoRenderReport(const juce::File& file, const KobberRenderResult& result);
}
