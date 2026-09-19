#include <vekt/glimmer/Parameters.h>
#include <vekt/glimmer/PluginProcessor.h>
#include "../../plugins/vekt_rav/Source/PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

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

TEST_CASE("Glimmer dry impulse follows the fixed pickup center", "[glimmer][processor][mix][latency]")
{
	for (const auto sampleRate : { 44'100.0, 48'000.0, 96'000.0, 192'000.0 })
	{
		vekt::glimmer::PluginProcessor processor;
		setParameter(processor, vekt::glimmer::parameters::trackingOversampling, 0.0f);
		setParameter(processor, vekt::glimmer::parameters::mix, 0.0f);
		processor.prepareToPlay(sampleRate, 128);
		const auto expectedLatency = static_cast<int>(std::lround(0.008 * sampleRate));
		REQUIRE(processor.getLatencySamples() == expectedLatency);
		juce::MidiBuffer midi;
		juce::AudioBuffer<float> warmup(2, static_cast<int>(sampleRate));
		warmup.clear();
		processor.processBlock(warmup, midi);
		juce::AudioBuffer<float> impulse(2, expectedLatency + 128);
		impulse.clear();
		impulse.setSample(0, 0, 0.25f);
		impulse.setSample(1, 0, -0.125f);
		processor.processBlock(impulse, midi);
		for (int sample = 0; sample < impulse.getNumSamples(); ++sample)
		{
			REQUIRE(impulse.getSample(0, sample) == Catch::Approx(sample == expectedLatency ? 0.25f : 0.0f).margin(1.0e-6f));
			REQUIRE(impulse.getSample(1, sample) == Catch::Approx(sample == expectedLatency ? -0.125f : 0.0f).margin(1.0e-6f));
		}
	}
}

TEST_CASE("Glimmer half Mix is the aligned linear blend", "[glimmer][processor][mix]")
{
	using namespace vekt::glimmer;
	PluginProcessor dry, wet, half;
	setParameter(dry, parameters::mix, 0);
	setParameter(half, parameters::mix, 50);
	for (auto* processor : { &dry, &wet, &half })
	{
		setParameter(*processor, parameters::trackingOversampling, 0);
		processor->prepareToPlay(48000, 128);
	}
	juce::MidiBuffer midi;
	for (int blockIndex = 0; blockIndex < 80; ++blockIndex)
	{
		juce::AudioBuffer<float> dryBuffer(2, 128), wetBuffer(2, 128), halfBuffer(2, 128);
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < 128; ++sample)
			{
				const auto value = 0.1f * std::sin(static_cast<float>(blockIndex * 128 + sample) * (channel == 0 ? 0.07f : 0.13f));
				for (auto* buffer : { &dryBuffer, &wetBuffer, &halfBuffer }) buffer->setSample(channel, sample, value);
			}
		dry.processBlock(dryBuffer, midi);
		wet.processBlock(wetBuffer, midi);
		half.processBlock(halfBuffer, midi);
		if (blockIndex > 40)
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < 128; ++sample)
					REQUIRE(halfBuffer.getSample(channel, sample) == Catch::Approx(
						0.5f * (dryBuffer.getSample(channel, sample) + wetBuffer.getSample(channel, sample))).margin(1.0e-6f));
	}
}

TEST_CASE("Glimmer model requests settle without changing latency or losing signal", "[glimmer][processor][model]")
{
	using namespace vekt::glimmer;
	PluginProcessor processor;
	setParameter(processor, parameters::trackingOversampling, 0);
	processor.prepareToPlay(48000, 128);
	const auto latency = processor.getLatencySamples();
	juce::MidiBuffer midi;
	float previous {};
	double energy {};
	for (int blockIndex = 0; blockIndex < 250; ++blockIndex)
	{
		if (blockIndex == 30) setParameter(processor, parameters::cabinetModel, 1);
		if (blockIndex == 35) setParameter(processor, parameters::cabinetModel, 2);
		juce::AudioBuffer<float> buffer(2, 128);
		for (int sample = 0; sample < 128; ++sample)
		{
			const auto input = 0.05f * std::sin(static_cast<float>(blockIndex * 128 + sample) * 0.06f);
			buffer.setSample(0, sample, input);
			buffer.setSample(1, sample, -input);
		}
		processor.processBlock(buffer, midi);
		REQUIRE(processor.getLatencySamples() == latency);
		for (int sample = 0; sample < 128; ++sample)
		{
			const auto value = buffer.getSample(0, sample);
			REQUIRE(std::isfinite(value));
			REQUIRE(std::abs(value - previous) < 0.015f);
			previous = value;
			energy += value * value;
		}
	}
	REQUIRE(processor.getActiveModel() == 2);
	REQUIRE_FALSE(processor.hasPendingModelChange());
	REQUIRE(energy > 0.1);
}

TEST_CASE("Glimmer Brake stops both rotors without bypassing the cabinet", "[glimmer][processor][brake]")
{
	using namespace vekt::glimmer;
	PluginProcessor processor;
	setParameter(processor, parameters::decelerationTime, 0.1f);
	setParameter(processor, parameters::manualSpeedEnabled, 1);
	setParameter(processor, parameters::speedPosition, 100);
	processor.prepareToPlay(48000, 128);
	setParameter(processor, parameters::brake, 1);
	juce::AudioBuffer<float> buffer(2, 24000);
	for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		for (int channel = 0; channel < 2; ++channel)
			buffer.setSample(channel, sample, 0.1f * std::sin(static_cast<float>(sample) * 0.1f));
	juce::MidiBuffer midi;
	processor.processBlock(buffer, midi);
	for (const auto rpm : processor.getRotorSpeeds()) REQUIRE(rpm == Catch::Approx(0.0f));
	REQUIRE(buffer.getRMSLevel(0, 20000, 4000) > 0.005f);
	REQUIRE(processor.getParameters().getRawParameterValue(parameters::manualSpeedEnabled)->load() > 0.5f);
}

TEST_CASE("Glimmer legacy project restores additive defaults in a modified instance", "[glimmer][processor][state]")
{
	using namespace vekt::glimmer;
	PluginProcessor processor;
	auto legacy = processor.getParameters().copyState();
	for (const auto* identifier : { parameters::cabinetModel, parameters::brake, parameters::stereoWidth,
		parameters::manualSpeedEnabled, parameters::speedPosition })
		legacy.removeChild(legacy.getChildWithProperty("id", identifier), nullptr);
	setParameter(processor, parameters::cabinetModel, 2);
	setParameter(processor, parameters::brake, 1);
	setParameter(processor, parameters::stereoWidth, 0);
	juce::MemoryBlock state;
	juce::MemoryOutputStream stream(state, false);
	legacy.writeToStream(stream);
	processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(processor.getParameters().getRawParameterValue(parameters::cabinetModel)->load() == Catch::Approx(0));
	REQUIRE(processor.getParameters().getRawParameterValue(parameters::brake)->load() == Catch::Approx(0));
	REQUIRE(processor.getParameters().getRawParameterValue(parameters::stereoWidth)->load() == Catch::Approx(100));
}

TEST_CASE("Glimmer live bypass crossfades then returns exact raw input", "[glimmer][processor][bypass]")
{
	using namespace vekt::glimmer;
	PluginProcessor processor;
	setParameter(processor, parameters::trackingOversampling, 0);
	setParameter(processor, parameters::outputGain, -24);
	processor.prepareToPlay(48000, 128);
	juce::MidiBuffer midi;
	float previous {};
	for (int blockIndex = 0; blockIndex < 100; ++blockIndex)
	{
		juce::AudioBuffer<float> buffer(2, 128);
		for (int sample = 0; sample < 128; ++sample)
			for (int channel = 0; channel < 2; ++channel) buffer.setSample(channel, sample, 0.2f);
		if (blockIndex < 50) processor.processBlock(buffer, midi);
		else processor.processBlockBypassed(buffer, midi);
		for (int sample = 0; sample < 128; ++sample)
		{
			const auto value = buffer.getSample(0, sample);
			REQUIRE(std::abs(value - previous) < 0.005f);
			previous = value;
			if (blockIndex > 55) REQUIRE(value == Catch::Approx(0.2f).margin(1.0e-7f));
		}
	}
}

TEST_CASE("Glimmer quality stays deferred while transport plays", "[glimmer][processor][quality]")
{
	class PlayHead final : public juce::AudioPlayHead
	{
	public:
		juce::Optional<PositionInfo> getPosition() const override
		{
			PositionInfo position;
			position.setIsPlaying(playing);
			return position;
		}
		bool playing { true };
	} playHead;
	vekt::glimmer::PluginProcessor processor;
	processor.setPlayHead(&playHead);
	processor.prepareToPlay(48000, 128);
	const auto originalLatency = processor.getLatencySamples();
	setParameter(processor, vekt::glimmer::parameters::trackingOversampling, 6);
	juce::AudioBuffer<float> buffer(2, 128);
	buffer.clear();
	juce::MidiBuffer midi;
	processor.processBlock(buffer, midi);
	REQUIRE(processor.hasPendingQualityChange());
	REQUIRE(processor.getLatencySamples() == originalLatency);
	playHead.playing = false;
	processor.processBlock(buffer, midi);
	REQUIRE_FALSE(processor.hasPendingQualityChange());
	REQUIRE(processor.getActiveQuality().factor == vekt::dsp::OversamplingFactor::x16);
}

TEST_CASE("Glimmer models and extremes remain stable across rates and qualities", "[glimmer][processor][matrix]")
{
	using namespace vekt::glimmer;
	for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
		for (const auto quality : { 0.0f, 2.0f, 6.0f })
			for (int model = 0; model < 3; ++model)
			{
				PluginProcessor processor;
				setParameter(processor, parameters::trackingOversampling, quality);
				setParameter(processor, parameters::cabinetModel, static_cast<float>(model));
				setParameter(processor, parameters::preampDrive, 36);
				setParameter(processor, parameters::hornTone, 12);
				setParameter(processor, parameters::drumTone, -12);
				setParameter(processor, parameters::stereoWidth, 200);
				setParameter(processor, parameters::micDistance, 0.3f);
				processor.prepareToPlay(rate, 64);
				juce::AudioBuffer<float> buffer(2, 2053);
				for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
					for (int channel = 0; channel < 2; ++channel)
						buffer.setSample(channel, sample, std::sin(static_cast<float>(sample) * 0.1f) * (channel == 0 ? 0.1f : -0.1f));
				juce::MidiBuffer midi;
				processor.processBlock(buffer, midi);
				for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
					for (int channel = 0; channel < 2; ++channel)
					{
						REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
						REQUIRE(std::abs(buffer.getSample(channel, sample)) < 16);
					}
			}
}

TEST_CASE("Glimmer automation is stable across block partitions", "[glimmer][processor][automation]")
{
	using namespace vekt::glimmer;
	PluginProcessor small, large;
	for (auto* processor : { &small, &large })
		setParameter(*processor, parameters::trackingOversampling, 0);
	small.prepareToPlay(48000, 32);
	large.prepareToPlay(48000, 257);
	juce::MidiBuffer midi;
	for (int segment = 0; segment < 4; ++segment)
	{
		for (auto* processor : { &small, &large })
		{
			setParameter(*processor, parameters::inputGain, segment == 1 ? 12 : 0);
			setParameter(*processor, parameters::preampDrive, segment == 2 ? 24 : 0);
			setParameter(*processor, parameters::micAngle, segment == 1 ? 359 : 1);
			setParameter(*processor, parameters::micDistance, segment == 2 ? 0.3f : 3);
			setParameter(*processor, parameters::stereoWidth, segment == 3 ? 0 : 150);
			setParameter(*processor, parameters::hornTone, segment == 2 ? 12 : -12);
		}
		juce::AudioBuffer<float> smallBuffer(2, 4096), largeBuffer(2, 4096);
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < 4096; ++sample)
			{
				const auto value = 0.05f * std::sin(static_cast<float>(segment * 4096 + sample) * (channel == 0 ? 0.1f : 0.13f));
				smallBuffer.setSample(channel, sample, value);
				largeBuffer.setSample(channel, sample, value);
			}
		small.processBlock(smallBuffer, midi);
		large.processBlock(largeBuffer, midi);
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < 4096; ++sample)
				REQUIRE(smallBuffer.getSample(channel, sample) == Catch::Approx(largeBuffer.getSample(channel, sample)).margin(1.0e-6f));
	}
}

TEST_CASE("Glimmer impulse decays within the reported tail", "[glimmer][processor][tail]")
{
	using namespace vekt::glimmer;
	for (int model = 0; model < 3; ++model)
		for (const auto quality : { 0.0f, 6.0f })
		{
			PluginProcessor processor;
			setParameter(processor, parameters::cabinetModel, static_cast<float>(model));
			setParameter(processor, parameters::trackingOversampling, quality);
			setParameter(processor, parameters::preampDrive, 36);
			setParameter(processor, parameters::drumTone, 12);
			setParameter(processor, parameters::hornTone, 12);
			processor.prepareToPlay(48000, 128);
			const auto tail = static_cast<int>(processor.getTailLengthSeconds() * 48000);
			juce::AudioBuffer<float> buffer(2, tail + 1024);
			buffer.clear();
			buffer.setSample(0, 0, 0.5f);
			juce::MidiBuffer midi;
			processor.processBlock(buffer, midi);
			REQUIRE(buffer.getMagnitude(0, tail, 1024) < 1.0e-6f);
		}
}

TEST_CASE("Glimmer callback benchmark includes model transitions and rack", "[glimmer][benchmark]")
{
	if (std::getenv("VEKT_GLIMMER_BENCHMARK") == nullptr)
	{
		SUCCEED("Set VEKT_GLIMMER_BENCHMARK=1 in a Release build to collect timings");
		return;
	}
	using namespace vekt::glimmer;
	for (const auto rate : { 48000.0, 96000.0 })
		for (const auto blockSize : { 64, 128 })
			for (const auto rack : { false, true })
			{
				PluginProcessor processor;
				vekt::rav::PluginProcessor rav;
				processor.prepareToPlay(rate, blockSize);
				rav.prepareToPlay(rate, blockSize);
				juce::AudioBuffer<float> buffer(2, blockSize);
				juce::MidiBuffer midi;
				std::vector<double> durations;
				durations.reserve(800);
				double sum {}, transitionMaximum {};
				int overBudget {};
				for (int blockIndex = 0; blockIndex < 800; ++blockIndex)
				{
					if (blockIndex == 50) setParameter(processor, parameters::cabinetModel, 1);
					if (blockIndex == 250) setParameter(processor, parameters::cabinetModel, 2);
					if (blockIndex == 500) setParameter(processor, parameters::cabinetModel, 0);
					for (int sample = 0; sample < blockSize; ++sample)
						for (int channel = 0; channel < 2; ++channel)
							buffer.setSample(channel, sample, 0.1f * std::sin(static_cast<float>(blockIndex * blockSize + sample) * 0.13f));
					const auto start = juce::Time::getMillisecondCounterHiRes();
					if (rack) rav.processBlock(buffer, midi);
					processor.processBlock(buffer, midi);
					const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
					durations.push_back(elapsed);
					sum += elapsed;
					if (elapsed > static_cast<double>(blockSize) * 1000.0 / rate) ++overBudget;
					if (processor.hasPendingModelChange()) transitionMaximum = std::max(transitionMaximum, elapsed);
				}
				std::sort(durations.begin(), durations.end());
				std::cout << "Glimmer benchmark rate=" << rate << " block=" << blockSize << " rack=" << rack
					<< " percent=" << sum * rate / (800.0 * blockSize * 10.0)
					<< " max_ms=" << durations.back() << " p99_ms=" << durations[792]
					<< " transition_max_ms=" << transitionMaximum << " over_budget=" << overBudget << '\n';
				REQUIRE(processor.getActiveModel() == 0);
			}
}
