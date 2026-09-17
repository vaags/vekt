#include <RavStageChain.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("RavStageChain accepts unique mode orders", "[processor][chain]")
{
	vekt::rav::RavStageChain chain;
	vekt::rav::RavStageChain::Order order {
		vekt::rav::RavMode::fuzz, vekt::rav::RavMode::saturation,
		vekt::rav::RavMode::bitcrush, vekt::rav::RavMode::wavefold,
		vekt::rav::RavMode::distortion, vekt::rav::RavMode::overdrive };
	REQUIRE(chain.setOrder(order));
	REQUIRE(chain.getOrder() == order);
}

TEST_CASE("RavStageChain rejects duplicate mode orders", "[processor][chain]")
{
	vekt::rav::RavStageChain chain;
	const auto original = chain.getOrder();
	auto invalid = original;
	invalid[1] = invalid[0];
	REQUIRE_FALSE(chain.setOrder(invalid));
	REQUIRE(chain.getOrder() == original);
}

TEST_CASE("RavStageChain serializes and restores order metadata", "[processor][chain]")
{
	vekt::rav::RavStageChain chain;
	vekt::rav::RavStageChain::Order custom {
		vekt::rav::RavMode::wavefold, vekt::rav::RavMode::overdrive,
		vekt::rav::RavMode::saturation, vekt::rav::RavMode::bitcrush,
		vekt::rav::RavMode::distortion, vekt::rav::RavMode::fuzz };
	REQUIRE(chain.setOrder(custom));

	juce::ValueTree metadata("ProjectMetadata");
	chain.writeMetadata(metadata);
	auto restored = vekt::rav::RavStageChain::readMetadata(metadata);
	REQUIRE(restored.getOrder() == custom);
}
