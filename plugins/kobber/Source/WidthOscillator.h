#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

namespace vekt::mono
{
// Anchor gains: every anchor's fundamental is +sin(2 pi phase) and has the saw's RMS (1/sqrt 3) at neutral Width.
inline constexpr double widthSineGain = 0.81649658;  // sqrt(2/3)
inline constexpr double widthPulseGain = 0.57735027; // 1/sqrt 3

// Width model (chosen by listening, 29 Sep 2026). Sine, triangle and square are a phase warp with
// breakpoint 50 + depth * (Width - 50) percent, time-shifted so their fundamental stays +sin; the
// saw is a two-tooth saw: the mean of two saws offset by +/- delta/2 cycles, delta = 0.25 |Width - 50| / 45,
// normalized to the saw's RMS by 1 / sqrt(1 - 3 delta + 3 delta^2).
inline constexpr std::array<double, 4> widthAnchorDepths { 0.45, 0.65, 0.0, 1.0 }; // saw entry unused

[[nodiscard]] inline double widthBreakpoint(int anchor, double width) noexcept
{
	return (50.0 + widthAnchorDepths[static_cast<std::size_t>(anchor)] * (std::clamp(width, 5.0, 95.0) - 50.0)) * 0.01;
}

// Morph is cyclic: 0 sine, 1 triangle, 2 saw, 3 square, and 4 is the sine again. Each segment mixes two
// adjacent anchors; the 3-4 segment runs from the square back to the sine.
inline constexpr float morphPeriod = 4.0f;

[[nodiscard]] inline float wrapMorph(float morph) noexcept
{
	const auto wrapped = morph - morphPeriod * std::floor(morph / morphPeriod);
	return wrapped < morphPeriod ? wrapped : 0.0f;
}

// Shortest signed distance from one Morph position to another round the cycle, in [-2, 2).
[[nodiscard]] inline float morphDistance(float from, float to) noexcept
{
	return wrapMorph(to - from + 0.5f * morphPeriod) - 0.5f * morphPeriod;
}

// A spectrally rich anchor can dominate a linear morph long before it is reached. This curve delays its share
// while keeping the endpoints exact: w(t) = 2t^2 - t^3 (37.5% halfway), with t in [0, 1] the distance from the
// plainer anchor (t = 1 at the rich one). It is the p = 2 member of t^p (p - (p - 1) t); unlike plain t^2 it
// meets the rich anchor with slope 1, so LFO sweeps do not accelerate into it.
[[nodiscard]] inline double delayedMorphWeight(double t) noexcept { return t * t * (2.0 - t); }

// Share of the segment's second anchor at a fraction through it. The invariant: the anchor that owns the curve
// gets w(its distance back from its own end), so a curved second anchor gets w(fraction) and a curved first
// anchor gets w(1 - fraction), making the blend 1 - w(1 - fraction). Which anchor owns the curve:
//   0 sine-triangle    linear
//   1 triangle-saw     the saw (second anchor): w(fraction)
//   2 saw-square       the saw (first anchor):  1 - w(1 - fraction)
//   3 square-sine      the square (first anchor): 1 - w(1 - fraction)
// Do not simplify 1 - w(1 - fraction) to w(fraction): that delays the second anchor instead, e.g. the sine rather
// than the square (62.5% square halfway instead of 37.5%). The saw's curve was chosen by ear over linear, p = 1.5
// and t^2; the square's (30 Sep 2026) over linear and p = 1.5, matching the saw.
[[nodiscard]] inline double morphSegmentBlend(int segment, double fraction) noexcept
{
	switch (segment)
	{
	case 0: return fraction;
	case 1: return delayedMorphWeight(fraction);
	default: return 1.0 - delayedMorphWeight(1.0 - fraction);
	}
}

[[nodiscard]] inline double twoToothDelta(double width) noexcept { return 0.25 * std::abs(std::clamp(width, 5.0, 95.0) - 50.0) / 45.0; }
[[nodiscard]] inline double twoToothGain(double delta) noexcept { return 1.0 / std::sqrt(1.0 - 3.0 * delta + 3.0 * delta * delta); }

// Exact Fourier coefficients c[h], h in [first, last], of one anchor at a Width, written to out[h]
// (synthesis c[0] + sum 2 Re(c[h] e^{i 2 pi h phase})). With w = 2 pi h:
//   square:   +-g pulse with duty d                     c[h] = 2 g (1 - e^{-i w d}) / (i w)
//   triangle: warped, continuous, piecewise linear      c[h] = -sum dS_k e^{-i w p_k} / w^2
//   sine:     half-sines on [0, d] and [d, 1]           closed-form segment integrals
//   saw:      two-tooth saw                             c[h] = gain cos(pi h delta) / (i pi h)
// Warped anchors are rotated so c[1] is a +sin fundamental unless !align.
inline void widthHarmonics(int anchor, double width, int first, int last, std::complex<double>* out, bool align = true)
{
	using namespace std::complex_literals;
	constexpr double pi = std::numbers::pi;
	if (last < first) return;
	if (anchor == 2)
	{
		const auto delta = twoToothDelta(width);
		const auto gain = twoToothGain(delta);
		for (auto h = first; h <= last; ++h)
			out[h] = h == 0 ? 0.0 : gain * std::cos(pi * h * delta) / (1i * pi * static_cast<double>(h));
		return;
	}
	const auto d = widthBreakpoint(anchor, width);
	// Half-sine segment integral: int_0^L sin(a x) e^{-i w x} dx with a L = pi.
	const auto halfSine = [](double a, double length, double omega, std::complex<double> endPhase)
	{
		if (std::abs(a - omega) < 1.0e-9 * std::max(1.0, omega)) return std::complex<double>(0.0, -0.5 * length);
		return (1.0 + endPhase) * a / (a * a - omega * omega);
	};
	if (first == 0)
		out[0] = anchor == 0 ? widthSineGain * 2.0 * (2.0 * d - 1.0) / pi : anchor == 1 ? d - 0.5 : widthPulseGain * (2.0 * d - 1.0);
	const auto start = std::max(first, 1);
	if (last < start) return;
	auto rotationStep = std::complex<double> { 1.0 };
	if (align)
	{
		std::complex<double> fundamental[2] {};
		widthHarmonics(anchor, width, 1, 1, fundamental, false);
		rotationStep = std::polar(1.0, -0.5 * pi - std::arg(fundamental[1]));
	}
	// e^{-i w p} for the event positions p, advanced harmonic by harmonic from `start`.
	const auto at = [&](double position) { return std::polar(1.0, -2.0 * pi * position * start); };
	const auto step = [](double position) { return std::polar(1.0, -2.0 * pi * position); };
	auto atD = at(d), atHalfD = at(0.5 * d), atRest = at(1.0 - d), atMid = at(0.5 * (1.0 + d));
	const auto stepD = step(d), stepHalfD = step(0.5 * d), stepRest = step(1.0 - d), stepMid = step(0.5 * (1.0 + d));
	auto rotation = align ? std::pow(rotationStep, start) : std::complex<double> { 1.0 };
	for (auto h = start; h <= last; ++h)
	{
		const auto omega = 2.0 * pi * static_cast<double>(h);
		std::complex<double> value;
		switch (anchor)
		{
		case 0:
			value = widthSineGain * (halfSine(pi / d, d, omega, atD) - atD * halfSine(pi / (1.0 - d), 1.0 - d, omega, atRest));
			break;
		case 1:
		{
			const auto edge = 2.0 / d - 2.0 / (1.0 - d); // slope change at 0 and at d
			value = -(edge * (1.0 + atD) - (4.0 / d) * atHalfD + (4.0 / (1.0 - d)) * atMid) / (omega * omega);
			break;
		}
		default: value = 2.0 * widthPulseGain * (1.0 - atD) / (1i * omega); break;
		}
		out[h] = value * rotation;
		atD *= stepD; atHalfD *= stepHalfD; atRest *= stepRest; atMid *= stepMid;
		rotation *= rotationStep;
	}
}

// Spectral guard: harmonic h fades with a smoothstep between 0.8 and 0.9 of the host Nyquist.
[[nodiscard]] inline double widthGuardGain(int harmonic, double pitch, double hostRate) noexcept
{
	const auto t = std::clamp((2.0 * harmonic * pitch / hostRate - 0.8) / 0.1, 0.0, 1.0);
	return 1.0 - t * t * (3.0 - 2.0 * t);
}

// Bandlimited Width oscillator: pitch levels 4 per octave from H1023 down to DC; each level fades as a
// group with its highest harmonic (adjacent levels telescope). Every anchor except the sine's smooth
// part is a sum of shifted copies of two Width-independent primitives,
//   saw primitive       S(x): c[h] = 1 / (i w)    unit value jump at 0   (square, two-tooth saw)
//   parabola primitive  P(x): c[h] = -1 / w^2     unit slope jump at 0   (triangle, sine corners)
// so each level stores only S and P; the sine minus its two corners (harmonics ~ 1/h^4) is kept to
// 24 harmonics in Width-frame tables. The tables do not depend on sample rate and are shared.
class WidthWavetable
{
public:
	static constexpr int topHarmonic = 1023, widthFrames = 65, residualHarmonics = 24, residualOrder = 9;

	struct Level
	{
		int limit {}, residualLimit {};
		// One cycle of N (a power of two) samples stored as N + 3 with guard samples x[-1] ... x[N + 1], so a
		// four-point read never wraps. Empty for the DC-only level.
		std::vector<float> saw, parabola;
	};

	// Built on first use (~tens of ms); MonoVoice::prepare touches it off the audio thread.
	static const WidthWavetable& instance();

	[[nodiscard]] const std::vector<Level>& levels() const noexcept { return bank; }
	[[nodiscard]] const std::vector<int>& limits() const noexcept { return limitList; } // bank's limits, compact
	// Index of the first (brightest) level whose limit is at most n, for any n >= 0: an O(1) level search.
	[[nodiscard]] std::size_t firstLevelAtMost(double n) const noexcept
	{
		// Clamp before converting: a near-zero pitch gives an n far beyond int's range.
		const auto clamped = static_cast<int>(std::clamp(n, 0.0, static_cast<double>(topHarmonic)));
		return levelAtMost[static_cast<std::size_t>(clamped)];
	}
	[[nodiscard]] const std::vector<float>& residual(int limit, int frame) const noexcept
	{
		return residuals[static_cast<std::size_t>(limit)][static_cast<std::size_t>(frame)];
	}
	[[nodiscard]] std::size_t bytes() const noexcept;

	// Four-point Lagrange read of a guarded table (see Level) at a phase in cycles.
	static float lookup(const std::vector<float>& table, double phase) noexcept
	{
		const auto size = static_cast<int>(table.size()) - 3;
		const auto position = (phase - std::floor(phase)) * size;
		const auto index = std::min(static_cast<int>(position), size - 1);
		const auto t = static_cast<float>(position - index);
		const auto* x = table.data() + index; // x[0..3] are samples index-1 .. index+2
		return x[0] * (-t * (t - 1.0f) * (t - 2.0f) / 6.0f) + x[1] * ((t + 1.0f) * (t - 1.0f) * (t - 2.0f) / 2.0f)
			+ x[2] * (-(t + 1.0f) * t * (t - 2.0f) / 2.0f) + x[3] * ((t + 1.0f) * t * (t - 1.0f) / 6.0f);
	}

private:
	WidthWavetable();
	std::vector<Level> bank; // limits descending
	std::vector<int> limitList;
	std::array<std::size_t, topHarmonic + 1> levelAtMost {};
	std::array<std::vector<std::vector<float>>, residualHarmonics + 1> residuals; // [limit][width frame]
};

// One anchor at one Width, reduced to primitive reads: weight * S or P at (phase + offset), plus the
// sine residual stencil and the anchor's DC.
struct WidthAnchorShape
{
	struct Event
	{
		double offset {};
		float weight {};
	};
	std::array<Event, 4> events {};
	int eventCount {};
	bool parabola {}, sine {};
	double sineOffset {};
	std::array<int, 4> frames {};
	std::array<float, 4> frameWeights {};
	int frameCount {};
	float dc {};
};

[[nodiscard]] WidthAnchorShape makeWidthAnchorShape(int anchor, double width);

// Per-oscillator cache of the anchor shapes at the current Width; Morph only reweights two of them.
struct WidthOscillatorState
{
	float width { -1.0f };
	std::array<WidthAnchorShape, 4> anchors {};
	std::array<bool, 4> valid {};
	// Pitch levels and their weights for the last pitch: the telescoped sum f_base + sum g_l (f_l - f_{l+1})
	// rewritten as sum w_l f_l. Recomputed only when pitch or host rate change.
	static constexpr std::size_t maximumLevels = 8;
	float levelPitch { -1.0f };
	double levelRate {};
	std::size_t levelCount {};
	std::array<std::size_t, maximumLevels> levelIndices {};
	std::array<float, maximumLevels> levelWeights {};

	const WidthAnchorShape& anchor(int index, float newWidth)
	{
		if (std::abs(newWidth - width) > 0.0f)
		{
			width = newWidth;
			valid = {};
		}
		auto& shape = anchors[static_cast<std::size_t>(index)];
		if (!valid[static_cast<std::size_t>(index)])
		{
			shape = makeWidthAnchorShape(index, newWidth);
			valid[static_cast<std::size_t>(index)] = true;
		}
		return shape;
	}
};

// One sample of the Width oscillator. frequencyHz and hostRate set the guard (host Nyquist, whatever
// the oversampled rate the voice runs at); zeroCenteredDc drops each anchor's frozen-Width mean. Morph
// wraps, so any value is valid.
inline float renderWidthOscillator(WidthOscillatorState& state, double phase, float frequencyHz, double hostRate,
	float morph, float width, bool zeroCenteredDc)
{
	const auto& table = WidthWavetable::instance();
	const auto position = wrapMorph(morph);
	const auto segment = std::clamp(static_cast<int>(position), 0, 3);
	const auto blend = static_cast<float>(morphSegmentBlend(segment, static_cast<double>(position) - segment));
	const auto& from = state.anchor(segment, width);
	const auto* to = blend > 0.0f ? &state.anchor((segment + 1) % 4, width) : nullptr;
	auto value = zeroCenteredDc ? 0.0 : static_cast<double>((1.0f - blend) * from.dc + (to != nullptr ? blend * to->dc : 0.0f));
	const auto pitch = static_cast<double>(std::abs(frequencyHz));
	if (pitch <= 0.0) return static_cast<float>(value);
	const auto x = phase;
	const auto anchorValue = [&](const WidthWavetable::Level& level, const WidthAnchorShape& shape)
	{
		const auto& primitive = shape.parabola ? level.parabola : level.saw;
		float sum {};
		for (int index = 0; index < shape.eventCount; ++index)
		{
			const auto& event = shape.events[static_cast<std::size_t>(index)];
			sum += event.weight * WidthWavetable::lookup(primitive, x + event.offset);
		}
		if (shape.sine)
			for (std::size_t tap = 0; tap < static_cast<std::size_t>(shape.frameCount); ++tap)
				sum += shape.frameWeights[tap]
					* WidthWavetable::lookup(table.residual(level.residualLimit, shape.frames[tap]), x + shape.sineOffset);
		return sum;
	};
	const auto levelValue = [&](const WidthWavetable::Level& level)
	{
		if (level.limit == 0) return 0.0f;
		auto sum = (1.0f - blend) * anchorValue(level, from);
		if (to != nullptr) sum += blend * anchorValue(level, *to);
		return sum;
	};
	const auto& levels = table.levels();
	const auto cycles = hostRate / pitch;
	const auto fullLimit = 0.4 * cycles, silentLimit = 0.45 * cycles;
	// Limits are integers: limit <= fullLimit means limit <= floor(fullLimit), and limit < silentLimit means
	// limit <= ceil(silentLimit) - 1.
	const auto base = table.firstLevelAtMost(fullLimit);
	const auto first = std::min(base, table.firstLevelAtMost(std::ceil(silentLimit) - 1.0));
	auto upper = levelValue(levels[base]);
	value += upper;
	for (auto level = base; level-- > first;)
	{
		const auto current = levelValue(levels[level]);
		value += widthGuardGain(levels[level].limit, pitch, hostRate) * (current - upper);
		upper = current;
	}
	return static_cast<float>(value);
}
}
