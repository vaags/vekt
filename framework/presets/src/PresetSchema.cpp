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

struct ParameterChange final
{
	juce::RangedAudioParameter* parameter {};
	float before {};
	float after {};
};

class ApplyPresetAction final : public juce::UndoableAction
{
public:
	explicit ApplyPresetAction(std::vector<ParameterChange> changesToApply)
		: changes(std::move(changesToApply))
	{
	}

	bool perform() override
	{
		applyValues(false);
		return true;
	}

	bool undo() override
	{
		applyValues(true);
		return true;
	}

	int getSizeInUnits() override
	{
		return static_cast<int>(changes.size());
	}

private:
	void applyValues(bool usePreviousValues)
	{
		for (const auto& change : changes)
		{
			const auto value = usePreviousValues ? change.before : change.after;
			change.parameter->setValueNotifyingHost(change.parameter->convertTo0to1(value));
		}
	}

	std::vector<ParameterChange> changes;
};
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

bool PresetSchema::matches(
	const Preset& preset,
	const juce::String& expectedProductIdentifier,
	const juce::AudioProcessorValueTreeState& parameters,
	std::span<const char* const> soundParameterIds)
{
	if (validate(preset, expectedProductIdentifier, parameters, soundParameterIds).failed())
		return false;

	for (const auto* parameterId : soundParameterIds)
	{
		const auto* parameter = parameters.getParameter(parameterId);
		const auto current = parameter->convertFrom0to1(parameter->getValue());
		const auto expected = findParameter(preset, parameterId)->value;
		const auto tolerance = std::max(
			parameter->getNormalisableRange().interval * 0.5f, 1.0e-6f);
		if (std::abs(current - expected) > tolerance)
			return false;
	}

	return true;
}

juce::Result PresetSchema::apply(
	const Preset& preset,
	const juce::String& expectedProductIdentifier,
	juce::AudioProcessorValueTreeState& parameters,
	std::span<const char* const> soundParameterIds,
	juce::UndoManager* undoManager)
{
	if (const auto result = validate(
			preset, expectedProductIdentifier, parameters, soundParameterIds);
		result.failed())
		return result;

	std::vector<ParameterChange> changes;
	changes.reserve(soundParameterIds.size());
	for (const auto* parameterId : soundParameterIds)
	{
		auto* parameter = parameters.getParameter(parameterId);
		changes.push_back({
			parameter,
			parameter->convertFrom0to1(parameter->getValue()),
			findParameter(preset, parameterId)->value });
	}

	if (undoManager != nullptr)
	{
		if (!undoManager->perform(new ApplyPresetAction(std::move(changes))))
			return juce::Result::fail("Could not apply preset transaction");
	}
	else
		ApplyPresetAction(std::move(changes)).perform();

	return juce::Result::ok();
}
}
