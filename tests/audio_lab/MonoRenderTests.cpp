#include "../../tools/audio_lab/MonoRender.h"

#include "NonlinearTptLadder.h"
#include "../../tools/audio_lab/NonlinearTptLadderReference.h"
#include "../../tools/audio_lab/MonoOnsetAnalysis.h"
#include "../../tools/audio_lab/MonoLadderStability.h"
#include <vekt/audio_analysis/Measurements.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <bit>
#include <cmath>
#include <numbers>
#include <utility>

namespace
{
double windowMeasurement(const juce::var& report, const juce::String& name, const juce::Identifier& measurement)
{
	if (const auto* windows = report.getProperty("windows", {}).getArray())
		for (const auto& window : *windows)
			if (window.getProperty("name", {}).toString() == name)
				if (const auto* channels = window.getProperty("channels", {}).getArray())
					return static_cast<double>((*channels)[0].getProperty(measurement, 0.0));
	return 0.0;
}
}

TEST_CASE("Mono Audio Lab fixtures use sample-positioned MIDI and parameter events", "[audio-lab][mono]")
{
	vekt::audio_lab::MonoRenderRequest filter;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("filter-sweep", 48'000.0, 127, 1234, filter));
	REQUIRE(filter.events.front().type == vekt::audio_lab::MonoEventType::noteOn);
	REQUIRE(filter.events[1].type == vekt::audio_lab::MonoEventType::parameter);
	REQUIRE(filter.events[1].sample == 24'000);
	REQUIRE(filter.events.back().type == vekt::audio_lab::MonoEventType::noteOff);
	REQUIRE(filter.seed == 1234);

	vekt::audio_lab::MonoRenderRequest envelope;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("envelope", 44'100.0, 64, 7, envelope));
	REQUIRE(envelope.events.front().sample == 2'205);
	REQUIRE(envelope.events.front().parameter == vekt::audio_lab::MonoParameter::ampRelease);
	REQUIRE(envelope.events[1].sample == 4'410);
	REQUIRE(envelope.events[1].type == vekt::audio_lab::MonoEventType::noteOn);
	REQUIRE_FALSE(vekt::audio_lab::makeMonoRenderFixture("unknown", 48'000.0, 64, 1, envelope));
}

TEST_CASE("Mono Audio Lab renders are deterministic and block-size invariant", "[audio-lab][mono][determinism]")
{
	vekt::audio_lab::MonoRenderRequest firstRequest, secondRequest, otherSeedRequest;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("filter-sweep", 48'000.0, 31, 0x12345678u, firstRequest));
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("filter-sweep", 48'000.0, 257, 0x12345678u, secondRequest));
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("filter-sweep", 48'000.0, 31, 0x87654321u, otherSeedRequest));
	const auto first = vekt::audio_lab::renderMono(firstRequest);
	const auto second = vekt::audio_lab::renderMono(secondRequest);
	const auto otherSeed = vekt::audio_lab::renderMono(otherSeedRequest);
	REQUIRE(first.audio.getNumSamples() == second.audio.getNumSamples());
	bool seedChangedOutput = false;
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < first.audio.getNumSamples(); ++sample)
		{
			REQUIRE(std::bit_cast<std::uint32_t>(first.audio.getSample(channel, sample))
				== std::bit_cast<std::uint32_t>(second.audio.getSample(channel, sample)));
			seedChangedOutput = seedChangedOutput
				|| std::bit_cast<std::uint32_t>(first.audio.getSample(channel, sample))
					!= std::bit_cast<std::uint32_t>(otherSeed.audio.getSample(channel, sample));
		}
	REQUIRE(seedChangedOutput);
}

TEST_CASE("Mono Q Comp Drive listening fixtures cover five matched pairs", "[audio-lab][mono][qcomp]")
{
	for (const auto* drive : { "0", "6", "12", "18", "24" })
	{
		vekt::audio_lab::MonoRenderRequest off, on;
		const auto prefix = juce::String("q-comp-drive-") + drive;
		REQUIRE(vekt::audio_lab::makeMonoRenderFixture(prefix + "-off", 48'000.0, 128, 42, off));
		REQUIRE(vekt::audio_lab::makeMonoRenderFixture(prefix + "-on", 48'000.0, 128, 42, on));
		REQUIRE(off.settings.drive == Catch::Approx(on.settings.drive));
		REQUIRE(off.settings.resonance == Catch::Approx(on.settings.resonance));
		REQUIRE_FALSE(off.settings.qCompensation);
		REQUIRE(on.settings.qCompensation);
	}
	vekt::audio_lab::MonoRenderRequest invalid;
	REQUIRE_FALSE(vekt::audio_lab::makeMonoRenderFixture("q-comp-drive-10-on", 48'000.0, 128, 42, invalid));
}

TEST_CASE("Mono 95 percent Q listening pairs hold matched harmonic notes and cutoff sweeps", "[audio-lab][mono][qcomp]")
{
	for (const auto* kind : { "sustain", "bass", "sweep" })
		for (const auto* drive : { "12", "18", "24" })
		{
			vekt::audio_lab::MonoRenderRequest off, on;
			const auto prefix = juce::String("q-comp-listen-95-") + kind + "-drive-" + drive;
			REQUIRE(vekt::audio_lab::makeMonoRenderFixture(prefix + "-off", 48'000.0, 128, 42, off));
			REQUIRE(vekt::audio_lab::makeMonoRenderFixture(prefix + "-on", 48'000.0, 128, 42, on));
			REQUIRE(off.settings.resonance == Catch::Approx(0.95f));
			REQUIRE(on.settings.resonance == Catch::Approx(off.settings.resonance));
			REQUIRE(on.settings.drive == Catch::Approx(off.settings.drive));
			REQUIRE_FALSE(off.settings.qCompensation);
			REQUIRE(on.settings.qCompensation);
			REQUIRE(off.events.size() == on.events.size());
			REQUIRE(off.windows.size() == 1);
			REQUIRE(off.windows[0].startSample == on.windows[0].startSample);
			REQUIRE(off.windows[0].endSample == on.windows[0].endSample);
			REQUIRE(off.events[0].note == (juce::String(kind) == "bass" ? 36 : 48));
			REQUIRE(off.events.size() == (juce::String(kind) == "sweep" ? 301u : 1u));
			for (std::size_t index = 0; index < off.events.size(); ++index)
			{
				REQUIRE(off.events[index].sample == on.events[index].sample);
				REQUIRE(off.events[index].value == Catch::Approx(on.events[index].value));
				REQUIRE(off.events[index].note == on.events[index].note);
			}
		}
	vekt::audio_lab::MonoRenderRequest invalid;
	REQUIRE_FALSE(vekt::audio_lab::makeMonoRenderFixture("q-comp-listen-95-sweep-drive-10-on", 48'000.0, 128, 42, invalid));
}

TEST_CASE("Mono voice resonance onset stays finite with an unboosted output tap", "[audio-lab][mono][resonance-onset]")
{
	for (const auto compensated : { false, true })
	{
		double previousRms {}, at97 {};
		for (const auto resonance : { 0.97f, 0.98f, 0.985f, 0.99f, 1.0f })
		{
			auto request = vekt::audio_lab::MonoRenderRequest {};
			REQUIRE(vekt::audio_lab::makeMonoRenderFixture("q-comp-listen-95-sustain-drive-12-off",
				48'000.0, 128, 42, request));
			request.settings.resonance = resonance;
			request.settings.qCompensation = compensated;
			const auto result = vekt::audio_lab::renderMono(request);
			const auto level = windowMeasurement(result.report, "listening", "rms");
			INFO("Q Comp=" << compensated << ", resonance=" << resonance << ", driven RMS=" << level
				<< ", previous RMS=" << previousRms);
			REQUIRE(std::isfinite(level));
			REQUIRE(level > 0.001);
			if (previousRms > 0.0) REQUIRE(level / previousRms < 2.0);
			if (resonance < 0.975f) at97 = level;
			if (resonance > 0.995f)
				REQUIRE(level / at97 < 1.1); // The former 1.6x post-ladder boost would fail.
			previousRms = level;
		}
	}
}

TEST_CASE("Mono onset log-envelope fit rejects floors and nonlinear saturation", "[audio-lab][mono][resonance-onset]")
{
	for (const auto slope : { -3.0, 2.0 })
	{
		std::vector<double> bins;
		for (int i = 0; i < 15; ++i)
			bins.push_back(0.001 * std::exp(slope * (i + 0.5) * 0.02));
		const auto fit = vekt::audio_lab::fitOnset(bins, 0.02);
		REQUIRE(fit.valid);
		REQUIRE(fit.usableBins == 15);
		REQUIRE(fit.slopePerSecond == Catch::Approx(slope).margin(1.0e-10));
		REQUIRE(fit.rSquared == Catch::Approx(1.0).margin(1.0e-10));
	}
	REQUIRE_FALSE(vekt::audio_lab::fitOnset(std::vector<double>(15, 1.0e-8), 0.02).valid);
	REQUIRE_FALSE(vekt::audio_lab::fitOnset(std::vector<double>(15, 0.05), 0.02).valid);
	std::vector<double> nonlinear;
	for (int i = 0; i < 15; ++i)
		nonlinear.push_back(0.001 * std::exp((i % 2 == 0 ? 1.0 : -1.0) * 0.3));
	REQUIRE_FALSE(vekt::audio_lab::fitOnset(nonlinear, 0.02).valid);
	std::vector<double> separated { 0.001, 0.0011, 0.0012, 0.0013, 0.0014, 0.02 };
	for (int i = 0; i < 10; ++i)
		separated.push_back(0.001 * std::exp(i * 0.02));
	REQUIRE_FALSE(vekt::audio_lab::fitOnset(separated, 0.02).valid);
}

TEST_CASE("Mono weak-signal onset changes growth sign near 98.4 percent", "[audio-lab][mono][resonance-onset]")
{
	constexpr double rate = 48'000.0;
	for (const auto [resonance, growing] : { std::pair { 0.9840f, false }, { 0.9841f, true } })
	{
		const auto predicted = vekt::audio_lab::ladderStability(rate, 1'000.0f, resonance);
		vekt::mono::NonlinearTptLadder ladder;
		ladder.prepare(rate);
		const vekt::mono::NonlinearTptLadderSettings settings { 1'000.0f, resonance, 0.0f };
		std::vector<double> bins;
		double squares {};
		for (int sample = 0; sample < 4'800 + 15 * 960; ++sample)
		{
			const auto input = sample < 4'800 ? 1.0e-4f * static_cast<float>(
				std::sin(2.0 * std::numbers::pi * 317.0 * sample / rate)) : 0.0f;
			const auto value = ladder.processCoupled(input, settings);
			REQUIRE(std::isfinite(value));
			if (sample < 4'800) continue;
			squares += static_cast<double>(value) * value;
			if ((sample - 4'800 + 1) % 960 == 0)
			{
				bins.push_back(std::sqrt(squares / 960));
				squares = 0.0;
			}
		}
		const auto fit = vekt::audio_lab::fitOnset(bins, 0.02);
		CAPTURE(resonance, fit.slopePerSecond, predicted.dominantGrowthPerSecond,
			fit.rSquared, fit.usableBins);
		REQUIRE(fit.valid);
		REQUIRE(fit.usableBins == 15);
		REQUIRE((fit.slopePerSecond > 0.0) == growing);
		REQUIRE(std::abs(fit.slopePerSecond - predicted.dominantGrowthPerSecond) < 0.1);
		REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
		REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
	}
}

TEST_CASE("Mono raw ladder maximum resonance level stays consistent at cutoff extremes", "[audio-lab][mono][resonance-matrix]")
{
	for (const auto cutoff : { 100.0f, 10'000.0f })
	{
		constexpr double rate = 48'000.0;
		vekt::mono::NonlinearTptLadder ladder;
		ladder.prepare(rate);
		const vekt::mono::NonlinearTptLadderSettings settings { cutoff, 1.0f, 0.0f };
		double squares {};
		const auto excitationHz = std::min(317.0, cutoff * 0.73);
		for (int sample = 0; sample < 168'000; ++sample)
		{
			const auto input = sample < 24'000 ? 0.5f * static_cast<float>(
				std::sin(2.0 * std::numbers::pi * excitationHz * sample / rate)) : 0.0f;
			const auto value = ladder.processCoupled(input, settings);
			REQUIRE(std::isfinite(value));
			if (sample >= 158'400)
				squares += static_cast<double>(value) * value;
		}
		const auto rms = std::sqrt(squares / 9'600);
		CAPTURE(cutoff, rms);
		REQUIRE(rms > 0.12);
		REQUIRE(rms < 0.13);
		REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
		REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
	}
}

TEST_CASE("Mono production zero-state Jacobian predicts resonance onset across rate and cutoff", "[audio-lab][mono][resonance-matrix]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0, 192'000.0 })
		for (const auto cutoff : { 100.0f, 1'000.0f, 10'000.0f })
		{
			const auto below = vekt::audio_lab::ladderStability(rate, cutoff, 0.98f);
			const auto above = vekt::audio_lab::ladderStability(rate, cutoff, 0.99f);
			CAPTURE(rate, cutoff, below.radius, above.radius);
			REQUIRE(below.radius < 1.0);
			REQUIRE(above.radius > 1.0);
			const auto k = vekt::mono::ladderFeedbackGain(static_cast<double>(0.984f));
			const auto root = std::polar(std::pow(k, 0.25), std::numbers::pi_v<double> / 4.0);
			const auto prewarp = std::tan(std::numbers::pi_v<double> * cutoff / rate)
				* vekt::mono::ladderResonanceTuning(static_cast<double>(0.984f));
			const auto mode = 2.0 / (1.0 + prewarp - prewarp * root) - 1.0;
			const auto analytic = vekt::audio_lab::ladderStability(rate, cutoff, 0.984f);
			std::array<std::complex<double>, 4> eigenvector {};
			for (std::size_t i = 0; i < eigenvector.size(); ++i)
				eigenvector[i] = std::pow(root, -static_cast<int>(i));
			for (std::size_t i = 0; i < eigenvector.size(); ++i)
			{
				std::complex<double> product {};
				for (std::size_t j = 0; j < eigenvector.size(); ++j)
					product += analytic.jacobian[i][j] * eigenvector[j];
				REQUIRE(std::abs(product - mode * eigenvector[i]) < 1.0e-7);
			}
			bool converged = true;
			double previousError = 1.0;
			for (const auto step : { 0.01, 0.001, 0.0001 })
			{
				const auto measured = vekt::audio_lab::measuredLadderJacobian(rate, cutoff,
					0.984f, step, converged);
				const auto error = vekt::audio_lab::maximumJacobianError(measured, analytic.jacobian);
				CAPTURE(step, error);
				REQUIRE(std::isfinite(error));
				REQUIRE(error < 0.001);
				REQUIRE(error <= previousError + 1.0e-13);
				if (step < 0.01) REQUIRE(error < 1.0e-7);
				previousError = error;
			}
			REQUIRE(converged);
		}
}

TEST_CASE("Mono held open self-oscillation survives removal of all excitation", "[audio-lab][mono][resonance-onset]")
{
	vekt::audio_lab::MonoRenderRequest off, on;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("self-osc-held-off", 48'000.0, 128, 42, off));
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("self-osc-held-on", 48'000.0, 128, 42, on));
	REQUIRE(off.events.size() == 2);
	REQUIRE(on.events.size() == 3);
	REQUIRE(off.events[1].sample == on.events[1].sample);
	REQUIRE(off.events[1].value == Catch::Approx(0.0f));
	REQUIRE(on.events[2].sample == off.events[1].sample);
	REQUIRE(on.events[2].parameter == vekt::audio_lab::MonoParameter::qCompensation);
	REQUIRE(off.settings.level[0] == Catch::Approx(0.0f));
	REQUIRE(off.settings.level[1] == Catch::Approx(0.0f));
	REQUIRE(off.settings.level[2] == Catch::Approx(0.0f));
	REQUIRE(off.settings.ampSustain == Catch::Approx(1.0f));
	const auto dry = vekt::audio_lab::renderMono(off);
	const auto wet = vekt::audio_lab::renderMono(on);
	const auto early = windowMeasurement(dry.report, "early_zero_input", "rms");
	const auto late = windowMeasurement(dry.report, "late_zero_input", "rms");
	INFO("early=" << early << ", late=" << late);
	REQUIRE(std::isfinite(late));
	REQUIRE(late > 0.01);
	REQUIRE(late == Catch::Approx(early).epsilon(0.05));
	REQUIRE(windowMeasurement(wet.report, "late_zero_input", "rms") == Catch::Approx(late).margin(1.0e-8));
	for (int channel = 0; channel < dry.audio.getNumChannels(); ++channel)
		for (int sample = 4'800; sample < dry.audio.getNumSamples(); ++sample)
			REQUIRE(std::bit_cast<std::uint32_t>(dry.audio.getSample(channel, sample))
				== std::bit_cast<std::uint32_t>(wet.audio.getSample(channel, sample)));
}

TEST_CASE("Mono held self-oscillation loses three dB per channel to centered equal-power pan", "[audio-lab][mono][gain-calibration]")
{
	constexpr double rate = 48'000.0;
	vekt::audio_lab::MonoRenderRequest request;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("self-osc-held-off", rate, 128, 42, request));
	REQUIRE(request.settings.ampSustain == Catch::Approx(1.0f));
	REQUIRE(request.settings.ampVelocity == Catch::Approx(0.0f));
	REQUIRE(request.settings.unison == 1);
	REQUIRE(request.settings.voiceWidth == Catch::Approx(0.0f));
	const auto voice = vekt::audio_lab::renderMono(request);
	const auto* windows = voice.report.getProperty("windows", {}).getArray();
	REQUIRE(windows != nullptr);
	const auto* channels = (*windows)[1].getProperty("channels", {}).getArray();
	REQUIRE(channels != nullptr);
	const auto left = static_cast<double>((*channels)[0].getProperty("rms", 0.0));
	const auto right = static_cast<double>((*channels)[1].getProperty("rms", 0.0));
	REQUIRE(left == Catch::Approx(right).margin(1.0e-8));
	// Compare separately excited, settled runs at the same cutoff, rate,
	// resonance and drive; the excitation waveforms need not be identical.
	vekt::mono::NonlinearTptLadder ladder;
	ladder.prepare(rate);
	const vekt::mono::NonlinearTptLadderSettings settings { 1'000.0f, 1.0f, 0.0f };
	double squares {};
	for (int sample = 0; sample < 168'000; ++sample)
	{
		const auto input = sample < 24'000 ? 0.5f * static_cast<float>(
			std::sin(2.0 * std::numbers::pi * 317.0 * sample / rate)) : 0.0f;
		const auto value = ladder.processCoupled(input, settings);
		REQUIRE(std::isfinite(value));
		if (sample >= 158'400) squares += static_cast<double>(value) * value;
	}
	const auto rawRms = std::sqrt(squares / 9'600);
	const auto reconstructed = std::sqrt(left * left + right * right);
	INFO("raw ladder RMS=" << rawRms << ", left=" << left << ", right=" << right
		<< ", pre-pan reconstruction=" << reconstructed);
	REQUIRE(rawRms > 0.12);
	REQUIRE(reconstructed == Catch::Approx(rawRms).margin(0.0005));
	REQUIRE(left / rawRms == Catch::Approx(std::sqrt(0.5)).margin(0.005));
	REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
	REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
}

TEST_CASE("Mono half input compensation preserves the zero-input trajectory and reference solve", "[audio-lab][mono][qcomp][ladder-reference]")
{
	vekt::mono::NonlinearTptLadder off, on;
	vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
	off.prepare(48'000.0); on.prepare(48'000.0); reference.prepare(48'000.0, 1);
	const vekt::mono::NonlinearTptLadderSettings dry { 1'000.0f, 1.0f, 12.0f };
	auto wet = dry;
	wet.inputFeedbackCompensation = 0.5f;
	for (int sample = 0; sample < 48'000; ++sample)
	{
		const auto input = sample < 4'800 ? 0.05f * static_cast<float>(
			std::sin(2.0 * std::numbers::pi * 317.0 * sample / 48'000.0)) : 0.0f;
		const auto actual = on.processCoupled(input, sample < 4'800 ? dry : wet);
		const auto expected = reference.process(input, { 1'000.0, 1.0, 12.0, false,
			sample < 4'800 ? 0.0 : 0.5 });
		const auto baseline = off.processCoupled(input, dry);
		if (sample >= 4'800)
			REQUIRE(std::bit_cast<std::uint32_t>(actual) == std::bit_cast<std::uint32_t>(baseline));
		if (sample >= 4'801)
			REQUIRE(std::abs(static_cast<double>(actual) - expected) < 1.0e-4);
	}
	REQUIRE(on.diagnostics().unconvergedSamples == 0);
	REQUIRE(on.diagnostics().nonFiniteSamples == 0);
	REQUIRE(reference.diagnostics().unconvergedSteps == 0);
}

TEST_CASE("Mono constant-half input compensation reaches the coupled solver under drive", "[audio-lab][mono][qcomp][ladder-reference]")
{
	for (const auto resonance : { 0.0f, 0.5f, 0.95f, 1.0f })
	{
		vekt::mono::NonlinearTptLadder off, on;
		vekt::audio_lab::NonlinearTptLadderOfflineReference independent;
		off.prepare(48'000.0); on.prepare(48'000.0); independent.prepare(48'000.0, 1);
		const vekt::mono::NonlinearTptLadderSettings dry { 1'000.0f, resonance, 6.0f };
		auto wet = dry;
		wet.inputFeedbackCompensation = 0.5f;
		bool changed = false;
		for (int sample = 0; sample < 512; ++sample)
		{
			const auto input = 0.05f * static_cast<float>(
				std::sin(2.0 * std::numbers::pi * 317.0 * sample / 48'000.0));
			const auto a = off.processCoupled(input, dry);
			const auto b = on.processCoupled(input, wet);
			const auto expected = independent.process(input, { 1'000.0, resonance, 6.0, false, 0.5 });
			CAPTURE(resonance, sample, a, b, expected);
			REQUIRE(std::abs(static_cast<double>(b) - expected) < 1.0e-4);
			if (resonance == 0.0f)
				REQUIRE(std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b));
			else if (std::bit_cast<std::uint32_t>(a) != std::bit_cast<std::uint32_t>(b)) changed = true;
		}
		if (resonance > 0.0f) REQUIRE(changed);
		REQUIRE(on.diagnostics().unconvergedSamples == 0);
		REQUIRE(on.diagnostics().nonFiniteSamples == 0);
		REQUIRE(independent.diagnostics().unconvergedSteps == 0);
	}
}

TEST_CASE("Mono Audio Lab reports filter and envelope measurements", "[audio-lab][mono][measurements]")
{
	vekt::audio_lab::MonoRenderRequest filterRequest;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("filter-sweep", 48'000.0, 128, 99, filterRequest));
	const auto filter = vekt::audio_lab::renderMono(filterRequest);
	REQUIRE(windowMeasurement(filter.report, "closed", "difference_rms") > 0.0);
	REQUIRE(windowMeasurement(filter.report, "open", "difference_rms")
		> windowMeasurement(filter.report, "closed", "difference_rms") * 1.5);

	vekt::audio_lab::MonoRenderRequest envelopeRequest;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("envelope", 48'000.0, 128, 99, envelopeRequest));
	const auto envelope = vekt::audio_lab::renderMono(envelopeRequest);
	const auto measurements = envelope.report.getProperty("envelope", {});
	REQUIRE(static_cast<double>(measurements.getProperty("peak", 0.0)) > 0.01);
	REQUIRE(static_cast<double>(measurements.getProperty("attack_10_to_90_seconds", -1.0)) > 0.0);
	REQUIRE(static_cast<double>(measurements.getProperty("release_to_10_seconds", -1.0)) > 0.0);
	REQUIRE(windowMeasurement(envelope.report, "silence", "peak") == Catch::Approx(0.0));
}

TEST_CASE("Mono input Q compensation preserves the exact zero-input feedback trajectory", "[audio-lab][mono][qcomp]")
{
	for (const auto drive : { 0.0f, 12.0f, 24.0f })
	{
		vekt::mono::NonlinearTptLadder off, on;
		off.prepare(48'000.0); on.prepare(48'000.0);
		const vekt::mono::NonlinearTptLadderSettings dry { 1'000.0f, 1.0f, drive };
		auto compensated = dry;
		compensated.inputFeedbackCompensation = 0.5f;
		for (int sample = 0; sample < 48'000; ++sample)
		{
			const auto input = sample < 4'800 ? 0.05f * std::sin(2.0f * std::numbers::pi_v<float>
				* static_cast<float>(sample) * 317.0f / 48'000.0f) : 0.0f;
			const auto left = off.processCoupled(input, dry);
			const auto right = on.processCoupled(input, sample < 4'800 ? dry : compensated);
			if (sample >= 4'800)
				REQUIRE(std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right));
		}
		REQUIRE(off.diagnostics().nonFiniteSamples == 0);
		REQUIRE(on.diagnostics().unconvergedSamples == 0);
	}
}

TEST_CASE("Mono maximum-resonance zero-input tail survives high-drive excitation", "[audio-lab][mono][filter][drive][ladder-self-oscillation]")
{
	constexpr double rate = 48'000.0;
	constexpr int excitationSamples = 24'000; // 500 ms of strong driven input.
	constexpr int tailSamples = 96'000; // Observe recovery for two seconds.
	constexpr int windowSamples = 9'600; // Last 200 ms of the unforced tail.
	for (const auto compensated : { false, true })
	{
		double baselineRms {}, baselineFrequency {};
		for (const auto drive : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
		{
			vekt::mono::NonlinearTptLadder ladder;
			ladder.prepare(rate);
			const vekt::mono::NonlinearTptLadderSettings settings {
				1'000.0f, 1.0f, drive, false, compensated ? 0.5f : 0.0f };
			double squares {}, tailPeak {};
			int crossings {}, firstCrossing = -1, lastCrossing = -1;
			float previous {};
			for (int sample = 0; sample < excitationSamples + tailSamples; ++sample)
			{
				const auto input = sample < excitationSamples
					? 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 317.0 * sample / rate)) : 0.0f;
				const auto output = ladder.processCoupled(input, settings);
				REQUIRE(std::isfinite(output));
				if (sample < excitationSamples + tailSamples - windowSamples) continue;
				squares += static_cast<double>(output) * output;
				tailPeak = std::max(tailPeak, std::abs(static_cast<double>(output)));
				if (previous <= 0.0f && output > 0.0f)
				{
					if (firstCrossing < 0) firstCrossing = sample;
					lastCrossing = sample;
					++crossings;
				}
				previous = output;
			}
			const auto tailRms = std::sqrt(squares / windowSamples);
			const auto frequency = crossings > 1
				? (crossings - 1) * rate / (lastCrossing - firstCrossing) : 0.0;
			INFO("Q Comp=" << compensated << ", Drive=" << drive << " dB, tail RMS=" << tailRms
				<< ", peak=" << tailPeak << ", frequency=" << frequency << " Hz");
			REQUIRE(tailRms > 0.08);
			REQUIRE(tailRms < 0.25);
			REQUIRE(frequency == Catch::Approx(1'000.0).margin(30.0));
			if (drive == 0.0f) { baselineRms = tailRms; baselineFrequency = frequency; }
			else
			{
				REQUIRE(std::abs(tailRms - baselineRms) < 0.005);
				REQUIRE(std::abs(frequency - baselineFrequency) < 10.0);
			}
			REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
			REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
		}
	}
}

TEST_CASE("Mono Drive is inert on an identical zero-input ladder state", "[audio-lab][mono][filter][drive]")
{
	vekt::mono::NonlinearTptLadder lowDrive, highDrive;
	lowDrive.prepare(48'000.0); highDrive.prepare(48'000.0);
	const vekt::mono::NonlinearTptLadderSettings low { 1'000.0f, 1.0f, 0.0f, false, 0.5f };
	const vekt::mono::NonlinearTptLadderSettings high { 1'000.0f, 1.0f, 24.0f, false, 0.5f };
	for (int sample = 0; sample < 48'000; ++sample)
	{
		const auto input = sample < 4'800 ? 0.05f * static_cast<float>(
			std::sin(2.0 * std::numbers::pi * 317.0 * sample / 48'000.0)) : 0.0f;
		const auto a = lowDrive.processCoupled(input, low);
		const auto b = highDrive.processCoupled(input, sample < 4'800 ? low : high);
		if (sample >= 4'800)
			REQUIRE(std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b));
	}
	REQUIRE(lowDrive.diagnostics().unconvergedSamples == 0);
	REQUIRE(highDrive.diagnostics().unconvergedSamples == 0);
}

TEST_CASE("Mono input Q compensation changes body relative to the resonant component", "[audio-lab][mono][qcomp]")
{
	for (const auto drive : { 0.0f, 12.0f, 24.0f })
	{
		vekt::mono::NonlinearTptLadder off, on;
		off.prepare(48'000.0); on.prepare(48'000.0);
		const vekt::mono::NonlinearTptLadderSettings dry { 1'000.0f, 1.0f, drive };
		auto compensated = dry;
		compensated.inputFeedbackCompensation = 0.5f;
		vekt::audio_analysis::SinusoidalProjector dryBody(48'000.0, 100.0, 24'000), wetBody(48'000.0, 100.0, 24'000);
		vekt::audio_analysis::SinusoidalProjector dryTone(48'000.0, 1'000.0, 24'000), wetTone(48'000.0, 1'000.0, 24'000);
		for (int sample = 0; sample < 48'000; ++sample)
		{
			const auto excitation = sample < 4'800 ? 0.05f * std::sin(2.0f * std::numbers::pi_v<float>
				* static_cast<float>(sample) * 317.0f / 48'000.0f) : 0.1f * std::sin(2.0f
				* std::numbers::pi_v<float> * static_cast<float>(sample) * 100.0f / 48'000.0f);
			const auto left = off.processCoupled(excitation, dry);
			const auto right = on.processCoupled(excitation, compensated);
			if (sample >= 24'000)
			{
				dryBody.add(left); wetBody.add(right);
				dryTone.add(left); wetTone.add(right);
			}
		}
		const auto bodyRatio = wetBody.peakAmplitude() / dryBody.peakAmplitude();
		const auto toneRatio = wetTone.peakAmplitude() / dryTone.peakAmplitude();
		INFO("drive=" << drive << ", body ratio=" << bodyRatio << ", resonant ratio=" << toneRatio);
		REQUIRE(std::isfinite(bodyRatio));
		REQUIRE(std::isfinite(toneRatio));
		REQUIRE(bodyRatio > toneRatio); // Invariant under any level-match gain.
		REQUIRE(off.diagnostics().nonFiniteSamples == 0);
		REQUIRE(on.diagnostics().nonFiniteSamples == 0);
		REQUIRE(on.diagnostics().unconvergedSamples == 0);
	}
}

TEST_CASE("Mono compensated coupled ladder agrees with the independent nested reference", "[audio-lab][mono][qcomp][ladder-reference]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0 })
		for (const auto cutoff : { 500.0f, 8'000.0f })
			for (const auto resonance : { 0.0f, 0.8f, 1.0f })
				for (const auto drive : { 0.0f, 12.0f, 24.0f })
					for (const auto enabled : { false, true })
					{
						vekt::mono::NonlinearTptLadder coupled;
						vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
						coupled.prepare(rate);
						reference.prepare(rate, 1); // Same host-rate discretization; independent nested feedback solver.
						for (int sample = 0; sample < 512; ++sample)
						{
							const auto time = sample / rate;
							const auto input = sample < 128
								? 0.35f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 317.0 * time))
								: sample % 64 < 32 ? 1.25f : -1.25f;
							const auto q = resonance == 0.0f ? 0.0f : std::clamp(resonance
								+ 0.02f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 23.0 * time)), 0.0f, 1.0f);
							const auto c = enabled ? 0.5f : 0.0f;
							const vekt::mono::NonlinearTptLadderSettings actualSettings { cutoff, q, drive, false, c };
							const vekt::audio_lab::NonlinearTptLadderReferenceSettings referenceSettings {
								cutoff, q, drive, false, c };
							const auto actual = coupled.processCoupled(input, actualSettings);
							const auto expected = reference.process(input, referenceSettings);
							CAPTURE(rate, cutoff, resonance, drive, enabled, sample, actual, expected);
							REQUIRE(std::isfinite(actual));
							REQUIRE(std::abs(static_cast<double>(actual) - expected) < 1.0e-4);
						}
						REQUIRE(coupled.diagnostics().unconvergedSamples == 0);
						REQUIRE(coupled.diagnostics().nonFiniteSamples == 0);
						REQUIRE(reference.diagnostics().unconvergedSteps == 0);
						REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
					}
}

TEST_CASE("Mono Q Comp small-signal reference follows the input excitation factor", "[audio-lab][mono][qcomp][ladder-reference]")
{
	for (const auto resonance : { 0.0, 0.5, 0.8, 0.98, 1.0 })
	{
		vekt::audio_lab::NonlinearTptLadderReferenceSettings off { 500.0, resonance, 0.0 };
		auto on = off;
		on.inputFeedbackCompensation = 0.5;
		const auto expected = 1.0 + vekt::mono::ladderFeedbackGain(resonance) * on.inputFeedbackCompensation;
		for (const auto frequency : { 100.0, 500.0, 2'000.0 })
		{
			const auto dry = vekt::audio_lab::nonlinearTptLadderDiscreteResponse(48'000.0, frequency, off);
			const auto wet = vekt::audio_lab::nonlinearTptLadderDiscreteResponse(48'000.0, frequency, on);
			CAPTURE(resonance, frequency);
			REQUIRE(std::abs(wet / dry - expected) < 1.0e-12);
		}
	}
}

TEST_CASE("Mono Audio Lab writes readable WAV and JSON outputs", "[audio-lab][mono][output]")
{
	vekt::audio_lab::MonoRenderRequest request;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("envelope", 24'000.0, 97, 42, request));
	const auto result = vekt::audio_lab::renderMono(request);
	juce::TemporaryFile wav(".wav"), json(".json");
	REQUIRE(vekt::audio_lab::writeMonoRenderWav(wav.getFile(), result, request.sampleRate));
	REQUIRE(vekt::audio_lab::writeMonoRenderReport(json.getFile(), result));
	juce::WavAudioFormat format;
	std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(
		wav.getFile().createInputStream().release(), true));
	REQUIRE(reader != nullptr);
	REQUIRE(reader->sampleRate == Catch::Approx(request.sampleRate));
	REQUIRE(reader->numChannels == 2);
	REQUIRE(reader->lengthInSamples == request.totalSamples);
	const auto parsed = juce::JSON::parse(json.getFile());
	REQUIRE(parsed.isObject());
	REQUIRE(parsed.getProperty("product", {}).toString() == "mono");
	REQUIRE(parsed.getProperty("engine", {}).toString() == "coupled");
	REQUIRE(parsed.getProperty("fixture", {}).toString() == "envelope");
}
