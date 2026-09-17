#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace vekt::audio_lab
{
enum class Source
{
	silence,
	sine,
	sweep,
	impulse,
	noise
};

class SignalSource final
{
public:
	SignalSource(Source sourceType, double rate, std::uint32_t seed = 0x6d2b79f5u) noexcept
		: source(sourceType), sampleRate(rate), randomState(seed)
	{
	}

	[[nodiscard]] float next(std::int64_t sampleIndex) noexcept
	{
		switch (source)
		{
			case Source::silence: return 0.0f;
			case Source::sine:
				return static_cast<float>(0.12589254117941673
					* std::sin(2.0 * std::numbers::pi * 1'000.0
						* static_cast<double>(sampleIndex) / sampleRate));
			case Source::sweep:
			{
				const auto duration = std::max(1.0, sampleRate * 5.0);
				const auto position = std::clamp(static_cast<double>(sampleIndex) / duration, 0.0, 1.0);
				const auto frequency = 20.0 * std::pow(10'000.0 / 20.0, position);
				return static_cast<float>(0.12589254117941673
					* std::sin(2.0 * std::numbers::pi * frequency
						* static_cast<double>(sampleIndex) / sampleRate));
			}
			case Source::impulse: return sampleIndex == 0 ? 1.0f : 0.0f;
			case Source::noise: return nextNoise();
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
};
}
