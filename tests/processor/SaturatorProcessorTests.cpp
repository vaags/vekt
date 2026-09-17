#include <Parameters.h>
#include <PluginProcessor.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace
{
class TestPlayHead final : public juce::AudioPlayHead
{
public:
	juce::Optional<PositionInfo> getPosition() const override
	{
		PositionInfo position;
		position.setIsPlaying(playing);
		return position;
	}

	bool playing {};
};
}

TEST_CASE("Saturator processor defaults to quality-first oversampling", "[processor]")
{
	vekt::saturator::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 128);

	REQUIRE(processor.getLatencySamples() > 0);
	REQUIRE(processor.getTotalNumInputChannels() == 2);
	REQUIRE(processor.getTotalNumOutputChannels() == 2);
}

TEST_CASE("Saturator processor produces finite stereo audio", "[processor]")
{
	vekt::saturator::PluginProcessor processor;
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer midi;
	processor.prepareToPlay(48'000.0, buffer.getNumSamples());

	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
			buffer.setSample(channel, sample, std::sin(static_cast<float>(sample) * 0.1f));

	processor.processBlock(buffer, midi);

	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
			REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
}

TEST_CASE("Saturator processor state round trips parameters", "[processor][state]")
{
	vekt::saturator::PluginProcessor source;
	vekt::saturator::PluginProcessor restored;
	auto* sourceDrive = source.getParameters().getParameter(vekt::saturator::parameters::drive);
	REQUIRE(sourceDrive != nullptr);
	sourceDrive->setValueNotifyingHost(sourceDrive->convertTo0to1(18.0f));

	juce::MemoryBlock state;
	source.getStateInformation(state);
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

	const auto* restoredDrive = restored.getParameters().getRawParameterValue(
		vekt::saturator::parameters::drive);
	REQUIRE(restoredDrive != nullptr);
	REQUIRE(restoredDrive->load() == Catch::Approx(18.0f));
}

TEST_CASE("Saturator processor bypass preserves reported latency across transitions", "[processor][bypass]")
{
	constexpr auto blockSize = 128;
	vekt::saturator::PluginProcessor processor;
	juce::AudioBuffer<float> buffer(2, blockSize);
	juce::MidiBuffer midi;
	processor.prepareToPlay(48'000.0, blockSize);

	const auto latency = processor.getLatencySamples();
	REQUIRE(latency > 0);
	REQUIRE(latency < blockSize);

	buffer.clear();
	buffer.setSample(0, blockSize - 1, 1.0f);
	processor.processBlock(buffer, midi);

	buffer.clear();
	processor.processBlockBypassed(buffer, midi);

	REQUIRE(buffer.getSample(0, latency - 1) == Catch::Approx(1.0f));
	REQUIRE(buffer.getSample(1, latency - 1) == Catch::Approx(0.0f));
}

TEST_CASE("Saturator processor applies quality changes while stopped", "[processor][quality]")
{
	vekt::saturator::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 128);
	auto* factor = processor.getParameters().getParameter(
		vekt::saturator::parameters::oversamplingFactor);
	REQUIRE(factor != nullptr);

	factor->setValueNotifyingHost(0.0f);
	processor.applyPendingQualityChange();

	REQUIRE(processor.getActiveQuality().factor == vekt::dsp::OversamplingFactor::off);
	REQUIRE(processor.getLatencySamples() == 0);
	REQUIRE_FALSE(processor.hasPendingQualityChange());
}

TEST_CASE("Saturator processor defers quality changes during playback", "[processor][quality]")
{
	vekt::saturator::PluginProcessor processor;
	TestPlayHead playHead;
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer midi;
	processor.setPlayHead(&playHead);
	processor.prepareToPlay(48'000.0, buffer.getNumSamples());

	playHead.playing = true;
	buffer.clear();
	processor.processBlock(buffer, midi);
	auto* factor = processor.getParameters().getParameter(
		vekt::saturator::parameters::oversamplingFactor);
	REQUIRE(factor != nullptr);
	factor->setValueNotifyingHost(0.0f);
	processor.applyPendingQualityChange();

	REQUIRE(processor.getActiveQuality().factor == vekt::dsp::OversamplingFactor::x4);
	REQUIRE(processor.hasPendingQualityChange());

	playHead.playing = false;
	processor.processBlock(buffer, midi);
	processor.applyPendingQualityChange();

	REQUIRE(processor.getActiveQuality().factor == vekt::dsp::OversamplingFactor::off);
	REQUIRE(processor.getLatencySamples() == 0);
	REQUIRE_FALSE(processor.hasPendingQualityChange());
}
