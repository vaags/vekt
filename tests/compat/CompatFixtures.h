#pragma once

// Shared plumbing for the compatibility fixtures in tests/fixtures: the products under test, the
// files on disk, and a small float WAV codec. Fixture files are deliberately framework-free (raw
// host-state bytes, plain text, standard WAV) so they stay readable whatever the plugins are built on.

#include <vekt/rav/PluginProcessor.h>
#include <vekt/glimmer/PluginProcessor.h>
#include <vekt/mono/PluginProcessor.h>
#include <vekt/flint/PluginProcessor.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace vekt::compat
{
struct Product
{
	std::string name;
	bool synth {};
	std::function<std::unique_ptr<juce::AudioProcessor>()> create;
	// Product-specific state outside the parameters, recorded alongside them (e.g. Rav's stage order).
	std::function<std::map<std::string, std::string>(juce::AudioProcessor&)> describeExtras;
	// Moves that state away from its default before a capture so a restore has to bring it back.
	std::function<void(juce::AudioProcessor&)> customiseExtras;
};

inline std::map<std::string, std::string> describeProgram(juce::AudioProcessor& processor)
{
	return { { "program", std::to_string(processor.getCurrentProgram()) } };
}

inline const std::vector<Product>& products()
{
	static const std::vector<Product> all {
		{ "rav", false,
			[] { return std::make_unique<rav::PluginProcessor>(); },
			[](juce::AudioProcessor& processor)
			{
				auto extras = describeProgram(processor);
				std::string order;
				for (const auto mode : dynamic_cast<rav::PluginProcessor&>(processor).getStageOrder())
					order += (order.empty() ? "" : ",") + std::to_string(static_cast<int>(mode));
				extras["stageOrder"] = order;
				return extras;
			},
			[](juce::AudioProcessor& processor)
			{
				auto& rav = dynamic_cast<rav::PluginProcessor&>(processor);
				juce::ignoreUnused(rav.reorderStage(0, 2), rav.reorderStage(4, -1));
			} },
		{ "glimmer", false,
			[] { return std::make_unique<glimmer::PluginProcessor>(); },
			describeProgram,
			[](juce::AudioProcessor&) {} },
		{ "mono", true,
			[] { return std::make_unique<mono::PluginProcessor>(); },
			describeProgram,
			[](juce::AudioProcessor&) {} },
		{ "flint", true,
			[] { return std::make_unique<flint::PluginProcessor>(); },
			[](juce::AudioProcessor& processor)
			{
				// The variation seed lives in project metadata, outside the parameters.
				auto extras = describeProgram(processor);
				extras["seed"] = std::to_string(dynamic_cast<flint::PluginProcessor&>(processor).getSeed());
				return extras;
			},
			[](juce::AudioProcessor& processor) { dynamic_cast<flint::PluginProcessor&>(processor).newSeed(); } },
	};
	return all;
}

inline const Product& product(const std::string& name)
{
	const auto& all = products();
	return *std::find_if(all.begin(), all.end(), [&](const auto& entry) { return entry.name == name; });
}

inline std::filesystem::path fixtureDirectory() { return VEKT_COMPAT_FIXTURES_DIR; }
inline std::filesystem::path outputDirectory() { return VEKT_COMPAT_OUTPUT_DIR; }

inline std::vector<juce::RangedAudioParameter*> rangedParameters(juce::AudioProcessor& processor)
{
	std::vector<juce::RangedAudioParameter*> result;
	for (auto* parameter : static_cast<const juce::AudioProcessor&>(processor).getParameters())
		if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
			result.push_back(ranged);
	return result;
}

inline juce::RangedAudioParameter* findParameter(juce::AudioProcessor& processor, const std::string& identifier)
{
	for (auto* parameter : rangedParameters(processor))
		if (parameter->paramID.toStdString() == identifier)
			return parameter;
	return nullptr;
}

inline void setPlainValue(juce::AudioProcessor& processor, const std::string& identifier, float value)
{
	auto* parameter = findParameter(processor, identifier);
	if (parameter == nullptr)
		throw std::runtime_error("unknown parameter " + identifier);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

inline float plainValue(const juce::RangedAudioParameter& parameter)
{
	return parameter.convertFrom0to1(parameter.getValue());
}

inline std::vector<char> readBytes(const std::filesystem::path& path)
{
	std::ifstream stream(path, std::ios::binary);
	return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

inline void writeBytes(const std::filesystem::path& path, const void* data, std::size_t size)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
	if (!stream)
		throw std::runtime_error("could not write " + path.string());
}

inline std::vector<std::filesystem::path> filesWithExtension(const std::filesystem::path& directory, const std::string& extension)
{
	std::vector<std::filesystem::path> files;
	if (std::filesystem::is_directory(directory))
		for (const auto& entry : std::filesystem::directory_iterator(directory))
			if (entry.path().extension() == extension)
				files.push_back(entry.path());
	std::sort(files.begin(), files.end());
	return files;
}

// Interleaved-free stereo float audio, written as a 32-bit float WAV so references can be auditioned.
struct StereoAudio
{
	double sampleRate {};
	std::array<std::vector<float>, 2> channels;

	[[nodiscard]] std::size_t frames() const noexcept { return channels[0].size(); }
};

inline void writeWav(const std::filesystem::path& path, const StereoAudio& audio)
{
	const auto frames = static_cast<std::uint32_t>(audio.frames());
	const std::uint32_t dataBytes = frames * 2u * 4u;
	std::vector<char> bytes;
	const auto append = [&](const auto value)
	{
		const auto* raw = reinterpret_cast<const char*>(&value); // little-endian hosts only (arm64/x86)
		bytes.insert(bytes.end(), raw, raw + sizeof(value));
	};
	const auto appendTag = [&](const char* tag) { bytes.insert(bytes.end(), tag, tag + 4); };
	appendTag("RIFF");
	append(static_cast<std::uint32_t>(36u + dataBytes));
	appendTag("WAVE");
	appendTag("fmt ");
	append(std::uint32_t { 16 });
	append(std::uint16_t { 3 }); // IEEE float
	append(std::uint16_t { 2 });
	append(static_cast<std::uint32_t>(audio.sampleRate));
	append(static_cast<std::uint32_t>(audio.sampleRate) * 8u);
	append(std::uint16_t { 8 });
	append(std::uint16_t { 32 });
	appendTag("data");
	append(dataBytes);
	for (std::uint32_t frame = 0; frame < frames; ++frame)
		for (const auto& channel : audio.channels)
			append(channel[frame]);
	writeBytes(path, bytes.data(), bytes.size());
}

inline std::optional<StereoAudio> readWav(const std::filesystem::path& path)
{
	const auto bytes = readBytes(path);
	const auto read = [&](std::size_t offset, auto& value)
	{
		if (offset + sizeof(value) > bytes.size()) return false;
		std::memcpy(&value, bytes.data() + offset, sizeof(value));
		return true;
	};
	const auto tagAt = [&](std::size_t offset, const char* tag)
	{
		return offset + 4 <= bytes.size() && std::memcmp(bytes.data() + offset, tag, 4) == 0;
	};
	if (!tagAt(0, "RIFF") || !tagAt(8, "WAVE")) return std::nullopt;

	std::uint16_t format {}, channelCount {}, bits {};
	std::uint32_t sampleRate {};
	for (std::size_t offset = 12; offset + 8 <= bytes.size();)
	{
		std::uint32_t chunkSize {};
		read(offset + 4, chunkSize);
		const auto body = offset + 8;
		if (tagAt(offset, "fmt "))
		{
			read(body, format);
			read(body + 2, channelCount);
			read(body + 4, sampleRate);
			read(body + 14, bits);
		}
		else if (tagAt(offset, "data"))
		{
			if (format != 3 || channelCount != 2 || bits != 32 || body + chunkSize > bytes.size()) return std::nullopt;
			StereoAudio audio;
			audio.sampleRate = sampleRate;
			const auto frames = chunkSize / 8u;
			for (auto& channel : audio.channels) channel.resize(frames);
			for (std::uint32_t frame = 0; frame < frames; ++frame)
				for (std::size_t channel = 0; channel < 2; ++channel)
					read(body + (frame * 2u + channel) * 4u, audio.channels[channel][frame]);
			return audio;
		}
		offset = body + chunkSize + (chunkSize & 1u);
	}
	return std::nullopt;
}
}
