#pragma once

#include "KobberChoiceTypes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace vekt::kobber
{
// How notes become voices: polyphonic reuse and stealing, the monophonic modes with note priority and held-key return,
// and the sustain pedal, per MIDI channel (1-16). It owns the per-channel held notes, pedal state and note ages; the
// voices stay the processor's. Templated on the voice so tests drive it with a recording fake (KobberVoice in the plugin):
// a voice provides isActive, isHeld, isSustained, matches, getChannel, getAge, setPanPosition, start, release,
// releaseSustain and stop. Audio thread only, after construction; no allocation (held notes are reserved).
template <typename Voice> class KobberVoiceAllocator
{
public:
	using Voices = std::span<const std::unique_ptr<Voice>>;

	// The parameters that decide allocation, read once per event.
	struct Rules
	{
		PerformanceMode mode { PerformanceMode::poly };
		NotePriority priority { NotePriority::last };
		bool heldKeyReturn { true };
		int voiceLimit { 8 }; // the first voiceLimit voices are used in Poly
	};

	KobberVoiceAllocator()
	{
		for (auto& heldNotes : heldNotesByChannel) heldNotes.reserve(128);
	}

	// settings: the voice settings to start a note with, passed through to Voice::start.
	template <typename Settings>
	void noteOn(
	    Voices voices, const Rules& rules, int channel, int note, float velocity, const Settings& settings) noexcept
	{
		if (rules.mode != PerformanceMode::poly)
		{
			auto& heldNotes = heldNotesByChannel[channelIndex(channel)];
			const auto legato = !heldNotes.empty();
			heldNotes.erase(std::remove_if(heldNotes.begin(), heldNotes.end(),
			                    [note](const auto& heldNote) { return heldNote.note == note; }),
			    heldNotes.end());
			heldNotes.push_back({ note, velocity });
			const auto lowPriority = rules.priority == NotePriority::low;
			const auto& selected = lowPriority ? lowestHeld(heldNotes) : heldNotes.back();
			if (lowPriority && selected.note != note) return;
			auto& voice = monoVoiceForChannel(voices, channel);
			voice.setPanPosition(0.0f);
			const auto retrigger = rules.mode == PerformanceMode::mono || !legato || !voice.isActive() ||
			    (!voice.isHeld() && !voice.isSustained());
			voice.start(channel, note, velocity, settings, retrigger, legato, ++noteAge);
			return;
		}
		// A key that is still sounding (held, sustained or releasing) retriggers its own voice, as on analog
		// polysynths: its envelopes restart from their current level instead of stacking a second copy.
		for (const auto& sounding : voices)
			if (sounding->matches(channel, note))
			{
				sounding->start(channel, note, velocity, settings, true, false, ++noteAge);
				return;
			}
		auto& voice = findVoiceForNote(voices, rules.voiceLimit);
		voice.setPanPosition(rules.voiceLimit <= 1
		        ? 0.0f
		        : 2.0f * static_cast<float>(noteAge % static_cast<std::uint64_t>(rules.voiceLimit)) /
		                static_cast<float>(rules.voiceLimit - 1) -
		            1.0f);
		voice.start(channel, note, velocity, settings, true, false, ++noteAge);
	}

	// currentSettings: called for the voice settings only when a held note is returned to.
	template <typename SettingsProvider>
	void noteOff(
	    Voices voices, const Rules& rules, int channel, int note, const SettingsProvider& currentSettings) noexcept
	{
		if (rules.mode != PerformanceMode::poly)
		{
			auto& heldNotes = heldNotesByChannel[channelIndex(channel)];
			auto& voice = monoVoiceForChannel(voices, channel);
			const auto wasActive = voice.matches(channel, note);
			heldNotes.erase(std::remove_if(heldNotes.begin(), heldNotes.end(),
			                    [note](const auto& heldNote) { return heldNote.note == note; }),
			    heldNotes.end());
			if (wasActive && rules.heldKeyReturn && !heldNotes.empty())
			{
				const auto& returned = rules.priority == NotePriority::low ? lowestHeld(heldNotes) : heldNotes.back();
				voice.start(channel, returned.note, returned.velocity, currentSettings(),
				    rules.mode == PerformanceMode::mono, true, ++noteAge);
				return;
			}
			if (wasActive) voice.release(sustainByChannel[channelIndex(channel)]);
			return;
		}
		for (const auto& voice : voices)
			if (voice->matches(channel, note)) voice->release(sustainByChannel[channelIndex(channel)]);
	}

	// Pedal down holds released notes; lifting it releases them.
	void setSustain(Voices voices, int channel, bool down) noexcept
	{
		auto& sustain = sustainByChannel[channelIndex(channel)];
		const auto wasDown = sustain;
		sustain = down;
		if (wasDown && !down)
			for (const auto& voice : voices)
				if (voice->getChannel() == channel) voice->releaseSustain();
	}

	// All Notes Off (immediate: All Sound Off) on one channel; the pedal is lifted without releasing again.
	void allNotesOff(Voices voices, int channel, bool immediate) noexcept
	{
		heldNotesByChannel[channelIndex(channel)].clear();
		sustainByChannel[channelIndex(channel)] = false;
		for (const auto& voice : voices)
			if (voice->getChannel() == channel)
			{
				if (immediate)
					voice->stop();
				else
					voice->release(false);
			}
	}

	// Forgets held notes, pedals and note ages (the voices are reset by their owner).
	void reset() noexcept
	{
		for (auto& heldNotes : heldNotesByChannel) heldNotes.clear();
		sustainByChannel.fill(false);
		noteAge = 0;
	}

private:
	struct HeldNote
	{
		int note {};
		float velocity {};
	};

	[[nodiscard]] static std::size_t channelIndex(int channel) noexcept
	{
		return static_cast<std::size_t>(channel - 1);
	}

	[[nodiscard]] static const HeldNote& lowestHeld(const std::vector<HeldNote>& heldNotes) noexcept
	{
		return *std::min_element(
		    heldNotes.begin(), heldNotes.end(), [](const auto& a, const auto& b) { return a.note < b.note; });
	}

	// A monophonic channel plays on its own voice.
	[[nodiscard]] static Voice& monoVoiceForChannel(Voices voices, int channel) noexcept
	{
		return *voices[static_cast<std::size_t>(std::clamp(channel - 1, 0, static_cast<int>(voices.size()) - 1))];
	}

	// A free voice among the first voiceLimit (at most every voice given), else the oldest released one, else the oldest
	// held one.
	[[nodiscard]] static Voice& findVoiceForNote(Voices voices, int requestedLimit) noexcept
	{
		const auto voiceLimit = std::min(requestedLimit, static_cast<int>(voices.size()));
		for (int index = 0; index < voiceLimit; ++index)
			if (!voices[static_cast<std::size_t>(index)]->isActive()) return *voices[static_cast<std::size_t>(index)];
		auto* selected = voices.front().get();
		for (int index = 0; index < voiceLimit; ++index)
		{
			auto* candidate = voices[static_cast<std::size_t>(index)].get();
			if ((!candidate->isHeld() && selected->isHeld()) ||
			    (candidate->isHeld() == selected->isHeld() && candidate->getAge() < selected->getAge()))
				selected = candidate;
		}
		return *selected;
	}

	std::array<std::vector<HeldNote>, 16> heldNotesByChannel;
	std::array<bool, 16> sustainByChannel {};
	std::uint64_t noteAge {};
};
}
