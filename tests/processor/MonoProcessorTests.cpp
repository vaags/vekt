#include <vekt/mono/PluginProcessor.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace
{
void setParameter(vekt::mono::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	REQUIRE(parameter != nullptr);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float rms(const juce::AudioBuffer<float>& buffer)
{
	double sum {};
	for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
	{
		const auto value = buffer.getSample(0, sample);
		sum += static_cast<double>(value) * value;
	}
	return static_cast<float>(std::sqrt(sum / static_cast<double>(buffer.getNumSamples())));
}

float stereoRms(const juce::AudioBuffer<float>& buffer)
{
	double sum {};
	for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		{
			const auto value = buffer.getSample(channel, sample);
			sum += static_cast<double>(value) * value;
		}
	return static_cast<float>(std::sqrt(sum / static_cast<double>(buffer.getNumChannels() * buffer.getNumSamples())));
}

float differenceRms(const juce::AudioBuffer<float>& buffer)
{
	double sum {};
	for (int sample = 1; sample < buffer.getNumSamples(); ++sample)
	{
		const auto difference = buffer.getSample(0, sample) - buffer.getSample(0, sample - 1);
		sum += static_cast<double>(difference) * difference;
	}
	return static_cast<float>(std::sqrt(sum / static_cast<double>(buffer.getNumSamples() - 1)));
}

float sinusoidMagnitude(const juce::AudioBuffer<float>& buffer, float frequency, float sampleRate)
{
	double real {}, imaginary {};
	for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * static_cast<double>(frequency) * static_cast<double>(sample) / sampleRate;
		const auto value = static_cast<double>(buffer.getSample(0, sample));
		real += value * std::cos(phase);
		imaginary -= value * std::sin(phase);
	}
	return static_cast<float>(2.0 * std::sqrt(real * real + imaginary * imaginary) / static_cast<double>(buffer.getNumSamples()));
}

std::pair<float, float> dominantFrequency(const juce::AudioBuffer<float>& buffer, float startFrequency,
	float endFrequency, float sampleRate)
{
	float strongestFrequency = startFrequency;
	float strongestMagnitude {};
	for (auto frequency = startFrequency; frequency <= endFrequency; frequency += 5.0f)
		if (const auto magnitude = sinusoidMagnitude(buffer, frequency, sampleRate); magnitude > strongestMagnitude)
		{
			strongestFrequency = frequency;
			strongestMagnitude = magnitude;
		}
	return { strongestFrequency, strongestMagnitude };
}

void renderBlock(vekt::mono::PluginProcessor& processor, juce::AudioBuffer<float>& buffer, juce::MidiBuffer midi = {})
{
	buffer.clear();
	processor.processBlock(buffer, midi);
}
}

TEST_CASE("Mono renders finite stereo MIDI output", "[mono][processor]")
{
	vekt::mono::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
	midi.addEvent(juce::MidiMessage::noteOff(1, 60), 384);
	processor.processBlock(buffer, midi);
	float energy {};
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		{
			REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
			energy += std::abs(buffer.getSample(channel, sample));
		}
	REQUIRE(energy > 0.01f);
}

TEST_CASE("Mono publishes post-output-gain stereo peaks", "[mono][processor][meter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::masterOutput, -6.0f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	processor.processBlock(buffer, midi);
	const auto peaks = processor.consumeOutputPeaks();
	REQUIRE(std::max(peaks[0], peaks[1]) > 0.0f);
	REQUIRE(peaks[0] == Catch::Approx(buffer.getMagnitude(0, 0, buffer.getNumSamples())).margin(1.0e-6f));
	REQUIRE(peaks[1] == Catch::Approx(buffer.getMagnitude(1, 0, buffer.getNumSamples())).margin(1.0e-6f));
	const auto consumed = processor.consumeOutputPeaks();
	REQUIRE(consumed[0] == 0.0f);
	REQUIRE(consumed[1] == 0.0f);
}

TEST_CASE("Mono Ladder cutoff responds smoothly while a note is held", "[mono][processor][filter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::osc1Morph, 2.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 120.0f);
	processor.prepareToPlay(48'000.0, 4096);
	juce::AudioBuffer<float> buffer(2, 4096);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	renderBlock(processor, buffer);
	const auto closedBrightness = differenceRms(buffer);

	setParameter(processor, vekt::mono::parameters::filterCutoff, 12'000.0f);
	renderBlock(processor, buffer);
	for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		REQUIRE(std::isfinite(buffer.getSample(0, sample)));
	const auto openBrightness = differenceRms(buffer);
	REQUIRE(openBrightness > closedBrightness * 3.0f);
}

TEST_CASE("Mono Ladder emphasis builds a resonant peak and remains stable", "[mono][processor][filter]")
{
	auto render = [](float emphasis)
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::noiseType, 1.0f);
		setParameter(processor, vekt::mono::parameters::noiseLevel, 50.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterResonance, emphasis);
		processor.prepareToPlay(48'000.0, 4096);
		juce::AudioBuffer<float> buffer(2, 4096);
		juce::MidiBuffer noteOn;
		noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
		renderBlock(processor, buffer, noteOn);
		renderBlock(processor, buffer);
		return std::pair { sinusoidMagnitude(buffer, 1'000.0f, 48'000.0f), rms(buffer) };
	};
	const auto [flatPeak, flatRms] = render(0.0f);
	const auto [emphasizedPeak, emphasizedRms] = render(100.0f);
	REQUIRE(std::isfinite(emphasizedRms));
	REQUIRE(emphasizedRms < 2.0f);
	REQUIRE(emphasizedRms >= flatRms);
	REQUIRE(emphasizedPeak > flatPeak * 1.5f);
}

TEST_CASE("Mono Ladder Q compensation preserves the source fundamental across emphasis", "[mono][processor][filter]")
{
	auto levelFor = [](float oscillatorLevel, float cutoff, float emphasis)
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::osc1Level, oscillatorLevel);
		setParameter(processor, vekt::mono::parameters::osc1Morph, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, cutoff);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterDrive, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterResonance, emphasis);
		processor.prepareToPlay(48'000.0, 4096);
		juce::AudioBuffer<float> buffer(2, 4096);
		juce::MidiBuffer noteOn;
		noteOn.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
		renderBlock(processor, buffer, noteOn);
		for (int block = 0; block < 12; ++block) renderBlock(processor, buffer);
		return sinusoidMagnitude(buffer, 65.4064f, 48'000.0f);
	};
	for (const auto oscillatorLevel : { 20.0f, 50.0f, 100.0f })
		for (const auto cutoff : { 500.0f, 1'000.0f, 4'000.0f })
		{
			const auto reference = levelFor(oscillatorLevel, cutoff, 0.0f);
			REQUIRE(reference > 0.01f);
			for (const auto emphasis : { 25.0f, 50.0f, 70.0f, 85.0f, 100.0f })
			{
				const auto level = levelFor(oscillatorLevel, cutoff, emphasis);
				INFO("oscillator=" << oscillatorLevel << "%, cutoff=" << cutoff << " Hz, emphasis=" << emphasis
					<< "%, fundamental=" << level << ", ratio=" << level / reference);
				CHECK(level > reference * 0.85f);
				CHECK(level < reference * 1.15f);
			}
		}
}

TEST_CASE("Mono Ladder self-oscillates at maximum emphasis", "[mono][processor][filter]")
{
	for (const auto quality : { 0.0f, 1.0f })
		for (const auto sampleRate : { 44'100.0f, 48'000.0f, 96'000.0f })
			for (const auto cutoff : { 250.0f, 1'000.0f, 4'000.0f })
		{
			vekt::mono::PluginProcessor processor;
			setParameter(processor, vekt::mono::parameters::quality, quality);
			setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
			setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
			setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
			setParameter(processor, vekt::mono::parameters::noiseType, 0.0f);
			setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
			setParameter(processor, vekt::mono::parameters::filterCutoff, cutoff);
			setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
			setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
			setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
			setParameter(processor, vekt::mono::parameters::filterDrive, 0.0f);
			setParameter(processor, vekt::mono::parameters::filterResonance, 100.0f);
			setParameter(processor, vekt::mono::parameters::ampSustain, 100.0f);
			setParameter(processor, vekt::mono::parameters::ampVelocity, 0.0f);
			setParameter(processor, vekt::mono::parameters::unison, 0.0f);
			setParameter(processor, vekt::mono::parameters::voiceWidth, 0.0f);
			setParameter(processor, vekt::mono::parameters::masterOutput, 0.0f);
			processor.prepareToPlay(sampleRate, 4096);
			juce::AudioBuffer<float> buffer(2, 4096);
			juce::MidiBuffer noteOn;
			noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
			renderBlock(processor, buffer, noteOn);
			for (int block = 0; block < 24; ++block) renderBlock(processor, buffer);
			const auto settledRms = rms(buffer);
			const auto [frequency, magnitude] = dominantFrequency(buffer, cutoff * 0.9f, cutoff * 1.1f, sampleRate);
			const auto secondHarmonic = sinusoidMagnitude(buffer, frequency * 2.0f, sampleRate);
			const auto thirdHarmonic = sinusoidMagnitude(buffer, frequency * 3.0f, sampleRate);
			INFO("quality=" << quality << ", sample rate=" << sampleRate << ", cutoff=" << cutoff << ", fundamental=" << frequency
				<< " Hz / " << magnitude << ", second=" << secondHarmonic << ", third=" << thirdHarmonic
				<< ", rms=" << settledRms);
			REQUIRE(settledRms > 0.1f);
			REQUIRE(settledRms < 1.0f);
			REQUIRE(frequency == Catch::Approx(cutoff).margin(cutoff * 0.03f));
			REQUIRE(magnitude > settledRms);
			REQUIRE(secondHarmonic < magnitude * 0.1f);
			REQUIRE(thirdHarmonic < magnitude * 0.2f);
			renderBlock(processor, buffer);
			REQUIRE(rms(buffer) >= settledRms * 0.9f);
		}
}

TEST_CASE("Mono Ladder self-oscillation is audible through a preset-style voice path", "[mono][processor][filter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseType, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterDrive, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 100.0f);
	setParameter(processor, vekt::mono::parameters::ampSustain, 64.0f);
	setParameter(processor, vekt::mono::parameters::ampVelocity, 55.0f);
	setParameter(processor, vekt::mono::parameters::unison, 1.0f);
	setParameter(processor, vekt::mono::parameters::unisonSpread, 48.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, 18.0f);
	setParameter(processor, vekt::mono::parameters::masterOutput, -7.0f);
	processor.prepareToPlay(48'000.0, 4096);
	juce::AudioBuffer<float> buffer(2, 4096);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	for (int block = 0; block < 12; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoRms(buffer) > 0.025f);
}

TEST_CASE("Mono Ladder enters self-oscillation when emphasis reaches maximum in real time", "[mono][processor][filter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseType, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterDrive, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
	setParameter(processor, vekt::mono::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::mono::parameters::ampVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::unison, 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, 0.0f);
	setParameter(processor, vekt::mono::parameters::masterOutput, 0.0f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	for (int block = 0; block < 8; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoRms(buffer) < 1.0e-6f);

	setParameter(processor, vekt::mono::parameters::filterResonance, 100.0f);
	for (int block = 0; block < 120; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoRms(buffer) > 0.1f);
	const auto [frequency, magnitude] = dominantFrequency(buffer, 700.0f, 1'400.0f, 48'000.0f);
	REQUIRE(frequency > 750.0f);
	REQUIRE(frequency < 1'300.0f);
	REQUIRE(magnitude > rms(buffer));
}

TEST_CASE("Mono Ladder develops an audible cutoff tone when GUI-style emphasis is raised over an active oscillator", "[mono][processor][filter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::osc1Level, 45.0f);
	setParameter(processor, vekt::mono::parameters::osc1Morph, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseType, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterDrive, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
	setParameter(processor, vekt::mono::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::mono::parameters::ampVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::unison, 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, 0.0f);
	setParameter(processor, vekt::mono::parameters::masterOutput, 0.0f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	for (int block = 0; block < 8; ++block) renderBlock(processor, buffer);
	const auto lowResonanceCutoffTone = sinusoidMagnitude(buffer, 1'000.0f, 48'000.0f);

	setParameter(processor, vekt::mono::parameters::filterResonance, 100.0f);
	for (int block = 0; block < 120; ++block) renderBlock(processor, buffer);
	const auto highResonanceCutoffTone = sinusoidMagnitude(buffer, 1'000.0f, 48'000.0f);
	const auto [frequency, magnitude] = dominantFrequency(buffer, 700.0f, 1'400.0f, 48'000.0f);
	INFO("low cutoff tone=" << lowResonanceCutoffTone << ", high cutoff tone=" << highResonanceCutoffTone
		<< ", dominant=" << frequency << " Hz / " << magnitude << ", rms=" << rms(buffer));
	REQUIRE(highResonanceCutoffTone > 0.1f);
	REQUIRE(highResonanceCutoffTone > lowResonanceCutoffTone * 10.0f);
	REQUIRE(frequency > 750.0f);
	REQUIRE(frequency < 1'300.0f);
	REQUIRE(magnitude > rms(buffer));
}

TEST_CASE("Mono Ladder does not self-oscillate below the upper emphasis range", "[mono][processor][filter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseType, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 70.0f);
	setParameter(processor, vekt::mono::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::mono::parameters::ampVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::unison, 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, 0.0f);
	setParameter(processor, vekt::mono::parameters::masterOutput, 0.0f);
	processor.prepareToPlay(48'000.0, 4096);
	juce::AudioBuffer<float> buffer(2, 4096);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	for (int block = 0; block < 12; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoRms(buffer) < 1.0e-4f);
}

TEST_CASE("Mono Ladder keyboard tracking follows one octave per keyboard octave", "[mono][processor][filter]")
{
	auto brightnessFor = [](int note)
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::noiseType, 1.0f);
		setParameter(processor, vekt::mono::parameters::noiseLevel, 50.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, 500.0f);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterKeyTracking, 100.0f);
		setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
		processor.prepareToPlay(48'000.0, 4096);
		juce::AudioBuffer<float> buffer(2, 4096);
		juce::MidiBuffer noteOn;
		noteOn.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
		renderBlock(processor, buffer, noteOn);
		renderBlock(processor, buffer);
		return differenceRms(buffer);
	};
	REQUIRE(brightnessFor(72) > brightnessFor(48) * 1.8f);
}

TEST_CASE("Mono Ladder contour is bipolar in octave space", "[mono][processor][filter]")
{
	auto brightnessFor = [](float contour)
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::noiseType, 1.0f);
		setParameter(processor, vekt::mono::parameters::noiseLevel, 50.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, contour);
		setParameter(processor, vekt::mono::parameters::filterAttack, 0.0005f);
		setParameter(processor, vekt::mono::parameters::filterSustain, 100.0f);
		setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
		processor.prepareToPlay(48'000.0, 4096);
		juce::AudioBuffer<float> buffer(2, 4096);
		juce::MidiBuffer noteOn;
		noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
		renderBlock(processor, buffer, noteOn);
		renderBlock(processor, buffer);
		return differenceRms(buffer);
	};
	REQUIRE(brightnessFor(100.0f) > brightnessFor(-100.0f) * 5.0f);
}

TEST_CASE("Mono Ladder drive adds harmonics without acting as output gain", "[mono][processor][filter]")
{
	auto render = [](float drive)
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::osc1Morph, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, 20'000.0f);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterDrive, drive);
		processor.prepareToPlay(48'000.0, 4096);
		juce::AudioBuffer<float> buffer(2, 4096);
		juce::MidiBuffer noteOn;
		noteOn.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
		renderBlock(processor, buffer, noteOn);
		renderBlock(processor, buffer);
		const auto fundamental = sinusoidMagnitude(buffer, 440.0f, 48'000.0f);
		const auto third = sinusoidMagnitude(buffer, 1'320.0f, 48'000.0f);
		return std::pair { third / fundamental, rms(buffer) };
	};
	const auto [cleanHarmonics, cleanRms] = render(0.0f);
	const auto [drivenHarmonics, drivenRms] = render(24.0f);
	REQUIRE(drivenHarmonics > cleanHarmonics * 2.0f);
	REQUIRE(drivenRms < cleanRms * 2.0f);
}

TEST_CASE("Mono preserves APVTS project state", "[mono][processor]")
{
	vekt::mono::PluginProcessor source;
	setParameter(source, vekt::mono::parameters::filterCutoff, 2'345.0f);
	juce::MemoryBlock state;
	source.getStateInformation(state);
	vekt::mono::PluginProcessor restored;
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(restored.getParameters().getRawParameterValue(vekt::mono::parameters::filterCutoff)->load() == Catch::Approx(2'345.0f));
}

TEST_CASE("Mono migrates legacy projects with neutral oscillator octaves", "[mono][processor][state]")
{
	vekt::mono::PluginProcessor source;
	juce::MemoryBlock currentState;
	source.getStateInformation(currentState);
	auto legacyState = juce::ValueTree::readFromData(currentState.getData(), currentState.getSize());
	REQUIRE(legacyState.isValid());
	legacyState.setProperty(vekt::state::StateManager::schemaVersionProperty, 1, nullptr);
	auto sound = legacyState.getChildWithName(vekt::mono::parameters::stateType);
	REQUIRE(sound.isValid());
	for (const auto* identifier : { vekt::mono::parameters::osc1Octave, vekt::mono::parameters::osc2Octave, vekt::mono::parameters::osc3Octave })
	{
		auto value = sound.getChildWithProperty("id", identifier);
		REQUIRE(value.isValid());
		sound.removeChild(value, nullptr);
	}
	juce::MemoryBlock legacyData;
	juce::MemoryOutputStream stream(legacyData, false);
	legacyState.writeToStream(stream);

	vekt::mono::PluginProcessor restored;
	restored.setStateInformation(legacyData.getData(), static_cast<int>(legacyData.getSize()));
	for (const auto* identifier : { vekt::mono::parameters::osc1Octave, vekt::mono::parameters::osc2Octave, vekt::mono::parameters::osc3Octave })
		REQUIRE(restored.getParameters().getRawParameterValue(identifier)->load() == Catch::Approx(0.0f));
}

TEST_CASE("Mono defers voice count while a note is active", "[mono][processor]")
{
	vekt::mono::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer on;
	on.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
	processor.processBlock(buffer, on);
	setParameter(processor, vekt::mono::parameters::voiceCount, 2.0f);
	juce::MidiBuffer empty;
	processor.processBlock(buffer, empty);
	REQUIRE(processor.hasPendingVoiceCountChange());
}

TEST_CASE("Mono provides 24 categorized factory presets", "[mono][processor]")
{
	vekt::mono::PluginProcessor processor;
	auto& session = processor.getPresetSession();
	const auto& catalog = session.library();
	REQUIRE(catalog.factoryPresetCount() == 24);
	REQUIRE(catalog.folders(vekt::presets::PresetOrigin::factory).size() == 6);
	REQUIRE(processor.getNumPrograms() == 24);
	processor.setCurrentProgram(23);
	REQUIRE(processor.getCurrentProgram() == 23);
	REQUIRE(processor.getProgramName(23) == "Transmission FX");
}

TEST_CASE("Mono factory presets use diverse oscillator and mixer designs", "[mono][processor][preset]")
{
	vekt::mono::PluginProcessor processor;
	const auto& catalog = processor.getPresetSession().library();
	std::set<juce::String> oscillatorShapes, oscillatorTunings;
	std::set<float> noiseLevels, voicePans;
	for (std::size_t index = 0; index < catalog.factoryPresetCount(); ++index)
	{
		vekt::presets::Preset preset;
		REQUIRE(catalog.loadFactoryPreset(index, preset).wasOk());
		REQUIRE(preset.soundSchemaVersion == 2);
		const auto value = [&preset](const char* identifier)
		{
			const auto found = std::find_if(preset.parameters.begin(), preset.parameters.end(), [identifier](const auto& parameter)
			{
				return parameter.identifier == identifier;
			});
			REQUIRE(found != preset.parameters.end());
			return found->value;
		};
		oscillatorShapes.insert(juce::String(value(vekt::mono::parameters::osc1Morph), 3) + "/"
			+ juce::String(value(vekt::mono::parameters::osc2Morph), 3) + "/"
			+ juce::String(value(vekt::mono::parameters::osc3Morph), 3) + ":"
			+ juce::String(value(vekt::mono::parameters::osc1PulseWidth), 2) + "/"
			+ juce::String(value(vekt::mono::parameters::osc2PulseWidth), 2) + "/"
			+ juce::String(value(vekt::mono::parameters::osc3PulseWidth), 2));
		oscillatorTunings.insert(juce::String(value(vekt::mono::parameters::osc1Octave), 0) + "/"
			+ juce::String(value(vekt::mono::parameters::osc2Octave), 0) + "/"
			+ juce::String(value(vekt::mono::parameters::osc3Octave), 0) + ":"
			+ juce::String(value(vekt::mono::parameters::osc1Fine), 1) + "/"
			+ juce::String(value(vekt::mono::parameters::osc2Fine), 1) + "/"
			+ juce::String(value(vekt::mono::parameters::osc3Fine), 1));
		noiseLevels.insert(value(vekt::mono::parameters::noiseLevel));
		voicePans.insert(value(vekt::mono::parameters::voiceWidth));
	}
	REQUIRE(oscillatorShapes.size() >= 20);
	REQUIRE(oscillatorTunings.size() >= 20);
	REQUIRE(noiseLevels.size() >= 10);
	REQUIRE(voicePans.size() >= 10);
}

TEST_CASE("Mono migrates legacy presets with neutral octave controls", "[mono][processor][preset]")
{
	vekt::mono::PluginProcessor processor;
	vekt::presets::Preset preset;
	REQUIRE(processor.getPresetSession().library().loadFactoryPreset(0, preset).wasOk());
	preset.soundSchemaVersion = 1;
	for (const auto* identifier : { vekt::mono::parameters::osc1Octave, vekt::mono::parameters::osc2Octave, vekt::mono::parameters::osc3Octave })
		preset.parameters.erase(std::remove_if(preset.parameters.begin(), preset.parameters.end(), [identifier](const auto& parameter)
		{
			return parameter.identifier == identifier;
		}), preset.parameters.end());
	REQUIRE(processor.getPresetSession().prepare(preset).wasOk());
	REQUIRE(preset.soundSchemaVersion == 2);
	for (const auto* identifier : { vekt::mono::parameters::osc1Octave, vekt::mono::parameters::osc2Octave, vekt::mono::parameters::osc3Octave })
	{
		const auto found = std::find_if(preset.parameters.begin(), preset.parameters.end(), [identifier](const auto& parameter)
		{
			return parameter.identifier == identifier;
		});
		REQUIRE(found != preset.parameters.end());
		REQUIRE(found->value == Catch::Approx(0.0f));
	}
}

TEST_CASE("Mono preset changes stop voices from the previous patch", "[mono][processor][preset]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::ampRelease, 20.0f);
	processor.prepareToPlay(48'000.0, 128);
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	processor.processBlock(buffer, noteOn);
	REQUIRE(buffer.getMagnitude(0, 0, buffer.getNumSamples()) > 0.0f);

	REQUIRE(processor.loadNextPreset().wasOk());
	juce::MidiBuffer empty;
	processor.processBlock(buffer, empty);
	REQUIRE(buffer.getMagnitude(0, 0, buffer.getNumSamples()) == Catch::Approx(0.0f).margin(1.0e-7f));
	REQUIRE(buffer.getMagnitude(1, 0, buffer.getNumSamples()) == Catch::Approx(0.0f).margin(1.0e-7f));
}

TEST_CASE("Mono Legato returns to the last held note", "[mono][processor][midi]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 2.0f);
	setParameter(processor, vekt::mono::parameters::ampRelease, 0.005f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
	midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.9f), 128);
	midi.addEvent(juce::MidiMessage::noteOff(1, 72), 256);
	processor.processBlock(buffer, midi);
	float returnedEnergy {};
	for (int sample = 360; sample < buffer.getNumSamples(); ++sample)
		returnedEnergy += std::abs(buffer.getSample(0, sample));
	REQUIRE(returnedEnergy > 0.01f);
}

TEST_CASE("Mono active-note transitions preserve the sample boundary", "[mono][processor][midi][declick]")
{
	for (const auto mode : { 1.0f, 2.0f })
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::performanceMode, mode);
		setParameter(processor, vekt::mono::parameters::osc1Morph, 2.0f);
		setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, 12'000.0f);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::mono::parameters::ampAttack, 0.0005f);
		processor.prepareToPlay(48'000.0, 1'024);
		juce::AudioBuffer<float> buffer(2, 1'024);
		juce::MidiBuffer midi;
		midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
		midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.35f), 512);
		processor.processBlock(buffer, midi);
		for (int channel = 0; channel < 2; ++channel)
			REQUIRE(buffer.getSample(channel, 512) == Catch::Approx(buffer.getSample(channel, 511)).margin(1.0e-5f));
	}
}

TEST_CASE("Mono held-note return preserves the sample boundary", "[mono][processor][midi][declick]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 2.0f);
	setParameter(processor, vekt::mono::parameters::osc1Morph, 3.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 12'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	processor.prepareToPlay(48'000.0, 1'024);
	juce::AudioBuffer<float> buffer(2, 1'024);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
	midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 384);
	midi.addEvent(juce::MidiMessage::noteOff(1, 72), 768);
	processor.processBlock(buffer, midi);
	for (int channel = 0; channel < 2; ++channel)
		REQUIRE(buffer.getSample(channel, 768) == Catch::Approx(buffer.getSample(channel, 767)).margin(1.0e-5f));
}

TEST_CASE("Mono voice stealing avoids an exceptional sample-boundary jump", "[mono][processor][midi][declick]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceCount, 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, 100.0f);
	setParameter(processor, vekt::mono::parameters::osc1Morph, 2.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 12'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::ampAttack, 0.0005f);
	processor.prepareToPlay(48'000.0, 1'024);
	juce::AudioBuffer<float> buffer(2, 1'024);
	juce::MidiBuffer midi;
	for (int voice = 0; voice < 8; ++voice)
		midi.addEvent(juce::MidiMessage::noteOn(1, 48 + voice * 2, 1.0f), voice * 32);
	constexpr auto stealSample = 768;
	midi.addEvent(juce::MidiMessage::noteOn(1, 84, 0.25f), stealSample);
	processor.processBlock(buffer, midi);

	for (int channel = 0; channel < 2; ++channel)
	{
		float nearbyMaximumDelta {};
		for (int sample = stealSample - 64; sample < stealSample; ++sample)
			nearbyMaximumDelta = std::max(nearbyMaximumDelta,
				std::abs(buffer.getSample(channel, sample) - buffer.getSample(channel, sample - 1)));
		const auto boundaryDelta = std::abs(buffer.getSample(channel, stealSample) - buffer.getSample(channel, stealSample - 1));
		REQUIRE(boundaryDelta <= nearbyMaximumDelta * 1.1f + 1.0e-5f);
	}
}

TEST_CASE("Mono modes isolate held-note stacks by MIDI channel", "[mono][processor][midi]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::mono::parameters::ampRelease, 0.005f);
	processor.prepareToPlay(48'000.0, 128);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
	midi.addEvent(juce::MidiMessage::noteOn(2, 72, 0.9f), 0);
	midi.addEvent(juce::MidiMessage::noteOff(1, 48), 128);
	processor.processBlock(buffer, midi);
	float postReleaseEnergy {};
	for (int sample = 300; sample < buffer.getNumSamples(); ++sample)
		postReleaseEnergy += std::abs(buffer.getSample(0, sample));
	REQUIRE(postReleaseEnergy > 0.01f);
}

TEST_CASE("Mono defers quality changes while transport playback is active", "[mono][processor]")
{
	class PlayHead final : public juce::AudioPlayHead
	{
	public:
		juce::Optional<PositionInfo> getPosition() const override
		{
			PositionInfo position;
			position.setIsPlaying(playing);
			return position;
		}

		bool playing { true };
	} playHead;
	vekt::mono::PluginProcessor processor;
	processor.setPlayHead(&playHead);
	processor.prepareToPlay(48'000.0, 128);
	juce::AudioBuffer<float> buffer(2, 128);
	setParameter(processor, vekt::mono::parameters::quality, 1.0f);
	juce::MidiBuffer empty;
	processor.processBlock(buffer, empty);
	REQUIRE(processor.hasPendingQualityChange());
	playHead.playing = false;
	processor.processBlock(buffer, empty);
	REQUIRE_FALSE(processor.hasPendingQualityChange());
}

TEST_CASE("Mono quality change waits for sustain and release tails after transport stops", "[mono][processor][quality]")
{
	class PlayHead final : public juce::AudioPlayHead
	{
	public:
		juce::Optional<PositionInfo> getPosition() const override
		{
			PositionInfo position;
			position.setIsPlaying(playing);
			return position;
		}

		bool playing { true };
	} playHead;
	vekt::mono::PluginProcessor processor;
	processor.setPlayHead(&playHead);
	setParameter(processor, vekt::mono::parameters::ampRelease, 0.005f);
	processor.prepareToPlay(48'000.0, 128);
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer held;
	held.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
	held.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
	held.addEvent(juce::MidiMessage::noteOff(1, 60), 64);
	processor.processBlock(buffer, held);

	setParameter(processor, vekt::mono::parameters::quality, 1.0f);
	playHead.playing = false;
	juce::MidiBuffer empty;
	processor.processBlock(buffer, empty);
	REQUIRE(processor.hasPendingQualityChange());

	juce::MidiBuffer releaseSustain;
	releaseSustain.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
	processor.processBlock(buffer, releaseSustain);
	REQUIRE(processor.hasPendingQualityChange());

	for (int block = 0; block < 8 && processor.hasPendingQualityChange(); ++block)
		processor.processBlock(buffer, empty);
	REQUIRE_FALSE(processor.hasPendingQualityChange());
	REQUIRE(processor.getActiveQuality() == 1);
}

TEST_CASE("Mono High quality oversamples synthesis and reports latency", "[mono][processor][quality]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::quality, 1.0f);
	processor.prepareToPlay(48'000.0, 512);
	REQUIRE(processor.getLatencySamples() > 0);

	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 96, 0.9f), 0);
	processor.processBlock(buffer, midi);

	float energy {};
	for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		{
			REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
			energy += std::abs(buffer.getSample(channel, sample));
		}
	REQUIRE(energy > 0.01f);
}

TEST_CASE("Mono High quality handles multiple note boundaries in one host block", "[mono][processor][quality][midi]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::quality, 1.0f);
	setParameter(processor, vekt::mono::parameters::ampRelease, 0.005f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	midi.addEvent(juce::MidiMessage::noteOff(1, 60), 96);
	midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.9f), 160);
	midi.addEvent(juce::MidiMessage::noteOff(1, 67), 288);
	processor.processBlock(buffer, midi);
	for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
			REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
}

TEST_CASE("Mono unison spread changes stereo rendering", "[mono][processor][unison]")
{
	vekt::mono::PluginProcessor centered, spread;
	for (auto* processor : { &centered, &spread })
	{
		setParameter(*processor, vekt::mono::parameters::performanceMode, 1.0f);
		setParameter(*processor, vekt::mono::parameters::unison, 2.0f);
		setParameter(*processor, vekt::mono::parameters::unisonDetune, 20.0f);
		setParameter(*processor, vekt::mono::parameters::unisonSpread, 0.0f);
		setParameter(*processor, vekt::mono::parameters::voiceWidth, 0.0f);
		setParameter(*processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(*processor, vekt::mono::parameters::osc3Level, 0.0f);
		processor->prepareToPlay(48'000.0, 512);
	}
	setParameter(spread, vekt::mono::parameters::unisonSpread, 100.0f);
	juce::AudioBuffer<float> centeredBuffer(2, 512), spreadBuffer(2, 512);
	juce::MidiBuffer centeredMidi, spreadMidi;
	centeredMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	spreadMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	centered.processBlock(centeredBuffer, centeredMidi);
	spread.processBlock(spreadBuffer, spreadMidi);
	float centeredDifference {}, spreadDifference {};
	for (int sample = 0; sample < centeredBuffer.getNumSamples(); ++sample)
	{
		centeredDifference += std::abs(centeredBuffer.getSample(0, sample) - centeredBuffer.getSample(1, sample));
		spreadDifference += std::abs(spreadBuffer.getSample(0, sample) - spreadBuffer.getSample(1, sample));
	}
	REQUIRE(centeredDifference == Catch::Approx(0.0f).margin(1.0e-6f));
	REQUIRE(spreadDifference > 0.01f);
}

TEST_CASE("Mono voice pan controls round-robin stereo mix", "[mono][processor][stereo]")
{
	vekt::mono::PluginProcessor centered, panned;
	for (auto* processor : { &centered, &panned })
	{
		setParameter(*processor, vekt::mono::parameters::performanceMode, 0.0f);
		setParameter(*processor, vekt::mono::parameters::unison, 0.0f);
		setParameter(*processor, vekt::mono::parameters::unisonSpread, 0.0f);
		setParameter(*processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(*processor, vekt::mono::parameters::osc3Level, 0.0f);
		processor->prepareToPlay(48'000.0, 512);
	}
	setParameter(centered, vekt::mono::parameters::voiceWidth, 0.0f);
	setParameter(panned, vekt::mono::parameters::voiceWidth, 100.0f);
	juce::AudioBuffer<float> centeredBuffer(2, 512), pannedBuffer(2, 512);
	juce::MidiBuffer centeredMidi, pannedMidi;
	centeredMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	pannedMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	centered.processBlock(centeredBuffer, centeredMidi);
	panned.processBlock(pannedBuffer, pannedMidi);

	float centeredDifference {}, pannedDifference {};
	for (int sample = 0; sample < centeredBuffer.getNumSamples(); ++sample)
	{
		centeredDifference += std::abs(centeredBuffer.getSample(0, sample) - centeredBuffer.getSample(1, sample));
		pannedDifference += std::abs(pannedBuffer.getSample(0, sample) - pannedBuffer.getSample(1, sample));
	}
	REQUIRE(centeredDifference == Catch::Approx(0.0f).margin(1.0e-6f));
	REQUIRE(pannedDifference > 0.01f);
}

TEST_CASE("Mono rendering is deterministic with drift enabled", "[mono][processor][determinism]")
{
	vekt::mono::PluginProcessor first, second;
	for (auto* processor : { &first, &second })
	{
		setParameter(*processor, vekt::mono::parameters::drift, 100.0f);
		setParameter(*processor, vekt::mono::parameters::osc1Morph, 2.0f);
		setParameter(*processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(*processor, vekt::mono::parameters::osc3Level, 0.0f);
		processor->prepareToPlay(48'000.0, 512);
	}
	juce::AudioBuffer<float> firstBuffer(2, 512), secondBuffer(2, 512);
	juce::MidiBuffer firstMidi, secondMidi;
	firstMidi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
	secondMidi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
	first.processBlock(firstBuffer, firstMidi);
	second.processBlock(secondBuffer, secondMidi);
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < 512; ++sample)
			REQUIRE(firstBuffer.getSample(channel, sample) == Catch::Approx(secondBuffer.getSample(channel, sample)).margin(1.0e-7f));
}

TEST_CASE("Mono PolyBLEP oscillator output remains finite across supported rates", "[mono][processor][matrix]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0, 192'000.0 })
		for (const auto morph : { 2.0f, 3.0f })
		{
			vekt::mono::PluginProcessor processor;
			setParameter(processor, vekt::mono::parameters::osc1Morph, morph);
			setParameter(processor, vekt::mono::parameters::osc1Level, 100.0f);
			setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
			setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
			processor.prepareToPlay(rate, 512);
			juce::AudioBuffer<float> buffer(2, 512);
			juce::MidiBuffer midi;
			midi.addEvent(juce::MidiMessage::noteOn(1, 120, 0.9f), 0);
			processor.processBlock(buffer, midi);
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
					REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
		}
}

TEST_CASE("Mono handles duplicate notes and channel panic messages without stuck output", "[mono][processor][midi]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::mono::parameters::ampRelease, 0.005f);
	processor.prepareToPlay(48'000.0, 128);
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 8);
	midi.addEvent(juce::MidiMessage::noteOff(1, 60), 16);
	midi.addEvent(juce::MidiMessage::allNotesOff(1), 32);
	midi.addEvent(juce::MidiMessage::allSoundOff(1), 64);
	processor.processBlock(buffer, midi);
	float tailEnergy {};
	for (int sample = 96; sample < buffer.getNumSamples(); ++sample)
		tailEnergy += std::abs(buffer.getSample(0, sample)) + std::abs(buffer.getSample(1, sample));
	REQUIRE(tailEnergy == Catch::Approx(0.0f).margin(1.0e-7f));
}