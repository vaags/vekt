#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <utility>

namespace vekt::state
{
class StateManager final
{
public:
    using Migration = std::function<bool(juce::ValueTree&, int)>;

    StateManager(
        juce::AudioProcessorValueTreeState& parameters,
        juce::Identifier projectStateType,
        int currentSchemaVersion,
        Migration migration = {})
        : parameterState(parameters),
          rootType(std::move(projectStateType)),
          currentVersion(currentSchemaVersion),
          migrate(std::move(migration)),
          metadata(metadataType)
    {
        jassert(currentVersion > 0);
    }

    [[nodiscard]] juce::ValueTree createState() const
    {
        juce::ValueTree root(rootType);
        root.setProperty(schemaVersionProperty, currentVersion, nullptr);
        root.addChild(parameterState.copyState(), -1, nullptr);
        root.addChild(metadata.createCopy(), -1, nullptr);
        return root;
    }

    bool restoreState(const juce::ValueTree& serializedState)
    {
        auto candidate = serializedState.createCopy();
        if (candidate.hasType(parameterState.state.getType()))
            candidate = wrapLegacyParameterState(candidate);

        if (!candidate.hasType(rootType))
            return false;

        auto version = static_cast<int>(candidate.getProperty(schemaVersionProperty, 0));
        if (version <= 0 || version > currentVersion)
            return false;

        while (version < currentVersion)
        {
            if (!migrate || !migrate(candidate, version))
                return false;

            ++version;
            candidate.setProperty(schemaVersionProperty, version, nullptr);
        }

        const auto parameters = candidate.getChildWithName(parameterState.state.getType());
        if (!parameters.isValid())
            return false;

        const auto restoredMetadata = candidate.getChildWithName(metadataType);
        parameterState.replaceState(parameters.createCopy());
        const auto metadataSource = restoredMetadata.isValid()
            ? restoredMetadata.createCopy()
            : juce::ValueTree(metadataType);
        metadata.copyPropertiesAndChildrenFrom(metadataSource, nullptr);
        return true;
    }

    [[nodiscard]] juce::ValueTree& getMetadata() noexcept
    {
        return metadata;
    }

    [[nodiscard]] const juce::ValueTree& getMetadata() const noexcept
    {
        return metadata;
    }

    inline static const juce::Identifier metadataType { "ProjectMetadata" };
    inline static const juce::Identifier schemaVersionProperty { "schemaVersion" };
    inline static const juce::Identifier legacyVersionProperty { "stateVersion" };

private:
    [[nodiscard]] juce::ValueTree wrapLegacyParameterState(juce::ValueTree legacyState) const
    {
        const auto legacyVersion = static_cast<int>(
            legacyState.getProperty(legacyVersionProperty, 1));
        legacyState.removeProperty(legacyVersionProperty, nullptr);

        juce::ValueTree root(rootType);
        root.setProperty(schemaVersionProperty, legacyVersion, nullptr);
        root.addChild(legacyState, -1, nullptr);
        root.addChild(juce::ValueTree(metadataType), -1, nullptr);
        return root;
    }

    juce::AudioProcessorValueTreeState& parameterState;
    juce::Identifier rootType;
    int currentVersion;
    Migration migrate;
    juce::ValueTree metadata;
};
}
