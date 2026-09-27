#pragma once

#include "MonoVoice.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace vekt::audio_lab
{
enum class MonoEventType { noteOn, noteOff, parameter };

enum class MonoParameter
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
	qCompensation
};

struct MonoRenderEvent
{
	std::int64_t sample {};
	MonoEventType type { MonoEventType::parameter };
	MonoParameter parameter { MonoParameter::cutoff };
	float value {};
	int note { 60 };
};

struct MonoMeasurementWindow
{
	juce::String name;
	std::int64_t startSample {};
	std::int64_t endSample {};
};

struct MonoRenderRequest
{
	juce::String fixture;
	double sampleRate { 48'000.0 };
	int blockSize { 128 };
	std::uint32_t seed { 0x4d6f6e6fu };
	bool developmentLadder {};
	std::int64_t totalSamples {};
	mono::MonoVoiceSettings settings;
	std::vector<MonoRenderEvent> events;
	std::vector<MonoMeasurementWindow> windows;
};

struct MonoRenderResult
{
	juce::AudioBuffer<float> audio;
	juce::var report;
};

[[nodiscard]] mono::MonoVoiceSettings defaultMonoRenderSettings() noexcept;
[[nodiscard]] bool makeMonoRenderFixture(const juce::String& name, double sampleRate,
	int blockSize, std::uint32_t seed, MonoRenderRequest& destination);
[[nodiscard]] MonoRenderResult renderMono(const MonoRenderRequest& request);
[[nodiscard]] bool writeMonoRenderWav(const juce::File& file, const MonoRenderResult& result,
	double sampleRate);
[[nodiscard]] bool writeMonoRenderReport(const juce::File& file, const MonoRenderResult& result);
}
