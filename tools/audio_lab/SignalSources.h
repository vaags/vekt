#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vekt::audio_lab
{
	inline constexpr auto pi = 3.14159265358979323846;

	enum class Source
	{
		sine,
		sawtooth,
		sweep,
		impulse,
		noise,
		kick
		, unison
	};

class SignalSource final
{
public:
	SignalSource() noexcept = default;

	SignalSource(Source sourceType, double rate, std::uint32_t seed = 0x6d2b79f5u) noexcept
		: source(sourceType), sampleRate(rate), randomState(seed)
	{
	}

	void prepare(Source sourceType, double rate) noexcept
	{
		source = sourceType;
		sampleRate = rate;
		randomState = 0x6d2b79f5u;
	}

	void setOctave(int octave) noexcept
	{
		octaveOffset = std::clamp(octave, -3, 3);
	}

	[[nodiscard]] float next(std::int64_t sampleIndex) noexcept
	{
		return next(sampleIndex, 0);
	}

	[[nodiscard]] float next(std::int64_t sampleIndex, int channel) noexcept
	{
		switch (source)
		{
		case Source::sine:
			return static_cast<float>(0.12589254117941673 * std::sin(
																2.0 * pi * 1'000.0 * std::exp2(octaveOffset) * static_cast<double>(sampleIndex) / sampleRate));
		case Source::sawtooth:
		{
			const auto phase = std::fmod(static_cast<double>(sampleIndex) * 110.0 * std::exp2(octaveOffset) / sampleRate, 1.0);
			return static_cast<float>(0.12589254117941673 * (2.0 * phase - 1.0));
		}
			case Source::unison:
			{
				constexpr auto baseFrequency = 110.0;
				constexpr auto detuneCents = 7.0;
				const auto firstFrequency = baseFrequency * std::exp2(octaveOffset);
				const auto secondFrequency = firstFrequency * std::pow(2.0, detuneCents / 1'200.0);
				const auto firstPhase = 2.0 * pi * firstFrequency
					* static_cast<double>(sampleIndex) / sampleRate;
				const auto secondPhase = 2.0 * pi * secondFrequency
					* static_cast<double>(sampleIndex) / sampleRate;
				const auto first = std::sin(firstPhase);
				const auto second = std::sin(secondPhase);
				const auto spread = channel == 0 ? 0.3 : 0.7;
				return static_cast<float>(0.12589254117941673
					* (first * (1.0 - spread) + second * spread));
			}
		case Source::sweep:
		{
			const auto duration = std::max(1.0, sampleRate * 5.0);
			const auto cycle = static_cast<std::int64_t>(
				std::floor(static_cast<double>(sampleIndex) / duration));
			const auto cycleSample = static_cast<double>(sampleIndex) - static_cast<double>(cycle) * duration;
			constexpr auto startFrequency = 20.0;
			constexpr auto endFrequency = 10'000.0;
			const auto sweepRate = std::log(endFrequency / startFrequency) / duration;
			const auto cyclePhase = 2.0 * pi *
									(endFrequency - startFrequency) / (sweepRate * sampleRate);
			const auto phase = static_cast<double>(cycle) * cyclePhase + 2.0 * pi * startFrequency * std::exp2(octaveOffset) * (std::exp(sweepRate * cycleSample) - 1.0) / (sweepRate * sampleRate);
			return static_cast<float>(0.12589254117941673 * std::sin(phase));
		}
		case Source::impulse:
		{
			const auto period = std::max<std::int64_t>(1, static_cast<std::int64_t>(sampleRate));
			return sampleIndex % period == 0 ? 1.0f : 0.0f;
		}
		case Source::noise:
			return nextNoise();
		case Source::kick:
		{
			const auto period = std::max<std::int64_t>(1, static_cast<std::int64_t>(sampleRate));
			const auto cycleSample = sampleIndex % period;
			const auto time = static_cast<double>(cycleSample) / sampleRate;
			constexpr auto startFrequency = 150.0;
			constexpr auto endFrequency = 48.0;
			const auto pitchDecay = 0.035;
			const auto pitchRate = std::log(startFrequency / endFrequency) / pitchDecay;
			const auto phase = 2.0 * pi * startFrequency * std::exp2(octaveOffset) * (1.0 - std::exp(-pitchRate * time)) / pitchRate;
			const auto body = std::sin(phase) * std::exp(-4.5 * time);
			const auto click = std::sin(2.0 * pi * 3'200.0 * time) * std::exp(-420.0 * time);
			return static_cast<float>(0.8 * body + 0.24 * click);
		}
		}

		return 0.0f;
	}

private:
	[[nodiscard]] float nextNoise() noexcept
	{
		randomState ^= randomState << 13;
		randomState ^= randomState >> 17;
		randomState ^= randomState << 5;
		return static_cast<float>(static_cast<double>(randomState) / 4'294'967'295.0 * 0.25 - 0.125);
	}

	Source source;
	double sampleRate;
	std::uint32_t randomState;
	int octaveOffset{};
};
}
