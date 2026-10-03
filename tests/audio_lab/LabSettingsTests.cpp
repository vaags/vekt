#include <audio_lab/LabSettings.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Audio Lab settings round trip through its rack state", "[audio-lab][settings]")
{
	juce::ValueTree state("VektAudioLabRackState");
	const vekt::audio_lab::LabSettings expected {
		8, -2, 4, 4, "midi-device-id", "/Users/tester/Audio/loop.wav", true, 1
	};

	vekt::audio_lab::writeLabSettings(state, expected);
	const auto restored = vekt::audio_lab::readLabSettings(state);

	REQUIRE(restored.source == expected.source);
	REQUIRE(restored.octave == expected.octave);
	REQUIRE(restored.selectedTab == expected.selectedTab);
	REQUIRE(restored.rackRoute == expected.rackRoute);
	REQUIRE(restored.midiInputIdentifier == expected.midiInputIdentifier);
	REQUIRE(restored.audioFilePath == expected.audioFilePath);
	REQUIRE(restored.outputArmed == expected.outputArmed);
	REQUIRE(restored.instrument == expected.instrument);
}

TEST_CASE("Audio Lab settings default and clamp invalid persisted values", "[audio-lab][settings]")
{
	juce::ValueTree state("VektAudioLabRackState");
	state.setProperty("source", 24, nullptr);
	state.setProperty("octave", -5, nullptr);
	state.setProperty("selectedTab", 0, nullptr);
	state.setProperty("rackRoute", 6, nullptr);
	state.setProperty("instrument", 5, nullptr);

	const auto restored = vekt::audio_lab::readLabSettings(state);

	REQUIRE(restored.source == 9);
	REQUIRE(restored.octave == -3);
	REQUIRE(restored.selectedTab == 1);
	REQUIRE(restored.rackRoute == 4);
	REQUIRE(restored.midiInputIdentifier.isEmpty());
	REQUIRE(restored.audioFilePath.isEmpty());
	REQUIRE_FALSE(restored.outputArmed);
	REQUIRE(restored.instrument == 1);

	// Settings saved before Flint joined the lab play Mono.
	REQUIRE(vekt::audio_lab::readLabSettings(juce::ValueTree("VektAudioLabRackState")).instrument == 0);
}
