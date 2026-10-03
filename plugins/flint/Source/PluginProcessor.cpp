#include <vekt/flint/PluginProcessor.h>

#include <vekt/flint/FactoryPresets.h>
#include <vekt/flint/PluginEditor.h>

#include "FlintEngineHost.h"
#include "FlintEngines.h"
#include "HitRandom.h"

#include <vekt/plugin_support/RequireParameter.h>
#include <vekt/presets/PresetPaths.h>
#include <vekt/presets/PresetSchema.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vekt::flint
{
struct PluginProcessor::CachedParameters
{
	explicit CachedParameters(juce::AudioProcessorValueTreeState& state)
	{
		const auto require = [&state](const char* identifier)
		{ return &plugin_support::requireParameter(state, identifier); };
		pitch = require(parameters::pitch);
		attack = require(parameters::attack);
		decay = require(parameters::decay);
		tone = require(parameters::tone);
		drive = require(parameters::drive);
		driveType = require(parameters::driveType);
		level = require(parameters::level);
		velocity = require(parameters::velocity);
		variation = require(parameters::variation);
		noteOffDamps = require(parameters::noteOffDamps);
		mode = require(parameters::mode);
		kickModel = require(parameters::kickModel);
		malletModel = require(parameters::malletModel);
		kickSweep = require(parameters::kickSweep);
		kickSweepTime = require(parameters::kickSweepTime);
		kickClick = require(parameters::kickClick);
		kickBodyShape = require(parameters::kickBodyShape);
		barMaterial = require(parameters::barMaterial);
		barHardness = require(parameters::barHardness);
		barPosition = require(parameters::barPosition);
		barOvertones = require(parameters::barOvertones);
		barResonator = require(parameters::barResonator);
	}

	std::atomic<float>* pitch {};
	std::atomic<float>* attack {};
	std::atomic<float>* decay {};
	std::atomic<float>* tone {};
	std::atomic<float>* drive {};
	std::atomic<float>* driveType {};
	std::atomic<float>* level {};
	std::atomic<float>* velocity {};
	std::atomic<float>* variation {};
	std::atomic<float>* noteOffDamps {};
	std::atomic<float>* mode {};
	std::atomic<float>* kickModel {};
	std::atomic<float>* malletModel {};
	std::atomic<float>* kickSweep {};
	std::atomic<float>* kickSweepTime {};
	std::atomic<float>* kickClick {};
	std::atomic<float>* kickBodyShape {};
	std::atomic<float>* barMaterial {};
	std::atomic<float>* barHardness {};
	std::atomic<float>* barPosition {};
	std::atomic<float>* barOvertones {};
	std::atomic<float>* barResonator {};
};

namespace
{
[[nodiscard]] double percent(const std::atomic<float>* parameter) noexcept
{
	return static_cast<double>(parameter->load()) / 100.0;
}

// A Mode change with its start values, undone as one step (as PresetSchema applies a preset).
class ModeChange final : public juce::UndoableAction
{
public:
	struct Value
	{
		juce::RangedAudioParameter* parameter {};
		float before {};
		float after {};
	};

	explicit ModeChange(std::vector<Value> newValues) : values(std::move(newValues)) {}

	bool perform() override
	{
		apply(false);
		return true;
	}

	bool undo() override
	{
		apply(true);
		return true;
	}

private:
	void apply(bool previous)
	{
		for (const auto& value : values)
		{
			value.parameter->beginChangeGesture();
			value.parameter->setValueNotifyingHost(
			    value.parameter->convertTo0to1(previous ? value.before : value.after));
			value.parameter->endChangeGesture();
		}
	}

	std::vector<Value> values;
};

[[nodiscard]] std::uint64_t randomSeed()
{
	auto& random = juce::Random::getSystemRandom();
	return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(random.nextInt())) << 32) |
	    static_cast<std::uint32_t>(random.nextInt());
}
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameterState(*this, &undoManager, parameters::stateType, parameters::createLayout()),
      presetHost(parameterState,
          { parameters::presetProductIdentifier, parameters::productName, parameters::presetSoundSchemaVersion },
          { [this](const juce::String& name) { return createPreset(name); },
              [](presets::Preset& preset)
              {
	              return preset.soundSchemaVersion == parameters::presetSoundSchemaVersion
	                  ? juce::Result::ok()
	                  : juce::Result::fail("Unsupported Flint preset sound schema");
              },
              [this](const presets::Preset& preset) { return validatePresetSound(preset); },
              [this](const presets::Preset& preset) { return applyPreset(preset); },
              [this](const presets::Preset& preset) { return matchesPresetSound(preset); } }),
      qualitySelection(parameterState, parameters::trackingOversampling, parameters::offlineOversampling),
      cached(std::make_unique<CachedParameters>(parameterState)),
      engineHost(std::make_unique<FlintEngineHost>(makeFlintEngines()))
{
	storeSeed(randomSeed());
	const auto factoryResult = addFactoryPresets(presetHost.catalog());
	jassert(factoryResult.wasOk());
	juce::ignoreUnused(factoryResult);
	juce::ignoreUnused(configureUserPresetDirectory(presets::PresetPaths::desktop(parameters::productName)));
	presets::Preset initialPreset;
	if (presetHost.catalog().loadFactoryPreset(0, initialPreset).wasOk() && matchesPresetSound(initialPreset))
		presetHost.session().adopt(initialPreset, presets::PresetOrigin::factory);
}

PluginProcessor::~PluginProcessor() = default;

void PluginProcessor::prepareToPlay(double newSampleRate, int maximumBlockSize)
{
	sampleRateHz = newSampleRate;
	preparedBlockSize = std::max(maximumBlockSize, 1);
	outputScope.prepare(sampleRateHz);
	oversampling.prepare(static_cast<std::size_t>(preparedBlockSize));
	oversampling.activate(qualitySelection.prepare(isNonRealtime()));
	// Buffers for the largest factor, so a later quality change allocates nothing.
	engineHost->prepare(sampleRateHz * static_cast<double>(oversampling.getActiveFactor()),
	    preparedBlockSize * static_cast<int>(dsp::maximumOversamplingFactor), sampleRateHz);
	engineHost->update(snapshot(), true);
	configureInternalRate();
}

void PluginProcessor::releaseResources()
{
	qualitySelection.release();
	preparedBlockSize = 0;
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
	return layouts.getMainInputChannelSet().isDisabled() &&
	    layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::configureInternalRate() noexcept
{
	engineHost->setInternalSampleRate(sampleRateHz * static_cast<double>(oversampling.getActiveFactor()));
	const auto latency = oversampling.getActiveLatencySamples();
	// The downsampling filter rings for about twice its latency after the internal path falls silent.
	engineHost->setDownsamplingTail(2 * latency + 64);
	setLatencySamples(latency);
	oversampling.reset();
}

void PluginProcessor::applyPendingQualityChange() noexcept
{
	// Applies at once, during playback too; the audio may drop at the switch (ADR 0010).
	const auto quality = qualitySelection.takeRequest(isNonRealtime());
	if (!quality) return;
	oversampling.activate(*quality);
	configureInternalRate();
}

FlintParameters PluginProcessor::snapshot() const noexcept
{
	FlintParameters result;
	result.mode = modes.at(cached->mode->load());
	result.modelIndex[static_cast<std::size_t>(Mode::kick)] = juce::roundToInt(cached->kickModel->load());
	result.modelIndex[static_cast<std::size_t>(Mode::mallet)] = juce::roundToInt(cached->malletModel->load());
	result.pitch = static_cast<double>(cached->pitch->load());
	result.attack = percent(cached->attack);
	result.decay = percent(cached->decay);
	result.tone = percent(cached->tone);
	result.drive = percent(cached->drive);
	result.driveType = driveTypes.at(cached->driveType->load());
	result.levelDecibels = static_cast<double>(cached->level->load());
	result.variation = percent(cached->variation);
	result.noteOffDamps = cached->noteOffDamps->load() >= 0.5f;
	result.kickClassicAnalog = { percent(cached->kickSweep), percent(cached->kickSweepTime), percent(cached->kickClick),
		percent(cached->kickBodyShape) };
	result.malletBar = { percent(cached->barMaterial), percent(cached->barHardness), percent(cached->barPosition),
		percent(cached->barOvertones), percent(cached->barResonator) };
	return result;
}

void PluginProcessor::readTransport() noexcept
{
	blockStartBeats.reset();
	if (const auto* playHead = getPlayHead())
		if (const auto position = playHead->getPosition(); position && position->getIsPlaying())
			if (const auto beats = position->getPpqPosition())
			{
				const auto bpm = position->getBpm().orFallback(120.0);
				samplesPerBeat = sampleRateHz * 60.0 / std::max(bpm, 1.0);
				blockStartBeats = *beats;
			}
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	juce::ScopedNoDenormals noDenormals;
	buffer.clear();
	if (preparedBlockSize <= 0) return;
	applyPendingQualityChange();
	if (presetLoaded.exchange(false))
	{
		// Preset loads must not leak the previous sound, from the engines or the oversampling filters.
		engineHost->resetAudio();
		oversampling.reset();
	}
	// The hold is read first: a preset load that starts while the snapshot is taken keeps the selection this block.
	const auto selectionMayChange = !presetLoading.load();
	engineHost->update(snapshot(), selectionMayChange);
	readTransport();
	lastHitSample = -1;
	hitOrder = 0;

	auto position = 0;
	for (const auto metadata : midi)
	{
		const auto eventPosition = std::clamp(metadata.samplePosition, 0, buffer.getNumSamples());
		render(buffer, position, eventPosition - position);
		handleMidi(metadata.getMessage(), eventPosition);
		position = eventPosition;
	}
	render(buffer, position, buffer.getNumSamples() - position);
	silent.store(engineHost->isSleeping(), std::memory_order_relaxed);
	outputMeter.publish(buffer);
	outputScope.publish(buffer);
}

void PluginProcessor::handleMidi(const juce::MidiMessage& message, int samplePosition) noexcept
{
	if (message.isNoteOn())
	{
		const auto sensitivity = percent(cached->velocity);
		const auto velocity = 1.0 - sensitivity + sensitivity * static_cast<double>(message.getFloatVelocity());
		std::uint64_t hash {};
		if (blockStartBeats)
		{
			hitOrder = samplePosition == lastHitSample ? hitOrder + 1 : 0;
			lastHitSample = samplePosition;
			const auto beats = *blockStartBeats + static_cast<double>(samplePosition) / samplesPerBeat;
			hash = hitHash(seed.load(std::memory_order_relaxed), songPositionKey(beats), hitOrder);
		}
		else
			hash = stoppedHitHash(seed.load(std::memory_order_relaxed), stoppedCounter++);
		engineHost->trigger({ velocity, message.getNoteNumber(), hash });
	}
	else if (message.isNoteOff())
		engineHost->release(message.getNoteNumber());
	else if (message.isAllSoundOff())
	{
		engineHost->resetAudio();
		oversampling.reset();
	}
}

void PluginProcessor::render(juce::AudioBuffer<float>& buffer, int start, int count) noexcept
{
	for (auto offset = 0; offset < count; offset += preparedBlockSize)
	{
		if (engineHost->isSleeping()) return; // the buffer is already cleared: exact zeros
		const auto size = std::min(preparedBlockSize, count - offset);
		juce::dsp::AudioBlock<float> block(buffer);
		auto segment = block.getSubBlock(
		    static_cast<std::size_t>(start) + static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
		if (oversampling.getActiveFactor() > 1)
		{
			auto internal = oversampling.processSamplesUp(juce::dsp::AudioBlock<const float>(segment));
			engineHost->renderInternal(std::span(internal.getChannelPointer(0), internal.getNumSamples()),
			    std::span(internal.getChannelPointer(1), internal.getNumSamples()));
			oversampling.processSamplesDown(segment);
		}
		else
			engineHost->renderInternal(std::span(segment.getChannelPointer(0), segment.getNumSamples()),
			    std::span(segment.getChannelPointer(1), segment.getNumSamples()));
		engineHost->finishHost(std::span(segment.getChannelPointer(0), segment.getNumSamples()),
		    std::span(segment.getChannelPointer(1), segment.getNumSamples()));
		if (engineHost->isSleeping()) oversampling.reset();
	}
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }

int PluginProcessor::getNumPrograms() { return presetHost.numPrograms(); }
int PluginProcessor::getCurrentProgram() { return presetHost.currentProgram(); }
void PluginProcessor::setCurrentProgram(int index) { presetHost.selectProgram(index); }
const juce::String PluginProcessor::getProgramName(int index) { return presetHost.programName(index); }
juce::Result PluginProcessor::loadNextPreset() { return presetHost.loadAdjacentPreset(true); }
juce::Result PluginProcessor::loadPreviousPreset() { return presetHost.loadAdjacentPreset(false); }

void PluginProcessor::getStateInformation(juce::MemoryBlock& destination) { presetHost.save(destination); }

void PluginProcessor::setStateInformation(const void* data, int size)
{
	// Hosts may call this from any thread: restore and the seed read stay together under the state lock.
	const auto locked = presetHost.lockState();
	if (!presetHost.restore(data, size)) return;
	// A project saved with a seed keeps it; one without keeps this instance's.
	const auto stored = presetHost.metadataValue(seedProperty).toString();
	if (stored.isNotEmpty())
		seed.store(static_cast<std::uint64_t>(stored.getHexValue64()));
	else
		storeSeed(seed.load());
}

void PluginProcessor::storeSeed(std::uint64_t value)
{
	seed.store(value);
	presetHost.setMetadataValue(seedProperty, juce::String::toHexString(static_cast<juce::int64>(value)));
}

void PluginProcessor::newSeed() { storeSeed(randomSeed()); }

void PluginProcessor::selectMode(Mode selected)
{
	const auto start = parameters::startValuesFor(selected);
	std::vector<ModeChange::Value> values;
	const auto add = [&](const char* identifier, float after)
	{
		auto* parameter = parameterState.getParameter(identifier);
		values.push_back({ parameter, parameter->convertFrom0to1(parameter->getValue()), after });
	};
	add(parameters::mode, static_cast<float>(modes.indexOf(selected)));
	add(parameters::pitch, start.pitch);
	add(parameters::attack, start.attack);
	add(parameters::decay, start.decay);
	add(parameters::tone, start.tone);
	// Flush pending parameter changes into the state first, so an earlier edit is not recorded in this step.
	juce::ignoreUnused(parameterState.copyState());
	undoManager.beginNewTransaction("Mode: " + juce::String(modes.nameOf(selected)));
	undoManager.perform(new ModeChange(std::move(values)));
}

presets::Preset PluginProcessor::createPreset(const juce::String& name) const
{
	auto preset = presets::PresetSchema::create(
	    parameters::presetProductIdentifier, name, parameterState, parameters::soundParameterIds);
	preset.soundSchemaVersion = parameters::presetSoundSchemaVersion;
	return preset;
}

juce::Result PluginProcessor::applyPreset(const presets::Preset& preset)
{
	if (const auto result = validatePresetSound(preset); result.failed()) return result;
	auto* const undo = plugin_support::editorUndo(undoManager);
	if (undo != nullptr)
	{
		// Flushes pending parameter values into the history first, so undo returns to them.
		juce::ignoreUnused(parameterState.copyState());
		undo->beginNewTransaction("Load preset: " + preset.name);
	}
	// The engine host holds its selection until every parameter is in place, then starts clean.
	presetLoading.store(true);
	const auto result = presets::PresetSchema::apply(
	    preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds, undo);
	presetLoading.store(false);
	presetLoaded.store(true);
	if (result.wasOk()) presetHost.session().clear();
	return result;
}

juce::Result PluginProcessor::configureUserPresetDirectory(const juce::File& directory)
{
	return presetHost.configureUserPresetDirectory(directory);
}

juce::Result PluginProcessor::validatePresetSound(const presets::Preset& preset) const
{
	if (preset.soundSchemaVersion != parameters::presetSoundSchemaVersion)
		return juce::Result::fail("Unsupported Flint preset sound schema");
	return presets::PresetSchema::validate(
	    preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds);
}

bool PluginProcessor::matchesPresetSound(const presets::Preset& preset) const
{
	return validatePresetSound(preset).wasOk() &&
	    presets::PresetSchema::matches(
	        preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds);
}
}
