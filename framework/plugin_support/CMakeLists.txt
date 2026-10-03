add_library(vekt_plugin_support STATIC
	src/PresetHost.cpp
	src/QualitySelection.cpp)
add_library(vekt::plugin_support ALIAS vekt_plugin_support)

target_include_directories(vekt_plugin_support PUBLIC include)
target_link_libraries(vekt_plugin_support PUBLIC
	vekt_project_options
	vekt::dsp
	vekt::state
	vekt::presets
	vekt::juce_headers)
