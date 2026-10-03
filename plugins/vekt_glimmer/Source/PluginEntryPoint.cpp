#include <vekt/glimmer/PluginProcessor.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new vekt::glimmer::PluginProcessor();
}
