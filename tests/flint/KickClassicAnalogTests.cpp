#include "AllocationCounter.h"
#include "FlintTestSupport.h"

#include <vekt/dsp/OversamplingQuality.h>
#include "KickClassicAnalog.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace
{
using vekt::flint::FlintParameters;
using vekt::flint::KickClassicAnalog;
using vekt::flint::Strike;
namespace support = vekt::test::flint;

constexpr double sampleRate = 44'100.0;

FlintParameters defaults() { return {}; }

std::vector<double> hit(
    const FlintParameters& parameters, double seconds, double rate = sampleRate, Strike strike = { 1.0, 60, 1 })
{
	KickClassicAnalog kick;
	return support::strikeAndRender(kick, parameters, rate, seconds, strike);
}

std::span<const double> window(
    const std::vector<double>& samples, double fromSeconds, double toSeconds, double rate = sampleRate)
{
	const auto from = static_cast<std::size_t>(fromSeconds * rate);
	const auto to = std::min(samples.size(), static_cast<std::size_t>(toSeconds * rate));
	return std::span(samples).subspan(from, to - from);
}
}

TEST_CASE("Flint kick peaks at -6 dBFS for a default full-velocity hit, whatever its Attack", "[flint][kick]")
{
	const auto reference = support::peak(hit(defaults(), 0.2));
	INFO("default peak " << reference);
	REQUIRE(std::abs(support::decibels(reference / 0.5)) < 1.0);
	for (const auto attack : { 0.0, 0.5, 1.0 })
	{
		auto parameters = defaults();
		parameters.attack = attack;
		const auto level = support::peak(hit(parameters, 0.2));
		INFO("attack " << attack << ", peak " << level);
		REQUIRE(std::abs(support::decibels(level / reference)) < 1.0);
	}
}

TEST_CASE("Flint kick keeps its level with Sweep", "[flint][kick]")
{
	// A full-accent hit's resonance is set to bodyLevel along the path it takes, sweep included, so Sweep never makes a
	// hit louder. A fast, deep sweep starts above the tone stage's cutoff and loses up to 3.2 dB there (measured 3
	// October 2026); predicting the strike without the sweep made the same hits up to 8.6 dB louder.
	const auto reference = support::peak(hit(defaults(), 0.3));
	for (const auto sweep : { 0.25, 0.5, 1.0 })
		for (const auto sweepTime : { 0.0, 0.3, 1.0 })
		{
			auto parameters = defaults();
			parameters.kickClassicAnalog.sweep = sweep;
			parameters.kickClassicAnalog.sweepTime = sweepTime;
			const auto change = support::decibels(support::peak(hit(parameters, 0.6)) / reference);
			INFO("Sweep " << sweep << ", Sweep Time " << sweepTime << ": " << change << " dB");
			REQUIRE(change <= 0.5);
			REQUIRE(change >= -4.0);
		}
}

TEST_CASE("Flint kick settles on its pitch within one cent across its range and clamps outside it", "[flint][kick]")
{
	// Measured from -30 to -60 dB of the default 333 ms decay, where the pitch sigh has faded below 0.6 cent.
	for (const auto note : { 23.0, 33.0, 45.0, 57.0, 72.0 })
	{
		auto parameters = defaults();
		parameters.pitch = note;
		const auto output = hit(parameters, 0.4);
		const auto frequency = support::zeroCrossingFrequency(window(output, 0.17, 0.33), sampleRate);
		const auto expected = 440.0 * std::exp2((note - 69.0) / 12.0); // equal temperament, A4 = 440 Hz
		const auto cents = 1'200.0 * std::log2(frequency / expected);
		INFO("note " << note << ", " << frequency << " Hz, " << cents << " cents");
		REQUIRE(std::abs(cents) < 1.0);
	}
	REQUIRE(juce::exactlyEqual(KickClassicAnalog::pitchHz(12.0), KickClassicAnalog::pitchHz(23.0)));
	REQUIRE(juce::exactlyEqual(KickClassicAnalog::pitchHz(100.0), KickClassicAnalog::pitchHz(72.0)));
}

TEST_CASE("Flint kick rings longer as Decay rises", "[flint][kick]")
{
	auto previous = 0.0;
	for (const auto decay : { 0.1, 0.3, 0.5, 0.7 })
	{
		auto parameters = defaults();
		parameters.decay = decay;
		const auto expected = KickClassicAnalog::t60Seconds(decay);
		const auto measured = support::t60(hit(parameters, 1.2 * expected + 0.1), sampleRate);
		INFO("decay " << decay << ": T60 " << measured << " s, nominal " << expected << " s");
		REQUIRE(measured > previous);
		REQUIRE(std::abs(measured / expected - 1.0) < 0.15);
		previous = measured;
	}
}

TEST_CASE("Flint kick brightens with Tone, its click included", "[flint][kick]")
{
	auto previous = 0.0;
	for (const auto tone : { 0.0, 0.25, 0.5, 0.75, 1.0 })
	{
		auto parameters = defaults();
		parameters.tone = tone;
		parameters.kickClassicAnalog.click = 0.5;
		const auto output = hit(parameters, 0.05);
		const auto value = support::brightness(window(output, 0.0, 0.02));
		INFO("tone " << tone << ", brightness " << value);
		REQUIRE(value > previous);
		previous = value;
	}
}

TEST_CASE("Flint kick restarts on a retrigger without a step beyond its own", "[flint][kick]")
{
	for (const auto sweep : { 0.0, 1.0 })
	{
		auto parameters = defaults();
		parameters.decay = 0.8;
		parameters.kickClassicAnalog.sweep = sweep;
		parameters.kickClassicAnalog.sweepTime = sweep;
		INFO("sweep " << sweep);
		const auto isolated = hit(parameters, 0.02);
		const auto onsetStep = support::largestStep(isolated);

		KickClassicAnalog kick;
		kick.prepare(sampleRate, 64);
		kick.activate(parameters);
		kick.update(parameters);
		kick.trigger({ 1.0, 60, 1 });
		std::vector<double> output;
		support::renderInto(kick, output, 4'410); // 100 ms
		const auto tailStep = support::largestStep(std::span(output).subspan(4'410 - 441));
		kick.trigger({ 1.0, 60, 2 });
		support::renderInto(kick, output, 441);
		const auto around = std::span<const double>(output).subspan(4'410 - 220, 441);
		REQUIRE(support::largestStep(around) <= onsetStep + tailStep + 1.0e-6);
	}
}

TEST_CASE("Flint kick build-up under fast strikes stays within 6.5 dB of one strike", "[flint][kick]")
{
	for (const auto sweep : { 0.0, 1.0 })
	{
		auto parameters = defaults();
		parameters.decay = 1.0;
		parameters.kickClassicAnalog.sweep = sweep;
		parameters.kickClassicAnalog.sweepTime = sweep;
		const auto single = support::peak(hit(parameters, 0.1));
		KickClassicAnalog kick;
		kick.prepare(sampleRate, 64);
		kick.activate(parameters);
		kick.update(parameters);
		std::vector<double> output;
		for (auto strike = 0; strike < 32; ++strike)
		{
			kick.trigger({ 1.0, 60, static_cast<std::uint64_t>(strike + 1) });
			support::renderInto(kick, output, 2'205); // 20 Hz
		}
		INFO("sweep " << sweep << ": single " << single << ", repeated " << support::peak(output));
		REQUIRE(support::decibels(support::peak(output) / single) <= 6.5);
	}
}

TEST_CASE("Flint kick Note Off Damps shortens the tail to 60 ms and does nothing when off", "[flint][kick]")
{
	auto parameters = defaults();
	parameters.decay = 0.8;
	const auto undamped = hit(parameters, 0.5);

	auto renderReleased = [&](bool damps)
	{
		auto settings = parameters;
		settings.noteOffDamps = damps;
		KickClassicAnalog kick;
		kick.prepare(sampleRate, 64);
		kick.activate(settings);
		kick.update(settings);
		kick.trigger({ 1.0, 60, 1 });
		std::vector<double> output;
		support::renderInto(kick, output, 4'410);
		kick.release(60);
		support::renderInto(kick, output, static_cast<std::size_t>(0.4 * sampleRate));
		return output;
	};
	const auto ignored = renderReleased(false);
	REQUIRE(std::equal(
	    ignored.begin(), ignored.end(), undamped.begin(), [](double a, double b) { return juce::exactlyEqual(a, b); }));
	const auto damped = renderReleased(true);
	const auto measured = support::t60(window(damped, 0.1, 0.4), sampleRate, -3.0, -30.0);
	INFO("damped T60 " << measured);
	REQUIRE(std::abs(measured / KickClassicAnalog::dampedT60Seconds - 1.0) < 0.2);
	REQUIRE(support::largestStep(window(damped, 0.095, 0.11)) <=
	    support::largestStep(window(undamped, 0.095, 0.11)) + 1.0e-6);
}

TEST_CASE("Flint kick extensions at zero leave the circuit untouched, whatever the variation", "[flint][kick]")
{
	auto parameters = defaults();
	parameters.variation = 1.0;
	const auto first = hit(parameters, 0.3, sampleRate, { 1.0, 60, 11 });
	const auto second = hit(parameters, 0.3, sampleRate, { 1.0, 60, 9'999 });
	auto otherSweepTime = parameters;
	otherSweepTime.kickClassicAnalog.sweepTime = 0.9; // irrelevant at Sweep 0
	const auto third = hit(otherSweepTime, 0.3, sampleRate, { 1.0, 60, 11 });
	auto same = [](const std::vector<double>& a, const std::vector<double>& b)
	{ return std::equal(a.begin(), a.end(), b.begin(), [](double x, double y) { return juce::exactlyEqual(x, y); }); };
	REQUIRE(same(first, second));
	REQUIRE(same(first, third));
}

TEST_CASE("Flint kick follows the circuit's attack shift and pitch sigh", "[flint][kick]")
{
	// At C5 the attack's 6 ms hold several periods: the resonance sits more than an octave up, then drops back.
	auto parameters = defaults();
	parameters.pitch = 72.0;
	parameters.decay = 0.7;
	const auto output = hit(parameters, 0.6);
	const auto base = KickClassicAnalog::pitchHz(72.0);
	const auto attack = support::zeroCrossingFrequency(window(output, 0.0015, 0.0055), sampleRate);
	INFO("attack " << attack << " Hz over a base of " << base << " Hz");
	REQUIRE(attack > 2.0 * base);
	// The sigh: well above the settled pitch early on, back within a cent once the note has decayed 40 dB.
	auto low = defaults();
	low.decay = 0.7;
	const auto note = hit(low, 1.6);
	const auto settled = KickClassicAnalog::pitchHz(low.pitch);
	const auto early = support::zeroCrossingFrequency(window(note, 0.02, 0.12), sampleRate);
	const auto late = support::zeroCrossingFrequency(window(note, 1.0, 1.5), sampleRate);
	INFO("early " << early << " Hz, late " << late << " Hz, settled " << settled << " Hz");
	REQUIRE(early > 1.05 * settled);
	REQUIRE(early < 1.25 * settled);
	REQUIRE(std::abs(1'200.0 * std::log2(late / settled)) < 1.0);
}

TEST_CASE("Flint kick keeps its pitch, decay and level at the highest internal rate", "[flint][kick][slow]")
{
	auto parameters = defaults();
	parameters.decay = 0.3;
	const auto reference = hit(parameters, 0.25);
	const auto referenceT60 = support::t60(reference, sampleRate);
	for (const auto rate : { 96'000.0, vekt::dsp::maximumInternalSampleRate })
	{
		const auto output = hit(parameters, 0.25, rate);
		const auto measuredT60 = support::t60(output, rate);
		const auto frequency = support::zeroCrossingFrequency(window(output, 0.12, 0.24, rate), rate);
		const auto expected = support::zeroCrossingFrequency(window(reference, 0.12, 0.24), sampleRate);
		INFO("rate " << rate << ": T60 " << measuredT60 << " vs " << referenceT60 << ", " << frequency << " vs "
		             << expected);
		REQUIRE(std::abs(measuredT60 / referenceT60 - 1.0) < 0.05);
		REQUIRE(std::abs(1'200.0 * std::log2(frequency / expected)) < 1.0);
		// The strike is predicted at 96 kHz at most, so its level holds at every rate (A1's -6 dBFS calibration).
		const auto change = support::decibels(support::peak(output) / support::peak(reference));
		INFO("peak change " << change << " dB");
		REQUIRE(std::abs(change) < 0.5);
	}
}

TEST_CASE("Flint kick allocates nothing while striking and ringing", "[flint][kick][allocation]")
{
	auto parameters = defaults();
	parameters.kickClassicAnalog.click = 0.5;
	parameters.kickClassicAnalog.sweep = 0.5;
	parameters.kickClassicAnalog.bodyShape = 0.5;
	KickClassicAnalog kick;
	kick.prepare(sampleRate, 64);
	kick.activate(parameters);
	std::vector<float> left(64), right(64);
	vekt::test::AllocationScope scope;
	kick.update(parameters);
	kick.trigger({ 0.8, 60, 5 });
	for (auto block = 0; block < 100; ++block) kick.process(left, right);
	kick.release(60);
	const auto counts = scope.stop();
	REQUIRE(counts.none());
}

TEST_CASE("Flint kick glides a ringing tail to a new pitch without a step", "[flint][kick]")
{
	auto parameters = defaults();
	parameters.decay = 0.8; // T60 2.8 s
	KickClassicAnalog kick;
	kick.prepare(sampleRate, 64);
	kick.activate(parameters);
	kick.update(parameters);
	kick.trigger({ 1.0, 60, 1 });
	std::vector<double> output;
	support::renderInto(kick, output, 8'820);
	const auto ownStep = support::largestStep(std::span(output).subspan(4'410));
	parameters.pitch = 45.0; // an octave up: A2, 110 Hz
	kick.update(parameters);
	support::renderInto(kick, output, static_cast<std::size_t>(2.4 * sampleRate));
	REQUIRE(support::largestStep(std::span<const double>(output).subspan(8'820, 1'323)) <= 2.0 * ownStep + 1.0e-6);
	// Below -45 dB the pitch sigh lifts less than 0.1 cent.
	const auto glided = support::zeroCrossingFrequency(window(output, 2.2, 2.6), sampleRate);
	INFO("glided to " << glided << " Hz");
	REQUIRE(std::abs(1'200.0 * std::log2(glided / 110.0)) < 1.0);
}

TEST_CASE("Flint kick stays finite and free of DC at extreme settings", "[flint][kick]")
{
	auto parameters = defaults();
	parameters.decay = 0.6;
	parameters.attack = 1.0;
	parameters.tone = 1.0;
	parameters.pitch = 12.0;
	parameters.kickClassicAnalog = { 1.0, 1.0, 1.0, 1.0 };
	const auto output = hit(parameters, 2.0);
	REQUIRE(std::all_of(output.begin(), output.end(), [](double x) { return std::isfinite(x); }));
	REQUIRE(std::abs(support::mean(window(output, 1.9, 2.0))) < 1.0e-4);
}

TEST_CASE("Flint kick sweeps the same at the highest internal rate", "[flint][kick][slow]")
{
	// The sweep's pitch lift over the same kick without a sweep, at each rate: the pitch sigh, which follows the level,
	// is common to both renders and cancels.
	auto lift = [](double rate)
	{
		auto swept = defaults();
		swept.kickClassicAnalog.sweep = 0.6;
		swept.kickClassicAnalog.sweepTime = 0.6;
		const auto withSweep = hit(swept, 0.08, rate);
		const auto without = hit(defaults(), 0.08, rate);
		return support::zeroCrossingFrequency(window(withSweep, 0.01, 0.08, rate), rate) /
		    support::zeroCrossingFrequency(window(without, 0.01, 0.08, rate), rate);
	};
	const auto reference = lift(sampleRate);
	const auto fast = lift(vekt::dsp::maximumInternalSampleRate);
	INFO("sweep lift " << fast << " at the highest rate, " << reference << " at 44.1 kHz");
	// Within 2 cents of a 40 % lift (measured 1.2 cents, 3 October 2026): zero crossings average a moving pitch, and the
	// strike prediction (96 kHz at most) leaves a small level difference between rates that the sigh turns into pitch.
	// A sweep envelope stepping at the wrong rate would be tens of cents off.
	REQUIRE(std::abs(1'200.0 * std::log2(fast / reference)) < 2.0);
}
