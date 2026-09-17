add_library(vekt_dsp INTERFACE)
add_library(vekt::dsp ALIAS vekt_dsp)

target_include_directories(vekt_dsp INTERFACE include)
target_link_libraries(vekt_dsp INTERFACE
	vekt_project_options
	juce::juce_dsp)
