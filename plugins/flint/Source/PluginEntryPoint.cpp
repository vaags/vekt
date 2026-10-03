#include <vekt/flint/Parameters.h>
#include <vekt/flint/PluginProcessor.h>

#include <string_view>

static_assert(std::string_view(JucePlugin_Name) == vekt::flint::parameters::productName,
    "PRODUCT_NAME in CMakeLists.txt and parameters::productName must name the product the same way");

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vekt::flint::PluginProcessor(); }
