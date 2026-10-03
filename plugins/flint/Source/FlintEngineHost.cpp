#include "FlintEngineHost.h"

#include "SafetyClip.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vekt::flint
{
namespace
{
constexpr double switchFadeSeconds = 0.005;
constexpr double levelRampSeconds = 0.02;
constexpr double dcBlockerCutoffHz = 5.0;
// Below this the host-rate tail is inaudible and the DC blocker is zeroed (−180 dBFS).
constexpr double silenceThreshold = 1.0e-9;

[[nodiscard]] double levelToGain(double decibels) noexcept { return juce::Decibels::decibelsToGain(decibels); }
}

FlintEngineHost::FlintEngineHost(FlintEngines newEngines) : engines(std::move(newEngines)) {}

void FlintEngineHost::prepare(double internalSampleRate, int maximumInternalBlockSize, double hostSampleRate)
{
	maximumInternalBlock = maximumInternalBlockSize;
	for (auto& buffer : scratch) buffer.assign(static_cast<std::size_t>(maximumInternalBlockSize), 0.0f);
	levelGain.reset(hostSampleRate, levelRampSeconds);
	levelGain.setCurrentAndTargetValue(levelToGain(parameters.levelDecibels));
	for (auto& blocker : dcBlockers) blocker.prepare(hostSampleRate, dcBlockerCutoffHz);
	setInternalSampleRate(internalSampleRate);
}

void FlintEngineHost::setInternalSampleRate(double internalSampleRate) noexcept
{
	// Flint's engines allocate nothing in prepare, so this is safe on the audio thread.
	for (const auto& engine : engines)
		if (engine != nullptr) engine->prepare(internalSampleRate, maximumInternalBlock);
	fadeStep = 1.0 / std::max(1.0, std::round(switchFadeSeconds * internalSampleRate));
	drive.prepare(internalSampleRate);
	if (auto* engine = engineAt(current)) engine->activate(parameters);
	resetAudio();
}

void FlintEngineHost::update(const FlintParameters& newParameters, bool selectionMayChange) noexcept
{
	parameters = newParameters;
	drive.setTarget(parameters.drive, parameters.driveType);
	levelGain.setTargetValue(levelToGain(parameters.levelDecibels));
	if (selectionMayChange) select(parameters.selectedModel());
	if (auto* engine = engineAt(current)) engine->update(parameters);
	if (auto* engine = engineAt(fading)) engine->update(parameters);
}

void FlintEngineHost::trigger(Strike strike) noexcept
{
	auto* engine = engineAt(current);
	if (engine == nullptr) return;
	// Nothing advanced the smoothing while the object was silent: start it at the current values, so a change made
	// between hits (Pitch, Level, a preset) does not glide into the next one.
	if (!engine->isActive()) engine->activate(parameters);
	if (sleeping)
	{
		levelGain.setCurrentAndTargetValue(levelGain.getTargetValue());
		drive.reset();
	}
	engine->trigger(strike);
	sleeping = false;
	internalActive = true;
	tailRemaining = downsamplingTail;
}

void FlintEngineHost::release(int note) noexcept
{
	if (auto* engine = engineAt(current)) engine->release(note);
}

void FlintEngineHost::renderInternal(std::span<float> left, std::span<float> right) noexcept
{
	const auto count = left.size();
	jassert(right.size() == count && count <= scratch.front().size());
	std::fill(left.begin(), left.end(), 0.0f);
	std::fill(right.begin(), right.end(), 0.0f);

	if (const auto* fadingEngine = engineAt(fading);
	    fading.has_value() && (fadingEngine == nullptr || !fadingEngine->isActive()))
		finishFade();
	auto* currentEngine = engineAt(current);
	auto* fadingEngine = engineAt(fading);
	const auto currentSounding = currentEngine != nullptr && currentEngine->isActive();
	const auto fadingSounding = fadingEngine != nullptr && fadingEngine->isActive();

	if (currentSounding || fadingSounding)
	{
		const auto currentLeft = std::span(scratch[0]).first(count), currentRight = std::span(scratch[1]).first(count);
		const auto fadingLeft = std::span(scratch[2]).first(count), fadingRight = std::span(scratch[3]).first(count);
		if (currentSounding) currentEngine->process(currentLeft, currentRight);
		if (fadingSounding) fadingEngine->process(fadingLeft, fadingRight);
		for (std::size_t index = 0; index < count; ++index)
		{
			currentGain = std::min(1.0, currentGain + fadeStep);
			fadingGain = std::max(0.0, fadingGain - fadeStep);
			auto mixedLeft = 0.0, mixedRight = 0.0;
			if (currentSounding)
			{
				mixedLeft += currentGain * static_cast<double>(currentLeft[index]);
				mixedRight += currentGain * static_cast<double>(currentRight[index]);
			}
			if (fadingSounding)
			{
				mixedLeft += fadingGain * static_cast<double>(fadingLeft[index]);
				mixedRight += fadingGain * static_cast<double>(fadingRight[index]);
			}
			left[index] = static_cast<float>(mixedLeft);
			right[index] = static_cast<float>(mixedRight);
		}
	}
	else
		currentGain = 1.0;
	if (fading.has_value() && fadingGain <= 0.0) finishFade();

	drive.process(left, right);
	internalActive = currentSounding || fadingSounding || fading.has_value() || drive.isRinging();
}

void FlintEngineHost::finishHost(std::span<float> left, std::span<float> right) noexcept
{
	const auto count = left.size();
	jassert(right.size() == count);
	if (sleeping)
	{
		std::fill(left.begin(), left.end(), 0.0f);
		std::fill(right.begin(), right.end(), 0.0f);
		return;
	}
	const std::array<std::span<float>, 2> channels { left, right };
	auto inputSilent = true;
	auto peak = 0.0;
	for (std::size_t index = 0; index < count; ++index)
	{
		const auto gain = levelGain.getNextValue();
		for (std::size_t channel = 0; channel < channels.size(); ++channel)
		{
			auto& sample = channels[channel][index];
			inputSilent = inputSilent && juce::exactlyEqual(sample, 0.0f);
			const auto output = safetyClip(dcBlockers[channel].processSample(gain * static_cast<double>(sample)));
			peak = std::max(peak, std::abs(output));
			sample = static_cast<float>(output);
		}
	}
	tailRemaining = internalActive ? downsamplingTail : std::max(0, tailRemaining - static_cast<int>(count));
	if (!internalActive && tailRemaining == 0 && inputSilent && peak < silenceThreshold)
	{
		for (auto& blocker : dcBlockers) blocker.reset();
		drive.reset();
		sleeping = true;
	}
}

void FlintEngineHost::resetAudio() noexcept
{
	if (auto* engine = engineAt(current)) engine->reset();
	if (auto* engine = engineAt(fading)) engine->reset();
	fading.reset();
	fadingGain = 0.0;
	currentGain = 1.0;
	if (pending.has_value())
	{
		const auto target = *pending;
		pending.reset();
		select(target);
	}
	drive.reset();
	// Start at the current Level, not ramping toward it: a hit after a reset sounds as every later one does.
	levelGain.setCurrentAndTargetValue(levelGain.getTargetValue());
	for (auto& blocker : dcBlockers) blocker.reset();
	tailRemaining = 0;
	internalActive = false;
	sleeping = true;
}

FlintEngine* FlintEngineHost::engineAt(std::optional<ModelId> model) const noexcept
{
	return model.has_value() ? engines[indexOf(*model)].get() : nullptr;
}

void FlintEngineHost::select(std::optional<ModelId> target) noexcept
{
	if (target == current)
	{
		pending.reset();
		return;
	}
	if (fading.has_value() && target == fading)
	{
		// Switching back during a fade reverses it: the engine keeps ringing and its gain turns around.
		std::swap(current, fading);
		std::swap(currentGain, fadingGain);
		pending.reset();
		return;
	}
	if (fading.has_value())
	{
		// A third engine waits for the running fade, so no more than two ever sound.
		pending = target;
		return;
	}
	if (auto* previous = engineAt(current); previous != nullptr)
	{
		if (previous->isActive())
		{
			fading = current;
			fadingGain = currentGain;
		}
		else
			previous->reset();
	}
	current = target;
	currentGain = 1.0; // a newly selected engine starts silent, so it needs no fade in
	if (auto* engine = engineAt(current))
	{
		engine->reset();
		engine->activate(parameters);
	}
}

void FlintEngineHost::finishFade() noexcept
{
	if (auto* engine = engineAt(fading)) engine->reset();
	fading.reset();
	fadingGain = 0.0;
	if (pending.has_value())
	{
		const auto target = *pending;
		pending.reset();
		select(target);
	}
}
}
