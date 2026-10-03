#include "KobberSettingsSnapshot.h"

#include "LfoDestinations.h"
#include "KobberParameterChoices.h"
#include "KobberVoice.h"

#include <vekt/kobber/Parameters.h>
#include <vekt/plugin_support/RequireParameter.h>

namespace vekt::kobber
{
namespace
{
[[nodiscard]] float value(const std::atomic<float>* parameter) noexcept { return parameter->load(); }
}

KobberVoiceSettings voiceSettingsFrom(const KobberParameterValues& cached, double transportBpm) noexcept
{
	KobberVoiceSettings settings;
	for (std::size_t index = 0; index < 3; ++index)
	{
		settings.rangeOctaves[index] = oscillatorRanges.at(value(cached.range[index]));
		settings.semitone[index] = value(cached.semitone[index]);
		settings.fine[index] = value(cached.fine[index]);
		settings.octave[index] = value(cached.octave[index]);
		settings.level[index] = value(cached.level[index]) * 0.01f;
		settings.morph[index] = value(cached.morph[index]);
		settings.pulseWidth[index] = value(cached.pulseWidth[index]);
	}
	settings.noiseType = noiseTypes.at(value(cached.noiseType));
	settings.noiseLevel = value(cached.noiseLevel) * 0.01f;
	settings.cutoff = value(cached.filterCutoff);
	settings.resonance = value(cached.filterResonance) * 0.01f;
	settings.tracking = value(cached.filterKeyTracking) * 0.01f;
	settings.envelopeAmount = value(cached.filterEnvelopeAmount) * 0.01f;
	settings.drive = value(cached.filterDrive);
	settings.qCompensation = value(cached.filterQCompensation) >= 0.5f;
	settings.filterMode = value(cached.filterMode);
	settings.filterType = filterTypes.at(value(cached.filterType));
	settings.ampAttack = value(cached.ampAttack);
	settings.ampDecay = value(cached.ampDecay);
	settings.ampSustain = value(cached.ampSustain) * 0.01f;
	settings.ampRelease = value(cached.ampRelease);
	settings.filterAttack = value(cached.filterAttack);
	settings.filterDecay = value(cached.filterDecay);
	settings.filterSustain = value(cached.filterSustain) * 0.01f;
	settings.filterRelease = value(cached.filterRelease);
	settings.ampVelocity = value(cached.ampVelocity) * 0.01f;
	settings.filterVelocity = value(cached.filterVelocity) * 0.01f;
	settings.calibration = value(cached.calibration);
	settings.unison = unisonCounts.at(value(cached.unison));
	settings.detune = value(cached.unisonDetune);
	settings.unisonSpread = value(cached.unisonSpread) * 0.01f;
	settings.voiceWidth = value(cached.voiceWidth) * 0.01f;
	settings.drift = value(cached.drift);
	settings.glideMode = glideModes.at(value(cached.glideMode));
	settings.glideTime = value(cached.glideTime);
	for (std::size_t index = 0; index < parameters::lfos.size(); ++index)
	{
		const auto& ids = cached.lfos[index];
		auto& lfo = settings.lfo[index];
		const auto division = juce::roundToInt(value(ids.division));
		lfo.source.rateHz = value(ids.sync) >= 0.5f ? syncedLfoRateHz(transportBpm, division) : value(ids.rate);
		lfo.source.shape = lfoShapes.at(value(ids.shape));
		lfo.source.polarity = lfoPolarities.at(value(ids.polarity));
		lfo.source.mode = lfoModes.at(value(ids.mode));
		lfo.source.phase = value(ids.phase) / 360.0f;
		lfo.source.delaySeconds = value(ids.delay);
		lfo.source.fadeSeconds = value(ids.fade);
		lfo.source.drift = settings.drift * 0.01f;
		// Convert each depth to its destination's own units, scaled by the master Amount. The editor's modulation
		// rings use the same table.
		const auto amount = value(ids.amount) * 0.01f;
		const auto& depths = ids.depths;
		const auto offset = [&](std::size_t destination)
		{ return amount * value(depths[destination]) * lfoDestinations[destination].offsetPerDepth; };
		for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
		{
			lfo.pitch[oscillator] = offset(lfo_depth::pitch + oscillator);
			lfo.morph[oscillator] = offset(lfo_depth::morph + oscillator);
			lfo.width[oscillator] = offset(lfo_depth::width + oscillator);
			lfo.level[oscillator] = offset(lfo_depth::level + oscillator);
		}
		lfo.filter = offset(lfo_depth::filter);
		lfo.amp = offset(lfo_depth::amp);
		lfo.drive = offset(lfo_depth::drive);
		lfo.noise = offset(lfo_depth::noise);
		lfo.detune = offset(lfo_depth::detune);
		lfo.spread = offset(lfo_depth::spread);
		lfo.filterMode = offset(lfo_depth::filterMode);
	}
	return settings;
}

KobberParameterValues KobberParameterValues::resolve(juce::AudioProcessorValueTreeState& state)
{
	const auto require = [&state](const char* identifier)
	{ return &plugin_support::requireParameter(state, identifier); };
	static_assert(std::tuple_size_v<decltype(KobberParameterValues::lfos)> == parameters::lfos.size());
	static_assert(std::tuple_size_v<decltype(KobberLfoParameterValues::depths)> == parameters::lfos[0].depths().size());
	KobberParameterValues result;
	const std::array oscillators { std::array { parameters::osc1Range, parameters::osc1Semitone, parameters::osc1Fine,
		                               parameters::osc1Octave, parameters::osc1Level, parameters::osc1Morph,
		                               parameters::osc1PulseWidth },
		std::array { parameters::osc2Range, parameters::osc2Semitone, parameters::osc2Fine, parameters::osc2Octave,
		    parameters::osc2Level, parameters::osc2Morph, parameters::osc2PulseWidth },
		std::array { parameters::osc3Range, parameters::osc3Semitone, parameters::osc3Fine, parameters::osc3Octave,
		    parameters::osc3Level, parameters::osc3Morph, parameters::osc3PulseWidth } };
	for (std::size_t index = 0; index < oscillators.size(); ++index)
	{
		const auto& ids = oscillators[index];
		result.range[index] = require(ids[0]);
		result.semitone[index] = require(ids[1]);
		result.fine[index] = require(ids[2]);
		result.octave[index] = require(ids[3]);
		result.level[index] = require(ids[4]);
		result.morph[index] = require(ids[5]);
		result.pulseWidth[index] = require(ids[6]);
	}
	result.noiseType = require(parameters::noiseType);
	result.noiseLevel = require(parameters::noiseLevel);
	result.filterCutoff = require(parameters::filterCutoff);
	result.filterResonance = require(parameters::filterResonance);
	result.filterKeyTracking = require(parameters::filterKeyTracking);
	result.filterEnvelopeAmount = require(parameters::filterEnvelopeAmount);
	result.filterDrive = require(parameters::filterDrive);
	result.filterQCompensation = require(parameters::filterQCompensation);
	result.filterMode = require(parameters::filterMode);
	result.filterType = require(parameters::filterType);
	result.ampAttack = require(parameters::ampAttack);
	result.ampDecay = require(parameters::ampDecay);
	result.ampSustain = require(parameters::ampSustain);
	result.ampRelease = require(parameters::ampRelease);
	result.filterAttack = require(parameters::filterAttack);
	result.filterDecay = require(parameters::filterDecay);
	result.filterSustain = require(parameters::filterSustain);
	result.filterRelease = require(parameters::filterRelease);
	result.ampVelocity = require(parameters::ampVelocity);
	result.filterVelocity = require(parameters::filterVelocity);
	result.calibration = require(parameters::calibration);
	result.unison = require(parameters::unison);
	result.unisonDetune = require(parameters::unisonDetune);
	result.unisonSpread = require(parameters::unisonSpread);
	result.voiceWidth = require(parameters::voiceWidth);
	result.drift = require(parameters::drift);
	result.glideMode = require(parameters::glideMode);
	result.glideTime = require(parameters::glideTime);
	result.multicore = require(parameters::multicore);
	result.voiceCount = require(parameters::voiceCount);
	result.pitchBendRange = require(parameters::pitchBendRange);
	result.performanceMode = require(parameters::performanceMode);
	result.notePriority = require(parameters::notePriority);
	result.heldKeyReturn = require(parameters::heldKeyReturn);
	result.vibratoRate = require(parameters::vibratoRate);
	result.vibratoShape = require(parameters::vibratoShape);
	result.vibratoDepth = require(parameters::vibratoDepth);
	result.vibratoAmount = require(parameters::vibratoAmount);
	result.masterOutput = require(parameters::masterOutput);
	for (std::size_t index = 0; index < parameters::lfos.size(); ++index)
	{
		const auto& ids = parameters::lfos[index];
		auto& lfo = result.lfos[index];
		lfo = { require(ids.rate), require(ids.sync), require(ids.division), require(ids.shape), require(ids.polarity),
			require(ids.mode), require(ids.phase), require(ids.delay), require(ids.fade), require(ids.amount) };
		const auto depths = ids.depths();
		for (std::size_t destination = 0; destination < depths.size(); ++destination)
			lfo.depths[destination] = require(depths[destination]);
	}
	return result;
}
}
