#include <vekt/flint/Parameters.h>
#include <vekt/flint/PluginProcessor.h>

#include "CallbackAllocationProbe.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Flint's cost (docs/FLINT_VALIDATION.md, A19), Release only, run alone:
//   VektFlintCost idle rate block seconds
//     an empty JUCE instrument, idle Flint, then the empty instrument again, in one process; reports Flint's idle
//     cost as a multiple of the empty instrument's (the mean of its two runs). A19: at most 2.
//   VektFlintCost kick|bar rate block seconds [factor]
//     the model struck at 8 Hz (16th notes at 120 BPM) at full velocity, at a quality factor of 1–16.
namespace
{
using Clock = std::chrono::steady_clock;

// The smallest instrument JUCE allows: stereo out, MIDI in, clears its buffer.
class EmptyInstrument final : public juce::AudioProcessor
{
public:
	EmptyInstrument() : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}
	void prepareToPlay(double, int) override {}
	void releaseResources() override {}
	void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override { buffer.clear(); }
	using AudioProcessor::processBlock;
	juce::AudioProcessorEditor* createEditor() override { return nullptr; }
	bool hasEditor() const override { return false; }
	const juce::String getName() const override { return "Empty"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return false; }
	double getTailLengthSeconds() const override { return 0.0; }
	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String&) override {}
	void getStateInformation(juce::MemoryBlock&) override {}
	void setStateInformation(const void*, int) override {}
};

struct Timing
{
	double meanUs {};
	double medianUs {};
	double p99_9Us {};
	double maximumUs {};
	double deadlineUs {};
	std::uint64_t newCalls {};
	double sumSquares {};
};

void setParameter(vekt::flint::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	if (parameter == nullptr) std::abort();
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

// Renders `seconds` of callbacks after 100 ms of warm-up; a note every `strikeSamples` (0: none).
Timing measure(juce::AudioProcessor& processor, double rate, int block, double seconds, int strikeSamples)
{
	juce::AudioBuffer<float> buffer(2, block);
	juce::MidiBuffer midi;
	long long position {};
	const auto render = [&]
	{
		midi.clear();
		if (strikeSamples > 0)
			for (auto offset = 0; offset < block; ++offset)
				if ((position + offset) % strikeSamples == 0)
					midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), offset);
		processor.processBlock(buffer, midi);
		position += block;
	};
	const auto warmup = static_cast<int>(std::ceil(0.1 * rate / block));
	for (auto callback = 0; callback < warmup; ++callback) render();
	const auto callbacks = std::max(1, static_cast<int>(std::ceil(rate * seconds / block)));
	std::vector<double> times;
	times.reserve(static_cast<std::size_t>(callbacks));
	Timing timing;
	for (auto callback = 0; callback < callbacks; ++callback)
	{
		const auto before = Clock::now();
		vekt::audio_lab::callback_allocation_probe::begin();
		render();
		timing.newCalls += vekt::audio_lab::callback_allocation_probe::end();
		times.push_back(std::chrono::duration<double, std::micro>(Clock::now() - before).count());
		for (auto channel = 0; channel < 2; ++channel)
			for (auto index = 0; index < block; ++index)
				timing.sumSquares +=
				    static_cast<double>(buffer.getSample(channel, index) * buffer.getSample(channel, index));
	}
	for (const auto time : times) timing.meanUs += time;
	timing.meanUs /= static_cast<double>(times.size());
	std::sort(times.begin(), times.end());
	const auto percentile = [&times](double fraction)
	{
		return times[std::min(
		    times.size() - 1, static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(times.size())) - 1.0))];
	};
	timing.medianUs = percentile(0.5);
	timing.p99_9Us = percentile(0.999);
	timing.maximumUs = times.back();
	timing.deadlineUs = block * 1'000'000.0 / rate;
	return timing;
}

void print(const char* name, const Timing& timing)
{
	std::cout << std::fixed << std::setprecision(4) << name << ": mean_us=" << timing.meanUs
	          << " median_us=" << timing.medianUs << " p99_9_us=" << timing.p99_9Us << " max_us=" << timing.maximumUs
	          << " deadline_us=" << timing.deadlineUs << " cpu_percent=" << 100.0 * timing.meanUs / timing.deadlineUs
	          << " callback_new_calls=" << timing.newCalls << " sum_squares=" << timing.sumSquares << '\n';
}

int qualityIndex(int factor)
{
	// Tracking choices (OversamplingChoices.h): Off, 2x IIR, 4x IIR, 2x FIR, 4x FIR, 8x FIR, 16x FIR.
	switch (factor)
	{
	case 1:
		return 0;
	case 2:
		return 1;
	case 4:
		return 2;
	case 8:
		return 5;
	case 16:
		return 6;
	default:
		return -1;
	}
}

std::unique_ptr<vekt::flint::PluginProcessor> flint(double rate, int block, vekt::flint::Mode mode, int factor)
{
	auto processor = std::make_unique<vekt::flint::PluginProcessor>();
	namespace parameters = vekt::flint::parameters;
	// The mode with its start values, as the editor sets them: the Bar plays C4, not the Kick's A1.
	setParameter(*processor, parameters::mode, static_cast<float>(mode));
	const auto start = parameters::startValuesFor(mode);
	setParameter(*processor, parameters::pitch, start.pitch);
	setParameter(*processor, parameters::attack, start.attack);
	setParameter(*processor, parameters::decay, start.decay);
	setParameter(*processor, parameters::tone, start.tone);
	setParameter(*processor, parameters::trackingOversampling, static_cast<float>(qualityIndex(factor)));
	processor->prepareToPlay(rate, block);
	return processor;
}

int run(std::string_view scenario, double rate, int block, double seconds, int factor)
{
	if (!vekt::audio_lab::callback_allocation_probe::verify() || qualityIndex(factor) < 0) return 64;
	if (scenario == "idle")
	{
		EmptyInstrument empty;
		empty.prepareToPlay(rate, block);
		const auto before = measure(empty, rate, block, seconds, 0);
		auto processor = flint(rate, block, vekt::flint::Mode::kick, 1);
		const auto idle = measure(*processor, rate, block, seconds, 0);
		const auto after = measure(empty, rate, block, seconds, 0);
		print("empty_before", before);
		print("flint_idle", idle);
		print("empty_after", after);
		const auto baseline = 0.5 * (before.meanUs + after.meanUs);
		const auto ratio = idle.meanUs / baseline;
		std::cout << std::fixed << std::setprecision(3) << "idle_ratio=" << ratio
		          << " a19_idle_met=" << (ratio <= 2.0 ? 1 : 0) << '\n';
		return idle.newCalls == 0 && juce::exactlyEqual(idle.sumSquares, 0.0) ? 0 : 1;
	}
	if (scenario != "kick" && scenario != "bar") return 64;
	const auto mode = scenario == "kick" ? vekt::flint::Mode::kick : vekt::flint::Mode::mallet;
	auto processor = flint(rate, block, mode, factor);
	if (processor->getActiveQuality().multiplier() != static_cast<std::size_t>(factor)) return 1;
	const auto timing = measure(*processor, rate, block, seconds, static_cast<int>(std::round(rate / 8.0)));
	std::cout << "model=" << scenario << " rate=" << rate << " block=" << block << " factor=" << factor
	          << " latency_samples=" << processor->getLatencySamples() << '\n';
	print("flint_active", timing);
	return timing.newCalls == 0 && std::isfinite(timing.sumSquares) && timing.sumSquares > 0.0 ? 0 : 1;
}
}

int main(int argc, char** argv)
{
	if (argc < 5 || argc > 6)
	{
		std::cerr << "Usage: VektFlintCost idle|kick|bar rate block_size seconds [factor(1|2|4|8|16)]\n";
		return 64;
	}
	try
	{
		juce::ScopedJuceInitialiser_GUI initialiseJuce;
		return run(
		    argv[1], std::stod(argv[2]), std::stoi(argv[3]), std::stod(argv[4]), argc == 6 ? std::stoi(argv[5]) : 1);
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 64;
	}
}
