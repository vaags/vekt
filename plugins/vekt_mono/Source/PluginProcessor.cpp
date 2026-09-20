#include <vekt/mono/PluginProcessor.h>

#include "FactoryPresets.h"
#include "PluginEditor.h"

#include <vekt/presets/PresetPaths.h>
#include <vekt/presets/PresetSchema.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vekt::mono
{
namespace
{
constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
constexpr float maximumContourOctaves = 8.0f;
constexpr float maximumVelocityOctaves = 4.0f;
constexpr float referenceOscillationFeedback = 4.58f;
constexpr float voiceTransitionSeconds = 0.003f;

struct LadderCoefficients
{
	float stageCoefficient {};
	float oscillationFeedback {};
};

LadderCoefficients ladderCoefficients(float cutoff, float sampleRate) noexcept
{
	// The feedback sample is one sample old, in addition to the phase shift of
	// the four one-pole stages. Solve that complete loop at the requested cutoff
	// so Resonance peaks and self-oscillates at the frequency shown by Cutoff.
	const auto omega = juce::jlimit(1.0e-6f, std::numbers::pi_v<float> * 0.9f, twoPi * cutoff / sampleRate);
	const auto stagePhase = (std::numbers::pi_v<float> - omega) * 0.25f;
	const auto tangent = std::tan(stagePhase);
	const auto pole = tangent / (std::sin(omega) + tangent * std::cos(omega));
	const auto coefficient = 1.0f - pole;
	const auto stageMagnitude = coefficient
		/ std::sqrt(1.0f + pole * pole - 2.0f * pole * std::cos(omega));
	return { coefficient, 1.0f / std::pow(stageMagnitude, 4.0f) };
}

float dbToGain(float decibels) noexcept { return std::pow(10.0f, decibels / 20.0f); }
float midiToHz(float note) noexcept { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }
int choiceToVoiceCount(float value) noexcept { return value < 0.5f ? 8 : value < 1.5f ? 12 : 16; }
int choiceToUnison(float value) noexcept { return value < 0.5f ? 1 : value < 1.5f ? 2 : 4; }

dsp::OversamplingQuality oversamplingQualityFor(int quality) noexcept
{
	return quality == 1
		? dsp::OversamplingQuality { dsp::OversamplingFactor::x2, dsp::OversamplingFilter::polyphaseIIR }
		: dsp::OversamplingQuality { dsp::OversamplingFactor::off, dsp::OversamplingFilter::polyphaseIIR };
}

bool migrateProjectState(juce::ValueTree& state, int sourceVersion)
{
	if (sourceVersion != 1)
		return false;
	auto sound = state.getChildWithName(parameters::stateType);
	if (!sound.isValid())
		return false;
	for (const auto* identifier : { parameters::osc1Octave, parameters::osc2Octave, parameters::osc3Octave })
		if (!sound.getChildWithProperty("id", identifier).isValid())
		{
			juce::ValueTree value("PARAM");
			value.setProperty("id", identifier, nullptr);
			value.setProperty("value", 0.0f, nullptr);
			sound.appendChild(value, nullptr);
		}
	return true;
}
}

struct PluginProcessor::Settings
{
	std::array<float, 3> range, semitone, fine, octave, level, morph, pulseWidth;
	float noiseLevel {}, cutoff {}, resonance {}, tracking {}, envelopeAmount {}, drive {};
	float ampAttack {}, ampDecay {}, ampSustain {}, ampRelease {};
	float filterAttack {}, filterDecay {}, filterSustain {}, filterRelease {};
	float ampVelocity {}, filterVelocity {}, calibration {};
	float detune {}, unisonSpread {}, voiceWidth {}, glideTime {}, drift {};
	int unison {}, glideMode {}, noiseType {};
};

PluginProcessor::Settings PluginProcessor::snapshotSettings() const
{
	Settings settings;
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
	return settings;
}

class PluginProcessor::Voice
{
public:
	void prepare(double newSampleRate, std::uint32_t seed)
	{
		sampleRate = static_cast<float>(newSampleRate);
		random.setSeed(seed);
		amp.setSampleRate(newSampleRate);
		filterEnvelope.setSampleRate(newSampleRate);
		cutoffOctaves.reset(newSampleRate, 0.015);
		resonance.reset(newSampleRate, 0.02);
		driveDecibels.reset(newSampleRate, 0.02);
		reset();
	}

	void reset()
	{
		active = held = sustained = false;
		amp.reset(); filterEnvelope.reset();
		for (auto& stackPhase : phase)
			for (auto& oscillatorPhase : stackPhase)
				oscillatorPhase = random.nextFloat();
		driftCents = random.nextFloat() * 2.0f - 1.0f;
		filterState = {};
		filterControlsInitialized = false;
		fadeInSamples = 0;
		continuitySamples = 0;
		continuityPending = false;
		continuityOffset = {};
		lastOutput = {};
	}

	void start(int newChannel, int newNote, float newVelocity, const Settings& settings, float bend, bool retrigger, std::uint64_t newAge)
	{
		const auto wasActive = active;
		const auto target = static_cast<float>(newNote) + settings.calibration * 0.01f;
		if (!active || settings.glideMode == 0 || !retrigger) currentNote = target + bend;
		targetNote = target;
		channel = newChannel; note = newNote; velocity = newVelocity; age = newAge;
		active = held = true; sustained = false;
		fadeInSamples = wasActive ? 0 : transitionLength();
		continuitySamples = 0;
		continuityPending = wasActive;
		continuityOffset = {};
		if (retrigger)
		{
			juce::ADSR::Parameters ampParameters { settings.ampAttack, settings.ampDecay, settings.ampSustain, settings.ampRelease };
			juce::ADSR::Parameters filterParameters { settings.filterAttack, settings.filterDecay, settings.filterSustain, settings.filterRelease };
			amp.setParameters(ampParameters); filterEnvelope.setParameters(filterParameters);
			amp.noteOn(); filterEnvelope.noteOn();
		}
	}

	void release(bool keepSustained)
	{
		held = false;
		if (keepSustained) { sustained = true; return; }
		sustained = false; amp.noteOff(); filterEnvelope.noteOff();
	}

	void releaseSustain()
	{
		if (sustained && !held) { sustained = false; amp.noteOff(); filterEnvelope.noteOff(); }
	}

	void stop()
	{
		active = held = sustained = false;
		amp.reset(); filterEnvelope.reset();
		filterState = {};
		fadeInSamples = 0;
		continuitySamples = 0;
		continuityPending = false;
		continuityOffset = {};
		lastOutput = {};
	}

	void render(float& left, float& right, const Settings& settings, float bend)
	{
		if (!active) return;
		float voiceLeft {}, voiceRight {};
		const auto glideCoefficient = settings.glideTime <= 0.0f ? 1.0f : 1.0f - std::exp(-1.0f / (settings.glideTime * sampleRate));
		currentNote += (targetNote + bend - currentNote) * glideCoefficient;
		const auto baseHz = midiToHz(currentNote);
		const auto unisonCount = settings.unison;
		const auto filterEnvelopeValue = filterEnvelope.getNextSample();
		updateFilterControlTargets(settings);
		const auto filterCutoff = std::exp2(cutoffOctaves.getNextValue());
		const auto filterResonance = resonance.getNextValue();
		const auto filterDrive = dbToGain(driveDecibels.getNextValue());
		const auto velocityGain = (1.0f - settings.ampVelocity) + settings.ampVelocity * std::pow(velocity, 0.65f);
		const auto allocationFade = fadeInSamples > 0
			? 1.0f - static_cast<float>(fadeInSamples--) / static_cast<float>(transitionLength())
			: 1.0f;
		const auto amplitude = amp.getNextSample() * velocityGain * allocationFade;
		for (int stack = 0; stack < unisonCount; ++stack)
		{
			const auto normalizedStack = unisonCount == 1 ? 0.0f : (2.0f * static_cast<float>(stack) / static_cast<float>(unisonCount - 1) - 1.0f);
			float mixer {};
			for (int oscillator = 0; oscillator < 3; ++oscillator)
			{
				const auto octave = 1.0f / std::exp2(settings.range[static_cast<std::size_t>(oscillator)] - 1.0f);
				const auto cents = settings.fine[static_cast<std::size_t>(oscillator)] + normalizedStack * settings.detune
					+ driftCents * settings.drift * 0.2f;
				const auto frequency = baseHz * octave * std::exp2(settings.octave[static_cast<std::size_t>(oscillator)]
					+ (settings.semitone[static_cast<std::size_t>(oscillator)] + cents * 0.01f) / 12.0f);
				const auto phaseIncrement = frequency / sampleRate;
				auto& oscillatorPhase = phase[static_cast<std::size_t>(stack)][static_cast<std::size_t>(oscillator)];
				oscillatorPhase += phaseIncrement;
				oscillatorPhase -= std::floor(oscillatorPhase);
				mixer += waveform(oscillatorPhase, phaseIncrement, settings.morph[static_cast<std::size_t>(oscillator)], settings.pulseWidth[static_cast<std::size_t>(oscillator)]) * settings.level[static_cast<std::size_t>(oscillator)];
			}
			if (settings.noiseType != 0)
			{
				auto noise = random.nextFloat() * 2.0f - 1.0f;
				if (settings.noiseType == 2) { pink = 0.98f * pink + 0.02f * noise; noise = pink; }
				mixer += noise * settings.noiseLevel;
			}
			const auto stackOutput = filter(mixer, settings, filterEnvelopeValue, filterCutoff,
				filterResonance, filterDrive, stack) * amplitude;
			const auto pan = juce::jlimit(-1.0f, 1.0f, settings.voiceWidth * panPosition
				+ normalizedStack * settings.unisonSpread);
			voiceLeft += stackOutput * std::sqrt(0.5f * (1.0f - pan)) / static_cast<float>(unisonCount);
			voiceRight += stackOutput * std::sqrt(0.5f * (1.0f + pan)) / static_cast<float>(unisonCount);
		}
		if (continuityPending)
		{
			continuityOffset = { lastOutput[0] - voiceLeft, lastOutput[1] - voiceRight };
			continuitySamples = transitionLength();
			continuityPending = false;
		}
		if (continuitySamples > 0)
		{
			const auto continuityGain = static_cast<float>(continuitySamples--) / static_cast<float>(transitionLength());
			voiceLeft += continuityOffset[0] * continuityGain;
			voiceRight += continuityOffset[1] * continuityGain;
		}
		lastOutput = { voiceLeft, voiceRight };
		left += voiceLeft;
		right += voiceRight;
		if (!amp.isActive()) active = false;
	}

	[[nodiscard]] bool isActive() const noexcept { return active; }
	[[nodiscard]] bool isHeld() const noexcept { return held; }
	[[nodiscard]] bool isSustained() const noexcept { return sustained; }
	[[nodiscard]] bool matches(int expectedChannel, int expectedNote) const noexcept { return active && channel == expectedChannel && note == expectedNote; }
	[[nodiscard]] std::uint64_t getAge() const noexcept { return age; }
	[[nodiscard]] int getChannel() const noexcept { return channel; }
	void setPanPosition(float value) noexcept { panPosition = value; }

private:
	[[nodiscard]] int transitionLength() const noexcept
	{
		return std::max(1, static_cast<int>(std::round(voiceTransitionSeconds * sampleRate)));
	}

	static float polyBlep(float position, float phaseIncrement) noexcept
	{
		const auto increment = juce::jlimit(1.0e-6f, 0.5f, phaseIncrement);
		if (position < increment)
		{
			const auto t = position / increment;
			return t + t - t * t - 1.0f;
		}
		if (position > 1.0f - increment)
		{
			const auto t = (position - 1.0f) / increment;
			return t * t + t + t + 1.0f;
		}
		return 0.0f;
	}

	static float waveform(float position, float phaseIncrement, float morph, float width) noexcept
	{
		const auto sine = std::sin(twoPi * position);
		const auto triangle = 1.0f - 4.0f * std::abs(position - 0.5f);
		const auto saw = 2.0f * position - 1.0f - polyBlep(position, phaseIncrement);
		const auto pulseWidth = width * 0.01f;
		const auto pulsePhase = position < pulseWidth ? position + 1.0f - pulseWidth : position - pulseWidth;
		const auto pulse = (position < pulseWidth ? 1.0f : -1.0f) + polyBlep(position, phaseIncrement) - polyBlep(pulsePhase, phaseIncrement);
		const auto segment = juce::jlimit(0, 2, static_cast<int>(morph));
		const auto fraction = morph - static_cast<float>(segment);
		const std::array<float, 4> waves { sine, triangle, saw, pulse };
		return waves[static_cast<std::size_t>(segment)] + fraction * (waves[static_cast<std::size_t>(segment + 1)] - waves[static_cast<std::size_t>(segment)]);
	}

	void updateFilterControlTargets(const Settings& settings)
	{
		const auto cutoffTarget = std::log2(juce::jlimit(10.0f, 32'000.0f, settings.cutoff));
		if (!filterControlsInitialized)
		{
			cutoffOctaves.setCurrentAndTargetValue(cutoffTarget);
			resonance.setCurrentAndTargetValue(settings.resonance);
			driveDecibels.setCurrentAndTargetValue(settings.drive);
			filterControlsInitialized = true;
			return;
		}
		if (!juce::approximatelyEqual(cutoffOctaves.getTargetValue(), cutoffTarget)) cutoffOctaves.setTargetValue(cutoffTarget);
		if (!juce::approximatelyEqual(resonance.getTargetValue(), settings.resonance)) resonance.setTargetValue(settings.resonance);
		if (!juce::approximatelyEqual(driveDecibels.getTargetValue(), settings.drive)) driveDecibels.setTargetValue(settings.drive);
	}

	float filter(float input, const Settings& settings, float envelopeValue, float baseCutoff,
		float resonanceAmount, float driveGain, int stack)
	{
		// A ladder is controlled exponentially: keyboard, contour and velocity all
		// offset cutoff in octave/control-voltage space rather than linear Hertz.
		const auto keyOctaves = (currentNote - 60.0f) / 12.0f * settings.tracking;
		const auto contourOctaves = settings.envelopeAmount * envelopeValue * maximumContourOctaves;
		const auto velocityResponse = std::pow(juce::jlimit(0.0f, 1.0f, velocity), 0.65f);
		const auto velocityOctaves = -settings.filterVelocity * (1.0f - velocityResponse) * maximumVelocityOctaves;
		const auto maximumCutoff = std::min(32'000.0f, sampleRate * 0.45f);
		const auto cutoff = juce::jlimit(10.0f, maximumCutoff,
			baseCutoff * std::exp2(keyOctaves + contourOctaves + velocityOctaves));
		const auto coefficients = ladderCoefficients(cutoff, sampleRate);
		auto& stackFilterState = filterState[static_cast<std::size_t>(stack)];
		// Preserve the established control taper at the 1 kHz / 48 kHz reference,
		// but scale it by this cutoff's actual oscillation threshold. Resonance then
		// has the same meaning as Cutoff or sample rate changes.
		const auto normalizedResonance = juce::jlimit(0.0f, 1.0f, resonanceAmount);
		const auto regenerationPosition = juce::jlimit(0.0f, 1.0f, (normalizedResonance - 0.65f) / 0.2f);
		const auto regeneration = regenerationPosition * regenerationPosition * (3.0f - 2.0f * regenerationPosition);
		const auto referenceFeedback = 4.05f * std::pow(normalizedResonance, 0.72f) + 3.0f * regeneration;
		const auto feedbackAmount = coefficients.oscillationFeedback * referenceFeedback / referenceOscillationFeedback;
		const auto feedback = stackFilterState[3] * feedbackAmount;
		// Q compensation offsets the passband loss caused by negative feedback.
		// At low frequencies the ladder approaches unity gain, so multiplying its
		// input by 1 + feedback preserves the programmed level as emphasis rises.
		const auto qCompensation = 1.0f + feedbackAmount;
		const auto stateEnergy = std::abs(stackFilterState[0]) + std::abs(stackFilterState[1])
			+ std::abs(stackFilterState[2]) + std::abs(stackFilterState[3]);
		const auto startupExcitation = regeneration > 0.0f && stateEnergy < 1.0e-12f ? 1.0e-4f * regeneration : 0.0f;
		// Keep the nonlinear transfer fixed as Emphasis moves. Varying its headroom
		// with resonance turns Q compensation into an unintended level control.
		constexpr auto ladderHeadroom = 4.0f;
		const auto summingNode = (input + startupExcitation) * driveGain * qCompensation - feedback;
		auto signal = ladderHeadroom * std::tanh(summingNode / ladderHeadroom);
		for (auto& stage : stackFilterState)
		{
			const auto saturated = ladderHeadroom * std::tanh(signal / ladderHeadroom);
			stage += coefficients.stageCoefficient * (saturated - stage);
			signal = stage;
		}
		return signal / std::sqrt(driveGain);
	}

	float sampleRate { 48'000.0f };
	int fadeInSamples {}, continuitySamples {};
	juce::Random random;
	juce::ADSR amp, filterEnvelope;
	juce::SmoothedValue<float> cutoffOctaves, resonance, driveDecibels;
	std::array<std::array<float, 3>, 4> phase {};
	std::array<std::array<float, 4>, 4> filterState {};
	std::array<float, 2> continuityOffset {}, lastOutput {};
	float pink {}, currentNote {}, targetNote {}, velocity {}, panPosition {}, driftCents {};
	int channel {}, note {};
	std::uint64_t age {};
	bool active {}, held {}, sustained {}, filterControlsInitialized {}, continuityPending {};
};

PluginProcessor::PluginProcessor()
	: AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  parameterState(*this, &undoManager, parameters::stateType, parameters::createLayout()),
	  stateManager(parameterState, parameters::projectStateType, 2, migrateProjectState),
	  presetSession(presetCatalog, { parameters::presetProductIdentifier, "Vekt Mono", 2 }, {
		[this](const juce::String& name)
		{
			auto preset = presets::PresetSchema::create(parameters::presetProductIdentifier, name, parameterState, parameters::soundParameterIds);
			preset.soundSchemaVersion = 2;
			return preset;
		},
		[](presets::Preset& preset)
		{
			if (preset.soundSchemaVersion != 1)
				return preset.soundSchemaVersion == 2 ? juce::Result::ok() : juce::Result::fail("Unsupported Mono preset sound schema");
			for (const auto* identifier : { parameters::osc1Octave, parameters::osc2Octave, parameters::osc3Octave })
				preset.parameters.push_back({ identifier, 0.0f });
			preset.soundSchemaVersion = 2;
			return juce::Result::ok();
		},
		[this](const presets::Preset& preset) { return validatePresetSound(preset); },
		[this](const presets::Preset& preset) { return applyPreset(preset); },
		[this](const presets::Preset& preset) { return matchesPresetSound(preset); } })
{
	for (std::size_t index = 0; index < voices.size(); ++index)
		voices[index] = std::make_unique<Voice>();
	for (auto& heldNotes : heldNotesByChannel)
		heldNotes.reserve(128);
	parameterState.addParameterListener(parameters::voiceCount, this);
	parameterState.addParameterListener(parameters::quality, this);
	const auto factoryResult = addFactoryPresets(presetCatalog);
	jassert(factoryResult.wasOk());
	juce::ignoreUnused(factoryResult);
	userPresetRepository = std::make_unique<presets::FilePresetRepository>(presets::PresetPaths::desktop("Vekt Mono"));
	presetCatalog.setUserRepository(userPresetRepository.get());
	presets::Preset initialPreset;
	if (presetCatalog.loadFactoryPreset(0, initialPreset).wasOk()
		&& presets::PresetSchema::apply(initialPreset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds).wasOk())
		presetSession.adopt(initialPreset, presets::PresetOrigin::factory);
}

PluginProcessor::~PluginProcessor()
{
	parameterState.removeParameterListener(parameters::voiceCount, this);
	parameterState.removeParameterListener(parameters::quality, this);
}

void PluginProcessor::prepareToPlay(double newSampleRate, int maximumBlockSize)
{
	sampleRateHz = newSampleRate;
	oversampling.prepare(static_cast<std::size_t>(std::max(maximumBlockSize, 1)));
	activeVoiceCount = choiceToVoiceCount(value(parameters::voiceCount));
	requestedVoiceCount = activeVoiceCount;
	requestedQuality = juce::roundToInt(value(parameters::quality));
	configureQuality(requestedQuality);
	pendingQuality.store(false);
}

void PluginProcessor::releaseResources()
{
	resetPlayingState();
	outputMeter.reset();
}
bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
float PluginProcessor::value(const char* identifier) const noexcept { return parameterState.getRawParameterValue(identifier)->load(); }

int PluginProcessor::activeVoiceLimit() const noexcept { return activeVoiceCount; }

void PluginProcessor::parameterChanged(const juce::String& identifier, float newValue)
{
	if (identifier == parameters::voiceCount) { requestedVoiceCount = choiceToVoiceCount(newValue); pendingVoiceCount.store(requestedVoiceCount != activeVoiceCount); }
	if (identifier == parameters::quality) { requestedQuality = juce::roundToInt(newValue); pendingQuality.store(requestedQuality != activeQuality); }
}

void PluginProcessor::applyDeferredConfiguration()
{
	const auto anyActive = std::any_of(voices.begin(), voices.end(), [] (const auto& voice) { return voice->isActive(); });
	const auto sustainActive = std::any_of(sustainByChannel.begin(), sustainByChannel.end(), [] (bool active) { return active; });
	if (!anyActive && !sustainActive && pendingVoiceCount.exchange(false)) activeVoiceCount = requestedVoiceCount;
	if (!anyActive && !sustainActive && pendingQuality.load() && isTransportStopped())
	{
		configureQuality(requestedQuality);
		pendingQuality.store(false);
	}
}

void PluginProcessor::configureQuality(int quality)
{
	activeQuality = quality;
	oversampling.activate(oversamplingQualityFor(activeQuality));
	const auto effectiveSampleRate = sampleRateHz * static_cast<double>(oversampling.getActiveFactor());
	for (std::size_t index = 0; index < voices.size(); ++index)
		voices[index]->prepare(effectiveSampleRate, 0x4d6f6e6fu + static_cast<std::uint32_t>(index * 977));
	setLatencySamples(oversampling.getActiveLatencySamples());
}

bool PluginProcessor::isTransportStopped() const noexcept
{
	if (isNonRealtime()) return true;
	if (const auto* playHead = getPlayHead())
		if (const auto position = playHead->getPosition())
			return !position->getIsPlaying();
	return true;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	juce::ScopedNoDenormals noDenormals;
	buffer.clear();
	if (pendingPresetReset.exchange(false)) resetPlayingState();
	applyDeferredConfiguration();
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
		if (sustainWasDown) releaseSustainedNotes(message.getChannel());
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

PluginProcessor::Voice& PluginProcessor::findVoiceForNote(int, int)
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

PluginProcessor::Voice& PluginProcessor::monoVoiceForChannel(int channel)
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
		heldNotes.erase(std::remove(heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
		heldNotes.push_back(note);
		auto& voice = monoVoiceForChannel(channel);
		voice.setPanPosition(0.0f);
		const auto retrigger = mode == 1 || !voice.isActive();
		voice.start(channel, note, velocity, settings, pitchBendByChannel[static_cast<std::size_t>(channel - 1)], retrigger, ++noteAge);
		return;
	}
	auto& voice = findVoiceForNote(channel, note);
	voice.setPanPosition(activeVoiceLimit() <= 1 ? 0.0f : 2.0f * static_cast<float>(noteAge % static_cast<std::uint64_t>(activeVoiceLimit())) / static_cast<float>(activeVoiceLimit() - 1) - 1.0f);
	voice.start(channel, note, velocity, settings, pitchBendByChannel[static_cast<std::size_t>(channel - 1)], true, ++noteAge);
}

void PluginProcessor::noteOff(int channel, int note)
{
	const auto mode = juce::roundToInt(value(parameters::performanceMode));
	if (mode != 0)
	{
		auto& heldNotes = heldNotesByChannel[static_cast<std::size_t>(channel - 1)];
		heldNotes.erase(std::remove(heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
		if (!heldNotes.empty())
		{
			retargetMonophonicVoice(channel, mode == 1);
			return;
		}
		monoVoiceForChannel(channel).release(sustainByChannel[static_cast<std::size_t>(channel - 1)]);
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
	monoVoiceForChannel(channel).start(channel, heldNotes.back(), 1.0f, settings, pitchBendByChannel[static_cast<std::size_t>(channel - 1)], retrigger, ++noteAge);
}
void PluginProcessor::releaseSustainedNotes(int channel)
{
	for (auto& voice : voices)
		if (voice->getChannel() == channel)
			voice->releaseSustain();
}

void PluginProcessor::resetPlayingState()
{
	for (auto& voice : voices) voice->reset();
	for (auto& heldNotes : heldNotesByChannel) heldNotes.clear();
	sustainByChannel.fill(false);
	noteAge = 0;
}

void PluginProcessor::render(juce::AudioBuffer<float>& buffer, int start, int count)
{
	if (count <= 0) return;
	const auto settings = snapshotSettings();
	const auto outputGain = dbToGain(value(parameters::masterOutput));
	juce::dsp::AudioBlock<float> outputBlock(buffer);
	auto renderBlock = outputBlock.getSubBlock(static_cast<std::size_t>(start), static_cast<std::size_t>(count));
	if (activeQuality == 1)
	{
		const juce::dsp::AudioBlock<const float> inputBlock(renderBlock);
		renderBlock = oversampling.processSamplesUp(inputBlock);
	}
	for (std::size_t sample = 0; sample < renderBlock.getNumSamples(); ++sample)
	{
		float left {}, right {};
		for (auto& voice : voices)
		{
			const auto channelIndex = juce::jlimit(0, 15, voice->getChannel() - 1);
			voice->render(left, right, settings, pitchBendByChannel[static_cast<std::size_t>(channelIndex)]);
		}
		const auto sampleIndex = static_cast<int>(sample);
		renderBlock.setSample(0, sampleIndex, left * outputGain);
		renderBlock.setSample(1, sampleIndex, right * outputGain);
	}
	if (activeQuality == 1)
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
	return preset.soundSchemaVersion != 2 ? juce::Result::fail("Unsupported Mono preset sound schema")
		: presets::PresetSchema::validate(preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds);
}
juce::Result PluginProcessor::applyPreset(const presets::Preset& preset)
{
	if (const auto result = validatePresetSound(preset); result.failed()) return result;
	undoManager.beginNewTransaction("Load preset: " + preset.name);
	const auto result = presets::PresetSchema::apply(preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds, &undoManager);
	if (result.wasOk()) pendingPresetReset.store(true);
	return result;
}
bool PluginProcessor::matchesPresetSound(const presets::Preset& preset) const
{
	return validatePresetSound(preset).wasOk()
		&& presets::PresetSchema::matches(preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds);
}
void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
	stateManager.getMetadata().setProperty("vektPresetSelection", presetSession.selectionState(), nullptr);
	juce::MemoryOutputStream stream(destination, false); stateManager.createState().writeToStream(stream);
}
void PluginProcessor::setStateInformation(const void* data, int size)
{
	if (const auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(size)); state.isValid() && stateManager.restoreState(state))
	{
		presetSession.clear();
		if (stateManager.getMetadata().hasProperty("vektPresetSelection"))
			juce::ignoreUnused(presetSession.restoreSelection(stateManager.getMetadata().getProperty("vektPresetSelection").toString()));
	}
}
}
