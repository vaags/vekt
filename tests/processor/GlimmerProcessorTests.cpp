#include <vekt/glimmer/Parameters.h>
#include <vekt/glimmer/PluginProcessor.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
void setParameter(vekt::glimmer::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	REQUIRE(parameter != nullptr);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
}

TEST_CASE("Glimmer renders finite stereo output across rotary modes", "[glimmer][processor]")
{
	vekt::glimmer::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 128);
	juce::AudioBuffer<float> buffer(2, 257);
	for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
	{
		buffer.setSample(0, sample, 0.2f);
		buffer.setSample(1, sample, -0.1f);
	}

	for (auto mode = 0; mode < 3; ++mode)
	{
		setParameter(processor, vekt::glimmer::parameters::speedMode, static_cast<float>(mode));
		juce::MidiBuffer midi;
		processor.processBlock(buffer, midi);
	}
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
			REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
}

TEST_CASE("Glimmer preserves APVTS project state", "[glimmer][processor]")
{
	vekt::glimmer::PluginProcessor source;
	setParameter(source, vekt::glimmer::parameters::micAngle, 245.0f);
	juce::MemoryBlock state;
	source.getStateInformation(state);

	vekt::glimmer::PluginProcessor restored;
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	const auto* value = restored.getParameters().getRawParameterValue(vekt::glimmer::parameters::micAngle);
	REQUIRE(value != nullptr);
	REQUIRE(value->load() == Catch::Approx(245.0f));
}

TEST_CASE("Glimmer selects tracking and offline oversampling profiles", "[glimmer][processor][quality]")
{
	vekt::glimmer::PluginProcessor processor;
	setParameter(processor, vekt::glimmer::parameters::trackingOversampling, 2.0f);
	setParameter(processor, vekt::glimmer::parameters::offlineOversampling, 4.0f);

	processor.setNonRealtime(false);
	processor.prepareToPlay(48'000.0, 128);
	REQUIRE(processor.getActiveQuality().factor == vekt::dsp::OversamplingFactor::x4);
	REQUIRE(processor.getActiveQuality().filter == vekt::dsp::OversamplingFilter::polyphaseIIR);
	REQUIRE(processor.getLatencySamples() > 0);

	processor.setNonRealtime(true);
	processor.prepareToPlay(48'000.0, 128);
	REQUIRE(processor.getActiveQuality().factor == vekt::dsp::OversamplingFactor::x16);
	REQUIRE(processor.getActiveQuality().filter == vekt::dsp::OversamplingFilter::polyphaseFIR);
}

TEST_CASE("Glimmer applies pending quality changes while stopped", "[glimmer][processor][quality]")
{
	vekt::glimmer::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 128);
	setParameter(processor, vekt::glimmer::parameters::trackingOversampling, 5.0f);
	REQUIRE(processor.hasPendingQualityChange());
	juce::AudioBuffer<float> buffer(2, 128);
	buffer.clear();
	juce::MidiBuffer midi;
	processor.processBlock(buffer, midi);
	REQUIRE_FALSE(processor.hasPendingQualityChange());
	REQUIRE(processor.getActiveQuality().factor == vekt::dsp::OversamplingFactor::x8);
	REQUIRE(processor.getActiveQuality().filter == vekt::dsp::OversamplingFilter::polyphaseFIR);
}

TEST_CASE("Glimmer bypass returns latency-aligned raw input", "[glimmer][processor][bypass]")
{
	vekt::glimmer::PluginProcessor processor;
	setParameter(processor, vekt::glimmer::parameters::trackingOversampling, 0.0f);
	processor.prepareToPlay(48'000.0, 128);
	const auto latency = processor.getLatencySamples();
	REQUIRE(latency > 0);
	juce::MidiBuffer midi;
	for (int blockIndex = 0; blockIndex < 8; ++blockIndex)
	{
		juce::AudioBuffer<float> buffer(2, 128);
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		{
			const auto sourceIndex = blockIndex * buffer.getNumSamples() + sample;
			buffer.setSample(0, sample, 0.25f * std::sin(static_cast<float>(sourceIndex) * 0.1f));
			buffer.setSample(1, sample, -0.1f * std::cos(static_cast<float>(sourceIndex) * 0.2f));
		}
		processor.processBlockBypassed(buffer, midi);
		for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
			for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
			{
				const auto sourceIndex = blockIndex * buffer.getNumSamples() + sample - latency;
				const auto expected = sourceIndex < 0 ? 0.0f
					: (channel == 0 ? 0.25f * std::sin(static_cast<float>(sourceIndex) * 0.1f)
						: -0.1f * std::cos(static_cast<float>(sourceIndex) * 0.2f));
				REQUIRE(buffer.getSample(channel, sample) == Catch::Approx(expected).margin(1.0e-6f));
			}
	}
}

TEST_CASE("Glimmer mic distance changes pickup without changing latency", "[glimmer][processor][mic]")
{
	vekt::glimmer::PluginProcessor close;
	vekt::glimmer::PluginProcessor distant;
	setParameter(close, vekt::glimmer::parameters::micDistance, 0.3f);
	setParameter(distant, vekt::glimmer::parameters::micDistance, 3.0f);
	close.prepareToPlay(48'000.0, 128);
	distant.prepareToPlay(48'000.0, 128);
	REQUIRE(close.getLatencySamples() == distant.getLatencySamples());

	juce::MidiBuffer midi;
	double difference {};
	for (int blockIndex = 0; blockIndex < 16; ++blockIndex)
	{
		juce::AudioBuffer<float> closeBuffer(2, 128);
		juce::AudioBuffer<float> distantBuffer(2, 128);
		for (int sample = 0; sample < 128; ++sample)
		{
			const auto value = 0.2f * std::sin(static_cast<float>(blockIndex * 128 + sample) * 0.1f);
			closeBuffer.setSample(0, sample, value);
			closeBuffer.setSample(1, sample, value);
			distantBuffer.setSample(0, sample, value);
			distantBuffer.setSample(1, sample, value);
		}
		close.processBlock(closeBuffer, midi);
		distant.processBlock(distantBuffer, midi);
		for (int sample = 0; sample < 128; ++sample)
			difference += std::abs(closeBuffer.getSample(0, sample) - distantBuffer.getSample(0, sample));
	}
	REQUIRE(difference > 1.0);
}

TEST_CASE("Glimmer Mix reaches dry and wet endpoints", "[glimmer][processor][mix]")
{
	vekt::glimmer::PluginProcessor dry;
	vekt::glimmer::PluginProcessor wet;
	setParameter(dry, vekt::glimmer::parameters::mix, 0.0f);
	setParameter(wet, vekt::glimmer::parameters::mix, 100.0f);
	dry.prepareToPlay(48'000.0, 128);
	wet.prepareToPlay(48'000.0, 128);
	REQUIRE(dry.getLatencySamples() == wet.getLatencySamples());

	juce::MidiBuffer midi;
	double difference {};
	for (int blockIndex = 0; blockIndex < 96; ++blockIndex)
	{
		juce::AudioBuffer<float> dryBuffer(2, 128);
		juce::AudioBuffer<float> wetBuffer(2, 128);
		for (int sample = 0; sample < 128; ++sample)
		{
			const auto value = 0.25f * std::sin(static_cast<float>(blockIndex * 128 + sample) * 0.1f);
			dryBuffer.setSample(0, sample, value);
			dryBuffer.setSample(1, sample, value);
			wetBuffer.setSample(0, sample, value);
			wetBuffer.setSample(1, sample, value);
		}
		dry.processBlock(dryBuffer, midi);
		wet.processBlock(wetBuffer, midi);
		if (blockIndex >= 80)
			for (int sample = 0; sample < 128; ++sample)
				difference += std::abs(dryBuffer.getSample(0, sample) - wetBuffer.getSample(0, sample));
	}
	REQUIRE(difference > 1.0);
}
