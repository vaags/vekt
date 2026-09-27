#include <vekt/mono/PluginProcessor.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
#if defined(VEKT_MONO_LADDER_PREVIEW) && defined(VEKT_MONO_LADDER_DEVELOPMENT)
	return new vekt::mono::PluginProcessor(true, true);
#else
	return new vekt::mono::PluginProcessor();
#endif
}