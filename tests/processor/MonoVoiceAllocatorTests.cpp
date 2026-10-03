#include "MonoVoiceAllocator.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <memory>

namespace
{
using vekt::mono::NotePriority;
using vekt::mono::PerformanceMode;

// Records what the allocator asks of a voice. Like MonoVoice, a released voice stays active (its release tail) until
// stopped or its tail ends, which a test marks by clearing active.
struct FakeVoice
{
	int channel {}, note {}, starts {};
	bool active {}, held {}, sustained {}, retriggered {}, legato {};
	std::uint64_t age {};
	float pan {};

	[[nodiscard]] bool isActive() const noexcept { return active; }
	[[nodiscard]] bool isHeld() const noexcept { return held; }
	[[nodiscard]] bool isSustained() const noexcept { return sustained; }
	[[nodiscard]] bool matches(int expectedChannel, int expectedNote) const noexcept
	{
		return active && channel == expectedChannel && note == expectedNote;
	}
	[[nodiscard]] int getChannel() const noexcept { return channel; }
	[[nodiscard]] std::uint64_t getAge() const noexcept { return age; }
	void setPanPosition(float value) noexcept { pan = value; }
	void start(int newChannel, int newNote, float, int, bool retrigger, bool isLegato, std::uint64_t newAge)
	{
		channel = newChannel;
		note = newNote;
		active = held = true;
		sustained = false;
		retriggered = retrigger;
		legato = isLegato;
		age = newAge;
		++starts;
	}
	void release(bool keepSustained)
	{
		held = false;
		sustained = keepSustained;
	}
	void releaseSustain()
	{
		if (sustained && !held) sustained = false;
	}
	void stop() { active = held = sustained = false; }
};

struct Fixture
{
	std::array<std::unique_ptr<FakeVoice>, 4> voices;
	vekt::mono::MonoVoiceAllocator<FakeVoice> allocator;
	vekt::mono::MonoVoiceAllocator<FakeVoice>::Rules rules { .voiceLimit = 4 }; // as many as the fixture has

	Fixture()
	{
		for (auto& voice : voices) voice = std::make_unique<FakeVoice>();
	}
	void on(int note, int channel = 1) { allocator.noteOn(voices, rules, channel, note, 0.8f, 0); }
	void off(int note, int channel = 1)
	{
		allocator.noteOff(voices, rules, channel, note, [] { return 0; });
	}
	[[nodiscard]] FakeVoice& voice(std::size_t index) const { return *voices[index]; }
};
}

TEST_CASE("Mono voice allocator fills free voices, then steals the oldest released voice before the oldest held",
    "[mono][allocator]")
{
	Fixture poly;
	poly.rules.voiceLimit = 3;
	poly.on(60);
	poly.on(62);
	poly.on(64);
	REQUIRE(poly.voice(0).note == 60);
	REQUIRE(poly.voice(1).note == 62);
	REQUIRE(poly.voice(2).note == 64);
	poly.off(62); // releasing: still sounding, but no longer held
	poly.on(65);
	REQUIRE(poly.voice(1).note == 65); // the released voice, although voice 0 is older
	poly.on(67);
	REQUIRE(poly.voice(0).note == 67); // every voice held: the oldest
	REQUIRE(poly.voice(3).starts == 0); // beyond the voice limit
}

TEST_CASE("Mono voice allocator restarts a still-sounding key on its own voice", "[mono][allocator]")
{
	Fixture poly;
	poly.on(60);
	poly.on(64);
	poly.off(60);
	poly.on(60);
	REQUIRE(poly.voice(0).note == 60);
	REQUIRE(poly.voice(0).starts == 2);
	REQUIRE(poly.voice(0).retriggered);
	REQUIRE(poly.voice(2).starts == 0); // not a second copy
}

TEST_CASE("Mono voice allocator spreads polyphonic voices across the stereo field", "[mono][allocator]")
{
	Fixture poly;
	poly.rules.voiceLimit = 3;
	poly.on(60);
	poly.on(62);
	poly.on(64);
	REQUIRE(juce::exactlyEqual(poly.voice(0).pan, -1.0f));
	REQUIRE(juce::exactlyEqual(poly.voice(1).pan, 0.0f));
	REQUIRE(juce::exactlyEqual(poly.voice(2).pan, 1.0f));
}

TEST_CASE("Mono voice allocator with last-note priority plays the newest held note and returns to the previous one",
    "[mono][allocator]")
{
	Fixture mono;
	mono.rules.mode = PerformanceMode::mono;
	mono.on(52);
	mono.on(45);
	REQUIRE(mono.voice(0).note == 45);
	REQUIRE(mono.voice(0).retriggered); // Mono retriggers every note
	mono.off(45);
	REQUIRE(mono.voice(0).note == 52); // held-key return
	REQUIRE(mono.voice(0).held);
	REQUIRE(mono.voice(0).legato);
	mono.off(52);
	REQUIRE_FALSE(mono.voice(0).held);
	REQUIRE(mono.voice(1).starts == 0); // channel 1 plays on voice 0 only
}

TEST_CASE("Mono voice allocator with low-note priority ignores higher notes until the lowest is released",
    "[mono][allocator]")
{
	Fixture mono;
	mono.rules.mode = PerformanceMode::mono;
	mono.rules.priority = NotePriority::low;
	mono.on(45);
	mono.on(52);
	REQUIRE(mono.voice(0).note == 45);
	REQUIRE(mono.voice(0).starts == 1);
	mono.off(45);
	REQUIRE(mono.voice(0).note == 52);
	REQUIRE(mono.voice(0).held);
}

TEST_CASE("Mono voice allocator in Mono Legato glides between held notes and retriggers otherwise", "[mono][allocator]")
{
	Fixture legato;
	legato.rules.mode = PerformanceMode::monoLegato;
	legato.on(45);
	REQUIRE(legato.voice(0).retriggered);
	legato.on(52);
	REQUIRE_FALSE(legato.voice(0).retriggered);
	REQUIRE(legato.voice(0).legato);
	legato.off(52);
	legato.off(45);
	legato.voice(0).active = false; // the release tail ends
	legato.on(48);
	REQUIRE(legato.voice(0).retriggered);
	REQUIRE_FALSE(legato.voice(0).legato);
}

TEST_CASE("Mono voice allocator without held-key return releases instead of returning", "[mono][allocator]")
{
	Fixture mono;
	mono.rules.mode = PerformanceMode::mono;
	mono.rules.heldKeyReturn = false;
	mono.on(45);
	mono.on(52);
	mono.off(52);
	REQUIRE(mono.voice(0).note == 52);
	REQUIRE_FALSE(mono.voice(0).held);
}

TEST_CASE("Mono voice allocator holds released notes on the sustain pedal until it lifts", "[mono][allocator]")
{
	Fixture poly;
	poly.allocator.setSustain(poly.voices, 1, true);
	poly.on(60);
	poly.off(60);
	REQUIRE_FALSE(poly.voice(0).held);
	REQUIRE(poly.voice(0).sustained);
	poly.allocator.setSustain(poly.voices, 2, false); // another channel's pedal
	REQUIRE(poly.voice(0).sustained);
	poly.allocator.setSustain(poly.voices, 1, false);
	REQUIRE_FALSE(poly.voice(0).sustained);
}

TEST_CASE("Mono voice allocator stops or releases only the addressed channel's notes", "[mono][allocator]")
{
	Fixture poly;
	poly.on(60, 1);
	poly.on(64, 2);
	poly.allocator.allNotesOff(poly.voices, 1, false);
	REQUIRE(poly.voice(0).active); // released into its tail
	REQUIRE_FALSE(poly.voice(0).held);
	REQUIRE(poly.voice(1).held);
	poly.allocator.allNotesOff(poly.voices, 2, true);
	REQUIRE_FALSE(poly.voice(1).active);
	REQUIRE(poly.voice(0).active);
}
