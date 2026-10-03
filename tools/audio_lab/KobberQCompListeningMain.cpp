#include "KobberRender.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace
{
constexpr double rate = 48'000.0;
constexpr int blockSize = 128;
constexpr std::uint32_t seed = 42;
constexpr float playbackGain = 0.7f; // Common post-render headroom; never changes ladder excitation.

double stereoRms(const vekt::audio_lab::KobberRenderResult& render,
	const vekt::audio_lab::KobberMeasurementWindow& window)
{
	double sum {};
	const auto count = window.endSample - window.startSample;
	for (int channel = 0; channel < 2; ++channel)
		for (auto sample = window.startSample; sample < window.endSample; ++sample)
		{
			const auto value = static_cast<double>(render.audio.getSample(channel, static_cast<int>(sample)));
			sum += value * value;
		}
	return std::sqrt(sum / static_cast<double>(2 * count));
}

bool write(const juce::File& directory, const juce::String& name,
	const vekt::audio_lab::KobberRenderResult& render)
{
	vekt::audio_lab::KobberRenderResult playback;
	playback.audio.makeCopyOf(render.audio);
	playback.audio.applyGain(playbackGain);
	return vekt::audio_lab::writeMonoRenderWav(directory.getChildFile(name + ".wav"), playback, rate)
		&& vekt::audio_lab::writeMonoRenderReport(directory.getChildFile(name + ".json"), render);
}
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: VektKobberQCompListening output-directory\n";
		return 64;
	}
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	const juce::File directory { juce::String(argv[1]) };
	if (!directory.createDirectory()) return 1;
	std::ofstream csv(directory.getChildFile("level-match.csv").getFullPathName().toStdString());
	if (!csv) return 1;
	csv << "fixture,drive_db,resonance,on_coefficient,note,cutoff_start_hz,off_listening_rms,on_listening_rms,"
		"on_match_gain,on_gain_db,matched_listening_rms,match_start_sample,match_end_sample,playback_gain\n";
	csv << std::setprecision(12);
	for (const auto* kind : { "sustain", "bass", "sweep" })
		for (const auto drive : { 12, 18, 24 })
		{
			const auto prefix = juce::String("q-comp-listen-95-") + kind + "-drive-"
				+ juce::String(drive);
			vekt::audio_lab::KobberRenderRequest offRequest, onRequest;
			if (!vekt::audio_lab::makeMonoRenderFixture(prefix + "-off", rate, blockSize, seed, offRequest)
				|| !vekt::audio_lab::makeMonoRenderFixture(prefix + "-on", rate, blockSize, seed, onRequest)) return 1;
			const auto off = vekt::audio_lab::renderMono(offRequest);
			const auto on = vekt::audio_lab::renderMono(onRequest);
			const auto& window = offRequest.windows.front();
			const auto offRms = stereoRms(off, window);
			const auto onRms = stereoRms(on, window);
			if (!(offRms > 0.0 && onRms > 0.0) || !std::isfinite(offRms)
				|| !std::isfinite(onRms)) return 1;
			const auto gain = offRms / onRms;
			if (!write(directory, prefix + "-off", off) || !write(directory, prefix + "-on", on)) return 1;
			vekt::audio_lab::KobberRenderResult matched;
			matched.audio.makeCopyOf(on.audio);
			matched.audio.applyGain(static_cast<float>(gain));
			const auto matchedRms = stereoRms(matched, window);
			if (!std::isfinite(matchedRms) || std::abs(matchedRms / offRms - 1.0) > 1.0e-6) return 1;
			matched.audio.applyGain(playbackGain);
			if (!vekt::audio_lab::writeMonoRenderWav(
				directory.getChildFile(prefix + "-on-level-matched.wav"), matched, rate)) return 1;
			csv << kind << ',' << drive << ",0.95,0.5"
				<< ',' << offRequest.events.front().note << ','
				<< offRequest.settings.cutoff << ',' << offRms << ',' << onRms << ',' << gain
				<< ',' << 20.0 * std::log10(onRms / offRms) << ',' << matchedRms << ','
				<< window.startSample << ',' << window.endSample << ',' << playbackGain << '\n';
			std::cout << prefix << " match_gain=" << gain << '\n';
		}
	return csv ? 0 : 1;
}