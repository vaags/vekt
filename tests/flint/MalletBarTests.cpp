#include "AllocationCounter.h"
#include "FlintTestSupport.h"

#include <vekt/dsp/OversamplingQuality.h>
#include "MalletBar.h"

#include <vekt/dsp/LinearTptSvf.h>

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace
{
using vekt::flint::FlintParameters;
using vekt::flint::MalletBar;
using vekt::flint::Mode;
using vekt::flint::Strike;
namespace support = vekt::test::flint;

constexpr double sampleRate = 44'100.0;

FlintParameters defaults()
{
	FlintParameters parameters;
	parameters.mode = Mode::mallet;
	parameters.pitch = 60.0; // C4
	return parameters;
}

std::vector<double> hit(
    const FlintParameters& parameters, double seconds, double rate = sampleRate, Strike strike = { 1.0, 60, 1 })
{
	MalletBar bar;
	return support::strikeAndRender(bar, parameters, rate, seconds, strike);
}

std::span<const double> window(
    const std::vector<double>& samples, double fromSeconds, double toSeconds, double rate = sampleRate)
{
	const auto from = static_cast<std::size_t>(fromSeconds * rate);
	const auto to = std::min(samples.size(), static_cast<std::size_t>(toSeconds * rate));
	return std::span(samples).subspan(from, to - from);
}

// The amplitude of the component at `frequency`: a Hann-windowed projection (the window by a recurrence, no cosines).
double componentAt(std::span<const double> samples, double frequency, double rate)
{
	std::complex<double> sum;
	const auto rotation = std::polar(1.0, -2.0 * std::numbers::pi * frequency / rate);
	const auto count = static_cast<double>(samples.size());
	const auto windowStep = std::polar(1.0, 2.0 * std::numbers::pi / count);
	std::complex<double> phasor { 1.0, 0.0 }, windowPhasor { 1.0, 0.0 };
	for (const auto sample : samples)
	{
		sum += (0.5 - 0.5 * windowPhasor.real()) * sample * phasor;
		phasor *= rotation;
		windowPhasor *= windowStep;
	}
	return 4.0 * std::abs(sum) / count;
}

// The frequency of the strongest component within +-1 % of `frequency`, in 0.01 % steps.
double peakFrequencyNear(std::span<const double> samples, double frequency, double rate)
{
	auto best = 0.0, bestFrequency = frequency;
	for (auto step = -100; step <= 100; ++step)
	{
		const auto probe = frequency * (1.0 + 0.0001 * step);
		if (const auto level = componentAt(samples, probe, rate); level > best)
		{
			best = level;
			bestFrequency = probe;
		}
	}
	return bestFrequency;
}

double highBandRms(std::span<const double> samples, double rate)
{
	vekt::dsp::LinearTptSvf filter;
	filter.prepare(rate);
	std::vector<double> filtered;
	for (const auto sample : samples) filtered.push_back(filter.process(sample, 2'000.0, std::numbers::sqrt2).highPass);
	return support::rms(filtered);
}
}

TEST_CASE("Flint bar peaks at -6 dBFS for a default full-velocity hit at C4", "[flint][bar]")
{
	const auto reference = support::peak(hit(defaults(), 0.3));
	INFO("default peak " << reference);
	REQUIRE(std::abs(support::decibels(reference / 0.5)) < 1.0);
}

TEST_CASE("Flint bar Attack and Hardness change its brightness, not its level", "[flint][bar]")
{
	// The struck bar alone: the tube swells with the fundamental, which a harder mallet excites less, as on a real
	// marimba.
	auto base = defaults();
	base.malletBar.resonator = 0.0;
	const auto reference = support::peak(hit(base, 0.3));
	const auto referenceBrightness = support::brightness(window(hit(base, 0.3), 0.0, 0.01));
	for (const auto attack : { 0.0, 1.0 })
	{
		auto parameters = base;
		parameters.attack = attack;
		const auto level = support::peak(hit(parameters, 0.3));
		INFO("attack " << attack << ", peak " << level);
		REQUIRE(std::abs(support::decibels(level / reference)) < 1.0);
	}
	for (const auto hardness : { 0.0, 1.0 })
	{
		auto parameters = base;
		parameters.malletBar.hardness = hardness;
		const auto output = hit(parameters, 0.3);
		const auto level = support::peak(output);
		const auto brightness = support::brightness(window(output, 0.0, 0.01));
		INFO("hardness " << hardness << ", peak " << level << ", brightness " << brightness);
		REQUIRE(std::abs(support::decibels(level / reference)) < 1.0);
		REQUIRE((hardness > 0.5) == (brightness > referenceBrightness));
	}
}

TEST_CASE("Flint bar sounds its pitch within one cent across its range and clamps outside it", "[flint][bar]")
{
	// Wood, no tube: by a third of the fundamental's T60 every upper mode has died away.
	for (const auto note : { 36.0, 48.0, 60.0, 72.0, 84.0, 96.0, 108.0 })
	{
		auto parameters = defaults();
		parameters.pitch = note;
		parameters.decay = 0.8;
		parameters.malletBar.material = 0.0;
		parameters.malletBar.resonator = 0.0;
		const auto t60 = MalletBar::fundamentalT60(0.8, MalletBar::pitchHz(note));
		const auto output = hit(parameters, 0.6 * t60);
		const auto frequency = support::zeroCrossingFrequency(window(output, 0.3 * t60, 0.6 * t60), sampleRate);
		const auto expected = 440.0 * std::exp2((note - 69.0) / 12.0);
		const auto cents = 1'200.0 * std::log2(frequency / expected);
		INFO("note " << note << ", " << frequency << " Hz, " << cents << " cents");
		REQUIRE(std::abs(cents) < 1.0);
	}
	REQUIRE(juce::exactlyEqual(MalletBar::pitchHz(20.0), MalletBar::pitchHz(36.0)));
	REQUIRE(juce::exactlyEqual(MalletBar::pitchHz(120.0), MalletBar::pitchHz(108.0)));
}

TEST_CASE("Flint bar rings longer with Decay and shorter as its pitch rises", "[flint][bar]")
{
	auto previous = 0.0;
	for (const auto decay : { 0.2, 0.4, 0.6 })
	{
		auto parameters = defaults();
		parameters.decay = decay;
		parameters.malletBar.material = 0.0;
		parameters.malletBar.resonator = 0.0;
		const auto expected = MalletBar::fundamentalT60(decay, MalletBar::pitchHz(60.0));
		const auto measured =
		    support::t60(window(hit(parameters, 1.2 * expected), 0.15 * expected, 1.2 * expected), sampleRate);
		INFO("decay " << decay << ": T60 " << measured << " s, nominal " << expected << " s");
		REQUIRE(measured > previous);
		REQUIRE(std::abs(measured / expected - 1.0) < 0.15);
		previous = measured;
	}
	REQUIRE(MalletBar::fundamentalT60(0.5, MalletBar::pitchHz(72.0)) <
	    MalletBar::fundamentalT60(0.5, MalletBar::pitchHz(60.0)));
}

TEST_CASE("Flint bar brightens with Tone, its contact noise included", "[flint][bar]")
{
	auto previous = 0.0;
	for (const auto tone : { 0.0, 0.25, 0.5, 0.75, 1.0 })
	{
		auto parameters = defaults();
		parameters.tone = tone;
		parameters.malletBar.material = 0.6; // upper partials ring long enough to weigh in
		const auto value = support::brightness(window(hit(parameters, 0.05), 0.0, 0.008));
		INFO("tone " << tone << ", brightness " << value);
		REQUIRE(value > previous);
		previous = value;
	}
}

TEST_CASE(
    "Flint bar takes a strike on a ringing tail without a step beyond its own, and its attack is heard", "[flint][bar]")
{
	// Wood: half a second in, the upper modes have died away while the fundamental rings on (T60 1.8 s).
	auto parameters = defaults();
	parameters.decay = 0.6;
	parameters.variation = 0.0; // the same contact noise on both strikes
	const auto isolated = hit(parameters, 0.02);
	const auto onsetStep = support::largestStep(isolated);

	MalletBar bar;
	bar.prepare(sampleRate, 64);
	bar.activate(parameters);
	bar.update(parameters);
	bar.trigger({ 1.0, 60, 1 });
	std::vector<double> output;
	support::renderInto(bar, output, 22'050); // 0.5 s
	const auto tailStep = support::largestStep(std::span(output).subspan(22'050 - 441));
	const auto before = highBandRms(std::span(output).subspan(22'050 - 220, 220), sampleRate);
	bar.trigger({ 1.0, 60, 2 });
	support::renderInto(bar, output, 441);
	REQUIRE(support::largestStep(std::span<const double>(output).subspan(22'050 - 220, 441)) <=
	    onsetStep + tailStep + 1.0e-6);
	const auto after = highBandRms(std::span(output).subspan(22'050, 220), sampleRate);
	INFO("high band before " << before << ", after " << after);
	REQUIRE(support::decibels(after / before) >= 6.0);
}

TEST_CASE("Flint bar build-up under fast strikes stays within 6.5 dB of one strike in loudness", "[flint][bar]")
{
	// The caps bound the bar's energy at +6 dB over one strike; a peak may rise a little more as the modes' relative phases
	// change over many strikes, so loudness is measured as the loudest 10 ms RMS. The tube is a resonance that swells
	// under sustained playing, as a real one does: with it the peak stays within +8.5 dB (measured +7.8-8.0 dB,
	// 3 October 2026).
	auto loudest = [](const std::vector<double>& samples)
	{
		auto best = 0.0;
		for (std::size_t start = 0; start + 441 <= samples.size(); start += 110)
			best = std::max(best, support::rms(std::span(samples).subspan(start, 441)));
		return best;
	};
	for (const auto resonator : { 0.0, 0.5 })
	{
		auto parameters = defaults();
		parameters.decay = 1.0;
		parameters.malletBar.material = 1.0;
		parameters.malletBar.resonator = resonator;
		const auto single = hit(parameters, 0.1);
		MalletBar bar;
		bar.prepare(sampleRate, 64);
		bar.activate(parameters);
		bar.update(parameters);
		std::vector<double> output;
		for (auto strike = 0; strike < 32; ++strike)
		{
			bar.trigger({ 1.0, 60, static_cast<std::uint64_t>(strike + 1) });
			support::renderInto(bar, output, 2'205);
		}
		const auto loudness = support::decibels(loudest(output) / loudest(single));
		const auto peakRise = support::decibels(support::peak(output) / support::peak(single));
		INFO("resonator " << resonator << ": loudness +" << loudness << " dB, peak +" << peakRise << " dB");
		if (resonator <= 0.0)
			REQUIRE(loudness <= 6.5);
		else
			REQUIRE(peakRise <= 8.5);
	}
}

TEST_CASE("Flint bar glides a ringing tail to a new pitch without a step", "[flint][bar]")
{
	auto parameters = defaults();
	parameters.decay = 0.9;
	parameters.malletBar.material = 0.0;
	parameters.malletBar.resonator = 0.0;
	MalletBar bar;
	bar.prepare(sampleRate, 64);
	bar.activate(parameters);
	bar.update(parameters);
	bar.trigger({ 1.0, 60, 1 });
	std::vector<double> output;
	support::renderInto(bar, output, 8'820); // 200 ms: upper modes gone
	const auto ownStep = support::largestStep(std::span(output).subspan(4'410));
	parameters.pitch = 72.0; // an octave up
	bar.update(parameters);
	support::renderInto(bar, output, 8'820);
	// An octave up at most doubles a sinusoid's step.
	REQUIRE(support::largestStep(std::span<const double>(output).subspan(8'820, 1'323)) <= 2.0 * ownStep + 1.0e-6);
	const auto settled =
	    support::zeroCrossingFrequency(std::span<const double>(output).subspan(8'820 + 1'764, 4'410), sampleRate);
	const auto cents = 1'200.0 * std::log2(settled / (440.0 * std::exp2(3.0 / 12.0)));
	INFO("after the glide " << settled << " Hz, " << cents << " cents");
	REQUIRE(std::abs(cents) < 1.0);
}

TEST_CASE("Flint bar Note Off Damps shortens the tail to 80 ms and does nothing when off", "[flint][bar]")
{
	auto parameters = defaults();
	parameters.decay = 0.8;
	parameters.malletBar.material = 0.0;
	parameters.malletBar.resonator = 0.0;
	const auto undamped = hit(parameters, 0.6);
	auto renderReleased = [&](bool damps)
	{
		auto settings = parameters;
		settings.noteOffDamps = damps;
		MalletBar bar;
		bar.prepare(sampleRate, 64);
		bar.activate(settings);
		bar.update(settings);
		bar.trigger({ 1.0, 60, 1 });
		std::vector<double> output;
		support::renderInto(bar, output, 4'416);
		bar.release(60);
		support::renderInto(bar, output, static_cast<std::size_t>(0.6 * sampleRate) - 4'416);
		return output;
	};
	const auto ignored = renderReleased(false);
	REQUIRE(std::equal(
	    ignored.begin(), ignored.end(), undamped.begin(), [](double a, double b) { return juce::exactlyEqual(a, b); }));
	const auto damped = renderReleased(true);
	const auto measured = support::t60(window(damped, 0.11, 0.5), sampleRate, -3.0, -30.0);
	INFO("damped T60 " << measured);
	REQUIRE(std::abs(measured / MalletBar::dampedT60Seconds - 1.0) < 0.2);
	REQUIRE(support::largestStep(window(damped, 0.095, 0.12)) <=
	    support::largestStep(window(undamped, 0.095, 0.12)) + 1.0e-6);
}

TEST_CASE("Flint bar brightens with velocity, every partial growing with it, and shortens its contact with Hardness",
    "[flint][bar]")
{
	auto parameters = defaults();
	parameters.malletBar.resonator = 0.0;
	auto previousBrightness = 0.0;
	std::array<double, 4> previousLevels {};
	const auto ratios = MalletBar::ratios(parameters.malletBar.overtones);
	for (auto step = 1; step <= 10; ++step)
	{
		const auto velocity = 0.1 * step;
		const auto output = hit(parameters, 0.1, sampleRate, { velocity, 60, 1 });
		const auto early = window(output, 0.0, 0.05);
		const auto value = support::brightness(early);
		INFO("velocity " << velocity << ", brightness " << value);
		REQUIRE(value > previousBrightness);
		previousBrightness = value;
		// No partial dips as velocity rises, as a half-sine contact's spectral nulls would make one.
		for (std::size_t mode = 0; mode < 4; ++mode)
		{
			const auto level = componentAt(early, MalletBar::pitchHz(60.0) * ratios[mode], sampleRate);
			INFO("mode " << mode + 1 << ", level " << level);
			if (step > 1 && previousLevels[mode] > 1.0e-4) // partials actually present, above -80 dB
				REQUIRE(level > previousLevels[mode]);
			previousLevels[mode] = level;
		}
	}
	REQUIRE(MalletBar::contactSeconds(1.0) < MalletBar::contactSeconds(0.5));
	REQUIRE(MalletBar::contactSeconds(0.5) < MalletBar::contactSeconds(0.0));
}

TEST_CASE(
    "Flint bar at marimba Overtones sounds partials at 1:4:10 and its tube stays silent at Resonator 0", "[flint][bar]")
{
	auto parameters = defaults();
	parameters.malletBar.material = 1.0; // metal: the partials ring long enough to measure
	parameters.malletBar.resonator = 0.0;
	parameters.malletBar.position = 0.5; // off centre, so mode 2 sounds
	parameters.decay = 0.8;
	parameters.tone = 1.0;
	MalletBar bar;
	const auto output = support::strikeAndRender(bar, parameters, sampleRate, 1.0);
	REQUIRE_FALSE(bar.tubeRinging());
	const auto fundamental = MalletBar::pitchHz(60.0);
	const auto segment = window(output, 0.0, 1.0);
	for (const auto ratio : { 1.0, 4.0, 10.0 })
	{
		const auto found = peakFrequencyNear(segment, fundamental * ratio, sampleRate);
		const auto cents = 1'200.0 * std::log2(found / (fundamental * ratio));
		INFO("ratio " << ratio << ": " << found << " Hz, " << cents << " cents");
		REQUIRE(std::abs(cents) < 5.0);
	}
}

TEST_CASE("Flint bar keeps its pitch and decay at the highest internal rate", "[flint][bar][slow]")
{
	auto parameters = defaults();
	parameters.decay = 0.2; // T60 0.17 s: the window reaches -40 dB
	parameters.malletBar.material = 0.0;
	parameters.malletBar.resonator = 0.0;
	const auto reference = hit(parameters, 0.3);
	const auto referenceT60 = support::t60(window(reference, 0.05, 0.3), sampleRate);
	const auto referenceFrequency = support::zeroCrossingFrequency(window(reference, 0.1, 0.3), sampleRate);
	// The body after the contact noise: at a high internal rate the noise carries inaudible energy above 20 kHz, which the
	// downsampling filter removes in the plugin.
	const auto referenceLevel = support::rms(window(reference, 0.02, 0.3));
	for (const auto rate : { 96'000.0, vekt::dsp::maximumInternalSampleRate })
	{
		const auto output = hit(parameters, 0.3, rate);
		const auto measuredT60 = support::t60(window(output, 0.05, 0.3, rate), rate);
		const auto frequency = support::zeroCrossingFrequency(window(output, 0.1, 0.3, rate), rate);
		INFO("rate " << rate << ": T60 " << measuredT60 << " vs " << referenceT60 << ", " << frequency << " vs "
		             << referenceFrequency);
		REQUIRE(std::abs(measuredT60 / referenceT60 - 1.0) < 0.05);
		REQUIRE(std::abs(1'200.0 * std::log2(frequency / referenceFrequency)) < 1.0);
		REQUIRE(std::abs(support::decibels(support::rms(window(output, 0.02, 0.3, rate)) / referenceLevel)) < 0.5);
	}
}

TEST_CASE("Flint bar contact has no spectral nulls: no partial dips as Hardness sweeps", "[flint][bar]")
{
	// A contact with spectral nulls (a half-sine's) would carve a partial out at some hardness: a dip of more than 6 dB
	// below both neighbouring hardness steps.
	auto parameters = defaults();
	parameters.malletBar.resonator = 0.0;
	parameters.malletBar.position = 0.5;
	parameters.variation = 0.0;
	const auto ratios = MalletBar::ratios(parameters.malletBar.overtones);
	std::vector<std::array<double, 4>> levels;
	for (auto step = 0; step <= 24; ++step)
	{
		parameters.malletBar.hardness = static_cast<double>(step) / 24.0;
		const auto early = hit(parameters, 0.06);
		std::array<double, 4> partials {};
		for (std::size_t mode = 0; mode < 4; ++mode)
			partials[mode] = componentAt(window(early, 0.0, 0.06), MalletBar::pitchHz(60.0) * ratios[mode], sampleRate);
		levels.push_back(partials);
	}
	for (std::size_t step = 1; step + 1 < levels.size(); ++step)
		for (std::size_t mode = 0; mode < 4; ++mode)
		{
			const auto neighbours = std::min(levels[step - 1][mode], levels[step + 1][mode]);
			INFO("hardness step " << step << ", mode " << mode + 1);
			if (neighbours > 1.0e-4) REQUIRE(support::decibels(levels[step][mode] / neighbours) > -6.0);
		}
}

TEST_CASE("Flint bar allocates nothing while striking, gliding and ringing", "[flint][bar][allocation]")
{
	auto parameters = defaults();
	MalletBar bar;
	bar.prepare(sampleRate, 64);
	bar.activate(parameters);
	std::vector<float> left(64), right(64);
	vekt::test::AllocationScope scope;
	bar.update(parameters);
	bar.trigger({ 0.8, 60, 5 });
	for (auto block = 0; block < 50; ++block) bar.process(left, right);
	parameters.pitch = 67.0;
	bar.update(parameters);
	bar.trigger({ 0.5, 60, 6 });
	for (auto block = 0; block < 50; ++block) bar.process(left, right);
	const auto counts = scope.stop();
	REQUIRE(counts.none());
}

TEST_CASE("Flint bar glides across the 20 kHz ceiling without a step", "[flint][bar]")
{
	// At marimba Overtones mode 3 (10x) crosses 20 kHz between A6 and C7; it fades out instead of stopping.
	auto parameters = defaults();
	parameters.pitch = 93.0; // A6
	parameters.decay = 0.9;
	parameters.tone = 1.0;
	parameters.malletBar.material = 1.0;
	parameters.malletBar.position = 0.5;
	parameters.malletBar.resonator = 0.0;
	parameters.variation = 0.0;
	MalletBar bar;
	bar.prepare(sampleRate, 64);
	bar.activate(parameters);
	bar.update(parameters);
	bar.trigger({ 1.0, 60, 1 });
	std::vector<double> output;
	support::renderInto(bar, output, 2'205);
	const auto ownStep = support::largestStep(std::span(output).subspan(441));
	parameters.pitch = 96.0; // C7
	bar.update(parameters);
	support::renderInto(bar, output, 2'205);
	// Near 20 kHz a mode steps by nearly its whole amplitude every sample anyway, so a cut shows instead as a broadband
	// click in the band between modes 2 (7–8.4 kHz) and 3 (17.6–20 kHz). There the glide stays as quiet as the steady
	// ring; a cut mode measured 4.7 times the steady peak.
	std::array<vekt::dsp::LinearTptSvf, 8> stages;
	for (auto& stage : stages) stage.prepare(sampleRate);
	std::vector<double> between;
	for (auto sample : output)
	{
		for (std::size_t stage = 0; stage < 4; ++stage)
			sample = stages[stage].process(sample, 14'000.0, std::numbers::sqrt2).lowPass;
		for (std::size_t stage = 4; stage < stages.size(); ++stage)
			sample = stages[stage].process(sample, 12'000.0, std::numbers::sqrt2).highPass;
		between.push_back(sample);
	}
	const auto steadyClick = support::peak(std::span<const double>(between).subspan(441, 1'764));
	const auto glideClick = support::peak(std::span<const double>(between).subspan(2'205, 1'323));
	INFO("12-14 kHz peak: steady " << steadyClick << ", glide " << glideClick);
	REQUIRE(glideClick <= 1.5 * steadyClick);
	REQUIRE(support::largestStep(std::span<const double>(output).subspan(2'205, 1'323)) <= 1.25 * ownStep);
}

TEST_CASE("Flint bar stays finite and free of DC at extreme settings", "[flint][bar]")
{
	auto parameters = defaults();
	parameters.pitch = 108.0;
	parameters.decay = 0.5;
	parameters.tone = 1.0;
	parameters.attack = 1.0;
	parameters.malletBar = { 1.0, 1.0, 1.0, 0.0, 1.0 };
	const auto output = hit(parameters, 1.0);
	REQUIRE(std::all_of(output.begin(), output.end(), [](double x) { return std::isfinite(x); }));
	REQUIRE(std::abs(support::mean(window(output, 0.9, 1.0))) < 1.0e-4);
}
