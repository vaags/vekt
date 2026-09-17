#include <vekt/presets/PresetSchema.h>

#include <cmath>

namespace vekt::presets
{
namespace
{
[[nodiscard]] const ParameterValue* findParameter(
	const Preset& preset, const juce::String& identifier)
{
	for (const auto& parameter : preset.parameters)
		if (parameter.identifier == identifier)
			return &parameter;

	return nullptr;
}

[[nodiscard]] bool contains(
	std::span<const char* const> parameterIds, const juce::String& candidate)
{
	for (const auto* parameterId : parameterIds)
		if (candidate == parameterId)
			return true;

	return false;
}
}

Preset PresetSchema::create(
	const juce::String& productIdentifier,
	const juce::String& name,
	const juce::AudioProcessorValueTreeState& parameters,
	std::span<const char* const> soundParameterIds,
	const juce::NamedValueSet& metadata)
{
	Preset preset;
	preset.schemaVersion = currentSchemaVersion;
	preset.productIdentifier = productIdentifier;
	preset.name = name.trim();
	preset.metadata = metadata;
	preset.parameters.reserve(soundParameterIds.size());

	for (const auto* parameterId : soundParameterIds)
	{
		const auto* parameter = dynamic_cast<const juce::RangedAudioParameter*>(
			parameters.getParameter(parameterId));
		if (parameter == nullptr)
			return {};

		preset.parameters.push_back({
			parameterId, parameter->convertFrom0to1(parameter->getValue()) });
	}

	return preset;
}

juce::Result PresetSchema::validate(
	const Preset& preset,
	const juce::String& expectedProductIdentifier,
	const juce::AudioProcessorValueTreeState& parameters,
	std::span<const char* const> soundParameterIds)
{
	if (const auto result = validateEnvelope(preset); result.failed())
		return result;
	if (preset.productIdentifier != expectedProductIdentifier)
		return juce::Result::fail("Preset belongs to a different product");
	if (preset.parameters.size() != soundParameterIds.size())
		return juce::Result::fail("Preset parameter set is incomplete");

	for (const auto& value : preset.parameters)
		if (!contains(soundParameterIds, value.identifier))
			return juce::Result::fail("Preset contains an unknown parameter");

	for (const auto* parameterId : soundParameterIds)
	{
		const auto* value = findParameter(preset, parameterId);
		if (value == nullptr)
			return juce::Result::fail("Preset parameter set is incomplete");

		const auto* parameter = dynamic_cast<const juce::RangedAudioParameter*>(
			parameters.getParameter(parameterId));
		if (parameter == nullptr)
			return juce::Result::fail("Preset references an unavailable parameter");

		const auto& range = parameter->getNormalisableRange();
		if (!std::isfinite(value->value) || value->value < range.start || value->value > range.end)
			return juce::Result::fail("Preset parameter value is out of range");
	}

	return juce::Result::ok();
}

juce::Result PresetSchema::validateEnvelope(const Preset& preset)
{
	if (preset.schemaVersion != currentSchemaVersion)
		return juce::Result::fail("Unsupported preset schema version");
	if (preset.productIdentifier.trim().isEmpty())
		return juce::Result::fail("Preset product identifier is empty");
	if (preset.name.trim().isEmpty())
		return juce::Result::fail("Preset name is empty");
	if (preset.parameters.empty())
		return juce::Result::fail("Preset parameter set is missing");

	return juce::Result::ok();
}

juce::Result PresetSchema::apply(
	const Preset& preset,
	const juce::String& expectedProductIdentifier,
	juce::AudioProcessorValueTreeState& parameters,
	std::span<const char* const> soundParameterIds)
{
	if (const auto result = validate(
			preset, expectedProductIdentifier, parameters, soundParameterIds);
		result.failed())
		return result;

	for (const auto* parameterId : soundParameterIds)
		parameters.getParameterAsValue(parameterId).setValue(
			findParameter(preset, parameterId)->value);

	return juce::Result::ok();
}
}