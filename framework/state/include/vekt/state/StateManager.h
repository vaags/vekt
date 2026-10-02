#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <utility>

namespace vekt::state
{
// Host project state as a UTF-8 JSON document the products own, readable without JUCE:
//
//   { "format": "vekt.project", "product": "com.vekt.rav", "schemaVersion": 1,
//     "parameters": { "<id>": <plain value>, ... }, "metadata": { "<key>": <any JSON value>, ... } }
//
// Parameters restore through APVTS::replaceState, so parameters missing from a project take their defaults;
// booleans then publish their exact restored value, since replaceState keeps an unsnapped host fraction.
// Restoring is all-or-nothing: a document that is not exactly this product's current schema changes nothing.
class StateManager final
{
public:
    StateManager(juce::AudioProcessorValueTreeState& parameters, juce::String productIdentifier, int currentSchemaVersion)
        : parameterState(parameters),
          product(std::move(productIdentifier)),
          currentVersion(currentSchemaVersion),
          metadata(metadataType)
    {
        jassert(currentVersion > 0);
    }

    void save(juce::MemoryBlock& destination) const
    {
        auto parameters = std::make_unique<juce::DynamicObject>();
        for (const auto& parameter : parameterState.copyState())
            if (parameter.hasProperty(idProperty))
                parameters->setProperty(parameter[idProperty].toString(), static_cast<double>(parameter[valueProperty]));

        auto properties = std::make_unique<juce::DynamicObject>();
        for (auto index = 0; index < metadata.getNumProperties(); ++index)
        {
            const auto name = metadata.getPropertyName(index);
            properties->setProperty(name, metadata[name]);
        }

        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty("format", format);
        root->setProperty("product", product);
        root->setProperty("schemaVersion", currentVersion);
        root->setProperty("parameters", juce::var(parameters.release()));
        root->setProperty("metadata", juce::var(properties.release()));
        const auto text = juce::JSON::toString(juce::var(root.release()),
            juce::JSON::FormatOptions {}.withSpacing(juce::JSON::Spacing::multiLine).withEncoding(juce::JSON::Encoding::utf8));
        destination.replaceAll(text.toRawUTF8(), text.getNumBytesAsUTF8());
    }

    [[nodiscard]] bool restore(const void* data, int size)
    {
        if (data == nullptr || size <= 0)
            return false;
        juce::var parsed;
        if (juce::JSON::parse(juce::String::fromUTF8(static_cast<const char*>(data), size), parsed).failed())
            return false;
        const auto* root = parsed.getDynamicObject();
        if (root == nullptr || root->getProperty("format").toString() != format
            || root->getProperty("product").toString() != product)
            return false;
        if (const auto version = root->getProperty("schemaVersion"); !version.isInt() || static_cast<int>(version) != currentVersion)
            return false;

        const auto parameters = root->getProperty("parameters");
        const auto properties = root->getProperty("metadata");
        if (!parameters.isObject() || !(properties.isVoid() || properties.isObject()))
            return false;

        // Everything is checked before live state changes.
        juce::ValueTree restored(parameterState.state.getType());
        for (const auto& [identifier, value] : parameters.getDynamicObject()->getProperties())
        {
            // JSON overflow (1e999) parses as infinity, which APVTS would clamp or keep and saving would turn into null.
            if ((!value.isInt() && !value.isInt64() && !value.isDouble()) || !std::isfinite(static_cast<double>(value)))
                return false;
            restored.appendChild(juce::ValueTree(parameterType, { { idProperty, identifier.toString() },
                { valueProperty, static_cast<double>(value) } }), nullptr);
        }

        parameterState.replaceState(restored);
        // replaceState skips a parameter whose plain value already matches, but a host can leave a boolean
        // at an unsnapped fraction (VST3 automation of 0.7 reads back as 0.7, not 1); publish the exact value.
        // Other parameter types snap in setValue, so only booleans can hold such a fraction.
        for (auto* processorParameter : parameterState.processor.getParameters())
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(processorParameter); parameter != nullptr && parameter->isBoolean())
                if (const auto* plain = parameterState.getRawParameterValue(parameter->paramID))
                    if (const auto value = parameter->convertTo0to1(plain->load()); !juce::approximatelyEqual(parameter->getValue(), value))
                        parameter->setValueNotifyingHost(value);
        metadata.removeAllProperties(nullptr);
        if (properties.isObject())
            for (const auto& [name, value] : properties.getDynamicObject()->getProperties())
                metadata.setProperty(name, value, nullptr);
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

    inline static constexpr auto format = "vekt.project";
    inline static const juce::Identifier metadataType { "ProjectMetadata" };

private:
    // How APVTS stores each parameter in its tree.
    inline static const juce::Identifier parameterType { "PARAM" };
    inline static const juce::Identifier idProperty { "id" };
    inline static const juce::Identifier valueProperty { "value" };

    juce::AudioProcessorValueTreeState& parameterState;
    juce::String product;
    int currentVersion;
    juce::ValueTree metadata;
};
}
