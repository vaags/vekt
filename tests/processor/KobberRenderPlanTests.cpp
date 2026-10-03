#include "KobberRenderPlan.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <initializer_list>
#include <vector>

namespace
{
std::array<bool, 16> soundingVoices(std::initializer_list<int> indices)
{
	std::array<bool, 16> sounding {};
	for (const auto index : indices) sounding[static_cast<std::size_t>(index)] = true;
	return sounding;
}

// Each group's voice indices, for comparison.
std::vector<std::vector<int>> members(const vekt::kobber::VoiceGroups& groups)
{
	std::vector<std::vector<int>> result;
	for (int group = 0; group < groups.count; ++group)
	{
		auto& voices = result.emplace_back();
		for (int index = 0; index < groups.voiceCount[static_cast<std::size_t>(group)]; ++index)
			voices.push_back(groups.voices[static_cast<std::size_t>(group)][static_cast<std::size_t>(index)]);
	}
	return result;
}
}

TEST_CASE("Kobber render plan groups sounding voices in voice order, four filter lanes per unit", "[kobber][render-plan]")
{
	const auto sounding = soundingVoices({ 0, 2, 3, 5, 6 });
	using Groups = std::vector<std::vector<int>>;
	REQUIRE(members(vekt::kobber::planRender(sounding, 1, 1).units) == Groups { { 0, 2, 3, 5 }, { 6 } });
	REQUIRE(members(vekt::kobber::planRender(sounding, 2, 1).units) == Groups { { 0, 2 }, { 3, 5 }, { 6 } });
	REQUIRE(members(vekt::kobber::planRender(sounding, 4, 1).units) == Groups { { 0 }, { 2 }, { 3 }, { 5 }, { 6 } });
	REQUIRE(vekt::kobber::planRender(soundingVoices({}), 1, 4).units.count == 0);
}

TEST_CASE("Kobber render plan renders units as jobs on one thread or when units fill the threads", "[kobber][render-plan]")
{
	const auto sounding = soundingVoices({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 });
	const auto single = vekt::kobber::planRender(sounding, 1, 1);
	REQUIRE(members(single.jobs) == members(single.units));
	const auto filled = vekt::kobber::planRender(sounding, 1, 3); // three units of up to four voices
	REQUIRE(filled.units.count == 3);
	REQUIRE(members(filled.jobs) == members(filled.units));
}

TEST_CASE("Kobber render plan splits into the job size with the shortest estimated time when threads outnumber units",
    "[kobber][render-plan]")
{
	using Groups = std::vector<std::vector<int>>;
	const auto sounding = soundingVoices({ 0, 1, 2, 3 }); // one unit of four voices
	// Four threads: four one-voice jobs in one round (0.30) beat two two-voice jobs (0.53) and one job (1.0).
	const auto four = vekt::kobber::planRender(sounding, 1, 4);
	REQUIRE(members(four.units) == Groups { { 0, 1, 2, 3 } });
	REQUIRE(members(four.jobs) == Groups { { 0 }, { 1 }, { 2 }, { 3 } });
	// Two threads: two two-voice jobs in one round (0.53) beat four one-voice jobs in two rounds (0.60).
	REQUIRE(members(vekt::kobber::planRender(sounding, 1, 2).jobs) == Groups { { 0, 1 }, { 2, 3 } });
	// Unison 4 fills a job's four lanes with one voice: nothing smaller to split into.
	const auto unison = vekt::kobber::planRender(soundingVoices({ 0 }), 4, 4);
	REQUIRE(members(unison.jobs) == Groups { { 0 } });
}
