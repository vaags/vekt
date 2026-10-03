#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdlib>

namespace vekt::ui
{
// Lists a choice parameter's own choices in a combo box (item IDs from 1, as ComboBoxAttachment expects), so the menu
// always matches the parameter. An identifier that names no choice parameter is a programming error and stops.
inline void addChoiceItems(juce::ComboBox& box, juce::AudioProcessorValueTreeState& state, const char* identifier)
{
	auto* choice = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(identifier));
	if (choice == nullptr)
	{
		jassertfalse;
		std::abort();
	}
	box.addItemList(choice->choices, 1);
}
}
