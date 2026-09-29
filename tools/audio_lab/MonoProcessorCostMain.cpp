#include <vekt/mono/PluginProcessor.h>

#include "CallbackAllocationProbe.h"
#include "MonoCostTimingRule.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <time.h>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

[[nodiscard]] double threadCpuMicroseconds() noexcept
{
	timespec time {};
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) != 0) std::abort();
	return static_cast<double>(time.tv_sec) * 1'000'000.0
		+ static_cast<double>(time.tv_nsec) * 0.001;
}

void setParameter(vekt::mono::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	if (parameter == nullptr) std::abort();
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

int run(double rate, int blockSize, int voices, int factor, double seconds, bool work, bool transitions, bool cpu, int unison, bool multicore)
{
	if (!vekt::audio_lab::callback_allocation_probe::verify()) return 1;
	vekt::mono::PluginProcessor processor;
	const auto qualityIndex = factor == 1 ? 0 : factor == 2 ? 1 : factor == 4 ? 2 : 3;
	setParameter(processor, vekt::mono::parameters::quality, static_cast<float>(qualityIndex));
	setParameter(processor, vekt::mono::parameters::unison, static_cast<float>(unison == 1 ? 0 : unison == 2 ? 1 : 2));
	// Voice Count choices are 2, 4, 8, 12, 16.
	setParameter(processor, vekt::mono::parameters::voiceCount, static_cast<float>(voices == 8 ? 2 : voices == 12 ? 3 : 4));
	setParameter(processor, vekt::mono::parameters::performanceMode, 0.0f);
	setParameter(processor, vekt::mono::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 85.0f);
	setParameter(processor, vekt::mono::parameters::filterDrive, 12.0f);
	setParameter(processor, vekt::mono::parameters::multicore, multicore ? 1.0f : 0.0f);
	processor.prepareToPlay(rate, blockSize);
	if (processor.getActiveQuality() != qualityIndex) return 1;
	juce::AudioBuffer<float> buffer(2, blockSize);
	juce::MidiBuffer midi;
	std::array<int, 16> activeNotes {};
	for (int note = 0; note < voices; ++note)
	{
		activeNotes[static_cast<std::size_t>(note)] = 48 + note;
		midi.addEvent(juce::MidiMessage::noteOn(1, 48 + note, 0.8f), 0);
	}
	processor.processBlock(buffer, midi);
	midi.clear();
	// Warm for at least 100 ms at every block size, including single-sample
	// callbacks; eight blocks alone do not warm the envelope at block size 1.
	const auto warmupCallbacks = static_cast<int>(std::ceil(0.1 * rate / blockSize));
	for (int i = 0; i < warmupCallbacks; ++i) processor.processBlock(buffer, midi);
	const auto callbacks = std::max(1, static_cast<int>(std::ceil(rate * seconds / blockSize)));
	std::vector<double> times;
	times.reserve(static_cast<std::size_t>(callbacks));
	double sumSquares {};
	std::size_t simulatedDeadlineExceeded {};
	std::size_t transitionDeadlineExceeded {}, otherDeadlineExceeded {};
	double maximumTransitionUs {}, maximumOtherUs {};
	std::uint64_t callbackNewCalls {};
	int callbacksWithNew {};
	int transitionCallbacks {};
	std::size_t wallOverrunsWithCpuBelowDeadline {};
	double maximumOverrunCpuUs {}, maximumOverrunOffCpuUs {};
	auto previousWork = processor.coupledWorkSnapshot();
	vekt::mono::PluginProcessor::CoupledWorkSnapshot slowestWork {};
	std::uint64_t maximumCallbackIterations {}, maximumCallbackTrials {};
	std::uint64_t overDeadlineIterations {}, overDeadlineTrials {};
	double slowestCallbackUs {};
	int slowestCallbackIndex {};
	for (int callback = 0; callback < callbacks; ++callback)
	{
		const bool hasTransition = transitions && callback % 8 == 0;
		if (hasTransition)
		{
			const auto lane = static_cast<std::size_t>((callback / 8) % voices);
			const auto nextNote = activeNotes[lane] < 72 ? 72 + static_cast<int>(lane) : 48 + static_cast<int>(lane);
			midi.addEvent(juce::MidiMessage::noteOff(1, activeNotes[lane]), blockSize / 3);
			midi.addEvent(juce::MidiMessage::noteOn(1, nextNote, 0.8f), 2 * blockSize / 3);
			activeNotes[lane] = nextNote;
			++transitionCallbacks;
		}
		const auto before = Clock::now();
		const auto cpuBefore = cpu ? threadCpuMicroseconds() : 0.0;
		vekt::audio_lab::callback_allocation_probe::begin();
		processor.processBlock(buffer, midi);
		const auto newCalls = vekt::audio_lab::callback_allocation_probe::end();
		const auto cpuElapsed = cpu ? threadCpuMicroseconds() - cpuBefore : 0.0;
		const auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - before).count();
		midi.clear();
		callbackNewCalls += newCalls;
		if (newCalls > 0) ++callbacksWithNew;
		if (work)
		{
			// Snapshot after the timer; the traversal itself is not charged to
			// processBlock, but may still affect scheduling of the next callback.
			const auto current = processor.coupledWorkSnapshot();
			const auto callbackWork = vekt::mono::PluginProcessor::CoupledWorkSnapshot {
				current.samples - previousWork.samples,
				current.iterations - previousWork.iterations,
				current.lineSearchTrials - previousWork.lineSearchTrials,
				current.unconverged - previousWork.unconverged,
				current.nonFinite - previousWork.nonFinite };
			maximumCallbackIterations = std::max(maximumCallbackIterations, callbackWork.iterations);
			maximumCallbackTrials = std::max(maximumCallbackTrials, callbackWork.lineSearchTrials);
			if (elapsed > blockSize * 1'000'000.0 / rate)
			{
				overDeadlineIterations += callbackWork.iterations;
				overDeadlineTrials += callbackWork.lineSearchTrials;
			}
			if (elapsed > slowestCallbackUs)
			{
				slowestCallbackUs = elapsed;
				slowestCallbackIndex = callback;
				slowestWork = callbackWork;
			}
			previousWork = current;
		}
		times.push_back(elapsed);
		if (hasTransition) maximumTransitionUs = std::max(maximumTransitionUs, elapsed);
		else maximumOtherUs = std::max(maximumOtherUs, elapsed);
		if (elapsed > blockSize * 1'000'000.0 / rate)
		{
			++simulatedDeadlineExceeded;
			if (cpu)
			{
				if (vekt::audio_lab::wallOverrunWithCpuBelowDeadline(elapsed, cpuElapsed,
					blockSize * 1'000'000.0 / rate)) ++wallOverrunsWithCpuBelowDeadline;
				maximumOverrunCpuUs = std::max(maximumOverrunCpuUs, cpuElapsed);
				maximumOverrunOffCpuUs = std::max(maximumOverrunOffCpuUs,
					std::max(0.0, elapsed - cpuElapsed));
			}
			if (hasTransition) ++transitionDeadlineExceeded;
			else ++otherDeadlineExceeded;
		}
		for (int channel = 0; channel < 2; ++channel)
			for (int index = 0; index < blockSize; ++index)
			{
				const auto value = static_cast<double>(buffer.getSample(channel, index));
				sumSquares += value * value;
			}
	}
	if (!work) previousWork = processor.coupledWorkSnapshot();
	std::sort(times.begin(), times.end());
	const auto percentile = [&times](double fraction)
	{
		return times[std::min(times.size() - 1,
			static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(times.size())) - 1.0))];
	};
	const auto deadlineUs = blockSize * 1'000'000.0 / rate;
	const auto p99_9Us = percentile(0.999);
	std::cout << std::fixed << std::setprecision(3)
		<< "engine=coupled"
		<< " workload=" << (transitions ? "transitions" : "sustained")
		<< " transition_callbacks=" << transitionCallbacks
		<< " rate=" << rate << " block=" << blockSize << " voices=" << voices << " unison=" << unison
		<< " factor=" << factor << " requested_duration_s=" << seconds
		<< " measured_duration_s=" << static_cast<double>(callbacks) * blockSize / rate
		<< " callbacks=" << times.size()
		<< " deadline_us=" << deadlineUs
		<< " median_us=" << percentile(0.5) << " p99_9_us=" << p99_9Us
		<< " max_us=" << times.back() << " simulated_deadline_exceeded=" << simulatedDeadlineExceeded
		<< " transition_deadline_exceeded=" << transitionDeadlineExceeded
		<< " other_deadline_exceeded=" << otherDeadlineExceeded
		<< " maximum_transition_us=" << maximumTransitionUs
		<< " maximum_other_us=" << maximumOtherUs
		<< " measured_timing_rules_met=" << (vekt::audio_lab::meetsMonoMeasuredTimingRule(
			p99_9Us, deadlineUs, simulatedDeadlineExceeded) ? 1 : 0)
		<< " latency_samples=" << processor.getLatencySamples() << " sum_squares=" << sumSquares
		<< " callback_new_calls=" << callbackNewCalls
		<< " callbacks_with_new=" << callbacksWithNew
		<< " new_probe_verified=1";
	if (cpu)
		std::cout << " thread_cpu_diagnostic=1"
			<< " wall_overruns_cpu_below_deadline=" << wallOverrunsWithCpuBelowDeadline
			<< " maximum_overrun_thread_cpu_us=" << maximumOverrunCpuUs
			<< " maximum_overrun_off_cpu_us=" << maximumOverrunOffCpuUs;
	std::cout << " work_snapshots=" << (work ? "per-callback" : "final-only")
			<< " coupled_samples=" << previousWork.samples
			<< " coupled_iterations=" << previousWork.iterations
			<< " coupled_line_search_trials=" << previousWork.lineSearchTrials
			<< " coupled_unconverged=" << previousWork.unconverged
			<< " coupled_non_finite=" << previousWork.nonFinite;
	if (work)
		std::cout << " maximum_callback_iterations=" << maximumCallbackIterations
			<< " maximum_callback_line_search_trials=" << maximumCallbackTrials
			<< " over_deadline_iterations=" << overDeadlineIterations
			<< " over_deadline_line_search_trials=" << overDeadlineTrials
			<< " slowest_callback_index=" << slowestCallbackIndex
			<< " slowest_callback_samples=" << slowestWork.samples
			<< " slowest_callback_iterations=" << slowestWork.iterations
			<< " slowest_callback_line_search_trials=" << slowestWork.lineSearchTrials
			<< " slowest_callback_unconverged=" << slowestWork.unconverged;
	std::cout << '\n';
	processor.releaseResources();
	return std::isfinite(sumSquares) && sumSquares > 0.0 && callbackNewCalls == 0 ? 0 : 1;
}
}

int main(int argc, char** argv)
{
	if (argc < 6 || argc > 9)
	{
		std::cerr << "Usage: VektMonoProcessorCost rate block_size voices(8|12|16) factor(1|2|4|8) seconds [work|transitions|cpu] [unison=1|2|4] [multicore]\n";
		return 64;
	}
	try
	{
		const auto rate = std::stod(argv[1]);
		const auto block = std::stoi(argv[2]);
		const auto voices = std::stoi(argv[3]);
		const auto factor = std::stoi(argv[4]);
		const auto seconds = std::stod(argv[5]);
		bool work {}, transitions {}, cpu {};
		int unison = 1;
		bool multicore {};
		for (int index = 6; index < argc; ++index)
		{
			const std::string_view option(argv[index]);
			if (option == "work") work = true;
			else if (option == "transitions") transitions = true;
			else if (option == "cpu") cpu = true;
			else if (option.starts_with("unison=")) unison = std::stoi(std::string(option.substr(7)));
			else if (option == "multicore") multicore = true;
			else return 64;
		}
		if (!std::isfinite(rate) || rate < 44'100.0 || rate > 192'000.0
			|| block < 1 || block > 257 || (voices != 8 && voices != 12 && voices != 16)
			|| (factor != 1 && factor != 2 && factor != 4 && factor != 8)
			|| !std::isfinite(seconds) || seconds <= 0.0 || seconds > 30.0
			|| static_cast<int>(work) + static_cast<int>(transitions) + static_cast<int>(cpu) > 1
			|| (unison != 1 && unison != 2 && unison != 4)) return 64;
		juce::ScopedJuceInitialiser_GUI juceInitialiser;
		return run(rate, block, voices, factor, seconds, work, transitions, cpu, unison, multicore);
	}
	catch (const std::exception&) { return 64; }
}