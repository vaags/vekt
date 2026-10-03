#include "AllocationCounter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <vector>

namespace
{
// Keeps an allocation observable, so the optimizer cannot remove the malloc/free or new/delete pair under test.
void* volatile sink = nullptr;
}

TEST_CASE("Flint allocation counter sees malloc, new and JUCE buffer resizes", "[flint][allocation]")
{
	vekt::test::AllocationScope mallocScope;
	void* block = std::malloc(64);
	sink = block;
	std::free(block);
	const auto mallocCounts = mallocScope.stop();
	REQUIRE(mallocCounts.allocations == 1);
	REQUIRE(mallocCounts.releases == 1);

	vekt::test::AllocationScope newScope;
	auto values = std::make_unique<std::vector<double>>(256);
	sink = values.get(); // both pointers escape, or the 24-byte vector object may be elided
	sink = values->data();
	values.reset();
	const auto newCounts = newScope.stop();
	REQUIRE(newCounts.allocations >= 2);
	REQUIRE(newCounts.releases >= 2);

	juce::AudioBuffer<float> buffer;
	vekt::test::AllocationScope bufferScope;
	buffer.setSize(2, 4096);
	sink = buffer.getWritePointer(0);
	const auto bufferCounts = bufferScope.stop();
	REQUIRE(bufferCounts.allocations >= 1);
}

TEST_CASE("Flint allocation counter counts nothing when nothing allocates", "[flint][allocation]")
{
	std::vector<double> values(1024, 0.5);
	vekt::test::AllocationScope scope;
	auto sum = 0.0;
	for (const auto value : values) sum += value;
	const auto counts = scope.stop();
	REQUIRE(counts.none());
	REQUIRE(sum > 0.0);
}
