#include <vekt/rav/PluginProcessor.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new vekt::rav::PluginProcessor();
}
