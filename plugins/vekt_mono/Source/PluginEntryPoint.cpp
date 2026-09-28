#include <vekt/mono/PluginProcessor.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new vekt::mono::PluginProcessor();
}