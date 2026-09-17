#include <vekt/presets/PresetJsonCodec.h>

#include <vekt/presets/PresetSchema.h>

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
	if (const auto result = PresetSchema::validateEnvelope(preset); result.failed())
		return result;

	auto root = std::make_unique<juce::DynamicObject>();
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

	if (const auto result = PresetSchema::validateEnvelope(preset); result.failed())
		return result;

	destination = std::move(preset);
	return juce::Result::ok();
}
}
