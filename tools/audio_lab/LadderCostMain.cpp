#include "NonlinearTptLadder.h"

#include <vekt/dsp/OversamplingBank.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using namespace vekt::dsp;

// Experimental persistent workers; neither this synchronization policy nor
// these shared-input ladders are a production Mono voice-render design.
class ParallelLadders
{
public:
	ParallelLadders(std::vector<vekt::audio_lab::NonlinearTptLadder>& filters,
		std::size_t maximumSamples, int lanes)
		: ladders(filters), laneCount(lanes), input(maximumSamples),
		outputs(static_cast<std::size_t>(lanes), std::vector<float>(maximumSamples))
	{
		for (int lane = 1; lane < laneCount; ++lane)
			workers.emplace_back([this, lane] { worker(lane); });
	}

	~ParallelLadders()
	{
		stopping.store(true, std::memory_order_release);
		generation.fetch_add(1, std::memory_order_release);
		generation.notify_all();
		for (auto& workerThread : workers) workerThread.join();
	}

	ParallelLadders(const ParallelLadders&) = delete;
	ParallelLadders& operator=(const ParallelLadders&) = delete;

	void render(juce::dsp::AudioBlock<float>& internal)
	{
		activeSamples = internal.getNumSamples();
		for (std::size_t index = 0; index < activeSamples; ++index)
			input[index] = internal.getSample(0, static_cast<int>(index));
		completed.store(0, std::memory_order_relaxed);
		generation.fetch_add(1, std::memory_order_release);
		generation.notify_all();
		processLane(0);
		while (completed.load(std::memory_order_acquire) != workers.size())
		{
			const auto current = completed.load(std::memory_order_relaxed);
			if (current != workers.size()) completed.wait(current, std::memory_order_relaxed);
		}
		for (std::size_t index = 0; index < activeSamples; ++index)
		{
			float mixed {};
			for (const auto& lane : outputs) mixed += lane[index];
			internal.setSample(0, static_cast<int>(index), mixed / static_cast<float>(ladders.size()));
		}
	}

private:
	void processLane(int lane) noexcept
	{
		const auto begin = static_cast<std::size_t>(lane) * ladders.size() / static_cast<std::size_t>(laneCount);
		const auto end = static_cast<std::size_t>(lane + 1) * ladders.size() / static_cast<std::size_t>(laneCount);
		for (std::size_t index = 0; index < activeSamples; ++index)
		{
			float mixed {};
			for (auto filter = begin; filter < end; ++filter)
				mixed += ladders[filter].process(input[index], { 1'000.0f, 0.85f, 12.0f });
			outputs[static_cast<std::size_t>(lane)][index] = mixed;
		}
	}

	void worker(int lane) noexcept
	{
		std::uint64_t seen {};
		for (;;)
		{
			generation.wait(seen, std::memory_order_acquire);
			seen = generation.load(std::memory_order_acquire);
			if (stopping.load(std::memory_order_acquire)) return;
			processLane(lane);
			completed.fetch_add(1, std::memory_order_release);
			completed.notify_one();
		}
	}

	std::vector<vekt::audio_lab::NonlinearTptLadder>& ladders;
	int laneCount {};
	std::vector<float> input;
	std::vector<std::vector<float>> outputs;
	std::vector<std::thread> workers;
	std::atomic<std::uint64_t> generation {};
	std::atomic<std::size_t> completed {};
	std::atomic<bool> stopping {};
	std::size_t activeSamples {};
};

bool measure(double rate, int blockSize, int voiceCount, OversamplingQuality quality, int lanes, int steps)
{
	constexpr double seconds = 0.5;
	const auto totalSamples = static_cast<int>(rate * seconds);
	OversamplingBank<float> bank(1);
	bank.prepare(static_cast<std::size_t>(blockSize));
	bank.activate(quality);
	std::vector<vekt::audio_lab::NonlinearTptLadder> voices(static_cast<std::size_t>(voiceCount));
	for (auto& voice : voices) voice.prepare(rate * static_cast<double>(bank.getActiveFactor()));
	ParallelLadders parallel(voices, static_cast<std::size_t>(blockSize) * bank.getActiveFactor(), lanes);
	juce::AudioBuffer<float> buffer(1, blockSize);
	std::vector<double> times;
	times.reserve(static_cast<std::size_t>((totalSamples + blockSize - 1) / blockSize));
	std::vector<float> recordedOutput;
	if (lanes > 1) recordedOutput.resize(static_cast<std::size_t>(totalSamples));
	double checksum {}, weightedChecksum {}, sumSquares {};
	const auto deadlineUs = blockSize * 1'000'000.0 / rate;
	std::size_t deadlineExceeded {};
	for (int start = 0; start < totalSamples; start += blockSize)
	{
		const auto count = std::min(blockSize, totalSamples - start);
		const auto before = Clock::now();
		for (int index = 0; index < count; ++index)
			buffer.setSample(0, index, 0.25f * static_cast<float>(std::sin(
				2.0 * std::numbers::pi * 330.0 * (start + index) / rate)));
		juce::dsp::AudioBlock<float> full(buffer);
		auto host = full.getSubBlock(0, static_cast<std::size_t>(count));
		const juce::dsp::AudioBlock<const float> input(host);
		auto internal = bank.processSamplesUp(input);
		if (lanes > 1) parallel.render(internal);
		else for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
		{
			const auto value = internal.getSample(0, static_cast<int>(index));
			float mixed {};
			for (auto& voice : voices)
				mixed += steps == 1
					? voice.process(value, { 1'000.0f, 0.85f, 12.0f })
					: voice.processSubstepped(value, { 1'000.0f, 0.85f, 12.0f }, steps);
			internal.setSample(0, static_cast<int>(index), mixed / static_cast<float>(voiceCount));
		}
		bank.processSamplesDown(host);
		const auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - before).count();
		times.push_back(elapsed);
		if (elapsed > count * 1'000'000.0 / rate) ++deadlineExceeded;
		for (int index = 0; index < count; ++index)
		{
			const auto output = static_cast<double>(buffer.getSample(0, index));
			if (lanes > 1) recordedOutput[static_cast<std::size_t>(start + index)] = static_cast<float>(output);
			checksum += output;
			weightedChecksum += output * static_cast<double>((start + index) % 1021 + 1);
			sumSquares += output * output;
		}
	}
	std::sort(times.begin(), times.end());
	const auto percentile = [&times](double fraction)
	{
		return times[std::min(times.size() - 1,
			static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(times.size())) - 1.0))];
	};
	std::uint64_t unconverged {}, nonFinite {};
	for (const auto& voice : voices)
	{
		unconverged += voice.diagnostics().unconvergedSamples;
		nonFinite += voice.diagnostics().nonFiniteSamples;
	}
	double maximumDifference {};
	if (lanes > 1)
	{
		OversamplingBank<float> serialBank(1);
		serialBank.prepare(static_cast<std::size_t>(blockSize));
		serialBank.activate(quality);
		std::vector<vekt::audio_lab::NonlinearTptLadder> serialVoices(static_cast<std::size_t>(voiceCount));
		for (auto& voice : serialVoices)
			voice.prepare(rate * static_cast<double>(serialBank.getActiveFactor()));
		juce::AudioBuffer<float> serialBuffer(1, blockSize);
		for (int start = 0; start < totalSamples; start += blockSize)
		{
			const auto count = std::min(blockSize, totalSamples - start);
			for (int index = 0; index < count; ++index)
				serialBuffer.setSample(0, index, 0.25f * static_cast<float>(std::sin(
					2.0 * std::numbers::pi * 330.0 * (start + index) / rate)));
			juce::dsp::AudioBlock<float> full(serialBuffer);
			auto host = full.getSubBlock(0, static_cast<std::size_t>(count));
			const juce::dsp::AudioBlock<const float> input(host);
			auto internal = serialBank.processSamplesUp(input);
			for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
			{
				const auto value = internal.getSample(0, static_cast<int>(index));
				float mixed {};
				for (auto& voice : serialVoices)
					mixed += voice.process(value, { 1'000.0f, 0.85f, 12.0f });
				internal.setSample(0, static_cast<int>(index), mixed / static_cast<float>(voiceCount));
			}
			serialBank.processSamplesDown(host);
			for (int index = 0; index < count; ++index)
				maximumDifference = std::max(maximumDifference, std::abs(static_cast<double>(
					serialBuffer.getSample(0, index)) - recordedOutput[static_cast<std::size_t>(start + index)]));
		}
	}
	std::cout << std::fixed << std::setprecision(3)
		<< "duration_s=" << seconds << " rate=" << rate << " block=" << blockSize
		<< " ladders=" << voiceCount << " lanes=" << lanes << " steps=" << steps
		<< " factor=" << bank.getActiveFactor() << " filter=" << static_cast<int>(quality.filter)
		<< " callbacks=" << times.size() << " deadline_us=" << deadlineUs
		<< " median_us=" << percentile(0.5) << " p99_9_us=" << percentile(0.999)
		<< " max_us=" << times.back() << " simulated_deadline_exceeded=" << deadlineExceeded
		<< " latency_samples=" << bank.getActiveLatencySamples()
		<< " unconverged=" << unconverged << " non_finite=" << nonFinite
		<< " checksum=" << checksum << " weighted_checksum=" << weightedChecksum
		<< " sum_squares=" << sumSquares << " maximum_parallel_difference="
		<< std::scientific << maximumDifference << '\n';
	return unconverged == 0 && nonFinite == 0 && std::isfinite(checksum)
		&& std::isfinite(weightedChecksum) && std::isfinite(sumSquares)
		&& maximumDifference <= 2.0e-6;
}
}

int main(int argc, char** argv)
{
	if (argc != 5 && argc != 6 && argc != 7)
	{
		std::cerr << "Usage: VektLadderCost rate block_size ladder_count factor(1|2|4|8|16) [lanes(1..10)] [steps(1|2|4; substeps serial only)]\n";
		return 64;
	}
	try
	{
		const auto rate = std::stod(argv[1]);
		const auto block = std::stoi(argv[2]);
		const auto voices = std::stoi(argv[3]);
		const auto factor = std::stoi(argv[4]);
		const auto lanes = argc >= 6 ? std::stoi(argv[5]) : 1;
		const auto steps = argc == 7 ? std::stoi(argv[6]) : 1;
		if (!std::isfinite(rate) || rate < 44'100.0 || rate > 192'000.0 || block < 1 || block > 257
			|| voices < 1 || voices > 16
			|| lanes < 1 || lanes > std::min(voices, 10)
			|| (steps != 1 && steps != 2 && steps != 4) || (steps != 1 && lanes != 1)
			|| (factor != 1 && factor != 2 && factor != 4 && factor != 8 && factor != 16))
			return 64;
		const auto quality = factor == 1 ? OversamplingFactor::off
			: factor == 2 ? OversamplingFactor::x2
			: factor == 4 ? OversamplingFactor::x4
			: factor == 8 ? OversamplingFactor::x8 : OversamplingFactor::x16;
		return measure(rate, block, voices,
			{ quality, factor == 1 || factor == 2
				? OversamplingFilter::polyphaseIIR : OversamplingFilter::polyphaseFIR }, lanes, steps) ? 0 : 1;
	}
	catch (const std::exception&)
	{
		return 64;
	}
}