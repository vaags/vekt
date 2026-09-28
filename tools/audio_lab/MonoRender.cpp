#include "MonoRender.h"

#include <vekt/audio_analysis/Measurements.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vekt::audio_lab
{
namespace
{
[[nodiscard]] juce::var statisticsReport(const audio_analysis::SampleStatistics& statistics)
{
	auto* object = new juce::DynamicObject;
	object->setProperty("rms", statistics.rms);
	object->setProperty("peak", statistics.peak);
	object->setProperty("dc", statistics.dc);
	object->setProperty("difference_rms", statistics.differenceRms);
	return juce::var(object);
}

[[nodiscard]] juce::String eventTypeName(MonoEventType type)
{
	switch (type)
	{
	case MonoEventType::noteOn: return "note_on";
	case MonoEventType::noteOff: return "note_off";
	case MonoEventType::parameter: return "parameter";
	}
	return {};
}

[[nodiscard]] juce::String parameterName(MonoParameter parameter)
{
	switch (parameter)
	{
	case MonoParameter::cutoff: return "cutoff";
	case MonoParameter::resonance: return "resonance";
	case MonoParameter::envelopeAmount: return "envelope_amount";
	case MonoParameter::drive: return "drive";
	case MonoParameter::ampAttack: return "amp_attack";
	case MonoParameter::ampDecay: return "amp_decay";
	case MonoParameter::ampSustain: return "amp_sustain";
	case MonoParameter::ampRelease: return "amp_release";
	case MonoParameter::filterAttack: return "filter_attack";
	case MonoParameter::filterDecay: return "filter_decay";
	case MonoParameter::filterSustain: return "filter_sustain";
	case MonoParameter::filterRelease: return "filter_release";
	case MonoParameter::qCompensation: return "q_compensation";
	case MonoParameter::noiseLevel: return "noise_level";
	}
	return {};
}

void applyParameter(mono::MonoVoiceSettings& settings, MonoParameter parameter, float value) noexcept
{
	switch (parameter)
	{
	case MonoParameter::cutoff: settings.cutoff = value; break;
	case MonoParameter::resonance: settings.resonance = value; break;
	case MonoParameter::envelopeAmount: settings.envelopeAmount = value; break;
	case MonoParameter::drive: settings.drive = value; break;
	case MonoParameter::ampAttack: settings.ampAttack = value; break;
	case MonoParameter::ampDecay: settings.ampDecay = value; break;
	case MonoParameter::ampSustain: settings.ampSustain = value; break;
	case MonoParameter::ampRelease: settings.ampRelease = value; break;
	case MonoParameter::filterAttack: settings.filterAttack = value; break;
	case MonoParameter::filterDecay: settings.filterDecay = value; break;
	case MonoParameter::filterSustain: settings.filterSustain = value; break;
	case MonoParameter::filterRelease: settings.filterRelease = value; break;
	case MonoParameter::qCompensation: settings.qCompensation = value >= 0.5f; break;
	case MonoParameter::noiseLevel: settings.noiseLevel = value; break;
	}
}

[[nodiscard]] std::int64_t at(double seconds, double sampleRate) noexcept
{
	return static_cast<std::int64_t>(std::llround(seconds * sampleRate));
}

[[nodiscard]] juce::var eventReport(const MonoRenderEvent& event)
{
	auto* object = new juce::DynamicObject;
	object->setProperty("sample", event.sample);
	object->setProperty("type", eventTypeName(event.type));
	if (event.type == MonoEventType::parameter)
	{
		object->setProperty("parameter", parameterName(event.parameter));
		object->setProperty("value", event.value);
	}
	else
	{
		object->setProperty("note", event.note);
		if (event.type == MonoEventType::noteOn) object->setProperty("velocity", event.value);
	}
	return juce::var(object);
}

[[nodiscard]] juce::var settingsReport(const mono::MonoVoiceSettings& settings)
{
	auto* object = new juce::DynamicObject;
	object->setProperty("cutoff_hz", settings.cutoff);
	object->setProperty("resonance", settings.resonance);
	object->setProperty("envelope_amount", settings.envelopeAmount);
	object->setProperty("drive_db", settings.drive);
	object->setProperty("amp_attack_seconds", settings.ampAttack);
	object->setProperty("amp_decay_seconds", settings.ampDecay);
	object->setProperty("amp_sustain", settings.ampSustain);
	object->setProperty("amp_release_seconds", settings.ampRelease);
	object->setProperty("filter_attack_seconds", settings.filterAttack);
	object->setProperty("filter_decay_seconds", settings.filterDecay);
	object->setProperty("filter_sustain", settings.filterSustain);
	object->setProperty("filter_release_seconds", settings.filterRelease);
	object->setProperty("q_compensation", settings.qCompensation);
	object->setProperty("noise_type", settings.noiseType);
	object->setProperty("noise_level", settings.noiseLevel);
	object->setProperty("drift", settings.drift);
	return juce::var(object);
}

[[nodiscard]] juce::var envelopeReport(const juce::AudioBuffer<float>& audio,
	const std::vector<MonoRenderEvent>& events, double sampleRate)
{
	std::int64_t noteOn = -1;
	std::int64_t noteOff = -1;
	for (const auto& event : events)
	{
		if (event.type == MonoEventType::noteOn && noteOn < 0) noteOn = event.sample;
		if (event.type == MonoEventType::noteOff && noteOff < 0) noteOff = event.sample;
	}
	auto* object = new juce::DynamicObject;
	if (noteOn < 0 || noteOff <= noteOn || audio.getNumSamples() == 0)
		return juce::var(object);

	const auto smoothingSamples = std::max(1, static_cast<int>(std::lround(sampleRate * 0.005)));
	std::vector<float> envelope(static_cast<std::size_t>(audio.getNumSamples()));
	float smoothed {};
	const auto coefficient = 1.0f / static_cast<float>(smoothingSamples);
	for (int sample = 0; sample < audio.getNumSamples(); ++sample)
	{
		const auto magnitude = std::max(std::abs(audio.getSample(0, sample)), std::abs(audio.getSample(1, sample)));
		smoothed += coefficient * (magnitude - smoothed);
		envelope[static_cast<std::size_t>(sample)] = smoothed;
	}
	const auto boundedOn = juce::jlimit<std::int64_t>(0, audio.getNumSamples() - 1, noteOn);
	const auto boundedOff = juce::jlimit<std::int64_t>(boundedOn + 1, audio.getNumSamples(), noteOff);
	const auto peak = *std::max_element(envelope.begin() + boundedOn, envelope.begin() + boundedOff);
	const auto findAfter = [&](std::int64_t start, float threshold, bool rising)
	{
		for (auto sample = start; sample < audio.getNumSamples(); ++sample)
			if ((rising && envelope[static_cast<std::size_t>(sample)] >= threshold)
				|| (!rising && envelope[static_cast<std::size_t>(sample)] <= threshold))
				return sample;
		return static_cast<std::int64_t>(-1);
	};
	const auto attack10 = findAfter(boundedOn, peak * 0.1f, true);
	const auto attack90 = findAfter(boundedOn, peak * 0.9f, true);
	const auto releaseStartLevel = envelope[static_cast<std::size_t>(std::min<std::int64_t>(audio.getNumSamples() - 1, boundedOff))];
	const auto release10 = findAfter(boundedOff, releaseStartLevel * 0.1f, false);
	object->setProperty("peak", peak);
	object->setProperty("attack_10_sample", attack10);
	object->setProperty("attack_90_sample", attack90);
	object->setProperty("attack_10_to_90_seconds", attack10 >= 0 && attack90 >= attack10
		? static_cast<double>(attack90 - attack10) / sampleRate : -1.0);
	object->setProperty("release_start_level", releaseStartLevel);
	object->setProperty("release_10_sample", release10);
	object->setProperty("release_to_10_seconds", release10 >= boundedOff
		? static_cast<double>(release10 - boundedOff) / sampleRate : -1.0);
	return juce::var(object);
}
}

mono::MonoVoiceSettings defaultMonoRenderSettings() noexcept
{
	mono::MonoVoiceSettings settings;
	settings.range = { 1.0f, 1.0f, 1.0f };
	settings.semitone = {};
	settings.fine = {};
	settings.octave = {};
	settings.level = { 0.7f, 0.0f, 0.0f };
	settings.morph = { 2.0f, 2.0f, 2.0f };
	settings.pulseWidth = { 50.0f, 50.0f, 50.0f };
	settings.cutoff = 1'000.0f;
	settings.resonance = 0.1f;
	settings.tracking = 0.0f;
	settings.envelopeAmount = 0.0f;
	settings.ampAttack = 0.005f;
	settings.ampDecay = 0.25f;
	settings.ampSustain = 0.75f;
	settings.ampRelease = 0.3f;
	settings.filterAttack = 0.005f;
	settings.filterDecay = 0.5f;
	settings.filterSustain = 0.25f;
	settings.filterRelease = 0.4f;
	settings.unison = 1;
	return settings;
}

bool makeMonoRenderFixture(const juce::String& name, double sampleRate, int blockSize,
	std::uint32_t seed, MonoRenderRequest& destination)
{
	if (sampleRate <= 0.0 || blockSize <= 0) return false;
	destination = {};
	destination.fixture = name;
	destination.sampleRate = sampleRate;
	destination.blockSize = blockSize;
	destination.seed = seed;
	destination.settings = defaultMonoRenderSettings();
	if (name.startsWith("q-comp-drive-") && (name.endsWith("-off") || name.endsWith("-on")))
	{
		const auto driveText = name.fromFirstOccurrenceOf("q-comp-drive-", false, false)
			.upToFirstOccurrenceOf("-", false, false);
		if (driveText != "0" && driveText != "6" && driveText != "12"
			&& driveText != "18" && driveText != "24") return false;
		destination.totalSamples = at(1.0, sampleRate);
		destination.settings.level = { 0.7f, 0.0f, 0.0f };
		destination.settings.morph[0] = 2.0f; // Harmonics expose Drive/Q interactions in listening.
		destination.settings.cutoff = 500.0f;
		destination.settings.resonance = 0.8f;
		destination.settings.drive = static_cast<float>(driveText.getIntValue());
		destination.settings.ampAttack = 0.0005f;
		destination.settings.ampSustain = 1.0f;
		destination.settings.qCompensation = name.endsWith("-on");
		destination.events = { { 0, MonoEventType::noteOn, MonoParameter::cutoff, 1.0f, 48 } };
		destination.windows = { { "settled", at(0.5, sampleRate), at(0.9, sampleRate) } };
		return true;
	}
	if (name == "q-comp-body-off" || name == "q-comp-body-on"
		|| name == "q-comp-tone-off" || name == "q-comp-tone-on")
	{
		const auto tone = name.contains("tone");
		destination.totalSamples = at(1.0, sampleRate);
		destination.settings.level = { tone ? 0.0f : 0.7f, 0.0f, 0.0f };
		destination.settings.morph[0] = 0.0f;
		destination.settings.cutoff = 1'000.0f;
		destination.settings.resonance = tone ? 1.0f : 0.8f;
		destination.settings.drive = 0.0f;
		destination.settings.ampAttack = 0.0005f;
		destination.settings.ampSustain = 1.0f;
		destination.settings.noiseType = tone ? 1 : 0;
		destination.settings.noiseLevel = tone ? 0.05f : 0.0f;
		destination.settings.qCompensation = name.endsWith("on");
		destination.events = { { 0, MonoEventType::noteOn, MonoParameter::cutoff, 1.0f, 48 } };
		// Silence the excitation, not the sustained envelope or feedback state.
		if (tone)
		{
			destination.settings.qCompensation = false;
			destination.events.push_back({ at(0.1, sampleRate), MonoEventType::parameter,
				MonoParameter::noiseLevel, 0.0f });
			if (name.endsWith("on"))
				destination.events.push_back({ at(0.1, sampleRate), MonoEventType::parameter,
					MonoParameter::qCompensation, 1.0f });
		}
		destination.windows = { { "settled", at(0.5, sampleRate), at(0.9, sampleRate) } };
		return true;
	}
	if (name == "filter-sweep")
	{
		destination.totalSamples = at(2.0, sampleRate);
		destination.settings.cutoff = 250.0f;
		destination.settings.resonance = 0.35f;
		destination.settings.drive = 6.0f;
		destination.settings.ampSustain = 1.0f;
		destination.settings.ampRelease = 0.15f;
		destination.settings.noiseType = 1;
		destination.settings.noiseLevel = 0.025f;
		destination.settings.drift = 15.0f;
		destination.events = {
			{ 0, MonoEventType::noteOn, MonoParameter::cutoff, 0.8f, 48 },
			{ at(0.5, sampleRate), MonoEventType::parameter, MonoParameter::cutoff, 2'000.0f },
			{ at(1.0, sampleRate), MonoEventType::parameter, MonoParameter::cutoff, 8'000.0f },
			{ at(1.25, sampleRate), MonoEventType::parameter, MonoParameter::resonance, 0.8f },
			{ at(1.6, sampleRate), MonoEventType::noteOff, MonoParameter::cutoff, 0.0f, 48 }
		};
		destination.windows = {
			{ "closed", at(0.25, sampleRate), at(0.45, sampleRate) },
			{ "middle", at(0.75, sampleRate), at(0.95, sampleRate) },
			{ "open", at(1.05, sampleRate), at(1.2, sampleRate) },
			{ "resonant", at(1.4, sampleRate), at(1.55, sampleRate) }
		};
		return true;
	}
	if (name == "envelope")
	{
		destination.totalSamples = at(1.5, sampleRate);
		destination.settings.morph[0] = 0.0f;
		destination.settings.cutoff = 18'000.0f;
		destination.settings.ampAttack = 0.08f;
		destination.settings.ampDecay = 0.12f;
		destination.settings.ampSustain = 0.65f;
		destination.settings.ampRelease = 0.35f;
		destination.settings.filterAttack = 0.02f;
		destination.settings.filterDecay = 0.2f;
		destination.settings.filterSustain = 0.4f;
		destination.events = {
			{ at(0.05, sampleRate), MonoEventType::parameter, MonoParameter::ampRelease, 0.2f },
			{ at(0.1, sampleRate), MonoEventType::noteOn, MonoParameter::cutoff, 0.9f, 57 },
			{ at(0.9, sampleRate), MonoEventType::noteOff, MonoParameter::cutoff, 0.0f, 57 }
		};
		destination.windows = {
			{ "silence", 0, at(0.09, sampleRate) },
			{ "attack", at(0.1, sampleRate), at(0.22, sampleRate) },
			{ "sustain", at(0.65, sampleRate), at(0.85, sampleRate) },
			{ "release", at(0.9, sampleRate), at(1.3, sampleRate) }
		};
		return true;
	}
	return false;
}

MonoRenderResult renderMono(const MonoRenderRequest& request)
{
	MonoRenderResult result;
	if (request.sampleRate <= 0.0 || request.blockSize <= 0 || request.totalSamples <= 0
		|| request.totalSamples > std::numeric_limits<int>::max())
		return result;
	result.audio.setSize(2, static_cast<int>(request.totalSamples));
	result.audio.clear();
	auto events = request.events;
	std::stable_sort(events.begin(), events.end(), [](const auto& left, const auto& right) { return left.sample < right.sample; });
	mono::MonoVoiceSettings settings = request.settings;
	mono::MonoVoice voice;
	voice.prepare(request.sampleRate, request.seed);
	voice.setPanPosition(0.0f);
	std::size_t eventIndex {};
	std::uint64_t age {};
	for (std::int64_t blockStart = 0; blockStart < request.totalSamples; blockStart += request.blockSize)
	{
		const auto blockEnd = std::min(request.totalSamples, blockStart + request.blockSize);
		for (auto sample = blockStart; sample < blockEnd; ++sample)
		{
			while (eventIndex < events.size() && events[eventIndex].sample == sample)
			{
				const auto& event = events[eventIndex++];
				if (event.type == MonoEventType::parameter) applyParameter(settings, event.parameter, event.value);
				else if (event.type == MonoEventType::noteOn)
				{
					const auto midiVelocity = juce::MidiMessage::noteOn(1, event.note, event.value).getFloatVelocity();
					voice.start(1, event.note, midiVelocity, settings, true, false, ++age);
				}
				else voice.release(false);
			}
			float left {}, right {};
			voice.render(left, right, settings, 0.0f);
			result.audio.setSample(0, static_cast<int>(sample), left);
			result.audio.setSample(1, static_cast<int>(sample), right);
		}
	}

	auto* report = new juce::DynamicObject;
	result.report = juce::var(report);
	report->setProperty("product", "mono");
	report->setProperty("engine", "coupled");
	report->setProperty("fixture", request.fixture);
	report->setProperty("sample_rate", request.sampleRate);
	report->setProperty("block_size", request.blockSize);
	report->setProperty("samples", request.totalSamples);
	report->setProperty("seed", static_cast<juce::int64>(request.seed));
	report->setProperty("initial_settings", settingsReport(request.settings));
	juce::Array<juce::var> eventArray;
	for (const auto& event : events) eventArray.add(eventReport(event));
	report->setProperty("events", eventArray);
	juce::Array<juce::var> channelArray;
	for (int channel = 0; channel < 2; ++channel)
	{
		const std::span samples(result.audio.getReadPointer(channel),
			static_cast<std::size_t>(result.audio.getNumSamples()));
		channelArray.add(statisticsReport(audio_analysis::measureSamples<float>(samples)));
	}
	report->setProperty("channels", channelArray);
	juce::Array<juce::var> windowArray;
	for (const auto& window : request.windows)
	{
		const auto start = juce::jlimit<std::int64_t>(0, request.totalSamples, window.startSample);
		const auto end = juce::jlimit<std::int64_t>(start, request.totalSamples, window.endSample);
		auto* object = new juce::DynamicObject;
		object->setProperty("name", window.name);
		object->setProperty("start_sample", start);
		object->setProperty("end_sample", end);
		juce::Array<juce::var> channels;
		for (int channel = 0; channel < 2; ++channel)
		{
			const std::span samples(result.audio.getReadPointer(channel) + start,
				static_cast<std::size_t>(end - start));
			channels.add(statisticsReport(audio_analysis::measureSamples<float>(samples)));
		}
		object->setProperty("channels", channels);
		windowArray.add(juce::var(object));
	}
	report->setProperty("windows", windowArray);
	report->setProperty("envelope", envelopeReport(result.audio, events, request.sampleRate));
	return result;
}

bool writeMonoRenderWav(const juce::File& file, const MonoRenderResult& result, double sampleRate)
{
	if (result.audio.getNumChannels() != 2 || result.audio.getNumSamples() <= 0 || sampleRate <= 0.0) return false;
	file.deleteFile();
	std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
	juce::WavAudioFormat format;
	auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions {}
		.withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(32));
	return writer != nullptr && writer->writeFromAudioSampleBuffer(result.audio, 0, result.audio.getNumSamples());
}

bool writeMonoRenderReport(const juce::File& file, const MonoRenderResult& result)
{
	return result.report.isObject() && file.replaceWithText(juce::JSON::toString(result.report, false));
}
}
