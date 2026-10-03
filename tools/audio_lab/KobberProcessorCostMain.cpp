#include <vekt/kobber/PluginProcessor.h>

#include "CallbackAllocationProbe.h"
#include "KobberCostTimingRule.h"
#include "KobberParameterChoices.h"
#include "WidthOscillator.h"

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

void setParameter(vekt::kobber::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	if (parameter == nullptr) std::abort();
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

int run(double rate, int blockSize, int voices, int factor, double seconds, bool work, bool transitions, bool cpu, int unison, bool multicore,
	bool svf, bool korg35, float mode)
{
	if (!vekt::audio_lab::callback_allocation_probe::verify()) return 1;
	vekt::kobber::PluginProcessor processor;
	// Tracking Oversampling choices: Off, 2x IIR, 4x IIR, 2x FIR, 4x FIR, 8x FIR, 16x FIR (2x IIR and FIR above, as before).
	const auto qualityIndex = factor == 1 ? 0 : factor == 2 ? 1 : factor == 4 ? 4 : factor == 8 ? 5 : 6;
	setParameter(processor, vekt::kobber::parameters::trackingOversampling, static_cast<float>(qualityIndex));
	setParameter(processor, vekt::kobber::parameters::unison, static_cast<float>(vekt::kobber::unisonCounts.indexOf(unison)));
	setParameter(processor, vekt::kobber::parameters::voiceCount, static_cast<float>(vekt::kobber::voiceCounts.indexOf(voices)));
	setParameter(processor, vekt::kobber::parameters::performanceMode, 0.0f);
	setParameter(processor, vekt::kobber::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::kobber::parameters::filterResonance, 85.0f);
	setParameter(processor, vekt::kobber::parameters::filterDrive, 12.0f);
	setParameter(processor, vekt::kobber::parameters::multicore, multicore ? 1.0f : 0.0f);
	const auto filterType = korg35 ? vekt::kobber::FilterType::korg35 : svf ? vekt::kobber::FilterType::svf : vekt::kobber::FilterType::ladder;
	setParameter(processor, vekt::kobber::parameters::filterType, static_cast<float>(vekt::kobber::filterTypes.indexOf(filterType)));
	setParameter(processor, vekt::kobber::parameters::filterMode, mode);
	processor.prepareToPlay(rate, blockSize);
	if (processor.getActiveQuality().multiplier() != static_cast<std::size_t>(factor)) return 1;
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
	vekt::kobber::PluginProcessor::CoupledWorkSnapshot slowestWork {};
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
			const auto callbackWork = vekt::kobber::PluginProcessor::CoupledWorkSnapshot {
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
		<< "engine=" << (korg35 ? "k35" : svf ? "svf" : "coupled")
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
	if (svf)
	{
		const auto svfWork = processor.svfWorkSnapshot();
		std::cout << " svf_samples=" << svfWork.samples
			<< " svf_iterations=" << svfWork.iterations
			<< " svf_fallback_steps=" << svfWork.fallbackSteps
			<< " svf_unconverged=" << svfWork.unconverged
			<< " svf_non_finite=" << svfWork.nonFinite
			<< " svf_maximum_iterations=" << svfWork.maximumIterations
			<< " svf_maximum_residual=" << std::scientific << svfWork.maximumResidual << std::fixed;
	}
	std::cout << '\n';
	processor.releaseResources();
	return std::isfinite(sumSquares) && sumSquares > 0.0 && callbackNewCalls == 0 ? 0 : 1;
}
// auval -stress style: every iteration sets each parameter to a random value, then renders one block of a random size
// up to 512, with notes held. Parameters named in skip keep their defaults, so a cost can be traced to its parameter.
// once: randomise only before the first block and render fixed 512-sample blocks, as auval's 20-second stress render
// does, so held notes keep sounding.
int runStress(int iterations, unsigned seed, const std::vector<std::string>& skip, int notes, bool once)
{
	// The Width wavetable is built once per process, on first use.
	const auto tableStart = Clock::now();
	juce::ignoreUnused(vekt::kobber::WidthWavetable::instance());
	const auto tableUs = std::chrono::duration<double, std::micro>(Clock::now() - tableStart).count();
	const auto constructStart = Clock::now();
	vekt::kobber::PluginProcessor processor;
	const auto constructUs = std::chrono::duration<double, std::micro>(Clock::now() - constructStart).count();
	const auto prepareStart = Clock::now();
	processor.prepareToPlay(44'100.0, 512);
	const auto prepareUs = std::chrono::duration<double, std::micro>(Clock::now() - prepareStart).count();
	juce::Random random(static_cast<juce::int64>(seed));
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	std::vector<juce::AudioProcessorParameter*> randomised;
	for (auto* parameter : static_cast<juce::AudioProcessor&>(processor).getParameters())
	{
		const auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
		if (withId == nullptr || std::find(skip.begin(), skip.end(), withId->paramID.toStdString()) == skip.end())
			randomised.push_back(parameter);
	}
	const auto holdNotes = [&midi, notes]
	{
		for (int note = 0; note < notes; ++note) midi.addEvent(juce::MidiMessage::noteOn(1, 48 + 3 * note, 0.8f), 0);
	};
	if (once)
	{
		for (auto* parameter : randomised) parameter->setValueNotifyingHost(random.nextFloat());
		// The first block applies voice count and quality, which cuts sounding voices; the notes follow it.
		juce::AudioBuffer<float> first(buffer.getArrayOfWritePointers(), 2, 512);
		processor.processBlock(first, midi);
	}
	holdNotes();
	double setUs {}, renderUs {}, maximumRenderUs {};
	std::int64_t samples {};
	const auto start = Clock::now();
	for (int iteration = 0; iteration < iterations; ++iteration)
	{
		const auto setStart = Clock::now();
		if (!once)
			for (auto* parameter : randomised) parameter->setValueNotifyingHost(random.nextFloat());
		const auto renderStart = Clock::now();
		setUs += std::chrono::duration<double, std::micro>(renderStart - setStart).count();
		const auto blockSize = once ? 512 : 1 + random.nextInt(512);
		juce::AudioBuffer<float> block(buffer.getArrayOfWritePointers(), 2, blockSize);
		processor.processBlock(block, midi);
		midi.clear();
		samples += blockSize;
		const auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - renderStart).count();
		renderUs += elapsed;
		maximumRenderUs = std::max(maximumRenderUs, elapsed);
	}
	const auto totalUs = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
	const auto soundingVoices = processor.getSoundingVoiceCount();
	const auto activeFactor = processor.getActiveQuality().multiplier();
	const auto helpers = processor.getRenderHelperCount();
	processor.releaseResources();
	// A host re-initialises between tests; time a second preparation of the same instance.
	const auto reprepareStart = Clock::now();
	processor.prepareToPlay(44'100.0, 512);
	const auto reprepareUs = std::chrono::duration<double, std::micro>(Clock::now() - reprepareStart).count();
	processor.releaseResources();
	// auval opens a fresh instance for each test; time a second instance's first preparation in the same process.
	const auto secondStart = Clock::now();
	{
		vekt::kobber::PluginProcessor second;
		second.prepareToPlay(44'100.0, 512);
		second.releaseResources();
	}
	const auto secondInstanceUs = std::chrono::duration<double, std::micro>(Clock::now() - secondStart).count();
	// An instance with Multicore on starts its helper threads in prepareToPlay and stops them when destroyed.
	std::cout << "multicore_lifecycle_us";
	for (int pass = 0; pass < 5; ++pass)
	{
		const auto lifecycleStart = Clock::now();
		std::chrono::duration<double, std::micro> destroyUs {};
		{
			auto multicoreInstance = std::make_unique<vekt::kobber::PluginProcessor>();
			setParameter(*multicoreInstance, vekt::kobber::parameters::multicore, 1.0f);
			multicoreInstance->prepareToPlay(44'100.0, 512);
			juce::AudioBuffer<float> multicoreBlock(2, 512);
			juce::MidiBuffer multicoreMidi;
			multicoreMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
			for (int block = 0; block < 20; ++block)
			{
				multicoreInstance->processBlock(multicoreBlock, multicoreMidi);
				multicoreMidi.clear();
			}
			if (pass == 0) std::cout << " helpers=" << multicoreInstance->getRenderHelperCount();
			multicoreInstance->releaseResources();
			const auto destroyStart = Clock::now();
			multicoreInstance.reset();
			destroyUs = Clock::now() - destroyStart;
		}
		std::cout << ' ' << std::chrono::duration<double, std::micro>(Clock::now() - lifecycleStart).count() << '('
				  << destroyUs.count() << ')';
	}
	std::cout << '\n';
	// auval also re-initialises at other rates and block sizes.
	std::cout << "rate_prepare_us";
	for (const auto [rate, block] : { std::pair { 48'000.0, 512 }, std::pair { 96'000.0, 512 }, std::pair { 22'050.0, 1'024 },
			 std::pair { 44'100.0, 4'096 }, std::pair { 48'000.0, 512 } })
	{
		const auto rateStart = Clock::now();
		processor.prepareToPlay(rate, block);
		processor.releaseResources();
		std::cout << ' ' << rate << '/' << block << '='
				  << std::chrono::duration<double, std::micro>(Clock::now() - rateStart).count();
	}
	std::cout << '\n';
	std::cout << std::fixed << std::setprecision(1) << "stress iterations=" << iterations << " seed=" << seed
			  << " randomised_parameters=" << randomised.size() << " width_table_us=" << tableUs << " construct_us=" << constructUs
			  << " prepare_us=" << prepareUs << " reprepare_us=" << reprepareUs << " second_instance_us=" << secondInstanceUs << " total_ms=" << totalUs * 0.001 << " set_ms=" << setUs * 0.001
			  << " render_ms=" << renderUs * 0.001 << " maximum_render_us=" << maximumRenderUs
			  << " audio_ms=" << static_cast<double>(samples) * 1'000.0 / 44'100.0 << " notes=" << notes
			  << " sounding_voices=" << soundingVoices << " factor=" << activeFactor << " helpers=" << helpers << '\n';
	return 0;
}
}

int main(int argc, char** argv)
{
	if (argc >= 2 && std::string_view(argv[1]) == "stress")
	{
		// stress iterations seed [skip=id,id,...] [notes=N] [once]
		if (argc < 4) return 64;
		std::vector<std::string> skip;
		int notes = 1;
		bool once {};
		for (int index = 4; index < argc; ++index)
		{
			const std::string_view option(argv[index]);
			if (option.starts_with("skip="))
			{
				juce::StringArray identifiers;
				identifiers.addTokens(juce::String(std::string(option.substr(5))), ",", "");
				for (const auto& identifier : identifiers) skip.push_back(identifier.toStdString());
			}
			else if (option.starts_with("notes=")) notes = std::stoi(std::string(option.substr(6)));
			else if (option == "once") once = true;
			else return 64;
		}
		if (notes < 0 || notes > 16) return 64;
		juce::ScopedJuceInitialiser_GUI juceInitialiser;
		return runStress(std::stoi(argv[2]), static_cast<unsigned>(std::stoul(argv[3])), skip, notes, once);
	}
	if (argc < 6 || argc > 11)
	{
		std::cerr << "Usage: VektKobberProcessorCost stress iterations seed [skip=id,...] [notes=N] [once]\n       VektKobberProcessorCost rate block_size voices(8|12|16) factor(1|2|4|8|16) seconds [work|transitions|cpu] [unison=1|2|4] [multicore] [svf|k35] [mode=-1..1]\n";
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
		bool multicore {}, svf {}, korg35 {};
		float mode = -1.0f; // Filter Mode, -1 LP .. +1 HP
		for (int index = 6; index < argc; ++index)
		{
			const std::string_view option(argv[index]);
			if (option == "work") work = true;
			else if (option == "transitions") transitions = true;
			else if (option == "cpu") cpu = true;
			else if (option.starts_with("unison=")) unison = std::stoi(std::string(option.substr(7)));
			else if (option == "multicore") multicore = true;
			else if (option == "svf") svf = true;
			else if (option == "k35") korg35 = true;
			else if (option.starts_with("mode=")) mode = std::stof(std::string(option.substr(5)));
			else return 64;
		}
		if (!std::isfinite(rate) || rate < 44'100.0 || rate > 192'000.0
			|| block < 1 || block > 257 || (voices != 8 && voices != 12 && voices != 16)
			|| (factor != 1 && factor != 2 && factor != 4 && factor != 8 && factor != 16)
			|| !std::isfinite(seconds) || seconds <= 0.0 || seconds > 30.0
			|| static_cast<int>(work) + static_cast<int>(transitions) + static_cast<int>(cpu) > 1
			|| (unison != 1 && unison != 2 && unison != 4) || !std::isfinite(mode) || mode < -1.0f || mode > 1.0f) return 64;
		juce::ScopedJuceInitialiser_GUI juceInitialiser;
		if (svf && korg35) return 64;
		return run(rate, block, voices, factor, seconds, work, transitions, cpu, unison, multicore, svf, korg35, mode);
	}
	catch (const std::exception&) { return 64; }
}