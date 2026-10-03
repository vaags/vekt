#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>

namespace vekt::plugin_support
{
// One choice of a choice parameter: the value it selects and the name the parameter lists.
template <typename Value> struct Choice
{
	Value value;
	const char* name;
};

// A choice parameter's values in choice order. The same table builds the parameter's choice names and decodes its
// value, so the names, their order and what they select cannot drift apart. Projects store the choice index, so a
// table only ever grows at its end (the parameter manifests freeze names, order and defaults).
template <typename Value, std::size_t Count> class ChoiceTable
{
public:
	static_assert(Count > 0);

	template <typename... Choices>
	    requires(sizeof...(Choices) == Count && (std::same_as<Choices, Choice<Value>> && ...))
	constexpr explicit ChoiceTable(Choices... choices) noexcept : entries { choices... }
	{
	}

	[[nodiscard]] static constexpr std::size_t size() noexcept { return Count; }

	// For the parameter's layout.
	[[nodiscard]] juce::StringArray names() const
	{
		juce::StringArray result;
		for (const auto& entry : entries) result.add(entry.name);
		return result;
	}

	// The value of the choice nearest a parameter value (its choice index; ties round to even), clamped to the table.
	// Audio-thread safe: no allocation.
	[[nodiscard]] Value at(float parameterValue) const noexcept
	{
		const auto index = std::clamp(juce::roundToInt(parameterValue), 0, static_cast<int>(Count) - 1);
		return entries[static_cast<std::size_t>(index)].value;
	}

	// The choice index of a value, for parameter defaults; the value must be in the table. Evaluated as a constant, a
	// missing value is a compile error.
	[[nodiscard]] constexpr int indexOf(const Value& value) const noexcept
	{
		for (std::size_t index = 0; index < Count; ++index)
			if (entries[index].value == value) return static_cast<int>(index);
		jassertfalse;
		return 0;
	}

	// The name a value's choice shows, for status text; the value must be in the table.
	[[nodiscard]] constexpr const char* nameOf(const Value& value) const noexcept
	{
		return entries[static_cast<std::size_t>(indexOf(value))].name;
	}

	[[nodiscard]] constexpr const Choice<Value>& operator[](std::size_t index) const noexcept { return entries[index]; }
	[[nodiscard]] constexpr auto begin() const noexcept { return entries.begin(); }
	[[nodiscard]] constexpr auto end() const noexcept { return entries.end(); }

private:
	std::array<Choice<Value>, Count> entries;
};

template <typename Value, typename... Rest>
ChoiceTable(Choice<Value>, Rest...) -> ChoiceTable<Value, 1 + sizeof...(Rest)>;
}
