#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace vekt::mono
{
// Sounding voices, by index in voice order, in groups that share one batched filter solve (up to four lanes).
struct VoiceGroups
{
	std::array<std::array<std::uint8_t, 4>, 16> voices {};
	std::array<int, 16> voiceCount {};
	int count {};
};

// One render segment's grouping. Units fix the summing order: sounding voices in voice order, four filter lanes each
// (four, two or one voice at unison 1, 2 or 4), so they do not depend on threads. Jobs split the same voices for
// rendering, into smaller groups when there are more threads than units; a voice renders the same bits in any job, so
// Multicore on and off render identical samples.
struct RenderPlan
{
	VoiceGroups units, jobs;
};

// Groups the sounding voices voicesPerGroup at a time.
[[nodiscard]] inline VoiceGroups groupVoices(const std::array<bool, 16>& sounding, int voicesPerGroup) noexcept
{
	VoiceGroups groups;
	auto& groupCount = groups.count;
	for (std::size_t index = 0; index < sounding.size(); ++index)
	{
		if (!sounding[index]) continue;
		if (groupCount == 0 || groups.voiceCount[static_cast<std::size_t>(groupCount - 1)] == voicesPerGroup)
			groups.voiceCount[static_cast<std::size_t>(groupCount++)] = 0;
		auto& groupSize = groups.voiceCount[static_cast<std::size_t>(groupCount - 1)];
		groups.voices[static_cast<std::size_t>(groupCount - 1)][static_cast<std::size_t>(groupSize++)] =
		    static_cast<std::uint8_t>(index);
	}
	return groups;
}

// lanesPerVoice: the unison count, 1 to 4. threads: the render threads available, the audio thread included.
[[nodiscard]] inline RenderPlan planRender(
    const std::array<bool, 16>& sounding, int lanesPerVoice, int threads) noexcept
{
	RenderPlan plan;
	plan.units = groupVoices(sounding, 4 / lanesPerVoice);
	plan.jobs = plan.units;
	if (threads > 1 && plan.units.count < threads && lanesPerVoice < 4)
	{
		// Fewer units than threads: render in smaller jobs. A smaller batched solve costs more per lane but less in
		// total, so pick the job size with the shortest estimated wall time (rounds of jobs across the threads).
		// Relative cost of a job of 4, 2 and 1 filter lanes: 16 ladder voices at 1x and 4x (VektMonoProcessorCost).
		constexpr std::array<std::pair<int, float>, 3> jobCosts { { { 4, 1.0f }, { 2, 0.53f }, { 1, 0.30f } } };
		const auto soundingCount = static_cast<int>(std::count(sounding.begin(), sounding.end(), true));
		auto bestLanes = 4;
		auto bestTime = std::numeric_limits<float>::max();
		for (const auto& [lanes, cost] : jobCosts)
		{
			if (lanes < lanesPerVoice) continue;
			const auto voicesPerJob = lanes / lanesPerVoice;
			const auto jobs = (soundingCount + voicesPerJob - 1) / voicesPerJob;
			const int rounds = (jobs + threads - 1) / threads; // whole rounds of jobs across the threads
			const auto time = static_cast<float>(rounds) * cost;
			if (time < bestTime)
			{
				bestLanes = lanes;
				bestTime = time;
			}
		}
		if (bestLanes < 4) plan.jobs = groupVoices(sounding, bestLanes / lanesPerVoice);
	}
	return plan;
}
}
