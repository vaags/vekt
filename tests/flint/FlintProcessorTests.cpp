#include "AllocationCounter.h"

#include <vekt/flint/Parameters.h>
#include <vekt/flint/PluginProcessor.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <vector>

namespace
{
using vekt::flint::Mode;
using vekt::flint::PluginProcessor;
namespace parameters = vekt::flint::parameters;

constexpr double sampleRate = 44'100.0;
constexpr int blockSize = 512;

class TestPlayHead final : public juce::AudioPlayHead
{
public:
	juce::Optional<PositionInfo> getPosition() const override
	{
		PositionInfo info;
		info.setIsPlaying(beats.has_value());
		info.setBpm(120.0);
		if (beats) info.setPpqPosition(*beats);
		return info;
	}

	std::optional<double> beats;
};

void setParameter(PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

// Renders `blocks` blocks, with a note at `noteOffsets[i]` (absolute sample) if any; returns the left channel.
std::vector<float> renderBlocks(
    PluginProcessor& processor, int blocks, const std::vector<int>& noteOffsets = {}, TestPlayHead* playHead = nullptr)
{
	std::vector<float> output;
	juce::AudioBuffer<float> buffer(2, blockSize);
	for (auto block = 0; block < blocks; ++block)
	{
		juce::MidiBuffer midi;
		for (const auto offset : noteOffsets)
			if (offset >= block * blockSize && offset < (block + 1) * blockSize)
				midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), offset - block * blockSize);
		processor.processBlock(buffer, midi);
		for (auto sample = 0; sample < blockSize; ++sample) output.push_back(buffer.getSample(0, sample));
		if (playHead != nullptr && playHead->beats) *playHead->beats += blockSize / (sampleRate * 0.5); // 120 bpm
	}
	return output;
}

std::unique_ptr<PluginProcessor> preparedProcessor()
{
	auto processor = std::make_unique<PluginProcessor>();
	processor->prepareToPlay(sampleRate, blockSize);
	return processor;
}

// Hits that vary differ by far more than rounding: Variation moves the click by decibels.
bool clearlyDifferent(std::span<const float> a, std::span<const float> b)
{
	auto largest = 0.0f;
	for (std::size_t index = 0; index < std::min(a.size(), b.size()); ++index)
		largest = std::max(largest, std::abs(a[index] - b[index]));
	return largest > 1.0e-3f;
}

bool identical(std::span<const float> a, std::span<const float> b)
{
	return a.size() == b.size() &&
	    std::equal(a.begin(), a.end(), b.begin(), [](float x, float y) { return juce::exactlyEqual(x, y); });
}
}

TEST_CASE("Flint sounds a note at its exact sample offset", "[flint][processor]")
{
	auto processor = preparedProcessor();
	const auto output = renderBlocks(*processor, 2, { 700 });
	const auto first = std::find_if(output.begin(), output.end(), [](float x) { return std::abs(x) > 0.0f; });
	REQUIRE(first != output.end());
	REQUIRE(std::distance(output.begin(), first) == 700);
}

TEST_CASE("Flint is silent and does no work until a note, and again once the note has died away", "[flint][processor]")
{
	auto processor = preparedProcessor();
	REQUIRE(processor->isSilent());
	renderBlocks(*processor, 4);
	REQUIRE(processor->isSilent());
	renderBlocks(*processor, 1, { 10 });
	REQUIRE_FALSE(processor->isSilent());
	const auto tail = renderBlocks(*processor, 200); // 2.3 s: the default kick's 333 ms tail and the DC blocker
	REQUIRE(processor->isSilent());
	const auto after = renderBlocks(*processor, 4);
	REQUIRE(std::all_of(after.begin(), after.end(), [](float x) { return juce::exactlyEqual(x, 0.0f); }));
}

TEST_CASE(
    "Flint plays the same hit at the same song position however playback started", "[flint][processor][variation]")
{
	// Notes at beats 8 and 12 (2 s apart at 120 bpm, longer than the tail), rendered from beat 0 and from beat 6.
	auto render = [](double startBeats)
	{
		auto processor = preparedProcessor();
		setParameter(*processor, parameters::variation, 100.0f);
		setParameter(*processor, parameters::kickClick, 50.0f);
		TestPlayHead playHead;
		playHead.beats = startBeats;
		processor->setPlayHead(&playHead);
		const auto samplesPerBeat = sampleRate * 0.5;
		const auto noteAt = [&](double beats)
		{ return static_cast<int>(std::lround((beats - startBeats) * samplesPerBeat)); };
		const auto blocks = static_cast<int>(std::ceil((13.0 - startBeats) * samplesPerBeat / blockSize));
		auto output = renderBlocks(*processor, blocks, { noteAt(8.0), noteAt(12.0) }, &playHead);
		std::vector<float> hits;
		for (const auto beats : { 8.0, 12.0 })
		{
			const auto start = static_cast<std::size_t>(noteAt(beats));
			hits.insert(hits.end(), output.begin() + static_cast<std::ptrdiff_t>(start),
			    output.begin() + static_cast<std::ptrdiff_t>(start + 4'410));
		}
		processor->setPlayHead(nullptr);
		return std::pair { hits, processor->getSeed() };
	};
	auto [fromStart, seed] = render(0.0);
	// Same seed for both renders: restore the first instance's seed into the second through project state.
	auto processor = preparedProcessor();
	REQUIRE(processor->getSeed() != seed);
	auto [fromLater, otherSeed] = render(6.0);
	juce::ignoreUnused(otherSeed);
	// Different instances have different seeds, so compare each against itself: the two hits of one render differ,
	// and rendering again with the same seed from another start gives the same hits.
	REQUIRE(clearlyDifferent(std::span(fromStart).first(4'410), std::span(fromStart).subspan(4'410)));

	auto renderWithSeed = [&](double startBeats, const juce::MemoryBlock& state)
	{
		auto instance = preparedProcessor();
		instance->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		instance->prepareToPlay(sampleRate, blockSize);
		TestPlayHead playHead;
		playHead.beats = startBeats;
		instance->setPlayHead(&playHead);
		const auto samplesPerBeat = sampleRate * 0.5;
		const auto noteAt = [&](double beats)
		{ return static_cast<int>(std::lround((beats - startBeats) * samplesPerBeat)); };
		const auto blocks = static_cast<int>(std::ceil((13.0 - startBeats) * samplesPerBeat / blockSize));
		auto output = renderBlocks(*instance, blocks, { noteAt(8.0), noteAt(12.0) }, &playHead);
		const auto start = static_cast<std::size_t>(noteAt(12.0));
		instance->setPlayHead(nullptr);
		return std::vector<float>(output.begin() + static_cast<std::ptrdiff_t>(start),
		    output.begin() + static_cast<std::ptrdiff_t>(start + 4'410));
	};
	auto source = preparedProcessor();
	setParameter(*source, parameters::variation, 100.0f);
	setParameter(*source, parameters::kickClick, 50.0f);
	juce::MemoryBlock state;
	source->getStateInformation(state);
	REQUIRE(identical(renderWithSeed(0.0, state), renderWithSeed(6.0, state)));
}

TEST_CASE("Flint hits at Variation 0 are identical in the processor, the first after preparing included",
    "[flint][processor][variation]")
{
	auto processor = preparedProcessor();
	setParameter(*processor, parameters::variation, 0.0f);
	setParameter(*processor, parameters::kickClick, 50.0f);
	const auto output = renderBlocks(*processor, 400, { 0, 88'200 });
	REQUIRE(identical(std::span(output).first(4'410), std::span(output).subspan(88'200, 4'410)));
}

TEST_CASE("Flint hits vary with the transport stopped", "[flint][processor][variation]")
{
	auto processor = preparedProcessor();
	setParameter(*processor, parameters::variation, 100.0f);
	setParameter(*processor, parameters::kickClick, 50.0f);
	const auto output = renderBlocks(*processor, 400, { 0, 88'200 });
	REQUIRE(clearlyDifferent(std::span(output).first(2'000), std::span(output).subspan(88'200, 2'000)));
}

TEST_CASE("Flint keeps its seed through project state, keeps it on a failed restore and draws a new one on request",
    "[flint][processor][state]")
{
	auto first = std::make_unique<PluginProcessor>();
	auto second = std::make_unique<PluginProcessor>();
	REQUIRE(first->getSeed() != second->getSeed());
	juce::MemoryBlock state;
	first->getStateInformation(state);
	second->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(second->getSeed() == first->getSeed());
	const auto kept = second->getSeed();
	const juce::String garbage = "{ not a project";
	second->setStateInformation(garbage.toRawUTF8(), static_cast<int>(garbage.getNumBytesAsUTF8()));
	REQUIRE(second->getSeed() == kept);
	second->newSeed();
	REQUIRE(second->getSeed() != kept);
}

TEST_CASE("Flint round-trips its parameters and preset selection through project state", "[flint][processor][state]")
{
	auto source = std::make_unique<PluginProcessor>();
	source->setCurrentProgram(4); // Rosewood Marimba
	setParameter(*source, parameters::barHardness, 77.0f);
	juce::MemoryBlock state;
	source->getStateInformation(state);
	auto restored = std::make_unique<PluginProcessor>();
	restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	for (const auto* identifier : parameters::soundParameterIds)
	{
		INFO(identifier);
		// Within half the parameters' 0.01 step: APVTS keeps a set value in its state tree to float precision.
		REQUIRE(std::abs(restored->getParameters().getRawParameterValue(identifier)->load() -
		            source->getParameters().getRawParameterValue(identifier)->load()) < 0.005f);
	}
	REQUIRE(restored->getCurrentProgram() == source->getCurrentProgram());
}

TEST_CASE("Flint offers its factory presets as host programs", "[flint][processor][presets]")
{
	auto processor = std::make_unique<PluginProcessor>();
	REQUIRE(processor->getNumPrograms() == 8);
	REQUIRE(processor->getProgramName(0) == "Classic");
	REQUIRE(processor->getProgramName(4) == "Rosewood Marimba");
	REQUIRE(processor->getCurrentProgram() == 0);
	processor->setCurrentProgram(6);
	REQUIRE(processor->getCurrentProgram() == 6);
	REQUIRE(juce::roundToInt(processor->getParameters().getRawParameterValue(parameters::mode)->load()) ==
	    static_cast<int>(Mode::mallet));
}

TEST_CASE("Flint preset loads silence the previous sound and still play a note in the same block",
    "[flint][processor][presets]")
{
	auto processor = preparedProcessor();
	setParameter(*processor, parameters::decay, 90.0f);
	renderBlocks(*processor, 2, { 0 });
	processor->setCurrentProgram(4); // Rosewood Marimba: a preset load while the kick rings
	const auto afterLoad = renderBlocks(*processor, 1);
	REQUIRE(std::all_of(afterLoad.begin(), afterLoad.end(), [](float x) { return juce::exactlyEqual(x, 0.0f); }));
	processor->setCurrentProgram(0);
	const auto withNote = renderBlocks(*processor, 1, { 0 });
	REQUIRE(std::abs(withNote[50]) > 0.0f);
}

TEST_CASE("Flint applies a quality change at the next block and reports its latency", "[flint][processor][quality]")
{
	auto processor = preparedProcessor();
	REQUIRE(processor->getLatencySamples() == 0);
	auto* tracking = processor->getParameters().getParameter(parameters::trackingOversampling);
	juce::AudioBuffer<float> buffer(2, blockSize);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
	for (const auto index : { 1, 4, 6, 0 })
	{
		tracking->setValueNotifyingHost(tracking->convertTo0to1(static_cast<float>(index)));
		vekt::test::AllocationScope scope;
		processor->processBlock(buffer, midi);
		const auto counts = scope.stop();
		const auto expected = vekt::dsp::trackingQualityFrom(static_cast<float>(index));
		INFO("choice " << index << ": latency " << processor->getLatencySamples());
		REQUIRE(processor->getActiveQuality() == expected);
		REQUIRE((index == 0) == (processor->getLatencySamples() == 0));
		REQUIRE(counts.none());
		REQUIRE(std::all_of(
		    buffer.getReadPointer(0), buffer.getReadPointer(0) + blockSize, [](float x) { return std::isfinite(x); }));
	}
}

TEST_CASE("Flint Mode change sets the mode's start values in one undo step", "[flint][processor][ui]")
{
	auto processor = std::make_unique<PluginProcessor>();
	setParameter(*processor, parameters::decay, 80.0f);
	setParameter(*processor, parameters::malletModel, 0.0f);
	processor->getUndoManager().beginNewTransaction();
	processor->selectMode(Mode::mallet);
	auto value = [&](const char* identifier)
	{ return processor->getParameters().getRawParameterValue(identifier)->load(); };
	const auto start = parameters::startValuesFor(Mode::mallet);
	REQUIRE(juce::roundToInt(value(parameters::mode)) == static_cast<int>(Mode::mallet));
	REQUIRE(juce::exactlyEqual(value(parameters::pitch), start.pitch));
	REQUIRE(juce::exactlyEqual(value(parameters::decay), start.decay));
	REQUIRE(processor->getUndoManager().undo());
	REQUIRE(juce::roundToInt(value(parameters::mode)) == static_cast<int>(Mode::kick));
	REQUIRE(juce::exactlyEqual(value(parameters::decay), 80.0f));
}

TEST_CASE("Flint pitch reads as note and cents and parses back", "[flint][processor]")
{
	REQUIRE(parameters::pitchText(33.0f) == "A1");
	REQUIRE(parameters::pitchText(45.12f) == "A2 +12 ct");
	REQUIRE(parameters::pitchText(44.9f) == "A2 -10 ct");
	REQUIRE(std::abs(parameters::pitchFromText("A2 +12 ct") - 45.12f) < 1.0e-4f);
	REQUIRE(std::abs(parameters::pitchFromText("C4") - 60.0f) < 1.0e-4f);
	REQUIRE(std::abs(parameters::pitchFromText("61.5") - 61.5f) < 1.0e-4f);
}

TEST_CASE("Flint allocates nothing while playing notes and switching modes", "[flint][processor][allocation]")
{
	auto processor = preparedProcessor();
	renderBlocks(*processor, 1);
	juce::AudioBuffer<float> buffer(2, blockSize);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.8f), 3);
	midi.addEvent(juce::MidiMessage::noteOff(1, 36), 300);
	midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.5f), 400);
	setParameter(*processor, parameters::mode, static_cast<float>(Mode::mallet));
	vekt::test::AllocationScope scope;
	for (auto block = 0; block < 20; ++block) processor->processBlock(buffer, midi);
	const auto counts = scope.stop();
	REQUIRE(counts.none());
}
