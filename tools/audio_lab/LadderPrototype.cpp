#include "LadderPrototype.h"

#include "DelayedFeedbackLadder.h"
#include "NonlinearTptLadderReference.h"

#include <vekt/audio_analysis/Measurements.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

namespace vekt::audio_lab
{
namespace
{
struct ProbeSettings
{
	float cutoffHz { 1'000.0f };
	float resonance {};
	float driveDecibels {};
	bool driveCompensation {};
};

struct Projection
{
	double fundamentalGain {};
	double phaseRadians {};
	double secondHarmonicGain {};
	double thirdHarmonicGain {};
};

struct ResponsePoint
{
	float frequency {};
	double gainDecibels {};
};

double gainToDecibels(double gain)
{
	return audio_analysis::gainToDecibels(gain);
}

double wrappedPhaseDifference(double candidate, double current)
{
	return audio_analysis::wrapPhase(candidate - current);
}

template <typename Probe>
Projection measureSine(double sampleRate, float frequency, float amplitude,
	const ProbeSettings& settings, juce::var& diagnostics);

template <typename Probe>
double measureMinus3DbFrequency(double sampleRate, float cutoffHz)
{
	constexpr int pointCount = 18;
	const auto minimumFrequency = std::max(20.0, static_cast<double>(cutoffHz) * 0.125);
	const auto maximumFrequency = std::min(sampleRate * 0.4, static_cast<double>(cutoffHz) * 2.5);
	const auto logarithmicRange = std::log(maximumFrequency / minimumFrequency);
	ResponsePoint previous;
	for (int index = 0; index < pointCount; ++index)
	{
		const auto fraction = static_cast<double>(index) / static_cast<double>(pointCount - 1);
		const auto frequency = minimumFrequency * std::exp(logarithmicRange * fraction);
		juce::var diagnostics;
		const auto measurement = measureSine<Probe>(sampleRate, static_cast<float>(frequency),
			0.001f, { cutoffHz, 0.0f, 0.0f }, diagnostics);
		const ResponsePoint point { static_cast<float>(frequency),
			gainToDecibels(measurement.fundamentalGain) };
		if (index > 0 && previous.gainDecibels > -3.0 && point.gainDecibels <= -3.0)
		{
			const auto interpolation = (-3.0 - previous.gainDecibels)
				/ (point.gainDecibels - previous.gainDecibels);
			return std::exp(std::log(static_cast<double>(previous.frequency))
				+ interpolation * (std::log(static_cast<double>(point.frequency))
					- std::log(static_cast<double>(previous.frequency))));
		}
		previous = point;
	}
	return 0.0;
}

class CandidateProbe
{
public:
	explicit CandidateProbe(double sampleRate) { ladder.prepare(sampleRate); }

	float process(float input, const ProbeSettings& settings)
	{
		return ladder.process(input, { settings.cutoffHz, settings.resonance,
			settings.driveDecibels, settings.driveCompensation });
	}

	juce::var diagnostics() const
	{
		const auto& source = ladder.diagnostics();
		auto* object = new juce::DynamicObject;
		object->setProperty("samples", static_cast<juce::int64>(source.samples));
		object->setProperty("unconverged_samples", static_cast<juce::int64>(source.unconvergedSamples));
		object->setProperty("non_finite_samples", static_cast<juce::int64>(source.nonFiniteSamples));
		object->setProperty("maximum_feedback_iterations", source.maximumFeedbackIterations);
		object->setProperty("maximum_stage_iterations", source.maximumStageIterations);
		object->setProperty("maximum_residual", source.maximumResidual);
		return juce::var(object);
	}

private:
	NonlinearTptLadder ladder;
};

class CurrentProbe
{
public:
	explicit CurrentProbe(double newSampleRate) : sampleRate(static_cast<float>(newSampleRate)) {}

	float process(float input, const ProbeSettings& settings)
	{
		const auto driveGain = std::pow(10.0f, settings.driveDecibels / 20.0f);
		const auto output = ladder.process(input, settings.cutoffHz, sampleRate,
			settings.resonance, driveGain);
		++samples;
		if (!std::isfinite(output)) ++nonFiniteSamples;
		peak = std::max(peak, std::abs(output));
		return std::isfinite(output) ? output : 0.0f;
	}

	juce::var diagnostics() const
	{
		auto* object = new juce::DynamicObject;
		object->setProperty("samples", static_cast<juce::int64>(samples));
		object->setProperty("non_finite_samples", static_cast<juce::int64>(nonFiniteSamples));
		object->setProperty("peak", peak);
		return juce::var(object);
	}

private:
	mono::DelayedFeedbackLadder ladder;
	float sampleRate {};
	std::uint64_t samples {}, nonFiniteSamples {};
	float peak {};
};

class OfflineReferenceProbe
{
public:
	explicit OfflineReferenceProbe(double sampleRate)
	{
		ladder.prepare(sampleRate, referenceSubsteps);
	}

	double process(float input, const ProbeSettings& settings)
	{
		return ladder.process(static_cast<double>(input), {
			settings.cutoffHz, settings.resonance, settings.driveDecibels,
			settings.driveCompensation });
	}

	juce::var diagnostics() const
	{
		const auto& source = ladder.diagnostics();
		auto* object = new juce::DynamicObject;
		object->setProperty("reference_substeps", referenceSubsteps);
		object->setProperty("host_samples", static_cast<juce::int64>(source.hostSamples));
		object->setProperty("internal_steps", static_cast<juce::int64>(source.internalSteps));
		object->setProperty("unconverged_steps", static_cast<juce::int64>(source.unconvergedSteps));
		object->setProperty("non_finite_steps", static_cast<juce::int64>(source.nonFiniteSteps));
		object->setProperty("maximum_feedback_iterations", source.maximumFeedbackIterations);
		object->setProperty("maximum_stage_iterations", source.maximumStageIterations);
		object->setProperty("maximum_residual", source.maximumResidual);
		return juce::var(object);
	}

private:
	static constexpr int referenceSubsteps = 16;
	NonlinearTptLadderOfflineReference ladder;
};

template <typename Probe>
Projection measureSine(double sampleRate, float frequency, float amplitude,
	const ProbeSettings& settings, juce::var& diagnostics)
{
	Probe probe(sampleRate);
	const auto settleSamples = static_cast<int>(std::lround(sampleRate * 0.25));
	const auto measurementSamples = static_cast<int>(std::lround(sampleRate * 0.5));
	const auto measurementStart = static_cast<std::size_t>(settleSamples);
	audio_analysis::SinusoidalProjector inputProjection(sampleRate, frequency, measurementStart);
	audio_analysis::SinusoidalProjector outputProjection(sampleRate, frequency, measurementStart);
	audio_analysis::SinusoidalProjector secondProjection(sampleRate, frequency, measurementStart, 2.0);
	audio_analysis::SinusoidalProjector thirdProjection(sampleRate, frequency, measurementStart, 3.0);
	for (int sample = 0; sample < settleSamples + measurementSamples; ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(sample) / sampleRate;
		const auto input = amplitude * static_cast<float>(std::sin(phase));
		const auto output = probe.process(input, settings);
		if (sample < settleSamples) continue;
		inputProjection.add(input);
		outputProjection.add(output);
		secondProjection.add(output);
		thirdProjection.add(output);
	}
	diagnostics = probe.diagnostics();
	const auto input = inputProjection.result();
	const auto output = outputProjection.result();
	const auto inputMagnitude = input.magnitude();
	const auto outputMagnitude = output.magnitude();
	Projection result;
	result.fundamentalGain = inputMagnitude > 0.0 ? outputMagnitude / inputMagnitude : 0.0;
	result.phaseRadians = audio_analysis::wrapPhase(output.phaseRadians() - input.phaseRadians());
	result.secondHarmonicGain = inputMagnitude > 0.0 ? secondProjection.result().magnitude() / inputMagnitude : 0.0;
	result.thirdHarmonicGain = inputMagnitude > 0.0 ? thirdProjection.result().magnitude() / inputMagnitude : 0.0;
	return result;
}

void setDiagnostics(juce::DynamicObject& object, const juce::var& diagnostics,
	bool includeSolverAlias)
{
	object.setProperty("stability", diagnostics);
	if (includeSolverAlias) object.setProperty("solver", diagnostics);
}

juce::var ringdownReport(const juce::AudioBuffer<float>& audio, double sampleRate,
	const std::array<juce::var, 2>& diagnostics, bool includeSolverAlias)
{
	juce::Array<juce::var> ringdown;
	for (int channel = 0; channel < 2; ++channel)
	{
		const std::span samples(audio.getReadPointer(channel),
			static_cast<std::size_t>(audio.getNumSamples()));
		const auto measurement = audio_analysis::measureRingdown<float>(samples, sampleRate);
		auto* object = new juce::DynamicObject;
		object->setProperty("input_peak", channel == 0 ? 0.05 : 1.0);
		object->setProperty("peak", measurement.peak);
		object->setProperty("onset_sample", static_cast<juce::int64>(measurement.onsetSample));
		object->setProperty("onset_seconds", static_cast<double>(measurement.onsetSample) / sampleRate);
		object->setProperty("peak_sample", static_cast<juce::int64>(measurement.peakSample));
		object->setProperty("extinction_sample", static_cast<juce::int64>(measurement.extinctionSample));
		object->setProperty("extinction_seconds", static_cast<double>(measurement.extinctionSample) / sampleRate);
		object->setProperty("extinguished", measurement.extinguished);
		object->setProperty("estimated_frequency_hz", measurement.estimatedFrequencyHz);
		setDiagnostics(*object, diagnostics[static_cast<std::size_t>(channel)], includeSolverAlias);
		ringdown.add(juce::var(object));
	}
	return ringdown;
}

juce::var validationContractReport()
{
	auto* contract = new juce::DynamicObject;
	contract->setProperty("product_direction", "hybrid-classic-ladder-modern-features");
	contract->setProperty("ladder_output", "raw-fourth-stage");
	contract->setProperty("drive_compensation", "optional-bypassable-post-ladder");
	contract->setProperty("q_compensation", "optional-default-off-post-ladder");
	contract->setProperty("nominal_self_oscillation_resonance", 1.0);
	contract->setProperty("fallback_policy", "zero-incidence-required-or-continuous-replacement");
	contract->setProperty("coverage_status", "representative-points-only");
	contract->setProperty("production_integration_allowed", false);
	contract->setProperty("required_control_modulation",
		juce::Array<juce::var> { "cutoff", "resonance", "drive" });
	contract->setProperty("quality_candidates",
		juce::Array<juce::var> { "Off", "2x", "4x" });
	contract->setProperty("deferred_features", juce::Array<juce::var> {
		"oscillator-3/noise-wheel-modulation", "mono-articulation-policy",
		"mixer/vca-nonlinearity", "dedicated-lfo", "output-feedback" });
	return juce::var(contract);
}

juce::var validationMatrixReport()
{
	auto* matrix = new juce::DynamicObject;
	matrix->setProperty("sample_rates_hz",
		juce::Array<juce::var> { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 });
	matrix->setProperty("cutoff_positions", juce::Array<juce::var> {
		"10-hz-floor", "100-hz", "1-khz", "quarter-rate", "0.45-rate-ceiling" });
	matrix->setProperty("resonance", juce::Array<juce::var> {
		0.0, 0.5, 0.85, 0.95, 0.98, 1.0 });
	matrix->setProperty("input_peaks", juce::Array<juce::var> {
		0.000001, 0.001, 0.05, 0.5, 1.0, 4.0 });
	matrix->setProperty("drive_decibels", juce::Array<juce::var> {
		0.0, 6.0, 12.0, 24.0 });
	matrix->setProperty("block_sizes", juce::Array<juce::var> {
		1, 16, 31, 32, 127, 128, 257 });
	matrix->setProperty("stimuli", juce::Array<juce::var> {
		"silence", "dc", "positive-impulse", "negative-impulse", "sine",
		"sweep", "deterministic-noise", "two-tone", "modulated-tone" });
	matrix->setProperty("controls", juce::Array<juce::var> {
		"static", "cutoff-modulated", "resonance-modulated", "drive-modulated",
		"simultaneously-modulated" });
	matrix->setProperty("quality_candidates",
		juce::Array<juce::var> { "Off", "2x", "4x" });
	matrix->setProperty("completion", "not-complete");
	return juce::var(matrix);
}

juce::var provisionalAcceptanceReport()
{
	auto* limits = new juce::DynamicObject;
	limits->setProperty("status", "provisional-not-adr-acceptance");
	limits->setProperty("low_level_gain_error_db", 0.01);
	limits->setProperty("representative_48khz_deep_stopband_gain_error_db", 1.0);
	limits->setProperty("low_level_phase_error_radians", 0.001);
	limits->setProperty("deep_stopband_phase_error_radians", 0.025);
	limits->setProperty("nonlinear_reference_fundamental_error_db", 0.03);
	limits->setProperty("nonlinear_reference_second_component_error", 1.0e-5);
	limits->setProperty("nonlinear_reference_third_component_error", 8.0e-4);
	limits->setProperty("candidate_solver_residual", 2.0e-7);
	limits->setProperty("offline_reference_solver_residual", 1.0e-13);
	limits->setProperty("permitted_unconverged_samples", 0);
	limits->setProperty("permitted_non_finite_samples", 0);
	limits->setProperty("stopband_policy",
		"unresolved-judge-absolute-output-error-and-relative-decibels-across-supported-rates");
	return juce::var(limits);
}

juce::var twoToneReport(double sampleRate)
{
	constexpr double firstFrequency = 300.0;
	constexpr double secondFrequency = 500.0;
	const auto settleSamples = static_cast<std::size_t>(std::lround(sampleRate * 0.25));
	const auto measurementSamples = static_cast<std::size_t>(std::lround(sampleRate * 0.5));
	CandidateProbe probe(sampleRate);
	std::vector<float> output(measurementSamples);
	const ProbeSettings settings { 1'000.0f, 0.85f, 12.0f, false };
	for (std::size_t sample = 0; sample < settleSamples + measurementSamples; ++sample)
	{
		const auto time = static_cast<double>(sample) / sampleRate;
		const auto input = 0.25f * static_cast<float>(
			std::sin(2.0 * std::numbers::pi * firstFrequency * time)
			+ std::sin(2.0 * std::numbers::pi * secondFrequency * time));
		const auto value = probe.process(input, settings);
		if (sample >= settleSamples) output[sample - settleSamples] = value;
	}
	const auto components = audio_analysis::measureTwoToneComponents<float>(
		output, sampleRate, firstFrequency, secondFrequency, settleSamples);
	constexpr std::array retained { firstFrequency, secondFrequency,
		2.0 * firstFrequency - secondFrequency, 2.0 * secondFrequency - firstFrequency,
		secondFrequency - firstFrequency, firstFrequency + secondFrequency };
	const auto residual = audio_analysis::measureComponentResidual<float>(
		output, sampleRate, retained, settleSamples);
	auto* report = new juce::DynamicObject;
	report->setProperty("first_frequency_hz", firstFrequency);
	report->setProperty("second_frequency_hz", secondFrequency);
	report->setProperty("input_peak_per_tone", 0.25);
	report->setProperty("first_fundamental", components.firstFundamental);
	report->setProperty("second_fundamental", components.secondFundamental);
	report->setProperty("lower_third_order", components.lowerThirdOrder);
	report->setProperty("upper_third_order", components.upperThirdOrder);
	report->setProperty("difference_component", components.difference);
	report->setProperty("sum_component", components.sum);
	report->setProperty("unattributed_residual_rms", residual.rms);
	report->setProperty("unattributed_residual_peak", residual.peak);
	report->setProperty("solver", probe.diagnostics());
	return juce::var(report);
}

juce::var stopbandToleranceReport(double sampleRate)
{
	constexpr std::array<float, 3> frequencies { 4'000.0f, 8'000.0f, 12'000.0f };
	constexpr float inputPeak = 0.001f;
	const ProbeSettings settings { 500.0f, 0.0f, 0.0f, false };
	juce::Array<juce::var> points;
	for (const auto requestedFrequency : frequencies)
	{
		const auto frequency = std::min(requestedFrequency,
			static_cast<float>(sampleRate * 0.4));
		juce::var diagnostics;
		const auto measured = measureSine<CandidateProbe>(sampleRate, frequency,
			inputPeak, settings, diagnostics);
		const auto analytical = nonlinearTptLadderDiscreteResponse(sampleRate, frequency,
			{ settings.cutoffHz, settings.resonance, settings.driveDecibels,
				settings.driveCompensation });
		const auto analyticalGain = std::abs(analytical);
		auto* point = new juce::DynamicObject;
		point->setProperty("frequency_hz", frequency);
		point->setProperty("measured_gain", measured.fundamentalGain);
		point->setProperty("analytical_gain", analyticalGain);
		point->setProperty("absolute_output_peak_error",
			inputPeak * std::abs(measured.fundamentalGain - analyticalGain));
		point->setProperty("relative_gain_error_db",
			gainToDecibels(measured.fundamentalGain) - gainToDecibels(analyticalGain));
		point->setProperty("solver", diagnostics);
		points.add(juce::var(point));
	}
	auto* report = new juce::DynamicObject;
	report->setProperty("input_peak", inputPeak);
	report->setProperty("cutoff_hz", settings.cutoffHz);
	report->setProperty("points", points);
	report->setProperty("interpretation",
		"Relative decibel error is diagnostic only when the analytical output is deeply attenuated; acceptance also uses absolute output error.");
	return juce::var(report);
}

template <typename Probe>
juce::var modelReport(double sampleRate, int blockSize, juce::AudioBuffer<float>* ringdownAudio)
{
	constexpr std::array<float, 8> frequencies { 125.0f, 250.0f, 500.0f, 750.0f,
		1'000.0f, 1'500.0f, 2'000.0f, 4'000.0f };
	constexpr auto includeSolverAlias = std::is_same_v<Probe, CandidateProbe>;
	auto* model = new juce::DynamicObject;
	juce::Array<juce::var> response;
	std::array<ResponsePoint, frequencies.size()> responsePoints {};
	std::size_t responseIndex {};
	const ProbeSettings responseSettings { 1'000.0f, 0.0f, 0.0f };
	for (const auto frequency : frequencies)
	{
		juce::var diagnostics;
		const auto measurement = measureSine<Probe>(sampleRate, frequency, 0.001f,
			responseSettings, diagnostics);
		const auto gainDecibels = 20.0 * std::log10(std::max(1.0e-12, measurement.fundamentalGain));
		responsePoints[responseIndex++] = { frequency, gainDecibels };
		auto* object = new juce::DynamicObject;
		object->setProperty("frequency_hz", frequency);
		object->setProperty("gain", measurement.fundamentalGain);
		object->setProperty("gain_db", gainDecibels);
		object->setProperty("phase_radians", measurement.phaseRadians);
		if constexpr (std::is_same_v<Probe, CandidateProbe>)
		{
			const auto reference = nonlinearTptLadderDiscreteResponse(sampleRate, frequency,
				{ responseSettings.cutoffHz, responseSettings.resonance, responseSettings.driveDecibels });
			const auto referenceGain = std::abs(reference);
			const auto referencePhase = std::arg(reference);
			object->setProperty("analytical_gain", referenceGain);
			object->setProperty("analytical_gain_db", gainToDecibels(referenceGain));
			object->setProperty("analytical_phase_radians", referencePhase);
			object->setProperty("measured_minus_analytical_gain_db",
				gainDecibels - gainToDecibels(referenceGain));
			object->setProperty("measured_minus_analytical_phase_radians",
				wrappedPhaseDifference(measurement.phaseRadians, referencePhase));
		}
		setDiagnostics(*object, diagnostics, includeSolverAlias);
		response.add(juce::var(object));
	}
	model->setProperty("frequency_response", response);
	double measuredCutoff {};
	for (std::size_t index = 1; index < responsePoints.size(); ++index)
	{
		const auto& lower = responsePoints[index - 1];
		const auto& upper = responsePoints[index];
		if (lower.gainDecibels > -3.0 && upper.gainDecibels <= -3.0)
		{
			const auto fraction = (-3.0 - lower.gainDecibels)
				/ (upper.gainDecibels - lower.gainDecibels);
			measuredCutoff = std::exp(std::log(static_cast<double>(lower.frequency))
				+ fraction * (std::log(static_cast<double>(upper.frequency))
					- std::log(static_cast<double>(lower.frequency))));
			break;
		}
	}
	auto* calibration = new juce::DynamicObject;
	calibration->setProperty("control_hz", responseSettings.cutoffHz);
	calibration->setProperty("measured_minus_3_db_hz", measuredCutoff);
	calibration->setProperty("ratio", measuredCutoff / responseSettings.cutoffHz);
	model->setProperty("cutoff_calibration", juce::var(calibration));

	juce::Array<juce::var> levelResponse;
	for (const auto amplitude : { 0.001f, 0.05f, 0.5f, 1.0f })
	{
		juce::var diagnostics;
		const auto measurement = measureSine<Probe>(sampleRate, 500.0f, amplitude,
			{ 1'000.0f, 0.85f, 6.0f }, diagnostics);
		auto* object = new juce::DynamicObject;
		object->setProperty("input_peak", amplitude);
		object->setProperty("fundamental_gain", measurement.fundamentalGain);
		object->setProperty("second_harmonic_relative", measurement.secondHarmonicGain);
		object->setProperty("third_harmonic_relative", measurement.thirdHarmonicGain);
		if constexpr (std::is_same_v<Probe, CandidateProbe>)
		{
			juce::var referenceDiagnostics;
			const auto reference = measureSine<OfflineReferenceProbe>(sampleRate, 500.0f, amplitude,
				{ 1'000.0f, 0.85f, 6.0f }, referenceDiagnostics);
			object->setProperty("offline_reference_fundamental_gain", reference.fundamentalGain);
			object->setProperty("offline_reference_second_harmonic_relative", reference.secondHarmonicGain);
			object->setProperty("offline_reference_third_harmonic_relative", reference.thirdHarmonicGain);
			object->setProperty("candidate_minus_offline_reference_fundamental_gain_db",
				gainToDecibels(measurement.fundamentalGain) - gainToDecibels(reference.fundamentalGain));
			object->setProperty("candidate_minus_offline_reference_second_harmonic",
				measurement.secondHarmonicGain - reference.secondHarmonicGain);
			object->setProperty("candidate_minus_offline_reference_third_harmonic",
				measurement.thirdHarmonicGain - reference.thirdHarmonicGain);
			object->setProperty("offline_reference", referenceDiagnostics);
		}
		setDiagnostics(*object, diagnostics, includeSolverAlias);
		levelResponse.add(juce::var(object));
	}
	model->setProperty("level_response", levelResponse);

	juce::AudioBuffer<float> localRingdown(2, static_cast<int>(std::lround(sampleRate * 2.0)));
	localRingdown.clear();
	std::array<Probe, 2> probes { Probe(sampleRate), Probe(sampleRate) };
	const ProbeSettings ringdownSettings { 1'000.0f, 0.98f, 0.0f };
	for (int blockStart = 0; blockStart < localRingdown.getNumSamples(); blockStart += blockSize)
	{
		const auto blockEnd = std::min(localRingdown.getNumSamples(), blockStart + blockSize);
		for (int sample = blockStart; sample < blockEnd; ++sample)
		{
			localRingdown.setSample(0, sample,
				probes[0].process(sample == 0 ? 0.05f : 0.0f, ringdownSettings));
			localRingdown.setSample(1, sample,
				probes[1].process(sample == 0 ? 1.0f : 0.0f, ringdownSettings));
		}
	}
	const std::array<juce::var, 2> diagnostics { probes[0].diagnostics(), probes[1].diagnostics() };
	model->setProperty("ringdown",
		ringdownReport(localRingdown, sampleRate, diagnostics, includeSolverAlias));
	if (ringdownAudio != nullptr) *ringdownAudio = localRingdown;
	return juce::var(model);
}

juce::var comparisonReport(const juce::var& current, const juce::var& candidate)
{
	auto* comparison = new juce::DynamicObject;
	const auto currentCutoff = static_cast<double>(current.getProperty("cutoff_calibration", {})
		.getProperty("measured_minus_3_db_hz", 0.0));
	const auto candidateCutoff = static_cast<double>(candidate.getProperty("cutoff_calibration", {})
		.getProperty("measured_minus_3_db_hz", 0.0));
	comparison->setProperty("current_minus_3_db_hz", currentCutoff);
	comparison->setProperty("candidate_minus_3_db_hz", candidateCutoff);
	comparison->setProperty("candidate_to_current_cutoff_ratio",
		currentCutoff > 0.0 ? candidateCutoff / currentCutoff : 0.0);

	const auto* currentResponse = current.getProperty("frequency_response", {}).getArray();
	const auto* candidateResponse = candidate.getProperty("frequency_response", {}).getArray();
	double outputScale = 1.0;
	if (currentResponse != nullptr && candidateResponse != nullptr
		&& !currentResponse->isEmpty() && !candidateResponse->isEmpty())
	{
		const auto currentReferenceGain = static_cast<double>((*currentResponse)[0].getProperty("gain", 0.0));
		const auto candidateReferenceGain = static_cast<double>((*candidateResponse)[0].getProperty("gain", 0.0));
		outputScale = audio_analysis::levelMatchScale(currentReferenceGain, candidateReferenceGain);
		auto* normalization = new juce::DynamicObject;
		normalization->setProperty("reference_frequency_hz",
			(*currentResponse)[0].getProperty("frequency_hz", 0.0));
		normalization->setProperty("current_gain", currentReferenceGain);
		normalization->setProperty("candidate_gain", candidateReferenceGain);
		normalization->setProperty("candidate_output_scale", outputScale);
		normalization->setProperty("candidate_output_scale_db", gainToDecibels(outputScale));
		comparison->setProperty("low_level_output_normalization", juce::var(normalization));
	}

	juce::Array<juce::var> frequencyDeltas;
	if (currentResponse != nullptr && candidateResponse != nullptr)
		for (int index = 0; index < std::min(currentResponse->size(), candidateResponse->size()); ++index)
		{
			const auto& currentPoint = (*currentResponse)[index];
			const auto& candidatePoint = (*candidateResponse)[index];
			const auto currentGainDb = static_cast<double>(currentPoint.getProperty("gain_db", 0.0));
			const auto candidateGainDb = static_cast<double>(candidatePoint.getProperty("gain_db", 0.0));
			const auto normalizedCandidateGainDb = candidateGainDb + gainToDecibels(outputScale);
			auto* delta = new juce::DynamicObject;
			delta->setProperty("frequency_hz", currentPoint.getProperty("frequency_hz", 0.0));
			delta->setProperty("candidate_minus_current_gain_db", candidateGainDb - currentGainDb);
			delta->setProperty("candidate_minus_current_phase_radians", wrappedPhaseDifference(
				static_cast<double>(candidatePoint.getProperty("phase_radians", 0.0)),
				static_cast<double>(currentPoint.getProperty("phase_radians", 0.0))));
			delta->setProperty("normalized_candidate_gain_db", normalizedCandidateGainDb);
			delta->setProperty("normalized_candidate_minus_current_gain_db",
				normalizedCandidateGainDb - currentGainDb);
			frequencyDeltas.add(juce::var(delta));
		}
	comparison->setProperty("frequency_response_delta", frequencyDeltas);

	const auto* currentLevels = current.getProperty("level_response", {}).getArray();
	const auto* candidateLevels = candidate.getProperty("level_response", {}).getArray();
	juce::Array<juce::var> levelDeltas;
	if (currentLevels != nullptr && candidateLevels != nullptr)
		for (int index = 0; index < std::min(currentLevels->size(), candidateLevels->size()); ++index)
		{
			const auto& currentLevel = (*currentLevels)[index];
			const auto& candidateLevel = (*candidateLevels)[index];
			const auto currentFundamental = static_cast<double>(currentLevel.getProperty("fundamental_gain", 0.0));
			const auto candidateFundamental = static_cast<double>(candidateLevel.getProperty("fundamental_gain", 0.0));
			auto* delta = new juce::DynamicObject;
			delta->setProperty("input_peak", currentLevel.getProperty("input_peak", 0.0));
			delta->setProperty("candidate_minus_current_fundamental_gain_db",
				gainToDecibels(candidateFundamental) - gainToDecibels(currentFundamental));
			delta->setProperty("normalized_candidate_fundamental_gain", candidateFundamental * outputScale);
			delta->setProperty("normalized_candidate_minus_current_fundamental_gain_db",
				gainToDecibels(candidateFundamental * outputScale) - gainToDecibels(currentFundamental));
			delta->setProperty("current_second_component_relative_input",
				currentLevel.getProperty("second_harmonic_relative", 0.0));
			delta->setProperty("candidate_second_component_relative_input",
				candidateLevel.getProperty("second_harmonic_relative", 0.0));
			delta->setProperty("current_third_component_relative_input",
				currentLevel.getProperty("third_harmonic_relative", 0.0));
			delta->setProperty("candidate_third_component_relative_input",
				candidateLevel.getProperty("third_harmonic_relative", 0.0));
			levelDeltas.add(juce::var(delta));
		}
	comparison->setProperty("level_response_delta", levelDeltas);

	const auto* currentRingdown = current.getProperty("ringdown", {}).getArray();
	const auto* candidateRingdown = candidate.getProperty("ringdown", {}).getArray();
	juce::Array<juce::var> ringdownDeltas;
	if (currentRingdown != nullptr && candidateRingdown != nullptr)
		for (int index = 0; index < std::min(currentRingdown->size(), candidateRingdown->size()); ++index)
		{
			const auto& currentEntry = (*currentRingdown)[index];
			const auto& candidateEntry = (*candidateRingdown)[index];
			auto* delta = new juce::DynamicObject;
			delta->setProperty("input_peak", currentEntry.getProperty("input_peak", 0.0));
			delta->setProperty("candidate_minus_current_onset_samples",
				static_cast<int>(candidateEntry.getProperty("onset_sample", 0))
					- static_cast<int>(currentEntry.getProperty("onset_sample", 0)));
			delta->setProperty("candidate_minus_current_peak_db", gainToDecibels(
				static_cast<double>(candidateEntry.getProperty("peak", 0.0))) - gainToDecibels(
				static_cast<double>(currentEntry.getProperty("peak", 0.0))));
			delta->setProperty("candidate_minus_current_frequency_hz",
				static_cast<double>(candidateEntry.getProperty("estimated_frequency_hz", 0.0))
					- static_cast<double>(currentEntry.getProperty("estimated_frequency_hz", 0.0)));
			delta->setProperty("current_extinguished", currentEntry.getProperty("extinguished", false));
			delta->setProperty("candidate_extinguished", candidateEntry.getProperty("extinguished", false));
			ringdownDeltas.add(juce::var(delta));
		}
	comparison->setProperty("ringdown_delta", ringdownDeltas);
	comparison->setProperty("level_probe_note",
		"At high resonance, autonomous oscillation can make components relative to input unsuitable as conventional harmonic-distortion ratios.");
	return juce::var(comparison);
}

juce::var cutoffMappingReport()
{
	constexpr std::array<double, 3> sampleRates { 44'100.0, 48'000.0, 96'000.0 };
	constexpr std::array<float, 3> cutoffControls { 250.0f, 1'000.0f, 4'000.0f };
	struct Measurement
	{
		double sampleRate {};
		float controlHz {};
		double currentHz {};
		double candidateHz {};
		double proportionalControlScaleEstimate {};
	};
	std::array<Measurement, sampleRates.size() * cutoffControls.size()> measurements {};
	std::size_t measurementIndex {};
	double logarithmicScaleSum {};
	for (const auto sampleRate : sampleRates)
		for (const auto controlHz : cutoffControls)
		{
			const auto currentHz = measureMinus3DbFrequency<CurrentProbe>(sampleRate, controlHz);
			const auto candidateHz = measureMinus3DbFrequency<CandidateProbe>(sampleRate, controlHz);
			const auto scale = candidateHz > 0.0 ? currentHz / candidateHz : 0.0;
			measurements[measurementIndex++] = { sampleRate, controlHz, currentHz, candidateHz, scale };
			if (scale > 0.0) logarithmicScaleSum += std::log(scale);
		}
	const auto constantScale = std::exp(logarithmicScaleSum
		/ static_cast<double>(measurements.size()));
	double minimumScale = std::numeric_limits<double>::max();
	double maximumScale {};
	double maximumAbsoluteResidualCents {};
	juce::Array<juce::var> rates;
	for (std::size_t rateIndex = 0; rateIndex < sampleRates.size(); ++rateIndex)
	{
		const auto sampleRate = sampleRates[rateIndex];
		auto* rate = new juce::DynamicObject;
		rate->setProperty("sample_rate", sampleRate);
		juce::Array<juce::var> points;
		for (std::size_t cutoffIndex = 0; cutoffIndex < cutoffControls.size(); ++cutoffIndex)
		{
			const auto& measurement = measurements[rateIndex * cutoffControls.size() + cutoffIndex];
			minimumScale = std::min(minimumScale, measurement.proportionalControlScaleEstimate);
			maximumScale = std::max(maximumScale, measurement.proportionalControlScaleEstimate);
			const auto mappedControl = static_cast<float>(measurement.controlHz * constantScale);
			const auto mappedCandidateHz = measureMinus3DbFrequency<CandidateProbe>(sampleRate, mappedControl);
			const auto residualRatio = measurement.currentHz > 0.0
				? mappedCandidateHz / measurement.currentHz : 0.0;
			const auto residualPercent = (residualRatio - 1.0) * 100.0;
			const auto residualCents = residualRatio > 0.0 ? 1'200.0 * std::log2(residualRatio) : 0.0;
			maximumAbsoluteResidualCents = std::max(maximumAbsoluteResidualCents,
				std::abs(residualCents));
			auto* point = new juce::DynamicObject;
			point->setProperty("control_hz", measurement.controlHz);
			point->setProperty("current_measured_minus_3_db_hz", measurement.currentHz);
			point->setProperty("candidate_measured_minus_3_db_hz", measurement.candidateHz);
			point->setProperty("candidate_to_current_measured_ratio",
				measurement.currentHz > 0.0 ? measurement.candidateHz / measurement.currentHz : 0.0);
			point->setProperty("proportional_candidate_control_scale_estimate",
				measurement.proportionalControlScaleEstimate);
			point->setProperty("constant_scale_candidate_control_hz", mappedControl);
			point->setProperty("constant_scale_candidate_measured_minus_3_db_hz", mappedCandidateHz);
			point->setProperty("constant_scale_residual_percent", residualPercent);
			point->setProperty("constant_scale_residual_cents", residualCents);
			points.add(juce::var(point));
		}
		rate->setProperty("points", points);
		rates.add(juce::var(rate));
	}
	auto* report = new juce::DynamicObject;
	report->setProperty("sample_rates", rates);
	report->setProperty("constant_candidate_control_scale", constantScale);
	report->setProperty("constant_candidate_control_scale_cents", 1'200.0 * std::log2(constantScale));
	report->setProperty("minimum_point_scale", minimumScale);
	report->setProperty("maximum_point_scale", maximumScale);
	report->setProperty("point_scale_spread_cents", 1'200.0 * std::log2(maximumScale / minimumScale));
	report->setProperty("maximum_absolute_constant_scale_residual_cents",
		maximumAbsoluteResidualCents);
	report->setProperty("constant_scale_is_adequate", maximumAbsoluteResidualCents <= 25.0);
	report->setProperty("conclusion",
		"A single proportional candidate cutoff-control scale is not adequate across the measured cutoff and sample-rate matrix.");
	report->setProperty("note",
		"Scale estimates assume local proportionality; the constant scale is re-probed. Control remapping is measurement-only and is not applied to either filter implementation.");
	return juce::var(report);
}
}

LadderPrototypeResult renderLadderPrototype(double sampleRate, int blockSize)
{
	LadderPrototypeResult result;
	if (sampleRate <= 0.0 || blockSize <= 0) return result;
	const auto candidate = modelReport<CandidateProbe>(sampleRate, blockSize, &result.audio);
	const auto current = modelReport<CurrentProbe>(sampleRate, blockSize, nullptr);
	auto* report = new juce::DynamicObject;
	result.report = juce::var(report);
	report->setProperty("product", "mono-ladder-comparison");
	report->setProperty("model", "four-stage-nonlinear-tpt-bounded-newton");
	report->setProperty("sample_rate", sampleRate);
	report->setProperty("block_size", blockSize);
	report->setProperty("samples", result.audio.getNumSamples());
	report->setProperty("validation_contract", validationContractReport());
	report->setProperty("planned_validation_matrix", validationMatrixReport());
	report->setProperty("provisional_acceptance_limits", provisionalAcceptanceReport());
	auto* models = new juce::DynamicObject;
	models->setProperty("current_delayed_feedback", current);
	models->setProperty("candidate_nonlinear_tpt", candidate);
	report->setProperty("models", juce::var(models));
	// Retain the original candidate fields for report-reader compatibility.
	report->setProperty("frequency_response", candidate.getProperty("frequency_response", {}));
	report->setProperty("cutoff_calibration", candidate.getProperty("cutoff_calibration", {}));
	report->setProperty("level_response", candidate.getProperty("level_response", {}));
	report->setProperty("ringdown", candidate.getProperty("ringdown", {}));
	report->setProperty("comparison", comparisonReport(current, candidate));
	report->setProperty("cutoff_mapping", cutoffMappingReport());
	report->setProperty("two_tone_imd", twoToneReport(sampleRate));
	report->setProperty("stopband_tolerance", stopbandToleranceReport(sampleRate));
	return result;
}

bool writeLadderPrototypeWav(const juce::File& file, const LadderPrototypeResult& result, double sampleRate)
{
	if (result.audio.getNumChannels() != 2 || result.audio.getNumSamples() <= 0 || sampleRate <= 0.0) return false;
	file.deleteFile();
	std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
	juce::WavAudioFormat format;
	auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions {}
		.withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(32));
	return writer != nullptr && writer->writeFromAudioSampleBuffer(result.audio, 0, result.audio.getNumSamples());
}

bool writeLadderPrototypeReport(const juce::File& file, const LadderPrototypeResult& result)
{
	return result.report.isObject() && file.replaceWithText(juce::JSON::toString(result.report, false));
}
}