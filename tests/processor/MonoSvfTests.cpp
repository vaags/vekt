#include <vekt/dsp/OversamplingQuality.h>
#include "LinearTptSvf.h"
#include "SvfResponse.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

namespace
{
using vekt::mono::LinearTptSvf;
using vekt::mono::LinearTptSvfOutputs;
using vekt::mono::svfModeMix;

using Complex = std::complex<double>;

struct Response
{
	Complex lowPass, bandPass, highPass;
	[[nodiscard]] Complex notch() const { return lowPass + highPass; }
};

// The prewarped analog prototype the TPT SVF maps to: s = j tan(pi f / fs) / tan(pi fc / fs), poles at |s| = 1.
Response analyticResponse(double sampleRate, double cutoffHz, double k, double frequency)
{
	const auto s = Complex(0.0, std::tan(std::numbers::pi * frequency / sampleRate) / std::tan(std::numbers::pi * cutoffHz / sampleRate));
	const auto denominator = s * s + k * s + 1.0;
	return { 1.0 / denominator, s / denominator, s * s / denominator };
}

// The filter's frequency response at several frequencies: the DTFT of its impulse response, run for at least 40
// envelope time constants (Q / (pi fc), at least that of Q = 1) and until a whole block of it is below 1e-15. Near
// Nyquist the warped poles decay more slowly per sample than that time constant says, hence the tail check.
std::vector<Response> measuredResponse(double sampleRate, double cutoffHz, double k, const std::vector<double>& frequencies)
{
	LinearTptSvf svf;
	svf.prepare(sampleRate);
	const auto timeConstant = std::max(1.0, 1.0 / k) / (std::numbers::pi * cutoffHz);
	const auto minimumSamples = static_cast<long>(std::ceil(40.0 * timeConstant * sampleRate));
	std::vector<Response> responses(frequencies.size());
	std::vector<Complex> rotation, phasor(frequencies.size(), Complex(1.0, 0.0));
	for (const auto frequency : frequencies) rotation.push_back(std::polar(1.0, -2.0 * std::numbers::pi * frequency / sampleRate));
	double blockPeak {};
	for (long sample = 0; sample < minimumSamples || (sample & 1023) != 0 || blockPeak > 1.0e-15; ++sample)
	{
		if ((sample & 1023) == 0) blockPeak = 0.0;
		const auto out = svf.process(sample == 0 ? 1.0 : 0.0, cutoffHz, k);
		blockPeak = std::max({ blockPeak, std::abs(out.lowPass), std::abs(out.bandPass), std::abs(out.highPass) });
		for (std::size_t index = 0; index < frequencies.size(); ++index)
		{
			responses[index].lowPass += out.lowPass * phasor[index];
			responses[index].bandPass += out.bandPass * phasor[index];
			responses[index].highPass += out.highPass * phasor[index];
			phasor[index] *= rotation[index];
			if ((sample & 1023) == 0) phasor[index] /= std::abs(phasor[index]);
		}
	}
	return responses;
}

// svfModeMix is linear in LP and HP: its two coefficients at one Mode position.
std::pair<double, double> modeCoefficients(double mode)
{
	return { svfModeMix(mode, 1.0, 0.0), svfModeMix(mode, 0.0, 1.0) };
}

bool near(Complex measured, Complex expected, double tolerance)
{
	return std::abs(measured - expected) <= tolerance * std::max(1.0, std::abs(expected));
}
}

TEST_CASE("Mono linear SVF matches its prewarped analog response", "[mono][filter][svf]")
{
	constexpr double sampleRate = 48'000.0;
	for (const auto cutoff : { 100.0, 1'000.0, 10'000.0 })
		for (const auto k : { 2.0, std::numbers::sqrt2, 0.5, 0.05 })
		{
			const std::vector frequencies { cutoff / 4.0, cutoff / 2.0, cutoff, cutoff * 1.5, std::min(cutoff * 2.0, 23'000.0) };
			const auto measured = measuredResponse(sampleRate, cutoff, k, frequencies);
			for (std::size_t index = 0; index < frequencies.size(); ++index)
			{
				INFO("cutoff " << cutoff << " Hz, k " << k << ", " << frequencies[index] << " Hz");
				const auto expected = analyticResponse(sampleRate, cutoff, k, frequencies[index]);
				CHECK(near(measured[index].lowPass, expected.lowPass, 1.0e-6));
				CHECK(near(measured[index].bandPass, expected.bandPass, 1.0e-6));
				CHECK(near(measured[index].highPass, expected.highPass, 1.0e-6));
			}
		}
}

TEST_CASE("Mono linear SVF puts its pole and notch exactly at the cutoff, up to 0.45 fs", "[mono][filter][svf]")
{
	constexpr double sampleRate = 48'000.0;
	for (const auto cutoff : { 50.0, 440.0, 5'000.0, 15'000.0, 21'600.0 })
		for (const auto k : { 2.0, 0.5, 0.05 })
		{
			INFO("cutoff " << cutoff << " Hz, k " << k);
			const auto at = measuredResponse(sampleRate, cutoff, k, { cutoff }).front();
			CHECK(std::abs(std::abs(at.lowPass) * k - 1.0) < 1.0e-6);
			CHECK(std::abs(std::abs(at.bandPass) * k - 1.0) < 1.0e-6);
			CHECK(std::abs(std::abs(at.highPass) * k - 1.0) < 1.0e-6);
			// LP lags the input by 90 degrees at the pole frequency, and LP + HP = x - k BP cancels there.
			CHECK(std::abs(std::arg(at.lowPass) + std::numbers::pi / 2.0) < 1.0e-6);
			CHECK(std::abs(at.notch()) < 1.0e-6);
		}
}

TEST_CASE("Mono linear SVF passes DC through LP and Nyquist through HP", "[mono][filter][svf]")
{
	constexpr double sampleRate = 48'000.0;
	for (const auto k : { 2.0, 0.05 })
	{
		INFO("k " << k);
		const auto responses = measuredResponse(sampleRate, 1'000.0, k, { 0.0, sampleRate / 2.0 });
		const auto& dc = responses[0];
		const auto& nyquist = responses[1];
		CHECK(near(dc.lowPass, 1.0, 1.0e-9));
		CHECK(std::abs(dc.bandPass) < 1.0e-9);
		CHECK(std::abs(dc.highPass) < 1.0e-9);
		CHECK(near(dc.notch(), 1.0, 1.0e-9));
		CHECK(std::abs(nyquist.lowPass) < 1.0e-9);
		CHECK(std::abs(nyquist.bandPass) < 1.0e-9);
		CHECK(near(nyquist.highPass, 1.0, 1.0e-9));
		CHECK(near(nyquist.notch(), 1.0, 1.0e-9));
	}
}

TEST_CASE("Mono linear SVF stays exact at the lowest cutoff and highest effective rate", "[mono][filter][svf]")
{
	// The 5 Hz Cutoff minimum at 192 kHz x16, the highest internal rate: g = tan(pi 5 / 3.072 MHz) is about 5e-6. (Modulation can reach the
	// 2.5 Hz floor; the impulse response this measures grows as fs / fc, so Q 0.5 only.)
	constexpr double sampleRate = vekt::dsp::maximumInternalSampleRate;
	constexpr double cutoff = 5.0;
	for (const auto k : { 2.0 })
	{
		INFO("k " << k);
		const auto responses = measuredResponse(sampleRate, cutoff, k, { 0.0, cutoff, 4.0 * cutoff });
		CHECK(near(responses[0].lowPass, 1.0, 1.0e-6));
		CHECK(std::abs(std::abs(responses[1].lowPass) * k - 1.0) < 1.0e-6);
		CHECK(std::abs(responses[1].notch()) < 1.0e-6);
		CHECK(near(responses[2].highPass, analyticResponse(sampleRate, cutoff, k, 4.0 * cutoff).highPass, 1.0e-6));
	}
}

TEST_CASE("Mono linear SVF stays bounded and decays under audio-rate cutoff modulation", "[mono][filter][svf]")
{
	constexpr double sampleRate = 48'000.0;
	constexpr double k = 0.05;
	LinearTptSvf svf;
	svf.prepare(sampleRate);
	std::mt19937 random { 6 };
	std::uniform_real_distribution noise { -1.0, 1.0 };
	// 20 Hz to 20 kHz and back, 200 times a second.
	const auto cutoffAt = [&](long sample)
	{
		return 20.0 * std::exp2(std::log2(1'000.0) * 0.5 * (1.0 + std::sin(2.0 * std::numbers::pi * 200.0 * static_cast<double>(sample) / sampleRate)));
	};
	double peak {};
	long sample {};
	for (; sample < static_cast<long>(sampleRate); ++sample)
	{
		const auto out = svf.process(noise(random), cutoffAt(sample), k);
		peak = std::max({ peak, std::abs(out.lowPass), std::abs(out.bandPass), std::abs(out.highPass) });
	}
	CHECK(std::isfinite(peak));
	CHECK(peak < 2.0 / k);

	// Zero input, modulation continuing: the energy has to drain.
	double tail {};
	for (const auto end = sample + static_cast<long>(2.0 * sampleRate); sample < end; ++sample)
	{
		const auto out = svf.process(0.0, cutoffAt(sample), k);
		if (end - sample <= 4'800) tail = std::max({ tail, std::abs(out.lowPass), std::abs(out.bandPass), std::abs(out.highPass) });
	}
	CHECK(tail < 1.0e-9);
}

TEST_CASE("Mono SVF Mode lands exactly on LP, Notch and HP", "[mono][filter][svf][svf-mode]")
{
	std::mt19937 random { 7 };
	std::uniform_real_distribution value { -3.0, 3.0 };
	for (int trial = 0; trial < 1'000; ++trial)
	{
		const auto lowPass = value(random), highPass = value(random);
		const auto bits = [](double sample) { return std::bit_cast<std::uint64_t>(sample); };
		REQUIRE(bits(svfModeMix(-1.0, lowPass, highPass)) == bits(lowPass));
		REQUIRE(bits(svfModeMix(0.0, lowPass, highPass)) == bits(lowPass + highPass));
		REQUIRE(bits(svfModeMix(1.0, lowPass, highPass)) == bits(highPass));
		// Out-of-range Mode (e.g. an LFO overshoot before clamping) holds the landmark.
		REQUIRE(bits(svfModeMix(-1.5, lowPass, highPass)) == bits(lowPass));
		REQUIRE(bits(svfModeMix(1.5, lowPass, highPass)) == bits(highPass));
	}
}

TEST_CASE("Mono SVF Mode sweeps continuously and monotonically, flat at each landmark", "[mono][filter][svf][svf-mode]")
{
	// LP weight falls 1 -> 1 -> 0 and HP weight rises 0 -> 1 -> 1 across -1 -> 0 -> +1, never faster than the
	// smoothstep's 1.5 per unit of position, with zero slope at -1, 0 and +1.
	constexpr int steps = 4'000;
	constexpr double step = 2.0 / steps;
	auto previous = modeCoefficients(-1.0);
	for (int index = 1; index <= steps; ++index)
	{
		const auto mode = -1.0 + index * step;
		const auto current = modeCoefficients(mode);
		INFO("mode " << mode);
		REQUIRE(current.first <= previous.first);
		REQUIRE(current.second >= previous.second);
		REQUIRE(previous.first - current.first <= 1.5 * step + 1.0e-12);
		REQUIRE(current.second - previous.second <= 1.5 * step + 1.0e-12);
		previous = current;
	}
	constexpr double delta = 1.0e-4;
	for (const auto landmark : { -1.0, 0.0, 1.0 })
	{
		INFO("landmark " << landmark);
		const auto at = modeCoefficients(landmark);
		for (const auto side : { -delta, delta })
		{
			if (std::abs(landmark + side) > 1.0) continue;
			const auto near = modeCoefficients(landmark + side);
			REQUIRE(std::abs(near.first - at.first) / delta < 1.0e-3);
			REQUIRE(std::abs(near.second - at.second) / delta < 1.0e-3);
		}
	}
}

TEST_CASE("Mono SVF Mode sweep keeps both passbands and deepens to the notch", "[mono][filter][svf][svf-mode]")
{
	constexpr double sampleRate = 48'000.0;
	constexpr double cutoff = 1'000.0;
	for (const auto k : { 2.0, 0.5, 0.05 })
	{
		const auto responses = measuredResponse(sampleRate, cutoff, k, { 0.0, cutoff, sampleRate / 2.0 });
		double previousNotch = std::numeric_limits<double>::infinity();
		for (int index = 0; index <= 40; ++index)
		{
			const auto mode = -1.0 + index * 0.05;
			const auto [lowWeight, highWeight] = modeCoefficients(mode);
			const auto at = [&](const Response& response) { return lowWeight * response.lowPass + highWeight * response.highPass; };
			INFO("k " << k << ", mode " << mode);
			// The low passband stays unity from LP to Notch and the high passband from Notch to HP.
			CHECK(std::abs(at(responses[0])) == Catch::Approx(lowWeight).margin(1.0e-9));
			CHECK(std::abs(at(responses[2])) == Catch::Approx(highWeight).margin(1.0e-9));
			if (mode <= 0.0) CHECK(std::abs(at(responses[0])) == Catch::Approx(1.0).margin(1.0e-9));
			if (mode >= 0.0) CHECK(std::abs(at(responses[2])) == Catch::Approx(1.0).margin(1.0e-9));
			// At the cutoff LP = -j/k and HP = +j/k, so the level there is |HP weight - LP weight| / k: it falls to the
			// notch at Mode 0 and rises again.
			const auto atCutoff = std::abs(at(responses[1]));
			CHECK(atCutoff == Catch::Approx(std::abs(highWeight - lowWeight) / k).margin(1.0e-6 / k));
			if (mode <= 0.0) CHECK(atCutoff <= previousNotch + 1.0e-9);
			previousNotch = atCutoff;
			if (index == 20) CHECK(atCutoff < 1.0e-6);
		}
	}
}

TEST_CASE("Mono SVF Resonance shapes keep both ends and only raise Q", "[mono][filter][svf][svf-mode]")
{
	const auto top = 1.0 / vekt::mono::svfMaximumQ;
	for (const auto shape : { 1.0, 0.7, 0.5, 0.35 })
	{
		INFO("shape " << shape);
		REQUIRE(vekt::mono::svfDamping(0.0, shape) == 2.0);
		REQUIRE(std::abs(vekt::mono::svfDamping(1.0, shape) - top) < 1.0e-15);
		// Out of range holds the ends, like the rest of the Resonance path.
		REQUIRE(vekt::mono::svfDamping(-0.5, shape) == 2.0);
		REQUIRE(std::abs(vekt::mono::svfDamping(1.5, shape) - top) < 1.0e-15);
		auto previous = vekt::mono::svfDamping(0.0, shape);
		for (int step = 1; step <= 1'000; ++step)
		{
			const auto k = vekt::mono::svfDamping(step / 1'000.0, shape);
			REQUIRE(k < previous);
			// A lower shape brings resonance in earlier: never less Q than the linear map at the same setting.
			REQUIRE(k <= vekt::mono::svfDamping(step / 1'000.0, 1.0) + 1.0e-15);
			previous = k;
		}
	}
	// Up to the extension, shape 1 is exactly the linear-in-k map and the locked shape 0.5 exactly its law.
	const auto bits = [](double value) { return std::bit_cast<std::uint64_t>(value); };
	for (int step = 0; step <= 90; ++step)
	{
		const auto r = step / 100.0;
		REQUIRE(bits(vekt::mono::svfDamping(r, 1.0)) == bits(2.0 - 1.875 * r));
		REQUIRE(bits(vekt::mono::svfDamping(r)) == bits(2.0 - 1.875 * std::pow(r, 0.5)));
	}
	REQUIRE(vekt::mono::svfResonanceShape == 0.5);
	REQUIRE(bits(vekt::mono::svfExtensionStart) == bits(0.9));
	REQUIRE(std::abs(1.0 / vekt::mono::svfDamping(0.5) - 1.48) < 0.01);
	REQUIRE(std::abs(1.0 / vekt::mono::svfDamping(0.9) - 4.52) < 0.01);
	REQUIRE(std::abs(1.0 / vekt::mono::svfDamping(1.0) - 20.0) < 1.0e-12);
	// The top extension joins without a slope step: one-sided slopes at 90 % agree to first order.
	constexpr double h = 1.0e-6;
	const auto below = (vekt::mono::svfDamping(0.9) - vekt::mono::svfDamping(0.9 - h)) / h;
	const auto above = (vekt::mono::svfDamping(0.9 + h) - vekt::mono::svfDamping(0.9)) / h;
	REQUIRE(std::abs(above - below) < 1.0e-3 * std::abs(below));
}

TEST_CASE("Mono SVF output trim follows most of the Ladder's passband loss", "[mono][filter][svf][switch-gain]")
{
	const auto decibels = [](double gain) { return 20.0 * std::log10(gain); };
	REQUIRE(vekt::mono::svfOutputTrim(0.0) == 1.0);
	for (int step = 1; step <= 100; ++step)
	{
		const auto r = step / 100.0;
		// 80 % of the Ladder's 1 / (1 + 4 r) passband loss, in dB, falling monotonically.
		REQUIRE(std::abs(decibels(vekt::mono::svfOutputTrim(r)) + 0.8 * decibels(1.0 + 4.0 * r)) < 1.0e-9);
		REQUIRE(vekt::mono::svfOutputTrim(r) < vekt::mono::svfOutputTrim((step - 1) / 100.0));
	}
	REQUIRE(std::abs(decibels(vekt::mono::svfOutputTrim(1.0)) + 11.18) < 0.01);
}
