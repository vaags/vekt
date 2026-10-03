#include <vekt/plugin_support/PresetHost.h>

namespace vekt::plugin_support
{
namespace
{
void assertMessageThread()
{
	jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr
		|| juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());
}
}

juce::UndoManager* editorUndo(juce::UndoManager& history) noexcept
{
	const auto* messages = juce::MessageManager::getInstanceWithoutCreating();
	return messages == nullptr || messages->isThisTheMessageThread() ? &history : nullptr;
}

PresetHost::PresetHost(juce::AudioProcessorValueTreeState& parameters, presets::PresetProduct product,
	presets::PresetSoundAdapter sound, int projectSchemaVersion)
	: projectState(parameters, product.identifier, projectSchemaVersion),
	  presetSession(presetCatalog, std::move(product), std::move(sound))
{
	presetSession.useLock(stateMutex);
}

juce::var PresetHost::metadataValue(const juce::Identifier& name) const
{
	const std::scoped_lock locked(stateMutex);
	return projectState.getMetadata().getProperty(name);
}

void PresetHost::setMetadataValue(const juce::Identifier& name, const juce::var& value)
{
	const std::scoped_lock locked(stateMutex);
	projectState.getMetadata().setProperty(name, value, nullptr);
}

juce::ValueTree PresetHost::metadataCopy() const
{
	const std::scoped_lock locked(stateMutex);
	return projectState.getMetadata().createCopy();
}

juce::Result PresetHost::configureUserPresetDirectory(const juce::File& directory)
{
	assertMessageThread();
	const std::scoped_lock locked(stateMutex);
	if (directory == juce::File {})
		return juce::Result::fail("User preset directory is empty");
	userRepository = std::make_unique<presets::FilePresetRepository>(directory);
	presetCatalog.setUserRepository(userRepository.get());
	return juce::Result::ok();
}

int PresetHost::numPrograms() const
{
	const std::scoped_lock locked(stateMutex);
	return static_cast<int>(presetCatalog.factoryPresetCount());
}

int PresetHost::currentProgram() const
{
	const std::scoped_lock locked(stateMutex);
	if (const auto index = presetSession.currentIndex(); index && presetSession.origin() == presets::PresetOrigin::factory)
		return static_cast<int>(*index);
	return 0;
}

void PresetHost::selectProgram(int index)
{
	const std::scoped_lock locked(stateMutex);
	if (index < 0)
		return;
	presets::Preset preset;
	if (presetCatalog.loadFactoryPreset(static_cast<std::size_t>(index), preset).wasOk())
		juce::ignoreUnused(presetSession.load(preset.identifier, presets::PresetOrigin::factory));
}

juce::String PresetHost::programName(int index) const
{
	const std::scoped_lock locked(stateMutex);
	return index < 0 ? juce::String {} : presetCatalog.factoryPresetName(static_cast<std::size_t>(index));
}

juce::Result PresetHost::loadAdjacentPreset(bool next)
{
	assertMessageThread();
	const std::scoped_lock locked(stateMutex);
	presetCatalog.refresh();
	const auto& entries = presetCatalog.entries();
	if (entries.empty())
		return juce::Result::fail("No presets available");
	const auto current = presetSession.currentIndex();
	const auto index = current ? (next ? presetCatalog.nextIndex(*current) : presetCatalog.previousIndex(*current))
		: std::optional<std::size_t> { next ? 0 : entries.size() - 1 };
	if (!index)
		return juce::Result::fail("No presets available");
	const auto entry = entries[*index];
	return presetSession.load(entry.identifier, entry.origin);
}

void PresetHost::save(juce::MemoryBlock& destination)
{
	const std::scoped_lock locked(stateMutex);
	projectState.getMetadata().setProperty(selectionProperty, presetSession.selectionState(), nullptr);
	projectState.save(destination);
}

bool PresetHost::restore(const void* data, int size)
{
	const std::scoped_lock locked(stateMutex);
	if (!projectState.restore(data, size))
		return false;
	projectState.getMetadata().removeProperty(legacyFactoryPresetProperty, nullptr);
	presetSession.clear();
	if (projectState.getMetadata().hasProperty(selectionProperty))
		juce::ignoreUnused(presetSession.restoreSelection(projectState.getMetadata().getProperty(selectionProperty)));
	return true;
}
}
