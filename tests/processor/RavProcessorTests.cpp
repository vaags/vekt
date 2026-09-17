#include <Parameters.h>
#include <PluginProcessor.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <numbers>

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

void setParameter(
	vekt::rav::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	REQUIRE(parameter != nullptr);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

double renderAutoGainErrorDb(
	double sampleRate, int oversamplingFactorIndex, int oversamplingPhaseIndex, float driveDb,
	float bias, float modeIndex = 0.0f)
{
	constexpr auto blockSize = 256;
	constexpr auto frequency = 1'000.0;
	constexpr auto inputPeak = 0.12589254117941673;
	const auto settlingSamples = static_cast<int>(std::ceil(sampleRate * 0.15));
	const auto measurementSamples = static_cast<int>(std::ceil(sampleRate * 0.1));
	const auto totalSamples = settlingSamples + measurementSamples;

	vekt::rav::PluginProcessor processor;
	setParameter(processor, vekt::rav::parameters::drive, driveDb);
	setParameter(processor, vekt::rav::parameters::bias, bias);
	setParameter(processor, vekt::rav::parameters::tone, 0.0f);
	setParameter(processor, vekt::rav::parameters::mix, 100.0f);
	setParameter(processor, vekt::rav::parameters::autoGain, 1.0f);
	setParameter(processor, vekt::rav::parameters::mode, modeIndex);
	setParameter(processor, vekt::rav::parameters::trackingOversampling,
				 static_cast<float>(oversamplingFactorIndex));
	juce::ignoreUnused(oversamplingPhaseIndex);
	processor.prepareToPlay(sampleRate, blockSize);

	juce::MidiBuffer midi;
	auto inputSumOfSquares = 0.0;
	auto outputSum = 0.0;
	auto outputSumOfSquares = 0.0;
	auto sampleIndex = 0;
	while (sampleIndex < totalSamples)
	{
		const auto samplesThisBlock = std::min(blockSize, totalSamples - sampleIndex);
		juce::AudioBuffer<float> buffer(2, samplesThisBlock);
		for (auto sample = 0; sample < samplesThisBlock; ++sample)
		{
			const auto input = static_cast<float>(inputPeak * std::sin(
				2.0 * std::numbers::pi * frequency * static_cast<double>(sampleIndex + sample)
				/ sampleRate));
			buffer.setSample(0, sample, input);
			buffer.setSample(1, sample, input);
		}

		processor.processBlock(buffer, midi);
		for (auto sample = 0; sample < samplesThisBlock; ++sample)
		{
			if (sampleIndex + sample < settlingSamples)
				continue;

			const auto input = inputPeak * std::sin(
				2.0 * std::numbers::pi * frequency * static_cast<double>(sampleIndex + sample)
				/ sampleRate);
			const auto output = static_cast<double>(buffer.getSample(0, sample));
			inputSumOfSquares += input * input;
			outputSum += output;
			outputSumOfSquares += output * output;
		}

		sampleIndex += samplesThisBlock;
	}

	const auto inputRms = std::sqrt(inputSumOfSquares / static_cast<double>(measurementSamples));
	const auto outputMean = outputSum / static_cast<double>(measurementSamples);
	const auto outputRms = std::sqrt(
		(outputSumOfSquares / static_cast<double>(measurementSamples)) - (outputMean * outputMean));
	return 20.0 * std::log10(outputRms / inputRms);
}
}

TEST_CASE("Rav processor defaults to quality-first oversampling", "[processor]")
{
	vekt::rav::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 128);

	REQUIRE(processor.getLatencySamples() > 0);
	REQUIRE(processor.getTotalNumInputChannels() == 2);
	REQUIRE(processor.getTotalNumOutputChannels() == 2);
}

TEST_CASE("Rav processor selects tracking and offline oversampling profiles", "[processor][quality]")
{
	vekt::rav::PluginProcessor processor;
	setParameter(processor, vekt::rav::parameters::trackingOversampling, 2.0f);
	setParameter(processor, vekt::rav::parameters::offlineOversampling, 4.0f);

	processor.setNonRealtime(false);
	processor.prepareToPlay(48'000.0, 64);
	REQUIRE(processor.getActiveQuality().factor == vekt::dsp::OversamplingFactor::x4);
	REQUIRE(processor.getActiveQuality().filter == vekt::dsp::OversamplingFilter::polyphaseIIR);

	processor.setNonRealtime(true);
	processor.prepareToPlay(48'000.0, 64);
	REQUIRE(processor.getActiveQuality().factor == vekt::dsp::OversamplingFactor::x16);
	REQUIRE(processor.getActiveQuality().filter == vekt::dsp::OversamplingFilter::polyphaseFIR);
}

TEST_CASE("Rav processor produces finite stereo audio", "[processor]")
{
	vekt::rav::PluginProcessor processor;
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer midi;
	processor.prepareToPlay(48'000.0, buffer.getNumSamples());
	auto* tone = processor.getParameters().getParameter(vekt::rav::parameters::tone);
	auto* autoGain = processor.getParameters().getParameter(vekt::rav::parameters::autoGain);
	REQUIRE(tone != nullptr);
	REQUIRE(autoGain != nullptr);
	tone->setValueNotifyingHost(tone->convertTo0to1(6.0f));
	autoGain->setValueNotifyingHost(1.0f);

	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
			buffer.setSample(channel, sample, std::sin(static_cast<float>(sample) * 0.1f));

	processor.processBlock(buffer, midi);

	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
			REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
}

TEST_CASE("Rav saturation keeps identical noise channels aligned at bright tone", "[processor][stereo]")
{
	vekt::rav::PluginProcessor processor;
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	setParameter(processor, vekt::rav::parameters::mode, 0.0f);
	setParameter(processor, vekt::rav::parameters::tone, 6.0f);
	setParameter(processor, vekt::rav::parameters::drive, 18.0f);
	processor.prepareToPlay(48'000.0, buffer.getNumSamples());

	for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
	{
		const auto noise = std::sin(static_cast<float>(sample) * 0.731f)
			+ 0.37f * std::sin(static_cast<float>(sample) * 1.913f);
		buffer.setSample(0, sample, noise);
		buffer.setSample(1, sample, noise);
	}

	processor.processBlock(buffer, midi);
	for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
		REQUIRE(buffer.getSample(0, sample) == Catch::Approx(buffer.getSample(1, sample)).margin(1.0e-6f));
}

TEST_CASE("Rav processor renders every mode across tracking qualities", "[processor][rav]")
{
	for (auto qualityIndex : { 0.0f, 1.0f, 2.0f })
	{
		for (auto modeIndex = 0.0f; modeIndex < 6.0f; modeIndex += 1.0f)
		{
			vekt::rav::PluginProcessor processor;
			setParameter(processor, vekt::rav::parameters::trackingOversampling, qualityIndex);
			setParameter(processor, vekt::rav::parameters::mode, modeIndex);
			setParameter(processor, vekt::rav::parameters::drive, 30.0f);
			setParameter(processor, vekt::rav::parameters::bias, 0.5f);
			setParameter(processor, vekt::rav::parameters::autoGain, 1.0f);
			processor.prepareToPlay(48'000.0, 127);
			juce::AudioBuffer<float> buffer(2, 127);
			juce::MidiBuffer midi;
			for (auto channel = 0; channel < 2; ++channel)
				for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
					buffer.setSample(channel, sample,
						(channel == 0 ? 1.0f : -1.0f) * std::sin(static_cast<float>(sample) * 0.17f));

			processor.processBlock(buffer, midi);
			for (auto channel = 0; channel < 2; ++channel)
				for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
					REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
		}
	}
}

TEST_CASE("Rav band wet controls do not couple unaffected bands", "[processor][multiband][auto-gain]")
{
	vekt::rav::PluginProcessor dryLowProcessor;
	vekt::rav::PluginProcessor wetLowProcessor;
	setParameter(dryLowProcessor, vekt::rav::parameters::autoGain, 1.0f);
	setParameter(wetLowProcessor, vekt::rav::parameters::autoGain, 1.0f);
	setParameter(dryLowProcessor, vekt::rav::parameters::lowBandMix, 0.0f);
	setParameter(wetLowProcessor, vekt::rav::parameters::lowBandMix, 100.0f);
	dryLowProcessor.prepareToPlay(48'000.0, 128);
	wetLowProcessor.prepareToPlay(48'000.0, 128);
	juce::MidiBuffer midi;
	juce::AudioBuffer<float> dryBuffer(2, 128);
	juce::AudioBuffer<float> wetBuffer(2, 128);
	for (auto sample = 0; sample < 128; ++sample)
	{
		const auto value = std::sin(static_cast<float>(sample) * 0.13f);
		dryBuffer.setSample(0, sample, value);
		dryBuffer.setSample(1, sample, value);
		wetBuffer.setSample(0, sample, value);
		wetBuffer.setSample(1, sample, value);
	}
	for (auto block = 0; block < 32; ++block)
	{
		dryLowProcessor.processBlock(dryBuffer, midi);
		wetLowProcessor.processBlock(wetBuffer, midi);
	}

	for (auto sample = 32; sample < 128; ++sample)
		REQUIRE(dryBuffer.getSample(0, sample)
			== Catch::Approx(wetBuffer.getSample(0, sample)).margin(1.0e-3f));
}

TEST_CASE("Rav Bitcrush Auto Gain stays near reference loudness", "[processor][auto-gain][bitcrush]")
{
	const auto errorDb = renderAutoGainErrorDb(48'000.0, 2, 0, 18.0f, 0.5f, 5.0f);
	INFO("Bitcrush Auto Gain error: " << errorDb << " dB");
	REQUIRE(std::abs(errorDb) < 1.0);
}

TEST_CASE("Rav Fuzz and Wavefold Auto Gain stay near default loudness", "[processor][auto-gain]")
{
	const auto fuzzErrorDb = renderAutoGainErrorDb(48'000.0, 2, 0, 6.0f, 0.0f, 3.0f);
	const auto wavefoldErrorDb = renderAutoGainErrorDb(48'000.0, 2, 0, 6.0f, 0.0f, 4.0f);
	REQUIRE(std::abs(fuzzErrorDb) < 2.5);
	REQUIRE(std::abs(wavefoldErrorDb) < 2.5);
}

TEST_CASE("Rav processor bounds oversized host blocks", "[processor]")
{
	constexpr auto preparedBlockSize = 64;
	constexpr auto hostBlockSize = 257;
	vekt::rav::PluginProcessor processor;
	juce::AudioBuffer<float> buffer(2, hostBlockSize);
	juce::MidiBuffer midi;
	processor.prepareToPlay(48'000.0, preparedBlockSize);

	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
			buffer.setSample(channel, sample, std::sin(static_cast<float>(sample) * 0.1f));

	processor.processBlock(buffer, midi);
	setParameter(processor, vekt::rav::parameters::bypass, 1.0f);
	processor.processBlock(buffer, midi);

	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
			REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
}

TEST_CASE("Rav processor state round trips parameters", "[processor][state]")
{
	vekt::rav::PluginProcessor source;
	vekt::rav::PluginProcessor restored;
	auto restoredMetadata = restored.getProjectMetadata();
	auto* sourceDrive = source.getParameters().getParameter(vekt::rav::parameters::drive);
	auto* sourceTone = source.getParameters().getParameter(vekt::rav::parameters::tone);
	auto* sourceAutoGain = source.getParameters().getParameter(vekt::rav::parameters::autoGain);
	auto* sourceBypass = source.getParameters().getParameter(vekt::rav::parameters::bypass);
	REQUIRE(sourceDrive != nullptr);
	REQUIRE(sourceTone != nullptr);
	REQUIRE(sourceAutoGain != nullptr);
	REQUIRE(sourceBypass != nullptr);
	sourceDrive->setValueNotifyingHost(sourceDrive->convertTo0to1(18.0f));
	sourceTone->setValueNotifyingHost(sourceTone->convertTo0to1(-3.0f));
	sourceAutoGain->setValueNotifyingHost(1.0f);
	sourceBypass->setValueNotifyingHost(1.0f);
	source.getProjectMetadata().setProperty("editorWidth", 900, nullptr);

	juce::MemoryBlock state;
	source.getStateInformation(state);
	const auto xml = juce::AudioProcessor::getXmlFromBinary(
		state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(xml != nullptr);
	const auto projectState = juce::ValueTree::fromXml(*xml);
	REQUIRE(projectState.hasType(vekt::rav::parameters::projectStateType));
	REQUIRE(static_cast<int>(projectState.getProperty(
		vekt::state::StateManager::schemaVersionProperty))
		== vekt::rav::parameters::projectStateVersion);
	REQUIRE(projectState.getChildWithName(vekt::rav::parameters::stateType).isValid());
	REQUIRE(projectState.getChildWithName(vekt::state::StateManager::metadataType).isValid());
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

	const auto* restoredDrive = restored.getParameters().getRawParameterValue(
		vekt::rav::parameters::drive);
	const auto* restoredTone = restored.getParameters().getRawParameterValue(
		vekt::rav::parameters::tone);
	const auto* restoredAutoGain = restored.getParameters().getRawParameterValue(
		vekt::rav::parameters::autoGain);
	const auto* restoredBypass = restored.getParameters().getRawParameterValue(
		vekt::rav::parameters::bypass);
	REQUIRE(restoredDrive != nullptr);
	REQUIRE(restoredTone != nullptr);
	REQUIRE(restoredAutoGain != nullptr);
	REQUIRE(restoredBypass != nullptr);
	REQUIRE(restoredDrive->load() == Catch::Approx(18.0f));
	REQUIRE(restoredTone->load() == Catch::Approx(-3.0f));
	REQUIRE(restoredAutoGain->load() == Catch::Approx(1.0f));
	REQUIRE(restoredBypass->load() == Catch::Approx(1.0f));
	REQUIRE(static_cast<int>(restoredMetadata.getProperty("editorWidth")) == 900);
}

TEST_CASE("Rav processor migrates legacy version one state", "[processor][state]")
{
	vekt::rav::PluginProcessor source;
	vekt::rav::PluginProcessor restored;
	auto* sourceDrive = source.getParameters().getParameter(vekt::rav::parameters::drive);
	REQUIRE(sourceDrive != nullptr);
	sourceDrive->setValueNotifyingHost(sourceDrive->convertTo0to1(24.0f));

	auto legacyState = source.getParameters().copyState();
	legacyState.setProperty(vekt::state::StateManager::legacyVersionProperty, 1, nullptr);
	juce::MemoryBlock binary;
	if (const auto xml = legacyState.createXml())
		juce::AudioProcessor::copyXmlToBinary(*xml, binary);
	restored.setStateInformation(binary.getData(), static_cast<int>(binary.getSize()));

	const auto* restoredDrive = restored.getParameters().getRawParameterValue(
		vekt::rav::parameters::drive);
	REQUIRE(restoredDrive != nullptr);
	REQUIRE(restoredDrive->load() == Catch::Approx(24.0f));
	REQUIRE(restored.getProjectMetadata().hasType(vekt::state::StateManager::metadataType));
}

TEST_CASE("Rav processor rejects future project state", "[processor][state]")
{
	vekt::rav::PluginProcessor processor;
	setParameter(processor, vekt::rav::parameters::drive, 12.0f);
	juce::MemoryBlock binary;
	processor.getStateInformation(binary);

	const auto xml = juce::AudioProcessor::getXmlFromBinary(
		binary.getData(), static_cast<int>(binary.getSize()));
	REQUIRE(xml != nullptr);
	auto futureState = juce::ValueTree::fromXml(*xml);
	futureState.setProperty(vekt::state::StateManager::schemaVersionProperty, 999, nullptr);
	if (const auto futureXml = futureState.createXml())
		juce::AudioProcessor::copyXmlToBinary(*futureXml, binary);

	setParameter(processor, vekt::rav::parameters::drive, 6.0f);
	processor.setStateInformation(binary.getData(), static_cast<int>(binary.getSize()));
	const auto* drive = processor.getParameters().getRawParameterValue(
		vekt::rav::parameters::drive);
	REQUIRE(drive != nullptr);
	REQUIRE(drive->load() == Catch::Approx(6.0f));
}

TEST_CASE("Rav processor bypass preserves reported latency across transitions", "[processor][bypass]")
{
	constexpr auto blockSize = 128;
	vekt::rav::PluginProcessor processor;
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

TEST_CASE("Rav bypass parameter returns latency-aligned raw input", "[processor][bypass]")
{
	constexpr auto blockSize = 128;
	vekt::rav::PluginProcessor processor;
	setParameter(processor, vekt::rav::parameters::inputGain, 24.0f);
	setParameter(processor, vekt::rav::parameters::drive, 36.0f);
	setParameter(processor, vekt::rav::parameters::tone, 6.0f);
	setParameter(processor, vekt::rav::parameters::bias, 1.0f);
	setParameter(processor, vekt::rav::parameters::outputGain, 24.0f);
	setParameter(processor, vekt::rav::parameters::bypass, 1.0f);
	processor.prepareToPlay(48'000.0, blockSize);

	auto* bypassParameter = processor.getParameters().getParameter(
		vekt::rav::parameters::bypass);
	REQUIRE(bypassParameter != nullptr);
	REQUIRE(processor.getBypassParameter() == bypassParameter);

	juce::AudioBuffer<float> buffer(2, blockSize);
	juce::MidiBuffer midi;
	buffer.clear();
	buffer.setSample(0, 0, 0.25f);
	buffer.setSample(1, 0, -0.5f);
	processor.processBlock(buffer, midi);

	const auto latency = processor.getLatencySamples();
	REQUIRE(buffer.getSample(0, latency) == Catch::Approx(0.25f));
	REQUIRE(buffer.getSample(1, latency) == Catch::Approx(-0.5f));
}

TEST_CASE("Rav processor applies quality changes while stopped", "[processor][quality]")
{
	vekt::rav::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 128);
	auto *factor = processor.getParameters().getParameter(
		vekt::rav::parameters::trackingOversampling);
	REQUIRE(factor != nullptr);

	factor->setValueNotifyingHost(0.0f);
	processor.applyPendingQualityChange();

	REQUIRE(processor.getActiveQuality().factor == vekt::dsp::OversamplingFactor::off);
	REQUIRE(processor.getLatencySamples() == 0);
	REQUIRE_FALSE(processor.hasPendingQualityChange());
}

TEST_CASE("Rav processor defers quality changes during playback", "[processor][quality]")
{
	vekt::rav::PluginProcessor processor;
	TestPlayHead playHead;
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer midi;
	processor.setPlayHead(&playHead);
	processor.prepareToPlay(48'000.0, buffer.getNumSamples());

	playHead.playing = true;
	buffer.clear();
	processor.processBlock(buffer, midi);
	auto *factor = processor.getParameters().getParameter(
		vekt::rav::parameters::trackingOversampling);
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

TEST_CASE("Rav auto-gain holds reference loudness through the wet chain", "[processor][auto-gain]")
{
	struct QualityMode
	{
		int factorIndex;
		int phaseIndex;
	};

	constexpr std::array qualityModes {
		QualityMode { 0, 0 },
		QualityMode { 1, 0 },
		QualityMode { 2, 0 },
		QualityMode { 1, 1 },
		QualityMode { 2, 1 }
	};

	for (const auto sampleRate : std::array { 44'100.0, 48'000.0, 96'000.0, 192'000.0 })
	{
		for (const auto quality : qualityModes)
		{
			INFO("sample rate: " << sampleRate << ", factor index: " << quality.factorIndex
				<< ", phase index: " << quality.phaseIndex);
			REQUIRE(std::abs(renderAutoGainErrorDb(
				sampleRate, quality.factorIndex, quality.phaseIndex, 18.0f, 0.5f)) < 2.5);
		}
	}

	struct CalibrationPoint
	{
		float driveDb;
		float bias;
	};

	for (const auto point : std::array {
		CalibrationPoint { 6.0f, 0.0f },
		CalibrationPoint { 18.0f, 0.5f },
		CalibrationPoint { 36.0f, 1.0f } })
	{
		INFO("drive: " << point.driveDb << " dB, bias: " << point.bias);
		REQUIRE(std::abs(renderAutoGainErrorDb(48'000.0, 2, 0, point.driveDb, point.bias)) < 4.5);
	}
}

TEST_CASE("Rav processor publishes and consumes stereo peak snapshots", "[processor][meter]")
{
	vekt::rav::PluginProcessor processor;
	juce::AudioBuffer<float> buffer(2, 32);
	juce::MidiBuffer midi;
	processor.prepareToPlay(48'000.0, buffer.getNumSamples());
	buffer.clear();
	buffer.setSample(0, 0, 0.25f);
	buffer.setSample(1, 0, -0.5f);

	processor.processBlock(buffer, midi);
	const auto input = processor.consumeInputPeaks();
	const auto output = processor.consumeOutputPeaks();
	REQUIRE(input[0] == Catch::Approx(0.25f));
	REQUIRE(input[1] == Catch::Approx(0.5f));
	REQUIRE(output[0] > 0.0f);
	REQUIRE(output[1] > 0.0f);
	REQUIRE(processor.consumeInputPeaks() == std::array { 0.0f, 0.0f });
	REQUIRE(processor.consumeOutputPeaks() == std::array { 0.0f, 0.0f });
}
