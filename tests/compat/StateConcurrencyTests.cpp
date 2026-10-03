// A host may save, restore and switch programs from any thread, at the same time (AU hosts and auval's stress test do):
// every product's state has to survive it. These tests race the three; a ThreadSanitizer build also reports data races
// that do not crash (docs/ARCHITECTURE.md, Plugin Support), though not every one: see the Rav rendering case.

#include <vekt/flint/PluginProcessor.h>
#include <vekt/glimmer/PluginProcessor.h>
#include <vekt/kobber/PluginProcessor.h>
#include <vekt/rav/PluginProcessor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <thread>

namespace
{
constexpr int iterations = 150;

void checkConcurrentState(juce::AudioProcessor& processor)
{
	processor.setCurrentProgram(std::min(1, processor.getNumPrograms() - 1));
	juce::MemoryBlock saved;
	processor.getStateInformation(saved);
	REQUIRE(saved.getSize() > 0);

	const auto save = [&]
	{
		for (int iteration = 0; iteration < iterations; ++iteration)
		{
			juce::MemoryBlock block;
			processor.getStateInformation(block);
		}
	};
	std::thread saver(save);
	const auto restore = [&]
	{
		for (int iteration = 0; iteration < iterations; ++iteration)
			processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
	};
	std::thread restorer(restore);
	const auto switchPrograms = [&]
	{
		for (int iteration = 0; iteration < iterations; ++iteration)
		{
			processor.setCurrentProgram(iteration % std::max(1, processor.getNumPrograms()));
			juce::ignoreUnused(processor.getCurrentProgram(), processor.getProgramName(0));
		}
	};
	std::thread programs(switchPrograms);
	saver.join();
	restorer.join();
	programs.join();

	// Still whole: restoring the saved state gives it back exactly.
	processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
	juce::MemoryBlock again;
	processor.getStateInformation(again);
	REQUIRE(again == saved);
}
}

TEST_CASE("Rav state survives concurrent save, restore and program changes", "[rav][state]")
{
	vekt::rav::PluginProcessor processor;
	checkConcurrentState(processor);
}

TEST_CASE("Glimmer state survives concurrent save, restore and program changes", "[glimmer][state]")
{
	vekt::glimmer::PluginProcessor processor;
	checkConcurrentState(processor);
}

TEST_CASE("Kobber state survives concurrent save, restore and program changes", "[kobber][state]")
{
	vekt::kobber::PluginProcessor processor;
	checkConcurrentState(processor);
}

TEST_CASE("Flint state survives concurrent save, restore and program changes", "[flint][state]")
{
	vekt::flint::PluginProcessor processor;
	checkConcurrentState(processor);
}

// Guards rendering against crashes while the order changes. It does not show the stage-order race to TSan: with the
// audio thread reading the chain directly (no snapshot), TSan reported nothing here while a plain race in the same loop
// was reported (3 October 2026), so the lock-free snapshot rests on review and RavStageChain's static checks.
TEST_CASE("Rav keeps rendering while its stage order is restored and reordered", "[rav][state]")
{
	vekt::rav::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 64);
	juce::ignoreUnused(processor.reorderStage(0, 2));
	juce::MemoryBlock saved;
	processor.getStateInformation(saved);
	std::atomic<bool> running { true };
	std::atomic<int> blocks {};
	const auto render = [&]
	{
		juce::AudioBuffer<float> buffer(2, 64);
		juce::MidiBuffer midi;
		while (running.load())
		{
			buffer.clear();
			buffer.setSample(0, 0, 0.5f);
			processor.processBlock(buffer, midi);
			blocks.fetch_add(1, std::memory_order_relaxed);
		}
	};
	std::thread audio(render);
	// Keep changing the order until the audio thread has rendered enough blocks to overlap the changes. The count is
	// relaxed so it does not order the audio thread's reads before the next change, which would hide a race from TSan.
	for (int iteration = 0; iteration < iterations || blocks.load(std::memory_order_relaxed) < 200; ++iteration)
	{
		processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
		juce::ignoreUnused(processor.reorderStage(static_cast<std::size_t>(iteration % 5), 1));
	}
	running.store(false);
	audio.join();
	processor.releaseResources();
	processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
	juce::MemoryBlock again;
	processor.getStateInformation(again);
	REQUIRE(again == saved);
}

TEST_CASE("The preset list refreshes while a host switches programs", "[presets][state]")
{
	vekt::rav::PluginProcessor processor;
	auto& session = processor.getPresetSession();
	const auto refreshed = [](vekt::presets::PresetCatalog& library)
	{
		library.refresh();
		return library.entries().size();
	};
	const auto refresh = [&]
	{
		for (int iteration = 0; iteration < iterations; ++iteration)
		{
			const auto count = session.withLibrary(refreshed);
			juce::ignoreUnused(count);
		}
	};
	std::thread editor(refresh);
	for (int iteration = 0; iteration < iterations; ++iteration)
	{
		processor.setCurrentProgram(iteration % processor.getNumPrograms());
		juce::ignoreUnused(processor.getCurrentProgram());
	}
	editor.join();

	processor.setCurrentProgram(1);
	REQUIRE(processor.getCurrentProgram() == 1);
}

TEST_CASE("A program change from a host thread leaves the editor's undo history alone", "[rav][state]")
{
	vekt::rav::PluginProcessor processor;
	auto& history = processor.getUndoManager();
	processor.setCurrentProgram(1);
	REQUIRE(history.canUndo());
	// Saving flushes the parameter values into the state, recording them on this (the message) thread.
	juce::MemoryBlock flushed;
	processor.getStateInformation(flushed);
	history.clearUndoHistory();

	std::thread host([&processor] { processor.setCurrentProgram(2); });
	host.join();
	REQUIRE(processor.getCurrentProgram() == 2);
	REQUIRE_FALSE(history.canUndo());
}
