#include "WidthOscillator.h"

#include <juce_dsp/juce_dsp.h>

namespace vekt::mono
{
namespace
{
constexpr double pi = std::numbers::pi;

// Descending harmonic limits, four per octave, from the top harmonic to 1, then 0 (DC only). Each step
// divides by 2^(1/4) and removes at least one harmonic.
std::vector<int> levelLimits(int top)
{
	std::vector<int> limits;
	for (auto harmonic = top; harmonic > 0;
		harmonic = std::min(harmonic - 1, static_cast<int>(std::floor(harmonic / std::exp2(0.25)))))
		limits.push_back(harmonic);
	limits.push_back(0);
	return limits;
}

template <typename Coefficient>
std::vector<float> synthesize(int order, int limit, const Coefficient& coefficient)
{
	const auto size = 1 << order;
	std::vector<juce::dsp::Complex<float>> bins(static_cast<std::size_t>(size)), cycle(static_cast<std::size_t>(size));
	for (int harmonic = 1; harmonic <= limit; ++harmonic)
	{
		const std::complex<double> c = coefficient(harmonic);
		bins[static_cast<std::size_t>(harmonic)] = { static_cast<float>(c.real()), static_cast<float>(c.imag()) };
		bins[static_cast<std::size_t>(size - harmonic)] = { static_cast<float>(c.real()), static_cast<float>(-c.imag()) };
	}
	juce::dsp::FFT(order).perform(bins.data(), cycle.data(), true);
	// JUCE's inverse FFT divides by N; the bins hold c[h], so scale back to Fourier synthesis. Guard samples:
	// x[-1] in front and x[N], x[N + 1] behind, so WidthWavetable::lookup never wraps.
	std::vector<float> table;
	table.reserve(static_cast<std::size_t>(size) + 3);
	const auto at = [&](int index) { return cycle[static_cast<std::size_t>((index + size) % size)].real() * static_cast<float>(size); };
	for (int index = -1; index <= size + 1; ++index) table.push_back(at(index));
	return table;
}

double frameWidth(int frame) { return 5.0 + 90.0 * frame / (WidthWavetable::widthFrames - 1); }

// Slope jump (per cycle) of the warped sine at both breakpoints.
double sineCorner(double d) { return widthSineGain * pi * (1.0 / d - 1.0 / (1.0 - d)); }

// Phase advance putting an anchor's unaligned fundamental at +sin.
double alignment(int anchor, double width)
{
	std::array<std::complex<double>, 2> c {};
	widthHarmonics(anchor, width, 1, 1, c.data(), false);
	return (-0.5 * pi - std::arg(c[1])) / (2.0 * pi);
}
}

const WidthWavetable& WidthWavetable::instance()
{
	static const WidthWavetable table;
	return table;
}

WidthWavetable::WidthWavetable()
{
	for (const auto limit : levelLimits(topHarmonic))
	{
		auto& level = bank.emplace_back();
		level.limit = limit;
		limitList.push_back(limit);
		level.residualLimit = std::min(limit, residualHarmonics);
		if (limit == 0) continue;
		// At least 16 samples per cycle of the top harmonic keeps cubic lookup error near -60 dB.
		const auto order = std::max(8, static_cast<int>(std::ceil(std::log2(16.0 * limit))));
		level.saw = synthesize(order, limit, [](int h) { return 1.0 / std::complex<double>(0.0, 2.0 * pi * h); });
		level.parabola = synthesize(order, limit, [](int h)
			{ const auto w = 2.0 * pi * h; return std::complex<double>(-1.0 / (w * w), 0.0); });
	}
	for (int n = 0; n <= topHarmonic; ++n)
		levelAtMost[static_cast<std::size_t>(n)] = static_cast<std::size_t>(std::partition_point(limitList.begin(), limitList.end(),
			[n](int limit) { return limit > n; }) - limitList.begin());
	std::array<std::complex<double>, residualHarmonics + 1> sine {};
	for (int limit = 1; limit <= residualHarmonics; ++limit)
		residuals[static_cast<std::size_t>(limit)].resize(widthFrames);
	for (int frame = 0; frame < widthFrames; ++frame)
	{
		const auto width = frameWidth(frame);
		const auto d = widthBreakpoint(0, width);
		widthHarmonics(0, width, 0, residualHarmonics, sine.data(), false);
		const auto corner = sineCorner(d);
		for (int limit = 1; limit <= residualHarmonics; ++limit)
			residuals[static_cast<std::size_t>(limit)][static_cast<std::size_t>(frame)] = synthesize(residualOrder, limit, [&](int h)
			{
				// Remove the two slope corners (at 0 and d), each -corner e^{-i w p} / w^2.
				const auto w = 2.0 * pi * h;
				return sine[static_cast<std::size_t>(h)] + corner * (1.0 + std::polar(1.0, -w * d)) / (w * w);
			});
	}
}

std::size_t WidthWavetable::bytes() const noexcept
{
	std::size_t total {};
	for (const auto& level : bank) total += (level.saw.size() + level.parabola.size()) * sizeof(float);
	for (const auto& byLimit : residuals)
		for (const auto& frame : byLimit) total += frame.size() * sizeof(float);
	return total;
}

WidthAnchorShape makeWidthAnchorShape(int anchor, double width)
{
	WidthAnchorShape shape;
	const auto add = [&](double offset, double weight)
	{ shape.events[static_cast<std::size_t>(shape.eventCount++)] = { offset, static_cast<float>(weight) }; };
	switch (anchor)
	{
	case 0:
	{
		const auto d = widthBreakpoint(0, width);
		const auto t = alignment(0, width);
		const auto corner = sineCorner(d);
		shape.parabola = true;
		add(t, corner);
		add(t - d, corner);
		shape.sine = true;
		shape.sineOffset = t;
		shape.dc = static_cast<float>(widthSineGain * 2.0 * (2.0 * d - 1.0) / pi);
		const auto position = (std::clamp(width, 5.0, 95.0) - 5.0) / 90.0 * (WidthWavetable::widthFrames - 1);
		const auto first = std::clamp(static_cast<int>(std::floor(position)) - 1, 0, WidthWavetable::widthFrames - 4);
		for (int tap = 0; tap < 4; ++tap)
		{
			double lagrange = 1.0;
			for (int other = 0; other < 4; ++other)
				if (other != tap) lagrange *= (position - (first + other)) / static_cast<double>(tap - other);
			// On a frame (e.g. the default 50 %) three weights are zero; keep only frames that contribute.
			if (lagrange == 0.0) continue;
			shape.frames[static_cast<std::size_t>(shape.frameCount)] = first + tap;
			shape.frameWeights[static_cast<std::size_t>(shape.frameCount)] = static_cast<float>(lagrange);
			++shape.frameCount;
		}
		break;
	}
	case 1:
	{
		const auto d = widthBreakpoint(1, width);
		const auto t = alignment(1, width);
		const auto edge = 2.0 / d - 2.0 / (1.0 - d);
		shape.parabola = true;
		add(t, edge);
		add(t - 0.5 * d, -4.0 / d);
		add(t - d, edge);
		add(t - 0.5 * (1.0 + d), 4.0 / (1.0 - d));
		shape.dc = static_cast<float>(d - 0.5);
		break;
	}
	case 2:
	{
		// Two-tooth saw: G (S(x + delta/2) + S(x - delta/2)); each S carries half the saw's jump of 2.
		const auto delta = twoToothDelta(width);
		const auto gain = twoToothGain(delta);
		add(0.5 * delta, gain);
		add(-0.5 * delta, gain);
		break;
	}
	default:
	{
		const auto d = widthBreakpoint(3, width);
		const auto t = alignment(3, width);
		add(t, 2.0 * widthPulseGain);
		add(t - d, -2.0 * widthPulseGain);
		shape.dc = static_cast<float>(widthPulseGain * (2.0 * d - 1.0));
		break;
	}
	}
	// Drop reads that cannot contribute (a corner of zero strength, e.g. at neutral Width) and merge
	// reads at the same phase (the two-tooth saw's teeth coincide at neutral Width).
	int kept {};
	for (int index = 0; index < shape.eventCount; ++index)
	{
		const auto event = shape.events[static_cast<std::size_t>(index)];
		if (event.weight == 0.0f) continue;
		auto merged = false;
		for (int other = 0; other < kept && !merged; ++other)
		{
			auto& target = shape.events[static_cast<std::size_t>(other)];
			const auto distance = event.offset - target.offset;
			if (std::abs(distance - std::round(distance)) < 1.0e-12)
			{
				target.weight += event.weight;
				merged = true;
			}
		}
		if (!merged) shape.events[static_cast<std::size_t>(kept++)] = event;
	}
	shape.eventCount = kept;
	return shape;
}
}
