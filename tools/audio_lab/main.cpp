#include "SignalSources.h"

#include <PluginProcessor.h>
#include <vekt/glimmer/PluginProcessor.h>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace
{
struct Options final
{
	juce::String product { "rav" };
	juce::String rack;
	juce::StringArray glimmerOverrides;
	vekt::audio_lab::Source source { vekt::audio_lab::Source::sine };
	double sampleRate { 48'000.0 };
	int blockSize { 128 };
	double seconds { 1.0 };
	double warmupSeconds { 0.2 };
	double frequencyHz { 1'000.0 };
	int mode {};
	int qualityIndex { -1 };
	int spectrumSize {};
	bool offlineProfile { true };
	float drive { 6.0f };
	float bias {};
	float shape { 0.5f };
	float dynamics { 0.5f };
	float texture { 0.5f };
	float tone {};
	float mix { 100.0f };
	float lowBandMix { 100.0f };
	float midBandMix { 100.0f };
	float highBandMix { 100.0f };
	float inputGainDb {};
	bool autoGain {};
	std::array<bool, 4> stageEnabled { true, false, false, false };
	vekt::rav::RavStageChain::Order stageOrder { vekt::rav::RavMode::saturation,
		vekt::rav::RavMode::overdrive, vekt::rav::RavMode::distortion, vekt::rav::RavMode::circuitFuzz };
	vekt::rav::RavProcessingModel model { vekt::rav::RavProcessingModel::legacy };
	std::uint32_t seed { 0x6d2b79f5u };
	juce::String inputPath;
	juce::String reportPath;
	juce::String wavPath;
};

struct InputFile final
{
	juce::AudioBuffer<float> samples;
	double sampleRate {};
};

struct ChannelMeasurements final
{
	double sumSquares {};
	double sum {};
	float peak {};

	void add(float value) noexcept
	{
		sumSquares += static_cast<double>(value) * value;
		sum += value;
		peak = std::max(peak, std::abs(value));
	}

	[[nodiscard]] double rms(std::int64_t sampleCount) const noexcept
	{
		return std::sqrt(sumSquares / static_cast<double>(sampleCount));
	}

	[[nodiscard]] double dc(std::int64_t sampleCount) const noexcept
	{
		return sum / static_cast<double>(sampleCount);
	}
};

[[nodiscard]] bool valueFor(int& index, int argc, char** argv, std::string_view name, std::string& value)
{
	if (std::string_view(argv[index]) != name || index + 1 >= argc)
		return false;
	value = argv[++index];
	return true;
}

[[nodiscard]] bool parseStageEnablement(std::string_view value, std::array<bool, 4>& destination)
{
	if (value.size() != destination.size())
		return false;
	for (std::size_t index = 0; index < destination.size(); ++index)
	{
		if (value[index] != '0' && value[index] != '1')
			return false;
		destination[index] = value[index] == '1';
	}
	return true;
}

[[nodiscard]] bool parseStageOrder(std::string_view value, vekt::rav::RavStageChain::Order& destination)
{
	return vekt::rav::RavStageChain::deserialise(
		juce::String(value.data(), value.size()), destination);
}

[[nodiscard]] bool parseProcessingModel(std::string_view value, vekt::rav::RavProcessingModel& destination)
{
	if (value == "legacy") destination = vekt::rav::RavProcessingModel::legacy;
	else if (value == "behavioral") destination = vekt::rav::RavProcessingModel::behavioralCandidate;
	else if (value == "overdrive-circuit") destination = vekt::rav::RavProcessingModel::overdriveCircuitCandidate;
	else if (value == "fuzz-circuit") destination = vekt::rav::RavProcessingModel::fuzzCircuitCandidate;
	else return false;
	return true;
}

[[nodiscard]] bool parseOptions(int argc, char** argv, Options& options)
{
	try
	{
		for (int index = 1; index < argc; ++index)
		{
			std::string value;
			if (valueFor(index, argc, argv, "--product", value))
			{
				if (value != "rav" && value != "glimmer") return false;
				options.product = value;
			}
			else if (valueFor(index, argc, argv, "--rack", value))
			{
				if (value != "rav,glimmer" && value != "glimmer,rav") return false;
				options.rack = value;
			}
			else if (valueFor(index, argc, argv, "--param", value))
			{
				const juce::String assignment(value);
				if (!assignment.startsWith("glimmer.") || !assignment.containsChar('=')) return false;
				options.glimmerOverrides.add(assignment.substring(8));
			}
			else if (valueFor(index, argc, argv, "--source", value))
			{
				if (value == "sine") options.source = vekt::audio_lab::Source::sine;
				else if (value == "sawtooth") options.source = vekt::audio_lab::Source::sawtooth;
				else if (value == "sweep") options.source = vekt::audio_lab::Source::sweep;
				else if (value == "impulse") options.source = vekt::audio_lab::Source::impulse;
				else if (value == "noise") options.source = vekt::audio_lab::Source::noise;
				else if (value == "kick") options.source = vekt::audio_lab::Source::kick;
				else if (value == "unison") options.source = vekt::audio_lab::Source::unison;
				else if (value == "two-tone") options.source = vekt::audio_lab::Source::twoTone;
				else return false;
			}
			else if (valueFor(index, argc, argv, "--sample-rate", value)) options.sampleRate = std::stod(value);
			else if (valueFor(index, argc, argv, "--block-size", value)) options.blockSize = std::stoi(value);
			else if (valueFor(index, argc, argv, "--seconds", value)) options.seconds = std::stod(value);
			else if (valueFor(index, argc, argv, "--warmup", value)) options.warmupSeconds = std::stod(value);
			else if (valueFor(index, argc, argv, "--frequency", value)) options.frequencyHz = std::stod(value);
			else if (valueFor(index, argc, argv, "--mode", value)) options.mode = std::clamp(std::stoi(value), 0, 3);
			else if (valueFor(index, argc, argv, "--quality", value)) options.qualityIndex = std::clamp(std::stoi(value), 0, 6);
			else if (valueFor(index, argc, argv, "--spectrum-size", value)) options.spectrumSize = std::stoi(value);
			else if (valueFor(index, argc, argv, "--profile", value))
			{
				if (value == "offline") options.offlineProfile = true;
				else if (value == "tracking") options.offlineProfile = false;
				else return false;
			}
			else if (valueFor(index, argc, argv, "--drive", value)) options.drive = std::stof(value);
			else if (valueFor(index, argc, argv, "--bias", value)) options.bias = std::stof(value);
			else if (valueFor(index, argc, argv, "--shape", value)) options.shape = std::stof(value);
			else if (valueFor(index, argc, argv, "--dynamics", value)) options.dynamics = std::stof(value);
			else if (valueFor(index, argc, argv, "--texture", value)) options.texture = std::stof(value);
			else if (valueFor(index, argc, argv, "--tone", value)) options.tone = std::stof(value);
			else if (valueFor(index, argc, argv, "--mix", value)) options.mix = std::stof(value);
			else if (valueFor(index, argc, argv, "--low-mix", value)) options.lowBandMix = std::stof(value);
			else if (valueFor(index, argc, argv, "--mid-mix", value)) options.midBandMix = std::stof(value);
			else if (valueFor(index, argc, argv, "--high-mix", value)) options.highBandMix = std::stof(value);
			else if (valueFor(index, argc, argv, "--input-gain", value)) options.inputGainDb = std::stof(value);
			else if (valueFor(index, argc, argv, "--stages", value))
			{
				if (!parseStageEnablement(value, options.stageEnabled)) return false;
			}
			else if (valueFor(index, argc, argv, "--stage-order", value))
			{
				if (!parseStageOrder(value, options.stageOrder)) return false;
			}
			else if (valueFor(index, argc, argv, "--model", value))
			{
				if (!parseProcessingModel(value, options.model)) return false;
			}
			else if (valueFor(index, argc, argv, "--seed", value)) options.seed = static_cast<std::uint32_t>(std::stoul(value));
			else if (valueFor(index, argc, argv, "--input", value)) options.inputPath = value;
			else if (valueFor(index, argc, argv, "--report", value)) options.reportPath = value;
			else if (valueFor(index, argc, argv, "--wav", value)) options.wavPath = value;
			else if (std::string_view(argv[index]) == "--auto-gain") options.autoGain = true;
			else return false;
		}
	}
	catch (const std::exception&)
	{
		return false;
	}
	if (options.rack.isNotEmpty() && options.product != "rav")
		return false;
	if (!options.glimmerOverrides.isEmpty() && options.product != "glimmer" && options.rack.isEmpty())
		return false;
	return options.sampleRate > 0.0 && options.blockSize > 0 && options.seconds > 0.0
		&& options.warmupSeconds >= 0.0 && options.frequencyHz > 0.0
		&& (options.spectrumSize == 0 || (options.spectrumSize > 1
			&& std::has_single_bit(static_cast<unsigned int>(options.spectrumSize))));
}

[[nodiscard]] juce::String modelName(vekt::rav::RavProcessingModel model)
{
	switch (model)
	{
		case vekt::rav::RavProcessingModel::legacy: return "legacy";
		case vekt::rav::RavProcessingModel::behavioralCandidate: return "behavioral";
		case vekt::rav::RavProcessingModel::overdriveCircuitCandidate: return "overdrive-circuit";
		case vekt::rav::RavProcessingModel::fuzzCircuitCandidate: return "fuzz-circuit";
	}
	return {};
}

[[nodiscard]] juce::String activeModelName(const Options& options)
{
	const auto compatibilityMode = options.stageEnabled[0]
		&& !options.stageEnabled[1] && !options.stageEnabled[2] && !options.stageEnabled[3];
	const auto fuzzIsActive = compatibilityMode ? options.mode == 3 : options.stageEnabled[3];
	if (options.model == vekt::rav::RavProcessingModel::fuzzCircuitCandidate && fuzzIsActive)
		return "fuzz-circuit";
	return "legacy";
}

[[nodiscard]] std::optional<InputFile> loadInputFile(const juce::String& path, juce::String& error)
{
	juce::AudioFormatManager formats;
	formats.registerBasicFormats();
	const auto file = juce::File(path);
	std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
	if (reader == nullptr)
	{
		error = "Unable to decode input audio";
		return std::nullopt;
	}
	if (reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples <= 0
		|| reader->sampleRate <= 0.0 || !std::isfinite(reader->sampleRate))
	{
		error = "Input audio must be a non-empty mono or stereo file with a valid sample rate";
		return std::nullopt;
	}
	if (reader->lengthInSamples > static_cast<juce::int64>(std::numeric_limits<int>::max()))
	{
		error = "Input audio exceeds the renderer's maximum in-memory length";
		return std::nullopt;
	}
	InputFile result { juce::AudioBuffer<float>(static_cast<int>(reader->numChannels),
		static_cast<int>(reader->lengthInSamples)), reader->sampleRate };
	if (!reader->read(&result.samples, 0, result.samples.getNumSamples(), 0, true, true))
	{
		error = "Unable to read input audio";
		return std::nullopt;
	}
	return result;
}

void setParameter(vekt::rav::PluginProcessor& processor, const char* identifier, float value)
{
	if (auto* parameter = processor.getParameters().getParameter(identifier))
		parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

[[nodiscard]] juce::String qualityName(vekt::dsp::OversamplingQuality quality)
{
	if (quality.factor == vekt::dsp::OversamplingFactor::off)
		return "Off";
	return juce::String(static_cast<int>(quality.multiplier())) + "x "
		+ (quality.filter == vekt::dsp::OversamplingFilter::polyphaseFIR ? "FIR" : "IIR");
}

[[nodiscard]] juce::String sourceName(vekt::audio_lab::Source source)
{
	switch (source)
	{
		case vekt::audio_lab::Source::sine: return "sine";
		case vekt::audio_lab::Source::sawtooth: return "sawtooth";
		case vekt::audio_lab::Source::sweep: return "sweep";
		case vekt::audio_lab::Source::impulse: return "impulse";
		case vekt::audio_lab::Source::noise: return "noise";
		case vekt::audio_lab::Source::kick: return "kick";
		case vekt::audio_lab::Source::unison: return "unison";
		case vekt::audio_lab::Source::twoTone: return "two-tone";
	}

	return {};
}

void setParameter(vekt::glimmer::PluginProcessor& processor, const char* identifier, float value)
{
	if (auto* parameter = processor.getParameters().getParameter(identifier))
		parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

bool applyGlimmerOverrides(vekt::glimmer::PluginProcessor& processor, const Options& options)
{
	std::vector<std::pair<juce::RangedAudioParameter*, float>> pending;
	for (const auto& assignment : options.glimmerOverrides)
	{
		const auto identifier = assignment.upToFirstOccurrenceOf("=", false, false);
		const auto text = assignment.fromFirstOccurrenceOf("=", false, false).trim();
		auto* parameter = processor.getParameters().getParameter(identifier);
		try
		{
			if (parameter == nullptr || text.isEmpty()) throw std::invalid_argument("unknown parameter or empty value");
			float value {};
			const auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter);
			const auto choiceIndex = choice == nullptr ? -1 : choice->choices.indexOf(text, true);
			if (choiceIndex >= 0) value = static_cast<float>(choiceIndex);
			else if (dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr && text.equalsIgnoreCase("true")) value = 1;
			else if (dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr && text.equalsIgnoreCase("false")) value = 0;
			else
			{
				std::size_t consumed {};
				const auto number = text.toStdString();
				value = std::stof(number, &consumed);
				if (consumed != number.size()) throw std::invalid_argument("trailing characters");
			}
			const auto& range = parameter->getNormalisableRange();
			if (!std::isfinite(value) || value < range.start || value > range.end
				|| ((choice != nullptr || dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr)
					&& std::abs(value - std::round(value)) > 1.0e-6f))
				throw std::invalid_argument("value outside parameter range");
			pending.emplace_back(parameter, value);
		}
		catch (const std::exception& error)
		{
			std::cerr << "Invalid Glimmer override " << assignment << ": " << error.what() << '\n';
			return false;
		}
	}
	for (const auto& [parameter, value] : pending)
		parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
	return true;
}

struct RenderTiming
{
	std::vector<double> milliseconds;
	double total {};
	double maximum {};
	int overBudget {};

	void add(double elapsed, double budget)
	{
		milliseconds.push_back(elapsed);
		total += elapsed;
		maximum = std::max(maximum, elapsed);
		if (elapsed > budget) ++overBudget;
	}

	juce::var report(double seconds)
	{
		std::sort(milliseconds.begin(), milliseconds.end());
		auto* result = new juce::DynamicObject;
		result->setProperty("dsp_percent", total / (seconds * 10.0));
		result->setProperty("max_callback_ms", maximum);
		result->setProperty("p99_callback_ms", milliseconds.empty() ? 0.0
			: milliseconds[std::min(milliseconds.size() - 1, static_cast<std::size_t>(0.99 * static_cast<double>(milliseconds.size())))]);
		result->setProperty("callbacks_over_budget", overBudget);
		return juce::var(result);
	}
};

juce::var glimmerSettingsReport(vekt::glimmer::PluginProcessor& processor)
{
	auto* report = new juce::DynamicObject;
	juce::var result(report);
	auto* settings = new juce::DynamicObject;
	report->setProperty("parameters", juce::var(settings));
	for (const auto* identifier : vekt::glimmer::parameters::soundParameterIds)
		settings->setProperty(identifier, processor.getParameters().getRawParameterValue(identifier)->load());
	for (const auto* identifier : { vekt::glimmer::parameters::trackingOversampling,
		vekt::glimmer::parameters::offlineOversampling, vekt::glimmer::parameters::bypass })
		settings->setProperty(identifier, processor.getParameters().getRawParameterValue(identifier)->load());
	const std::array<juce::String, 3> names { "Classic", "Drum", "Wide" };
	report->setProperty("active_model", names[static_cast<std::size_t>(processor.getActiveModel())]);
	report->setProperty("model_pending", processor.hasPendingModelChange());
	report->setProperty("latency_samples", processor.getLatencySamples());
	const auto speeds = processor.getRotorSpeeds();
	report->setProperty("horn_rpm", speeds[0]);
	report->setProperty("drum_rpm", speeds[1]);
	report->setProperty("auto_target_fast", processor.isAutoTargetFast());
	return result;
}

int renderGlimmer(const Options& options, const std::optional<InputFile>& inputFile)
{
	vekt::glimmer::PluginProcessor processor;
	setParameter(processor, vekt::glimmer::parameters::inputGain, options.inputGainDb);
	setParameter(processor, vekt::glimmer::parameters::preampDrive, options.drive);
	setParameter(processor, vekt::glimmer::parameters::hornTone, options.tone);
	setParameter(processor, vekt::glimmer::parameters::autoGain, options.autoGain ? 1.0f : 0.0f);
	setParameter(processor, vekt::glimmer::parameters::mix, options.mix);
	if (options.qualityIndex >= 0)
		setParameter(processor, options.offlineProfile ? vekt::glimmer::parameters::offlineOversampling
			: vekt::glimmer::parameters::trackingOversampling, static_cast<float>(options.qualityIndex));
	if (!applyGlimmerOverrides(processor, options)) return 64;
	processor.setNonRealtime(options.offlineProfile);
	processor.prepareToPlay(options.sampleRate, options.blockSize);

	const auto warmupSamples = static_cast<std::int64_t>(options.sampleRate * options.warmupSeconds);
	const auto totalSamples = static_cast<std::int64_t>(options.sampleRate * options.seconds);
	vekt::audio_lab::SignalSource source(options.source, options.sampleRate, options.seed);
	source.setFrequency(options.frequencyHz);
	juce::MidiBuffer midi;
	std::array<ChannelMeasurements, 2> measurements;
	RenderTiming timing;
	juce::WavAudioFormat wavFormat;
	std::unique_ptr<juce::AudioFormatWriter> wavWriter;
	if (options.wavPath.isNotEmpty())
	{
		std::unique_ptr<juce::OutputStream> stream = juce::File(options.wavPath).createOutputStream();
		wavWriter = wavFormat.createWriterFor(stream, juce::AudioFormatWriterOptions {}
			.withSampleRate(options.sampleRate).withNumChannels(2).withBitsPerSample(32));
		if (wavWriter == nullptr) return 1;
	}
	const auto renderBlock = [&](std::int64_t offset, int blockSize, bool measure) -> bool
	{
		juce::AudioBuffer<float> buffer(2, blockSize);
		for (int sample = 0; sample < blockSize; ++sample)
		{
			if (inputFile)
			{
				const auto sourceSample = static_cast<int>((offset + sample) % inputFile->samples.getNumSamples());
				buffer.setSample(0, sample, inputFile->samples.getSample(0, sourceSample));
				buffer.setSample(1, sample, inputFile->samples.getSample(inputFile->samples.getNumChannels() == 1 ? 0 : 1, sourceSample));
			}
			else
			{
				buffer.setSample(0, sample, source.next(offset + sample, 0));
				buffer.setSample(1, sample, source.next(offset + sample, 1));
			}
		}
		const auto start = juce::Time::getMillisecondCounterHiRes();
		processor.processBlock(buffer, midi);
		const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
		if (!measure) return true;
		timing.add(elapsed, static_cast<double>(blockSize) * 1000.0 / options.sampleRate);
		for (int sample = 0; sample < blockSize; ++sample)
		{
			measurements[0].add(buffer.getSample(0, sample));
			measurements[1].add(buffer.getSample(1, sample));
		}
		return wavWriter == nullptr || wavWriter->writeFromAudioSampleBuffer(buffer, 0, blockSize);
	};
	for (std::int64_t offset = 0; offset < warmupSamples; offset += options.blockSize)
		if (!renderBlock(offset, static_cast<int>(std::min<std::int64_t>(options.blockSize, warmupSamples - offset)), false)) return 1;
	for (std::int64_t offset = 0; offset < totalSamples; offset += options.blockSize)
		if (!renderBlock(warmupSamples + offset, static_cast<int>(std::min<std::int64_t>(options.blockSize, totalSamples - offset)), true)) return 1;
	std::cout << "product=glimmer samples=" << totalSamples
		<< " rms_left=" << measurements[0].rms(totalSamples)
		<< " rms_right=" << measurements[1].rms(totalSamples)
		<< " peak_left=" << measurements[0].peak
		<< " peak_right=" << measurements[1].peak
		<< " latency=" << processor.getLatencySamples() << '\n';
	if (options.reportPath.isNotEmpty())
	{
		auto report = glimmerSettingsReport(processor);
		auto* object = report.getDynamicObject();
		object->setProperty("product", "glimmer");
		object->setProperty("samples", totalSamples);
		object->setProperty("mix_percent", processor.getParameters().getRawParameterValue(vekt::glimmer::parameters::mix)->load());
		object->setProperty("timing", timing.report(options.seconds));
		object->setProperty("rms_left", measurements[0].rms(totalSamples));
		object->setProperty("rms_right", measurements[1].rms(totalSamples));
		object->setProperty("peak_left", measurements[0].peak);
		object->setProperty("peak_right", measurements[1].peak);
		if (!juce::File(options.reportPath).replaceWithText(juce::JSON::toString(report))) return 1;
	}
	return 0;
}

int renderRack(const Options& options, const std::optional<InputFile>& inputFile)
{
	vekt::rav::PluginProcessor rav;
	vekt::glimmer::PluginProcessor glimmer;
	if (!applyGlimmerOverrides(glimmer, options)) return 64;
	rav.setNonRealtime(options.offlineProfile);
	glimmer.setNonRealtime(options.offlineProfile);
	rav.prepareToPlay(options.sampleRate, options.blockSize);
	glimmer.prepareToPlay(options.sampleRate, options.blockSize);
	const auto glimmerFirst = options.rack == "glimmer,rav";
	const auto warmupSamples = static_cast<std::int64_t>(options.sampleRate * options.warmupSeconds);
	const auto totalSamples = static_cast<std::int64_t>(options.sampleRate * options.seconds);
	vekt::audio_lab::SignalSource source(options.source, options.sampleRate, options.seed);
	source.setFrequency(options.frequencyHz);
	juce::MidiBuffer midi;
	std::array<ChannelMeasurements, 2> measurements;
	RenderTiming timing;
	juce::WavAudioFormat wavFormat;
	std::unique_ptr<juce::AudioFormatWriter> wavWriter;
	if (options.wavPath.isNotEmpty())
	{
		std::unique_ptr<juce::OutputStream> stream = juce::File(options.wavPath).createOutputStream();
		wavWriter = wavFormat.createWriterFor(stream, juce::AudioFormatWriterOptions {}
			.withSampleRate(options.sampleRate).withNumChannels(2).withBitsPerSample(32));
		if (wavWriter == nullptr) return 1;
	}
	const auto renderBlock = [&](std::int64_t offset, int blockSize, bool measure) -> bool
	{
		juce::AudioBuffer<float> buffer(2, blockSize);
		for (int sample = 0; sample < blockSize; ++sample)
		{
			if (inputFile)
			{
				const auto sourceSample = static_cast<int>((offset + sample) % inputFile->samples.getNumSamples());
				buffer.setSample(0, sample, inputFile->samples.getSample(0, sourceSample));
				buffer.setSample(1, sample, inputFile->samples.getSample(inputFile->samples.getNumChannels() == 1 ? 0 : 1, sourceSample));
			}
			else
			{
				buffer.setSample(0, sample, source.next(offset + sample, 0));
				buffer.setSample(1, sample, source.next(offset + sample, 1));
			}
		}
		const auto start = juce::Time::getMillisecondCounterHiRes();
		if (glimmerFirst) { glimmer.processBlock(buffer, midi); rav.processBlock(buffer, midi); }
		else { rav.processBlock(buffer, midi); glimmer.processBlock(buffer, midi); }
		const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
		if (!measure) return true;
		timing.add(elapsed, static_cast<double>(blockSize) * 1000.0 / options.sampleRate);
		for (int sample = 0; sample < blockSize; ++sample)
		{
			measurements[0].add(buffer.getSample(0, sample));
			measurements[1].add(buffer.getSample(1, sample));
		}
		return wavWriter == nullptr || wavWriter->writeFromAudioSampleBuffer(buffer, 0, blockSize);
	};
	for (std::int64_t offset = 0; offset < warmupSamples; offset += options.blockSize)
		if (!renderBlock(offset, static_cast<int>(std::min<std::int64_t>(options.blockSize, warmupSamples - offset)), false)) return 1;
	for (std::int64_t offset = 0; offset < totalSamples; offset += options.blockSize)
		if (!renderBlock(warmupSamples + offset, static_cast<int>(std::min<std::int64_t>(options.blockSize, totalSamples - offset)), true)) return 1;
	std::cout << "rack=" << options.rack << " samples=" << totalSamples
		<< " rms_left=" << measurements[0].rms(totalSamples)
		<< " rms_right=" << measurements[1].rms(totalSamples)
		<< " latency=" << rav.getLatencySamples() + glimmer.getLatencySamples() << '\n';
	if (options.reportPath.isNotEmpty())
	{
		auto* object = new juce::DynamicObject;
		juce::var report(object);
		object->setProperty("rack", options.rack);
		object->setProperty("samples", totalSamples);
		object->setProperty("latency_samples", rav.getLatencySamples() + glimmer.getLatencySamples());
		object->setProperty("glimmer", glimmerSettingsReport(glimmer));
		object->setProperty("timing", timing.report(options.seconds));
		if (!juce::File(options.reportPath).replaceWithText(juce::JSON::toString(report))) return 1;
	}
	return 0;
}
}

int main(int argc, char** argv)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	Options options;
	if (!parseOptions(argc, argv, options))
	{
		std::cerr << "Usage: VektRavRender [--product rav|glimmer] [--rack rav,glimmer|glimmer,rav] [--source sine|sawtooth|sweep|impulse|noise|kick|unison|two-tone] "
					 "[--input path] [--model legacy|behavioral|overdrive-circuit|fuzz-circuit] "
					 "[--profile tracking|offline] [--quality 0-6] [--sample-rate Hz] [--block-size samples] "
					 "[--seconds duration] [--warmup duration] [--frequency Hz] [--mode 0-3] "
					 "[--drive dB] [--bias value] [--shape value] [--dynamics value] [--texture value] "
					 "[--tone dB] [--mix percent] [--low-mix percent] [--mid-mix percent] "
					 "[--high-mix percent] [--input-gain dB] [--stages 0000-1111] [--stage-order 0,1,2,3] "
					 "[--auto-gain] [--seed value] [--report path] [--wav path] [--param glimmer.id=value]\n";
		return 64;
	}
	std::optional<InputFile> inputFile;
	if (options.inputPath.isNotEmpty())
	{
		juce::String inputError;
		inputFile = loadInputFile(options.inputPath, inputError);
		if (!inputFile)
		{
			std::cerr << inputError << '\n';
			return 1;
		}
		if (std::abs(inputFile->sampleRate - options.sampleRate) > 1.0e-6)
		{
			std::cerr << "Input audio sample rate must match --sample-rate; resampling is intentionally excluded from comparison renders\n";
			return 1;
		}
	}
	if (options.product == "glimmer")
		return renderGlimmer(options, inputFile);
	if (options.rack.isNotEmpty())
		return renderRack(options, inputFile);

	vekt::rav::PluginProcessor processor;
	processor.setDevelopmentProcessingModel(options.model);
	for (std::size_t index = 0; index < options.stageEnabled.size(); ++index)
		setParameter(processor, vekt::rav::parameters::stageEnabledIds[index],
			options.stageEnabled[index] ? 1.0f : 0.0f);
	for (std::size_t index = 0; index < options.stageOrder.size(); ++index)
	{
		const auto currentOrder = processor.getStageOrder();
		const auto desired = options.stageOrder[index];
		const auto current = std::find(currentOrder.begin(), currentOrder.end(), desired);
		if (current == currentOrder.end())
			return 1;
		const auto currentIndex = static_cast<std::size_t>(std::distance(currentOrder.begin(), current));
		if (!processor.reorderStage(currentIndex, static_cast<int>(index) - static_cast<int>(currentIndex)))
			return 1;
	}
	setParameter(processor, vekt::rav::parameters::inputGain, options.inputGainDb);
	setParameter(processor, vekt::rav::parameters::mode, static_cast<float>(options.mode));
	setParameter(processor, vekt::rav::parameters::drive, options.drive);
	setParameter(processor, vekt::rav::parameters::bias, options.bias);
	setParameter(processor, vekt::rav::parameters::shape, options.shape);
	setParameter(processor, vekt::rav::parameters::dynamics, options.dynamics);
	setParameter(processor, vekt::rav::parameters::texture, options.texture);
	setParameter(processor, vekt::rav::parameters::tone, options.tone);
	setParameter(processor, vekt::rav::parameters::mix, options.mix);
	setParameter(processor, vekt::rav::parameters::lowBandMix, options.lowBandMix);
	setParameter(processor, vekt::rav::parameters::midBandMix, options.midBandMix);
	setParameter(processor, vekt::rav::parameters::highBandMix, options.highBandMix);
	setParameter(processor, vekt::rav::parameters::autoGain, options.autoGain ? 1.0f : 0.0f);
	if (options.qualityIndex >= 0)
		setParameter(processor, options.offlineProfile ? vekt::rav::parameters::offlineOversampling
			: vekt::rav::parameters::trackingOversampling, static_cast<float>(options.qualityIndex));
	processor.setNonRealtime(options.offlineProfile);
	processor.prepareToPlay(options.sampleRate, options.blockSize);

	const auto warmupSamples = static_cast<std::int64_t>(options.sampleRate * options.warmupSeconds);
	const auto totalSamples = static_cast<std::int64_t>(options.sampleRate * options.seconds);
	vekt::audio_lab::SignalSource source(options.source, options.sampleRate, options.seed);
	source.setFrequency(options.frequencyHz);
	juce::MidiBuffer midi;
	std::array<ChannelMeasurements, 2> measurements;
	double processTicks = 0.0;
	double maximumBlockTicks {};
	std::int64_t measuredBlockCount {};
	std::vector<float> spectrumSamples;
	spectrumSamples.reserve(static_cast<std::size_t>(options.spectrumSize));
	juce::WavAudioFormat wavFormat;
	std::unique_ptr<juce::AudioFormatWriter> wavWriter;
	if (options.wavPath.isNotEmpty())
	{
		std::unique_ptr<juce::OutputStream> stream = juce::File(options.wavPath).createOutputStream();
		wavWriter = wavFormat.createWriterFor(stream, juce::AudioFormatWriterOptions {}
			.withSampleRate(options.sampleRate)
			.withNumChannels(2)
			.withBitsPerSample(32));
		if (wavWriter == nullptr)
		{
			std::cerr << "Unable to create WAV output\n";
			return 1;
		}
	}
	const auto renderBlock = [&](std::int64_t offset, int blockSize, bool measure) -> bool
	{
		juce::AudioBuffer<float> buffer(2, blockSize);
		for (int sample = 0; sample < blockSize; ++sample)
		{
			if (inputFile)
			{
				const auto sourceSample = static_cast<int>((offset + sample) % inputFile->samples.getNumSamples());
				buffer.setSample(0, sample, inputFile->samples.getSample(0, sourceSample));
				buffer.setSample(1, sample, inputFile->samples.getSample(
					inputFile->samples.getNumChannels() == 1 ? 0 : 1, sourceSample));
			}
			else
			{
				buffer.setSample(0, sample, source.next(offset + sample, 0));
				buffer.setSample(1, sample, source.next(offset + sample, 1));
			}
		}
		const auto startTicks = juce::Time::getHighResolutionTicks();
		processor.processBlock(buffer, midi);
		if (!measure)
			return true;
		const auto blockTicks = static_cast<double>(juce::Time::getHighResolutionTicks() - startTicks);
		processTicks += blockTicks;
		maximumBlockTicks = std::max(maximumBlockTicks, blockTicks);
		++measuredBlockCount;
		for (int sample = 0; sample < blockSize; ++sample)
		{
			measurements[0].add(buffer.getSample(0, sample));
			measurements[1].add(buffer.getSample(1, sample));
			if (spectrumSamples.size() < static_cast<std::size_t>(options.spectrumSize))
				spectrumSamples.push_back(buffer.getSample(0, sample));
		}
		if (wavWriter != nullptr && !wavWriter->writeFromAudioSampleBuffer(buffer, 0, blockSize))
		{
			std::cerr << "Unable to write WAV output\n";
			return false;
		}
		return true;
	};
	for (std::int64_t offset = 0; offset < warmupSamples; offset += options.blockSize)
		if (!renderBlock(offset, static_cast<int>(std::min<std::int64_t>(options.blockSize,
				warmupSamples - offset)), false))
			return 1;
	for (std::int64_t offset = 0; offset < totalSamples; offset += options.blockSize)
		if (!renderBlock(warmupSamples + offset, static_cast<int>(std::min<std::int64_t>(options.blockSize,
				totalSamples - offset)), true))
			return 1;

	const auto processingSeconds = processTicks
		/ static_cast<double>(juce::Time::getHighResolutionTicksPerSecond());
	const auto cpuPercent = 100.0 * processingSeconds / options.seconds;
	const auto nanosecondsPerSample = 1.0e9 * processingSeconds / static_cast<double>(totalSamples);
	const auto maximumBlockMicroseconds = 1.0e6 * maximumBlockTicks
		/ static_cast<double>(juce::Time::getHighResolutionTicksPerSecond());
	const auto meanBlockMicroseconds = 1.0e6 * processTicks / static_cast<double>(measuredBlockCount)
		/ static_cast<double>(juce::Time::getHighResolutionTicksPerSecond());
	std::optional<double> spectrumPeakDbfs;
	if (options.spectrumSize > 0
		&& spectrumSamples.size() == static_cast<std::size_t>(options.spectrumSize))
	{
		const auto size = spectrumSamples.size();
			double windowSum = 0.0;
		std::vector<float> fftData(size * 2);
		for (std::size_t sample = 0; sample < size; ++sample)
		{
			const auto window = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi
				* static_cast<double>(sample) / static_cast<double>(size - 1));
				windowSum += window;
			fftData[sample] = static_cast<float>(static_cast<double>(spectrumSamples[sample]) * window);
		}
		juce::dsp::FFT(std::countr_zero(static_cast<unsigned int>(size)))
			.performFrequencyOnlyForwardTransform(fftData.data(), true);
		const auto peakMagnitude = *std::max_element(fftData.cbegin() + 1,
			fftData.cbegin() + static_cast<std::ptrdiff_t>(size / 2));
		spectrumPeakDbfs = juce::Decibels::gainToDecibels(
			2.0 * static_cast<double>(peakMagnitude) / windowSum, -200.0);
	}
	std::cout << "samples=" << totalSamples << " rms_left=" << measurements[0].rms(totalSamples)
		<< " rms_right=" << measurements[1].rms(totalSamples)
		<< " peak_left=" << measurements[0].peak << " peak_right=" << measurements[1].peak
		<< " dc_left=" << measurements[0].dc(totalSamples) << " dc_right=" << measurements[1].dc(totalSamples)
		<< " latency=" << processor.getLatencySamples()
		<< " quality=" << qualityName(processor.getActiveQuality())
		<< " cpu_percent=" << cpuPercent << " ns_per_sample=" << nanosecondsPerSample
		<< " mean_block_us=" << meanBlockMicroseconds << " max_block_us=" << maximumBlockMicroseconds;
	if (spectrumPeakDbfs)
		std::cout << " spectrum_peak_dbfs=" << *spectrumPeakDbfs;
	std::cout << '\n';
	if (options.reportPath.isNotEmpty())
	{
		const auto report = "{\n"
			"  \"samples\": " + juce::String(totalSamples) + ",\n"
			"  \"sample_rate\": " + juce::String(options.sampleRate) + ",\n"
			"  \"block_size\": " + juce::String(options.blockSize) + ",\n"
			"  \"warmup_seconds\": " + juce::String(options.warmupSeconds) + ",\n"
			"  \"source\": " + juce::JSON::toString(options.inputPath.isNotEmpty() ? "file" : sourceName(options.source)) + ",\n"
			"  \"input_path\": " + (options.inputPath.isNotEmpty()
				? juce::JSON::toString(options.inputPath) : juce::String("null")) + ",\n"
			"  \"frequency_hz\": " + juce::String(options.frequencyHz) + ",\n"
			"  \"seed\": " + juce::String(static_cast<juce::int64>(options.seed)) + ",\n"
			"  \"mode\": " + juce::String(options.mode) + ",\n"
			"  \"requested_model\": " + juce::JSON::toString(modelName(options.model)) + ",\n"
			"  \"active_model\": " + juce::JSON::toString(activeModelName(options)) + ",\n"
			"  \"stage_enabled\": " + juce::JSON::toString(
				juce::String(options.stageEnabled[0] ? "1" : "0")
					+ (options.stageEnabled[1] ? "1" : "0")
					+ (options.stageEnabled[2] ? "1" : "0")
					+ (options.stageEnabled[3] ? "1" : "0")) + ",\n"
			"  \"stage_order\": " + juce::JSON::toString(vekt::rav::RavStageChain::serialise(options.stageOrder)) + ",\n"
			"  \"profile\": " + juce::JSON::toString(options.offlineProfile ? "offline" : "tracking") + ",\n"
			"  \"requested_quality_index\": " + juce::String(options.qualityIndex) + ",\n"
			"  \"quality\": " + juce::JSON::toString(qualityName(processor.getActiveQuality())) + ",\n"
			"  \"parameters\": {\n"
			"    \"input_gain_db\": " + juce::String(options.inputGainDb) + ",\n"
			"    \"drive_db\": " + juce::String(options.drive) + ",\n"
			"    \"bias\": " + juce::String(options.bias) + ",\n"
			"    \"shape\": " + juce::String(options.shape) + ",\n"
			"    \"dynamics\": " + juce::String(options.dynamics) + ",\n"
			"    \"texture\": " + juce::String(options.texture) + ",\n"
			"    \"tone_db\": " + juce::String(options.tone) + ",\n"
			"    \"mix_percent\": " + juce::String(options.mix) + ",\n"
			"    \"low_mix_percent\": " + juce::String(options.lowBandMix) + ",\n"
			"    \"mid_mix_percent\": " + juce::String(options.midBandMix) + ",\n"
			"    \"high_mix_percent\": " + juce::String(options.highBandMix) + ",\n"
			"    \"auto_gain\": " + juce::String(options.autoGain ? "true" : "false") + "\n"
			"  },\n"
			"  \"latency_samples\": " + juce::String(processor.getLatencySamples()) + ",\n"
			"  \"channels\": [\n"
			"    {\"rms\": " + juce::String(measurements[0].rms(totalSamples), 12)
				+ ", \"peak\": " + juce::String(measurements[0].peak, 12)
				+ ", \"dc\": " + juce::String(measurements[0].dc(totalSamples), 12) + "},\n"
			"    {\"rms\": " + juce::String(measurements[1].rms(totalSamples), 12)
				+ ", \"peak\": " + juce::String(measurements[1].peak, 12)
				+ ", \"dc\": " + juce::String(measurements[1].dc(totalSamples), 12) + "}\n"
			"  ],\n"
			"  \"cpu_percent\": " + juce::String(cpuPercent, 12) + ",\n"
			"  \"nanoseconds_per_sample\": " + juce::String(nanosecondsPerSample, 12) + ",\n"
			"  \"mean_block_microseconds\": " + juce::String(meanBlockMicroseconds, 12) + ",\n"
			"  \"maximum_block_microseconds\": " + juce::String(maximumBlockMicroseconds, 12) + ",\n"
			"  \"spectrum_peak_dbfs\": " + (spectrumPeakDbfs
				? juce::String(*spectrumPeakDbfs, 12) : juce::String("null")) + "\n"
			"}\n";
		if (!juce::File(options.reportPath).replaceWithText(report))
		{
			std::cerr << "Unable to write report output\n";
			return 1;
		}
	}
	return 0;
}
