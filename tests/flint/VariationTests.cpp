#include "FlintTestSupport.h"
#include "HitRandom.h"
#include "KickClassicAnalog.h"
#include "MalletBar.h"

#include <vekt/dsp/LinearTptSvf.h>

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <set>
#include <vector>

namespace
{
using vekt::flint::FlintEngine;
using vekt::flint::FlintParameters;
using vekt::flint::KickClassicAnalog;
using vekt::flint::MalletBar;
using vekt::flint::Mode;
namespace support = vekt::test::flint;

constexpr double sampleRate = 44'100.0;

template <typename Engine>
std::vector<double> hit(const FlintParameters& parameters, std::uint64_t hash, double seconds)
{
	Engine engine;
	return support::strikeAndRender(engine, parameters, sampleRate, seconds, { 1.0, 60, hash });
}

FlintParameters kickParameters(double variation)
{
	FlintParameters parameters;
	parameters.variation = variation;
	parameters.kickClassicAnalog.click = 0.3;
	return parameters;
}

FlintParameters barParameters(double variation)
{
	FlintParameters parameters;
	parameters.mode = Mode::mallet;
	parameters.pitch = 60.0;
	parameters.variation = variation;
	parameters.malletBar.resonator = 0.0;
	return parameters;
}

std::vector<double> highBand(std::span<const double> samples)
{
	vekt::dsp::LinearTptSvf filter;
	filter.prepare(sampleRate);
	std::vector<double> filtered;
	for (const auto sample : samples) filtered.push_back(filter.process(sample, 2'000.0, std::numbers::sqrt2).highPass);
	return filtered;
}

double correlation(const std::vector<double>& a, const std::vector<double>& b)
{
	auto ab = 0.0, aa = 0.0, bb = 0.0;
	for (std::size_t index = 0; index < std::min(a.size(), b.size()); ++index)
	{
		ab += a[index] * b[index];
		aa += a[index] * a[index];
		bb += b[index] * b[index];
	}
	return ab / std::sqrt(aa * bb);
}

bool identical(const std::vector<double>& a, const std::vector<double>& b)
{
	return a.size() == b.size() &&
	    std::equal(a.begin(), a.end(), b.begin(), [](double x, double y) { return juce::exactlyEqual(x, y); });
}

std::span<const double> window(const std::vector<double>& samples, double fromSeconds, double toSeconds)
{
	const auto from = static_cast<std::size_t>(fromSeconds * sampleRate);
	const auto to = std::min(samples.size(), static_cast<std::size_t>(toSeconds * sampleRate));
	return std::span(samples).subspan(from, to - from);
}
}

TEST_CASE(
    "Flint hit hash depends on seed, song position and order, and rounds positions to its grid", "[flint][variation]")
{
	using vekt::flint::hitHash;
	using vekt::flint::songPositionKey;
	using vekt::flint::stoppedHitHash;
	const auto at = [](double beats) { return songPositionKey(beats); };
	REQUIRE(hitHash(7, at(8.0), 0) == hitHash(7, at(8.0 + 1.0e-9), 0)); // floating noise rounds away
	REQUIRE(hitHash(7, at(8.0), 0) != hitHash(7, at(8.0 + 1.0 / 3'840.0), 0));
	REQUIRE(hitHash(7, at(8.0), 0) != hitHash(7, at(8.0), 1));
	REQUIRE(hitHash(7, at(8.0), 0) != hitHash(8, at(8.0), 0));
	REQUIRE(stoppedHitHash(7, 1) != stoppedHitHash(7, 2));
	REQUIRE(stoppedHitHash(7, 0) != hitHash(7, 0, 0));
	std::set<std::uint64_t> hashes;
	for (auto sixteenth = 0; sixteenth < 4'096; ++sixteenth) hashes.insert(hitHash(1, at(0.25 * sixteenth), 0));
	REQUIRE(hashes.size() == 4'096);
}

TEST_CASE("Flint variation draws are bell-shaped within their range", "[flint][variation]")
{
	vekt::flint::HitRandom random(12'345);
	auto middle = 0, count = 0;
	for (; count < 20'000; ++count)
	{
		const auto value = random.bell();
		REQUIRE(std::abs(value) <= 1.0);
		if (std::abs(value) < 0.5) ++middle;
	}
	// The mean of three uniform draws lies in the middle half of its range three times in four.
	REQUIRE(static_cast<double>(middle) / count > 0.7);
}

TEST_CASE("Flint hits at Variation 0 are identical whatever their hash", "[flint][variation]")
{
	REQUIRE(identical(
	    hit<KickClassicAnalog>(kickParameters(0.0), 1, 0.2), hit<KickClassicAnalog>(kickParameters(0.0), 99, 0.2)));
	REQUIRE(identical(hit<MalletBar>(barParameters(0.0), 1, 0.2), hit<MalletBar>(barParameters(0.0), 99, 0.2)));
}

TEST_CASE("Flint hits with the same hash are identical", "[flint][variation]")
{
	REQUIRE(identical(
	    hit<KickClassicAnalog>(kickParameters(1.0), 42, 0.2), hit<KickClassicAnalog>(kickParameters(1.0), 42, 0.2)));
	REQUIRE(identical(hit<MalletBar>(barParameters(1.0), 42, 0.2), hit<MalletBar>(barParameters(1.0), 42, 0.2)));
}

TEST_CASE("Flint kick at full Variation keeps its low end and varies its transient", "[flint][variation]")
{
	const auto reference = hit<KickClassicAnalog>(kickParameters(0.0), 1, 0.4);
	const auto settled = support::zeroCrossingFrequency(window(reference, 0.17, 0.33), sampleRate);
	const auto referenceBody = support::rms(window(reference, 0.05, 0.3));
	std::vector<double> previousTransient;
	for (std::uint64_t hash = 1; hash <= 32; ++hash)
	{
		const auto output = hit<KickClassicAnalog>(kickParameters(1.0), hash, 0.4);
		const auto frequency = support::zeroCrossingFrequency(window(output, 0.17, 0.33), sampleRate);
		INFO("hash " << hash);
		REQUIRE(std::abs(1'200.0 * std::log2(frequency / settled)) < 0.1);
		REQUIRE(std::abs(support::decibels(support::rms(window(output, 0.05, 0.3)) / referenceBody)) < 0.1);
		auto transient = highBand(window(output, 0.0, 0.02));
		if (!previousTransient.empty())
		{
			INFO("correlation " << correlation(transient, previousTransient));
			REQUIRE(correlation(transient, previousTransient) < 0.9);
		}
		previousTransient = std::move(transient);
	}
}

TEST_CASE("Flint bar at full Variation keeps its modes and varies its transient", "[flint][variation]")
{
	auto parameters = barParameters(0.0);
	parameters.decay = 0.6;
	const auto reference = hit<MalletBar>(parameters, 1, 0.8);
	const auto settled = support::zeroCrossingFrequency(window(reference, 0.4, 0.8), sampleRate);
	const auto referenceT60 = support::t60(window(reference, 0.1, 0.8), sampleRate, -3.0, -15.0);
	std::vector<double> previousTransient;
	parameters.variation = 1.0;
	for (std::uint64_t hash = 1; hash <= 16; ++hash)
	{
		const auto output = hit<MalletBar>(parameters, hash, 0.8);
		INFO("hash " << hash);
		REQUIRE(std::abs(1'200.0 *
		            std::log2(support::zeroCrossingFrequency(window(output, 0.4, 0.8), sampleRate) / settled)) < 0.1);
		REQUIRE(std::abs(support::t60(window(output, 0.1, 0.8), sampleRate, -3.0, -15.0) / referenceT60 - 1.0) < 0.01);
		auto transient = highBand(window(output, 0.0, 0.02));
		if (!previousTransient.empty())
		{
			INFO("correlation " << correlation(transient, previousTransient));
			REQUIRE(correlation(transient, previousTransient) < 0.9);
		}
		previousTransient = std::move(transient);
	}
}

TEST_CASE("Flint hit noise keeps its level at every internal sample rate", "[flint][variation]")
{
	// The noise is low-passed at 18 kHz and scaled with the rate, so its level is the same at 1x, 2x and 16x (A29).
	auto level = [](double rate)
	{
		vekt::flint::HitNoise noise;
		noise.prepare(rate);
		noise.start(7, 1.0);
		const auto count = static_cast<std::size_t>(0.1 * rate);
		auto sum = 0.0;
		for (std::size_t index = 0; index < count; ++index)
		{
			const auto value = noise.next();
			sum += value * value;
		}
		return std::sqrt(sum / static_cast<double>(count));
	};
	const auto reference = level(44'100.0);
	for (const auto rate : { 96'000.0, 16.0 * 44'100.0 })
	{
		INFO("rate " << rate);
		REQUIRE(std::abs(support::decibels(level(rate) / reference)) < 0.5);
	}
}
