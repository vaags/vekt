#include <vekt/presets/PresetJsonCodec.h>

#include <vekt/presets/PresetDocument.h>

namespace vekt::presets
{
namespace
{
inline const juce::Identifier schemaVersionProperty { "schemaVersion" };
inline const juce::Identifier productProperty { "product" };
inline const juce::Identifier nameProperty { "name" };
inline const juce::Identifier parametersProperty { "parameters" };
inline const juce::Identifier metadataProperty { "metadata" };

[[nodiscard]] juce::var toObject(const juce::NamedValueSet& properties)
{
	auto object = std::make_unique<juce::DynamicObject>();
	for (auto index = 0; index < properties.size(); ++index)
		object->setProperty(properties.getName(index), properties.getValueAt(index));
	return juce::var(object.release());
}
}

juce::Result PresetJsonCodec::encode(const Preset& preset, juce::String& destination)
{
	if (const auto result = PresetDocument::validate(preset); result.failed())
		return result;

	auto root = std::make_unique<juce::DynamicObject>();
	root->setProperty("format", PresetDocument::format);
	root->setProperty("id", preset.identifier);
	root->setProperty("soundSchemaVersion", preset.soundSchemaVersion);
	juce::Array<juce::var> tags;
	for (const auto& tag : normaliseTags(preset.tags))
		tags.add(tag);
	root->setProperty("tags", tags);
	root->setProperty("soundState", toObject(preset.soundState));
	root->setProperty(schemaVersionProperty, preset.schemaVersion);
	root->setProperty(productProperty, preset.productIdentifier);
	root->setProperty(nameProperty, preset.name);

	auto parameters = std::make_unique<juce::DynamicObject>();
	for (const auto& parameter : preset.parameters)
		parameters->setProperty(parameter.identifier, parameter.value);
	root->setProperty(parametersProperty, juce::var(parameters.release()));

	if (preset.metadata.size() > 0)
		root->setProperty(metadataProperty, toObject(preset.metadata));

	destination = juce::JSON::toString(
		juce::var(root.release()),
		juce::JSON::FormatOptions {}
			.withSpacing(juce::JSON::Spacing::multiLine)
			.withEncoding(juce::JSON::Encoding::utf8)
			.withMaxDecimalPlaces(9));
	return juce::Result::ok();
}

juce::Result PresetJsonCodec::decode(const juce::String& json, Preset& destination)
{
	juce::var parsed;
	if (const auto result = juce::JSON::parse(json, parsed); result.failed())
		return result;

	const auto* root = parsed.getDynamicObject();
	if (root == nullptr)
		return juce::Result::fail("Preset JSON root must be an object");

	const auto schemaVersion = root->getProperty(schemaVersionProperty);
	const auto product = root->getProperty(productProperty);
	const auto name = root->getProperty(nameProperty);
	const auto parameterValues = root->getProperty(parametersProperty);
	if (!schemaVersion.isInt() || !product.isString() || !name.isString()
		|| !parameterValues.isObject())
		return juce::Result::fail("Preset JSON has invalid field types");

	Preset preset;
	preset.schemaVersion = static_cast<int>(schemaVersion);
	preset.productIdentifier = product.toString();
	preset.name = name.toString();
	if (preset.schemaVersion == 1)
	{
		// Legacy IDs are deterministic for embedded factories. Repositories qualify
		// them by location until the first successful write persists a UUID.
		preset.identifier = "legacy:" + preset.productIdentifier + ":" + preset.name;
		preset.schemaVersion = PresetDocument::version;
	}
	else
	{
		const auto id = root->getProperty("id");
		const auto soundVersion = root->getProperty("soundSchemaVersion");
		const auto tags = root->getProperty("tags");
		const auto soundState = root->getProperty("soundState");
		if (root->getProperty("format").toString() != PresetDocument::format
			|| !id.isString() || !soundVersion.isInt() || !tags.isArray()
			|| (!soundState.isVoid() && !soundState.isObject()))
			return juce::Result::fail("Invalid Vekt preset envelope");
		preset.identifier = id.toString();
		preset.soundSchemaVersion = static_cast<int>(soundVersion);
		for (const auto& tag : *tags.getArray())
		{
			if (!tag.isString() || tag.toString().trim().isEmpty())
				return juce::Result::fail("Preset tags must be non-empty strings");
			preset.tags.add(tag.toString());
		}
		preset.tags = normaliseTags(preset.tags);
		if (soundState.isObject())
			preset.soundState = soundState.getDynamicObject()->getProperties();
	}
	for (const auto& property : parameterValues.getDynamicObject()->getProperties())
	{
		const auto& value = property.value;
		if (!value.isDouble() && !value.isInt() && !value.isInt64())
			return juce::Result::fail("Preset parameter value is not numeric");
		preset.parameters.push_back({ property.name.toString(), static_cast<float>(value) });
	}

	const auto metadata = root->getProperty(metadataProperty);
	if (!metadata.isVoid())
	{
		if (!metadata.isObject())
			return juce::Result::fail("Preset metadata must be an object");
		preset.metadata = metadata.getDynamicObject()->getProperties();
	}
	if (static_cast<int>(schemaVersion) == 1 && preset.metadata["category"].isString())
		preset.tags = normaliseTags({ preset.metadata["category"].toString() });

	if (const auto result = PresetDocument::validate(preset); result.failed())
		return result;

	destination = std::move(preset);
	return juce::Result::ok();
}
}
