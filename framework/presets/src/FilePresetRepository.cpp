#include <vekt/presets/FilePresetRepository.h>

#include <vekt/presets/PresetJsonCodec.h>
#include <vekt/presets/PresetDocument.h>

namespace vekt::presets
{
namespace
{
[[nodiscard]] bool isValidPresetName(const juce::String& name)
{
	const auto trimmed = name.trim();
	return trimmed.isNotEmpty() && juce::File::createLegalFileName(trimmed) == trimmed;
}

void collectChildren(const juce::File& directory, juce::Array<juce::File>& files,
	juce::Array<juce::File>& directories, int depth = 0)
{
	// Do not traverse symlinks (including cycles), even before path validation.
	if (directory.isSymbolicLink() || depth > 32) return;
	for (const auto& child : directory.findChildFiles(juce::File::findFilesAndDirectories, false))
	{
		if (child.isSymbolicLink()) continue;
		if (child.isDirectory())
		{
			directories.add(child);
			collectChildren(child, files, directories, depth + 1);
		}
		else if (child.hasFileExtension("vektpreset")) files.add(child);
	}
}
}

FilePresetRepository::FilePresetRepository(juce::File directory)
	: rootDirectory(std::move(directory))
{
}

juce::StringArray FilePresetRepository::list() const
{
	juce::StringArray names;
	juce::Array<juce::File> files, directories;
	collectChildren(rootDirectory, files, directories);
	files.sort();
	for (const auto& file : files)
	{
		const auto location = file.getRelativePathFrom(rootDirectory).dropLastCharacters(
			static_cast<int>(juce::String(fileExtension).length()));
		if (safePath(location))
			names.addIfNotAlreadyThere(location, true);
	}

	names.sortNatural();
	return names;
}

juce::Result FilePresetRepository::load(
	const juce::String& name, Preset& destination) const
{
	if (!safePath(name))
		return juce::Result::fail("Preset name is not valid for storage");

	const auto file = fileFor(name);
	if (!file.existsAsFile())
		return juce::Result::fail("Preset does not exist");

	if (file.getSize() > 4 * 1024 * 1024)
		return juce::Result::fail("Preset exceeds the 4 MB size limit");
	const auto json = file.loadFileAsString();
	const auto legacyDocument = static_cast<int>(juce::JSON::parse(json).getProperty("schemaVersion", 0)) == 1;
	if (const auto result = PresetJsonCodec::decode(json, destination); result.failed())
		return result;
	// The repository location is authoritative after an external file rename.
	destination.name = name.fromLastOccurrenceOf("/", false, false);
	destination.folder = name.containsChar('/') ? name.upToLastOccurrenceOf("/", false, false) : juce::String {};
	if (legacyDocument)
		destination.identifier = "legacy:" + destination.productIdentifier + ":" + name;
	return juce::Result::ok();
}

juce::Result FilePresetRepository::save(const Preset& preset, PresetSaveMode mode)
{
	juce::InterProcessLock lock("vekt-presets-" + juce::String(rootDirectory.getFullPathName().hashCode64()));
	if (!lock.enter(0)) return juce::Result::fail("Preset library is busy; please retry");
	const juce::ScopeGuard unlock([&lock] { lock.exit(); });
	if (const auto result = PresetDocument::validate(preset); result.failed())
		return result;

	const auto name = preset.name.trim();
	const auto location = preset.folder.isEmpty() ? name : preset.folder + "/" + name;
	if (!isValidPresetName(name) || !safePath(location))
		return juce::Result::fail("Preset name is not valid for storage");

	for (const auto& existingName : list())
	{
		if (!existingName.equalsIgnoreCase(location))
			continue;
		if (mode == PresetSaveMode::createOnly)
			return juce::Result::fail("Preset already exists");
		if (existingName != location)
			return juce::Result::fail("Preset name differs only by letter case");
	}

	if (mode == PresetSaveMode::replaceExisting && !fileFor(location).existsAsFile())
		return juce::Result::fail("Preset to replace no longer exists");
	if (const auto result = fileFor(location).getParentDirectory().createDirectory(); result.failed())
		return result;

	juce::String json;
	if (const auto result = PresetJsonCodec::encode(preset, json); result.failed())
		return result;

	juce::TemporaryFile temporaryFile(fileFor(location));
	if (!temporaryFile.getFile().replaceWithText(json)
		|| !temporaryFile.overwriteTargetFileWithTemporary())
		return juce::Result::fail("Could not write preset");

	return juce::Result::ok();
}

juce::Result FilePresetRepository::remove(const juce::String& name)
{
	juce::InterProcessLock lock("vekt-presets-" + juce::String(rootDirectory.getFullPathName().hashCode64()));
	if (!lock.enter(0)) return juce::Result::fail("Preset library is busy; please retry");
	const juce::ScopeGuard unlock([&lock] { lock.exit(); });
	if (!safePath(name))
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
		name + fileExtension);
}

bool FilePresetRepository::safePath(const juce::String& path, bool allowEmpty) const
{
	if (path.isEmpty()) return allowEmpty;
	if (juce::File::isAbsolutePath(path) || path.containsChar('\\')
		|| path.contains("//") || path.endsWithChar('/')) return false;
	juce::StringArray parts;
	parts.addTokens(path, "/", "");
	auto file = rootDirectory;
	if (file.isSymbolicLink()) return false;
	for (const auto& part : parts)
	{
		if (part == "." || part == ".." || !isValidPresetName(part) || part != part.trim())
			return false;
		file = file.getChildFile(part);
		if (file.isSymbolicLink()) return false;
	}
	return !fileFor(path).isSymbolicLink();
}

juce::StringArray FilePresetRepository::folders() const
{
	juce::StringArray result;
	juce::Array<juce::File> files, directories;
	collectChildren(rootDirectory, files, directories);
	for (const auto& directory : directories)
	{
		const auto path = directory.getRelativePathFrom(rootDirectory);
		if (safePath(path)) result.add(path);
	}
	result.sortNatural();
	return result;
}

juce::Result FilePresetRepository::createFolder(const juce::String& path)
{
	if (!safePath(path)) return juce::Result::fail("Invalid folder path");
	for (const auto& existing : folders())
		if (existing.equalsIgnoreCase(path)) return juce::Result::fail("Folder already exists");
	return rootDirectory.getChildFile(path).createDirectory();
}

juce::Result FilePresetRepository::move(const juce::String& source, const juce::String& destination)
{
	if (!safePath(source) || !safePath(destination))
		return juce::Result::fail("Invalid preset location");
	for (const auto& existing : list())
		if (existing.equalsIgnoreCase(destination)) return juce::Result::fail("Destination already exists");
	if (!fileFor(source).existsAsFile()) return juce::Result::fail("Preset no longer exists");
	if (const auto result = fileFor(destination).getParentDirectory().createDirectory(); result.failed())
		return result;
	// Persist legacy identity before moving so subsequent scans retain it.
	Preset preset;
	if (const auto result = load(source, preset); result.failed()) return result;
	// Preserve the reference, including legacy references, across managed moves.
	preset.name = destination.fromLastOccurrenceOf("/", false, false);
	preset.folder = destination.containsChar('/') ? destination.upToLastOccurrenceOf("/", false, false) : juce::String {};
	if (const auto result = save(preset); result.failed()) return result;
	if (const auto result = remove(source); result.failed())
	{
		juce::ignoreUnused(remove(destination));
		return result;
	}
	return juce::Result::ok();
}

juce::Result FilePresetRepository::removeEmptyFolder(const juce::String& path)
{
	if (!safePath(path)) return juce::Result::fail("Invalid folder path");
	const auto directory = rootDirectory.getChildFile(path);
	if (!directory.isDirectory() || directory.getNumberOfChildFiles(juce::File::findFilesAndDirectories) != 0)
		return juce::Result::fail("Only empty folders can be deleted");
	return directory.deleteFile() ? juce::Result::ok() : juce::Result::fail("Could not delete folder");
}
}
