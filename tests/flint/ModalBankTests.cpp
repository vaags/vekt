#include "FlintTestSupport.h"

#include <vekt/dsp/OversamplingQuality.h>
#include "ModalBank.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <vector>

namespace
{
using vekt::flint::ModalBank;
using vekt::flint::strikeScale;
namespace support = vekt::test::flint;

std::vector<double> ring(ModalBank& bank, std::size_t count)
{
	std::vector<double> output;
	output.reserve(count);
	output.push_back(bank.process(1.0));
	for (std::size_t index = 1; index < count; ++index) output.push_back(bank.process(0.0));
	return output;
}
}

TEST_CASE("Flint modal bank rings at its frequency with its T60", "[flint][modal-bank]")
{
	for (const auto sampleRate : { 44'100.0, vekt::dsp::maximumInternalSampleRate })
	{
		ModalBank bank;
		bank.prepare(sampleRate);
		bank.setModeCount(1);
		bank.setInputGain(0, 1.0);
		bank.setOutputGain(0, 1.0);
		bank.setMode(0, 220.0, 0.5);
		const auto output = ring(bank, static_cast<std::size_t>(0.45 * sampleRate));
		INFO("sample rate " << sampleRate);
		const auto frequency = support::zeroCrossingFrequency(output, sampleRate);
		REQUIRE(std::abs(1'200.0 * std::log2(frequency / 220.0)) < 0.1);
		REQUIRE(std::abs(support::t60(output, sampleRate) / 0.5 - 1.0) < 0.02);
	}
}

TEST_CASE("Flint modal bank drops modes above 20 kHz at every sample rate", "[flint][modal-bank]")
{
	for (const auto sampleRate : { 96'000.0, vekt::dsp::maximumInternalSampleRate })
	{
		ModalBank bank;
		bank.prepare(sampleRate);
		bank.setModeCount(2);
		for (std::size_t mode = 0; mode < 2; ++mode)
		{
			bank.setInputGain(mode, 1.0);
			bank.setOutputGain(mode, 1.0);
		}
		bank.setMode(0, 1'000.0, 0.2);
		bank.setMode(1, 21'000.0, 0.2);
		static_cast<void>(bank.process(1.0));
		REQUIRE(std::norm(bank.state(1)) <= 0.0);
		REQUIRE(std::norm(bank.state(0)) > 0.0);
	}
}

TEST_CASE("Flint modal bank changes pitch without a step in its output", "[flint][modal-bank]")
{
	ModalBank bank;
	bank.prepare(44'100.0);
	bank.setModeCount(1);
	bank.setInputGain(0, 1.0);
	bank.setOutputGain(0, 1.0);
	bank.setMode(0, 110.0, 2.0);
	auto output = ring(bank, 4'410);
	const auto steady = support::largestStep(output);
	bank.setMode(0, 220.0, 2.0); // an octave at once
	for (auto index = 0; index < 4'410; ++index) output.push_back(bank.process(0.0));
	// Doubling the frequency at most doubles the per-sample step of a sinusoid of the same amplitude.
	REQUIRE(support::largestStep(output) <= 2.0 * steady + 1.0e-9);
}

TEST_CASE("Flint modal bank sleeps at -120 dBFS and wakes on input", "[flint][modal-bank]")
{
	ModalBank bank;
	bank.prepare(44'100.0);
	bank.setModeCount(1);
	bank.setInputGain(0, 1.0);
	bank.setOutputGain(0, 1.0);
	bank.setMode(0, 440.0, 0.05);
	static_cast<void>(ring(bank, 2'205)); // 50 ms: 60 dB down
	REQUIRE(bank.isActive());
	static_cast<void>(ring(bank, 4'410)); // another 100 ms: past -120 dB
	for (auto index = 0; index < 4'410; ++index) static_cast<void>(bank.process(0.0));
	REQUIRE_FALSE(bank.isActive());
	REQUIRE(bank.energy() <= 0.0);
	static_cast<void>(bank.process(0.5));
	REQUIRE(bank.isActive());
}

TEST_CASE("Flint strike cap lets a silent mode take the full strike and caps build-up at twice a strike",
    "[flint][modal-bank]")
{
	const std::complex<double> strike { 0.3, -0.1 };
	REQUIRE(juce::exactlyEqual(strikeScale({}, strike), 1.0));
	// In phase on a mode already at twice a strike: nothing more is added.
	REQUIRE(strikeScale(2.0 * strike, strike) <= 1.0e-12);
	// In phase on a mode at one strike: it may reach exactly twice a strike.
	const auto half = strikeScale(strike, strike);
	REQUIRE(std::abs(std::abs(strike + half * strike) - 2.0 * std::abs(strike)) < 1.0e-12);
	// Out of phase strikes are never reduced.
	REQUIRE(juce::exactlyEqual(strikeScale(-strike, strike), 1.0));
	// A loud mode does not stop a strike that leaves it no louder.
	REQUIRE(strikeScale(std::complex<double> { 0.0, 1.0 } * 5.0 * strike, strike) > 0.0);
}
