#include <vekt/presets/FilePresetRepository.h>

#include <vekt/presets/PresetJsonCodec.h>
#include <vekt/presets/PresetSchema.h>

namespace vekt::presets
{
namespace
{
[[nodiscard]] bool isValidPresetName(const juce::String& name)
{
	const auto trimmed = name.trim();
	return trimmed.isNotEmpty() && juce::File::createLegalFileName(trimmed) == trimmed;
}
}

FilePresetRepository::FilePresetRepository(juce::File directory)
	: rootDirectory(std::move(directory))
{
}

juce::StringArray FilePresetRepository::list() const
{
	juce::StringArray names;
	for (const auto& file : rootDirectory.findChildFiles(
			juce::File::findFiles, false, "*" + juce::String(fileExtension)))
		names.add(file.getFileNameWithoutExtension());

	names.sortNatural();
	return names;
}

juce::Result FilePresetRepository::load(
	const juce::String& name, Preset& destination) const
{
	if (!isValidPresetName(name))
		return juce::Result::fail("Preset name is not valid for storage");

	const auto file = fileFor(name);
	if (!file.existsAsFile())
		return juce::Result::fail("Preset does not exist");

	return PresetJsonCodec::decode(file.loadFileAsString(), destination);
}

juce::Result FilePresetRepository::save(const Preset& preset)
{
	if (const auto result = PresetSchema::validateEnvelope(preset); result.failed())
		return result;

	const auto name = preset.name.trim();
	if (!isValidPresetName(name))
		return juce::Result::fail("Preset name is not valid for storage");
	if (const auto result = rootDirectory.createDirectory(); result.failed())
		return result;

	juce::String json;
	if (const auto result = PresetJsonCodec::encode(preset, json); result.failed())
		return result;

	juce::TemporaryFile temporaryFile(fileFor(name));
	if (!temporaryFile.getFile().replaceWithText(json)
		|| !temporaryFile.overwriteTargetFileWithTemporary())
		return juce::Result::fail("Could not write preset");

	return juce::Result::ok();
}

juce::Result FilePresetRepository::remove(const juce::String& name)
{
	if (!isValidPresetName(name))
		return juce::Result::fail("Preset name is not valid for storage");

	const auto file = fileFor(name);
	if (!file.existsAsFile())
		return juce::Result::fail("Preset does not exist");

	return file.deleteFile()
		? juce::Result::ok()
		: juce::Result::fail("Could not delete preset");
}

juce::File FilePresetRepository::fileFor(const juce::String& name) const
{
	return rootDirectory.getChildFile(
		juce::File::createLegalFileName(name.trim()) + fileExtension);
}
}