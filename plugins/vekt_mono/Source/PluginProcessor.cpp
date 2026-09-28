#include <vekt/mono/PluginProcessor.h>

#include "FactoryPresets.h"
#include "MonoVoice.h"
#include "PluginEditor.h"

#include <vekt/presets/PresetPaths.h>
#include <vekt/presets/PresetSchema.h>

#include <algorithm>
#include <array>

namespace vekt::mono
{
namespace
{
juce::Result migrateContourPreset(presets::Preset& preset)
{
	if (preset.soundSchemaVersion == 6) return juce::Result::ok();
	if (preset.soundSchemaVersion == 4)
		preset.parameters.push_back({ parameters::notePriority, 0.0f });
	else if (preset.soundSchemaVersion == 5)
	{
		preset.parameters.erase(std::remove_if(preset.parameters.begin(), preset.parameters.end(), [](const auto& parameter)
		{
			return parameter.identifier == "contourCurve" || parameter.identifier == "releasePolicy";
		}), preset.parameters.end());
	}
	else return juce::Result::fail("Unsupported Mono preset sound schema");
	preset.soundSchemaVersion = 6;
	return juce::Result::ok();
}
// Schema 7 adds the LFOs and vibrato. Older presets get every new parameter at its default: no LFO depth, and a
// vibrato that stays silent until the mod wheel or aftertouch is used.
juce::Result migrateLfoPreset(presets::Preset& preset, const juce::AudioProcessorValueTreeState& state)
{
	if (preset.soundSchemaVersion == 4 || preset.soundSchemaVersion == 5)
		if (const auto result = migrateContourPreset(preset); result.failed()) return result;
	if (preset.soundSchemaVersion != 6) return juce::Result::fail("Unsupported Mono preset sound schema");
	for (const auto* identifier : parameters::schema7ParameterIds)
	{
		const auto* parameter = state.getParameter(identifier);
		preset.parameters.push_back({ identifier, parameter->convertFrom0to1(parameter->getDefaultValue()) });
	}
	preset.soundSchemaVersion = 7;
	return juce::Result::ok();
}

int choiceToVoiceCount(float value) noexcept
{
	constexpr std::array counts { 2, 4, 8, 12, 16 };
	return counts[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(counts.size()) - 1, juce::roundToInt(value)))];
}
int choiceToUnison(float value) noexcept { return value < 0.5f ? 1 : value < 1.5f ? 2 : 4; }

dsp::OversamplingQuality oversamplingQualityFor(int quality) noexcept
{
	switch (quality)
	{
	case 1: return { dsp::OversamplingFactor::x2, dsp::OversamplingFilter::polyphaseIIR };
	case 2: return { dsp::OversamplingFactor::x4, dsp::OversamplingFilter::polyphaseFIR };
	case 3: return { dsp::OversamplingFactor::x8, dsp::OversamplingFilter::polyphaseFIR };
	default: return { dsp::OversamplingFactor::off, dsp::OversamplingFilter::polyphaseIIR };
	}
}

}

MonoVoiceSettings PluginProcessor::snapshotSettings() const
{
	MonoVoiceSettings settings;
	const std::array ranges { parameters::osc1Range, parameters::osc2Range, parameters::osc3Range };
	const std::array semitones { parameters::osc1Semitone, parameters::osc2Semitone, parameters::osc3Semitone };
	const std::array fines { parameters::osc1Fine, parameters::osc2Fine, parameters::osc3Fine };
	const std::array octaves { parameters::osc1Octave, parameters::osc2Octave, parameters::osc3Octave };
	const std::array levels { parameters::osc1Level, parameters::osc2Level, parameters::osc3Level };
	const std::array morphs { parameters::osc1Morph, parameters::osc2Morph, parameters::osc3Morph };
	const std::array widths { parameters::osc1PulseWidth, parameters::osc2PulseWidth, parameters::osc3PulseWidth };
	for (std::size_t index = 0; index < 3; ++index)
	{
		settings.range[index] = value(ranges[index]);
		settings.semitone[index] = value(semitones[index]);
		settings.fine[index] = value(fines[index]);
		settings.octave[index] = value(octaves[index]);
		settings.level[index] = value(levels[index]) * 0.01f;
		settings.morph[index] = value(morphs[index]);
		settings.pulseWidth[index] = value(widths[index]);
	}
	settings.noiseType = juce::roundToInt(value(parameters::noiseType));
	settings.noiseLevel = value(parameters::noiseLevel) * 0.01f;
	settings.cutoff = value(parameters::filterCutoff);
	settings.resonance = value(parameters::filterResonance) * 0.01f;
	settings.tracking = value(parameters::filterKeyTracking) * 0.01f;
	settings.envelopeAmount = value(parameters::filterEnvelopeAmount) * 0.01f;
	settings.drive = value(parameters::filterDrive);
	settings.qCompensation = value(parameters::filterQCompensation) >= 0.5f;
	settings.ampAttack = value(parameters::ampAttack);
	settings.ampDecay = value(parameters::ampDecay);
	settings.ampSustain = value(parameters::ampSustain) * 0.01f;
	settings.ampRelease = value(parameters::ampRelease);
	settings.filterAttack = value(parameters::filterAttack);
	settings.filterDecay = value(parameters::filterDecay);
	settings.filterSustain = value(parameters::filterSustain) * 0.01f;
	settings.filterRelease = value(parameters::filterRelease);
	settings.ampVelocity = value(parameters::ampVelocity) * 0.01f;
	settings.filterVelocity = value(parameters::filterVelocity) * 0.01f;
	settings.calibration = value(parameters::calibration);
	settings.unison = choiceToUnison(value(parameters::unison));
	settings.detune = value(parameters::unisonDetune);
	settings.unisonSpread = value(parameters::unisonSpread) * 0.01f;
	settings.voiceWidth = value(parameters::voiceWidth) * 0.01f;
	settings.drift = value(parameters::drift);
	settings.glideMode = juce::roundToInt(value(parameters::glideMode));
	settings.glideTime = value(parameters::glideTime);
	for (std::size_t index = 0; index < parameters::lfos.size(); ++index)
	{
		const auto& ids = parameters::lfos[index];
		auto& lfo = settings.lfo[index];
		const auto division = juce::roundToInt(value(ids.division));
		lfo.source.rateHz = value(ids.sync) >= 0.5f ? syncedLfoRateHz(transportBpm, division) : value(ids.rate);
		lfo.source.shape = static_cast<LfoShape>(juce::roundToInt(value(ids.shape)));
		lfo.source.polarity = static_cast<LfoPolarity>(juce::roundToInt(value(ids.polarity)));
		lfo.source.mode = static_cast<LfoMode>(juce::roundToInt(value(ids.mode)));
		lfo.source.phase = value(ids.phase) / 360.0f;
		lfo.source.delaySeconds = value(ids.delay);
		lfo.source.fadeSeconds = value(ids.fade);
		lfo.source.drift = settings.drift * 0.01f;
		// Convert each depth to its destination's own units, scaled by the master Amount.
		const auto amount = value(ids.amount) * 0.01f;
		for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
		{
			lfo.pitch[oscillator] = amount * value(ids.pitch[oscillator]);
			lfo.morph[oscillator] = amount * value(ids.morph[oscillator]) * 0.03f;
			lfo.width[oscillator] = amount * value(ids.width[oscillator]) * 0.45f;
			lfo.level[oscillator] = amount * value(ids.level[oscillator]) * 0.01f;
		}
		lfo.filter = amount * value(ids.filter);
		lfo.amp = amount * value(ids.amp) * 0.01f;
		lfo.drive = amount * value(ids.drive);
		lfo.noise = amount * value(ids.noise) * 0.01f;
		lfo.detune = amount * value(ids.detune) * 0.5f;
		lfo.spread = amount * value(ids.spread) * 0.01f;
	}
	return settings;
}

PluginProcessor::PluginProcessor()
	: AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  parameterState(*this, &undoManager, parameters::stateType, parameters::createLayout()),
	  stateManager(parameterState, parameters::projectStateType, 3),
	  presetSession(presetCatalog, { parameters::presetProductIdentifier, "Vekt Mono", 7 }, {
		[this](const juce::String& name)
		{
			auto preset = presets::PresetSchema::create(parameters::presetProductIdentifier, name, parameterState, parameters::soundParameterIds);
			preset.soundSchemaVersion = 7;
			return preset;
		},
		[this](presets::Preset& preset) { return migrateLfoPreset(preset, parameterState); },
		[this](const presets::Preset& preset) { return validatePresetSound(preset); },
		[this](const presets::Preset& preset) { return applyPreset(preset); },
		[this](const presets::Preset& preset) { return matchesPresetSound(preset); } })
{
	for (std::size_t index = 0; index < voices.size(); ++index)
		voices[index] = std::make_unique<MonoVoice>();
	for (auto& clock : lfoClocks) clock = std::make_unique<LfoClock>();
	vibratoClock = std::make_unique<LfoClock>();
	for (auto& heldNotes : heldNotesByChannel)
		heldNotes.reserve(128);
	const auto factoryResult = addFactoryPresets(presetCatalog);
	jassert(factoryResult.wasOk());
	juce::ignoreUnused(factoryResult);
	userPresetRepository = std::make_unique<presets::FilePresetRepository>(presets::PresetPaths::desktop("Vekt Mono"));
	presetCatalog.setUserRepository(userPresetRepository.get());
	presets::Preset initialPreset;
	if (presetCatalog.loadFactoryPreset(0, initialPreset).wasOk()
		&& presetSession.prepare(initialPreset).wasOk()
		&& presets::PresetSchema::apply(initialPreset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds).wasOk())
		presetSession.adopt(initialPreset, presets::PresetOrigin::factory);
}

PluginProcessor::~PluginProcessor() = default;

PluginProcessor::CoupledWorkSnapshot PluginProcessor::coupledWorkSnapshot() const noexcept
{
	CoupledWorkSnapshot total;
	for (const auto& voice : voices)
	{
		const auto value = voice->coupledDiagnostics();
		total.samples += value.samples;
		total.iterations += value.coupledIterations;
		total.lineSearchTrials += value.coupledLineSearchTrials;
		total.unconverged += value.unconvergedSamples;
		total.nonFinite += value.nonFiniteSamples;
	}
	return total;
}

void PluginProcessor::prepareToPlay(double newSampleRate, int maximumBlockSize)
{
	sampleRateHz = newSampleRate;
	oversampling.prepare(static_cast<std::size_t>(std::max(maximumBlockSize, 1)));
	for (auto& clock : lfoClocks) clock->reset();
	vibratoClock->reset();
	activeVoiceCount = choiceToVoiceCount(value(parameters::voiceCount));
	configureQuality(juce::roundToInt(value(parameters::quality)));
}

void PluginProcessor::releaseResources()
{
	resetPlayingState();
	soundingVoiceDisplay.store(0, std::memory_order_relaxed);
	outputMeter.reset();
}
bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
float PluginProcessor::value(const char* identifier) const noexcept { return parameterState.getRawParameterValue(identifier)->load(); }

int PluginProcessor::activeVoiceLimit() const noexcept { return activeVoiceCount; }
int PluginProcessor::getSoundingVoiceCount() const noexcept
{
	return static_cast<int>(std::count_if(voices.begin(), voices.end(), [](const auto& voice) { return voice->isActive(); }));
}

void PluginProcessor::applyConfigurationChanges()
{
	const auto voiceCount = choiceToVoiceCount(value(parameters::voiceCount));
	const auto quality = juce::roundToInt(value(parameters::quality));
	if (voiceCount == activeVoiceCount && quality == activeQuality) return;
	// Voice count and quality apply at once and cut whatever is sounding.
	resetPlayingState();
	oversampling.reset();
	activeVoiceCount = voiceCount;
	if (quality != activeQuality) configureQuality(quality);
}

void PluginProcessor::configureQuality(int quality)
{
	activeQuality = quality;
	oversampling.activate(oversamplingQualityFor(activeQuality));
	const auto effectiveSampleRate = sampleRateHz * static_cast<double>(oversampling.getActiveFactor());
	for (auto& clock : lfoClocks) clock->setSampleRate(effectiveSampleRate);
	vibratoClock->setSampleRate(effectiveSampleRate);
	for (std::size_t index = 0; index < voices.size(); ++index)
	{
		voices[index]->prepare(effectiveSampleRate,
			0x4d6f6e6fu + static_cast<std::uint32_t>(index * 977));
	}
	setLatencySamples(oversampling.getActiveLatencySamples());
}

void PluginProcessor::readTransport()
{
	transportPpq.reset();
	if (const auto* playHead = getPlayHead())
		if (const auto position = playHead->getPosition())
		{
			if (const auto bpm = position->getBpm(); bpm && *bpm > 0.0) transportBpm = *bpm;
			if (const auto ppq = position->getPpqPosition(); ppq && position->getIsPlaying()) transportPpq = *ppq;
		}
	// While the host plays, a synced Free LFO follows the song position, so it lands on the grid on every playback.
	if (!transportPpq) return;
	for (std::size_t index = 0; index < parameters::lfos.size(); ++index)
	{
		const auto& ids = parameters::lfos[index];
		if (value(ids.sync) >= 0.5f)
			lfoClocks[index]->setPosition(*transportPpq / lfoDivisionBeats(juce::roundToInt(value(ids.division))));
	}
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	juce::ScopedNoDenormals noDenormals;
	buffer.clear();
	if (pendingPresetReset.exchange(false))
	{
		resetPlayingState();
		// Preset loads must not leak old-patch samples from the oversampling filters.
		// Reset before handling MIDI so a new note can sound in this callback.
		oversampling.reset();
	}
	applyConfigurationChanges();
	readTransport();
	int position {};
	for (const auto metadata : midi)
	{
		const auto eventPosition = juce::jlimit(0, buffer.getNumSamples(), metadata.samplePosition);
		render(buffer, position, eventPosition - position);
		handleMidi(metadata.getMessage());
		position = eventPosition;
	}
	render(buffer, position, buffer.getNumSamples() - position);
	outputMeter.publish(buffer);
	const MonoVoice* newest {};
	for (const auto& voice : voices)
		if (voice->isActive() && (newest == nullptr || voice->getAge() > newest->getAge())) newest = voice.get();
	for (std::size_t index = 0; index < lfoDisplayValues.size(); ++index)
		lfoDisplayValues[index].store(newest != nullptr ? newest->getLfoOutput(index) : 0.0f, std::memory_order_relaxed);
	auto control = std::max(*std::max_element(modWheelByChannel.begin(), modWheelByChannel.end()),
		*std::max_element(pressureByChannel.begin(), pressureByChannel.end()));
	for (const auto& voice : voices) if (voice->isActive()) control = std::max(control, voice->getPolyPressure());
	vibratoControlDisplay.store(control, std::memory_order_relaxed);
	soundingVoiceDisplay.store(getSoundingVoiceCount(), std::memory_order_relaxed);
}

void PluginProcessor::handleMidi(const juce::MidiMessage& message)
{
	if (message.isNoteOn()) noteOn(message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
	else if (message.isNoteOff()) noteOff(message.getChannel(), message.getNoteNumber());
	else if (message.isAllSoundOff()) allNotesOff(message.getChannel(), true);
	else if (message.isAllNotesOff()) allNotesOff(message.getChannel(), false);
	else if (message.isResetAllControllers())
	{
		const auto channelIndex = static_cast<std::size_t>(message.getChannel() - 1);
		const auto sustainWasDown = sustainByChannel[channelIndex];
		sustainByChannel[channelIndex] = false;
		pitchBendByChannel[channelIndex] = 0.0f;
		modWheelByChannel[channelIndex] = pressureByChannel[channelIndex] = 0.0f;
		for (auto& voice : voices) if (voice->getChannel() == message.getChannel()) voice->setPolyPressure(0.0f);
		if (sustainWasDown) releaseSustainedNotes(message.getChannel());
	}
	else if (message.isController() && message.getControllerNumber() == 1)
		modWheelByChannel[static_cast<std::size_t>(message.getChannel() - 1)] = static_cast<float>(message.getControllerValue()) / 127.0f;
	else if (message.isChannelPressure())
		pressureByChannel[static_cast<std::size_t>(message.getChannel() - 1)] = static_cast<float>(message.getChannelPressureValue()) / 127.0f;
	else if (message.isAftertouch())
	{
		for (auto& voice : voices)
			if (voice->matches(message.getChannel(), message.getNoteNumber()))
				voice->setPolyPressure(static_cast<float>(message.getAfterTouchValue()) / 127.0f);
	}
	else if (message.isPitchWheel())
		pitchBendByChannel[static_cast<std::size_t>(message.getChannel() - 1)]
			= (static_cast<float>(message.getPitchWheelValue()) - 8192.0f) / 8192.0f * value(parameters::pitchBendRange);
	else if (message.isController() && message.getControllerNumber() == 64)
	{
		auto& sustain = sustainByChannel[static_cast<std::size_t>(message.getChannel() - 1)];
		const auto wasDown = sustain; sustain = message.getControllerValue() >= 64;
		if (wasDown && !sustain) releaseSustainedNotes(message.getChannel());
	}
}

MonoVoice& PluginProcessor::findVoiceForNote(int, int)
{
	for (int index = 0; index < activeVoiceLimit(); ++index) if (!voices[static_cast<std::size_t>(index)]->isActive()) return *voices[static_cast<std::size_t>(index)];
	auto* selected = voices.front().get();
	for (int index = 0; index < activeVoiceLimit(); ++index)
	{
		auto* candidate = voices[static_cast<std::size_t>(index)].get();
		if ((!candidate->isHeld() && selected->isHeld()) || (candidate->isHeld() == selected->isHeld() && candidate->getAge() < selected->getAge())) selected = candidate;
	}
	return *selected;
}

MonoVoice& PluginProcessor::monoVoiceForChannel(int channel)
{
	return *voices[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(voices.size()) - 1, channel - 1))];
}

void PluginProcessor::noteOn(int channel, int note, float velocity)
{
	const auto settings = snapshotSettings();
	const auto mode = juce::roundToInt(value(parameters::performanceMode));
	if (mode != 0)
	{
		auto& heldNotes = heldNotesByChannel[static_cast<std::size_t>(channel - 1)];
		const auto legato = !heldNotes.empty();
		heldNotes.erase(std::remove_if(heldNotes.begin(), heldNotes.end(), [note](const auto& heldNote) { return heldNote.note == note; }), heldNotes.end());
		heldNotes.push_back({ note, velocity });
		const auto lowPriority = value(parameters::notePriority) >= 0.5f;
		const auto& selected = lowPriority ? *std::min_element(heldNotes.begin(), heldNotes.end(), [](const auto& a, const auto& b) { return a.note < b.note; }) : heldNotes.back();
		if (lowPriority && selected.note != note) return;
		auto& voice = monoVoiceForChannel(channel);
		voice.setPanPosition(0.0f);
		const auto retrigger = mode == 1 || !legato || !voice.isActive() || (!voice.isHeld() && !voice.isSustained());
		voice.start(channel, note, velocity, settings, retrigger, legato, ++noteAge);
		return;
	}
	// A key that is still sounding (held, sustained or releasing) retriggers its own voice, as on analog
	// polysynths: its envelopes restart from their current level instead of stacking a second copy.
	for (auto& sounding : voices)
		if (sounding->matches(channel, note))
		{
			sounding->start(channel, note, velocity, settings, true, false, ++noteAge);
			return;
		}
	auto& voice = findVoiceForNote(channel, note);
	voice.setPanPosition(activeVoiceLimit() <= 1 ? 0.0f : 2.0f * static_cast<float>(noteAge % static_cast<std::uint64_t>(activeVoiceLimit())) / static_cast<float>(activeVoiceLimit() - 1) - 1.0f);
	voice.start(channel, note, velocity, settings, true, false, ++noteAge);
}

void PluginProcessor::noteOff(int channel, int note)
{
	const auto mode = juce::roundToInt(value(parameters::performanceMode));
	if (mode != 0)
	{
		auto& heldNotes = heldNotesByChannel[static_cast<std::size_t>(channel - 1)];
		auto& voice = monoVoiceForChannel(channel);
		const auto wasActive = voice.matches(channel, note);
		heldNotes.erase(std::remove_if(heldNotes.begin(), heldNotes.end(), [note](const auto& heldNote) { return heldNote.note == note; }), heldNotes.end());
		if (wasActive && value(parameters::heldKeyReturn) >= 0.5f && !heldNotes.empty())
		{
			retargetMonophonicVoice(channel, mode == 1);
			return;
		}
		if (wasActive)
			voice.release(sustainByChannel[static_cast<std::size_t>(channel - 1)]);
		return;
	}
	for (auto& voice : voices) if (voice->matches(channel, note)) voice->release(sustainByChannel[static_cast<std::size_t>(channel - 1)]);
}

void PluginProcessor::allNotesOff(int channel, bool immediate)
{
	const auto channelIndex = static_cast<std::size_t>(channel - 1);
	heldNotesByChannel[channelIndex].clear();
	sustainByChannel[channelIndex] = false;
	for (auto& voice : voices)
		if (voice->getChannel() == channel)
		{
			if (immediate) voice->stop();
			else voice->release(false);
		}
}

void PluginProcessor::retargetMonophonicVoice(int channel, bool retrigger)
{
	const auto& heldNotes = heldNotesByChannel[static_cast<std::size_t>(channel - 1)];
	if (heldNotes.empty()) return;
	const auto settings = snapshotSettings();
	const auto& returned = value(parameters::notePriority) >= 0.5f
		? *std::min_element(heldNotes.begin(), heldNotes.end(), [](const auto& a, const auto& b) { return a.note < b.note; })
		: heldNotes.back();
	monoVoiceForChannel(channel).start(channel, returned.note, returned.velocity, settings, retrigger, true, ++noteAge);
}

void PluginProcessor::releaseSustainedNotes(int channel)
{
	for (auto& voice : voices)
		if (voice->getChannel() == channel)
			voice->releaseSustain();
}

void PluginProcessor::resetPlayingState()
{
	// Restart the shared modulation clocks with the voices, so output after a preset load does not depend on
	// how long the previous patch ran. A synced Free LFO then re-locks to the host position while it plays.
	for (auto& clock : lfoClocks) clock->reset();
	vibratoClock->reset();
	for (auto& voice : voices) voice->reset();
	for (auto& heldNotes : heldNotesByChannel) heldNotes.clear();
	sustainByChannel.fill(false);
	noteAge = 0;
}

void PluginProcessor::render(juce::AudioBuffer<float>& buffer, int start, int count)
{
	if (count <= 0) return;
	const auto settings = snapshotSettings();
	for (std::size_t index = 0; index < lfoClocks.size(); ++index) lfoClocks[index]->setRate(settings.lfo[index].source.rateHz);
	vibratoClock->setRate(value(parameters::vibratoRate));
	const auto vibratoShape = juce::roundToInt(value(parameters::vibratoShape)) == 1 ? LfoShape::triangle : LfoShape::sine;
	const auto vibratoDepthSemitones = value(parameters::vibratoDepth) * 0.01f;
	std::array<float, 16> channelControl {};
	for (std::size_t channel = 0; channel < channelControl.size(); ++channel)
		channelControl[channel] = std::max(modWheelByChannel[channel], pressureByChannel[channel]);
	const auto outputGain = dbToGain(value(parameters::masterOutput));
	juce::dsp::AudioBlock<float> outputBlock(buffer);
	auto renderBlock = outputBlock.getSubBlock(static_cast<std::size_t>(start), static_cast<std::size_t>(count));
	if (activeQuality != 0)
	{
		const juce::dsp::AudioBlock<const float> inputBlock(renderBlock);
		renderBlock = oversampling.processSamplesUp(inputBlock);
	}
	for (std::size_t sample = 0; sample < renderBlock.getNumSamples(); ++sample)
	{
		float left {}, right {};
		const std::array lfoPositions { lfoClocks[0]->getPosition(), lfoClocks[1]->getPosition() };
		const auto vibrato = Lfo::bipolarShape(vibratoShape, vibratoClock->getPosition(), 0) * vibratoDepthSemitones;
		for (auto& voice : voices)
		{
			const auto channelIndex = static_cast<std::size_t>(juce::jlimit(0, 15, voice->getChannel() - 1));
			voice->render(left, right, settings, pitchBendByChannel[channelIndex], lfoPositions, vibrato, channelControl[channelIndex]);
		}
		for (auto& clock : lfoClocks) clock->advance();
		vibratoClock->advance();
		const auto sampleIndex = static_cast<int>(sample);
		renderBlock.setSample(0, sampleIndex, left * outputGain);
		renderBlock.setSample(1, sampleIndex, right * outputGain);
	}
	if (activeQuality != 0)
	{
		auto outputSegment = outputBlock.getSubBlock(static_cast<std::size_t>(start), static_cast<std::size_t>(count));
		oversampling.processSamplesDown(outputSegment);
	}
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }
int PluginProcessor::getNumPrograms() { return static_cast<int>(presetCatalog.factoryPresetCount()); }
int PluginProcessor::getCurrentProgram()
{
	if (const auto index = presetSession.currentIndex(); index && presetSession.origin() == presets::PresetOrigin::factory)
		return static_cast<int>(*index);
	return 0;
}
void PluginProcessor::setCurrentProgram(int index)
{
	if (index < 0) return;
	presets::Preset preset;
	if (presetCatalog.loadFactoryPreset(static_cast<std::size_t>(index), preset).wasOk())
		juce::ignoreUnused(presetSession.load(preset.identifier, presets::PresetOrigin::factory));
}
const juce::String PluginProcessor::getProgramName(int index)
{
	return index < 0 ? juce::String {} : presetCatalog.factoryPresetName(static_cast<std::size_t>(index));
}
juce::Result PluginProcessor::loadNextPreset() { return loadAdjacentPreset(true); }
juce::Result PluginProcessor::loadPreviousPreset() { return loadAdjacentPreset(false); }
juce::Result PluginProcessor::loadAdjacentPreset(bool next)
{
	presetCatalog.refresh();
	const auto& entries = presetCatalog.entries();
	if (entries.empty()) return juce::Result::fail("No presets available");
	const auto current = presetSession.currentIndex();
	const auto index = current ? (next ? presetCatalog.nextIndex(*current) : presetCatalog.previousIndex(*current))
		: std::optional<std::size_t> { next ? 0 : entries.size() - 1 };
	if (!index) return juce::Result::fail("No presets available");
	const auto entry = entries[*index];
	return presetSession.load(entry.identifier, entry.origin);
}
juce::Result PluginProcessor::validatePresetSound(const presets::Preset& preset) const
{
	return preset.soundSchemaVersion != 7 ? juce::Result::fail("Unsupported Mono preset sound schema")
		: presets::PresetSchema::validate(preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds);
}
juce::Result PluginProcessor::applyPreset(const presets::Preset& preset)
{
	auto prepared = preset;
	if (prepared.soundSchemaVersion >= 4 && prepared.soundSchemaVersion <= 6)
		if (const auto result = migrateLfoPreset(prepared, parameterState); result.failed()) return result;
	if (const auto result = validatePresetSound(prepared); result.failed()) return result;
	undoManager.beginNewTransaction("Load preset: " + preset.name);
	const auto result = presets::PresetSchema::apply(prepared, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds, &undoManager);
	if (result.wasOk()) pendingPresetReset.store(true);
	return result;
}
bool PluginProcessor::matchesPresetSound(const presets::Preset& preset) const
{
	auto prepared = preset;
	if (prepared.soundSchemaVersion >= 4 && prepared.soundSchemaVersion <= 6 && migrateLfoPreset(prepared, parameterState).failed()) return false;
	return validatePresetSound(prepared).wasOk()
		&& presets::PresetSchema::matches(prepared, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds);
}
void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
	stateManager.getMetadata().setProperty("vektPresetSelection", presetSession.selectionState(), nullptr);
	juce::MemoryOutputStream stream(destination, false); stateManager.createState().writeToStream(stream);
}
void PluginProcessor::setStateInformation(const void* data, int size)
{
	if (size <= 0 || data == nullptr) return;
	const auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(size));
	// Reject obsolete 16x projects before APVTS can clamp choice index 4 to 8x.
	// Leave the entire live state unchanged rather than silently altering the sound.
	const auto parameterTree = state.hasType(parameterState.state.getType())
		? state : state.getChildWithName(parameterState.state.getType());
	const auto savedQuality = parameterTree.getChildWithProperty("id", parameters::quality);
	if (savedQuality.isValid() && static_cast<double>(savedQuality.getProperty("value")) >= 4.0) return;
	if (state.isValid() && stateManager.restoreState(state))
	{
		// Parameters added after a project was saved take their defaults rather than keeping the live value.
		const auto restoreDefault = [&](const char* identifier)
		{
			if (parameterTree.getChildWithProperty("id", identifier).isValid()) return;
			auto* parameter = parameterState.getParameter(identifier);
			parameter->setValueNotifyingHost(parameter->getDefaultValue());
		};
		restoreDefault(parameters::notePriority);
		for (const auto* identifier : parameters::schema7ParameterIds) restoreDefault(identifier);
		for (const auto* identifier : { parameters::heldKeyReturn, parameters::filterQCompensation })
		{
			auto* parameter = parameterState.getParameter(identifier);
			const auto restoredValue = parameter->convertTo0to1(parameterState.getRawParameterValue(identifier)->load());
			if (!juce::approximatelyEqual(parameter->getValue(), restoredValue))
				parameter->setValueNotifyingHost(restoredValue);
		}
		presetSession.clear();
		if (stateManager.getMetadata().hasProperty("vektPresetSelection"))
			juce::ignoreUnused(presetSession.restoreSelection(stateManager.getMetadata().getProperty("vektPresetSelection").toString()));
	}
}
}
