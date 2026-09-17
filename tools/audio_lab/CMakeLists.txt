add_executable(VektRavRender
	main.cpp)

target_link_libraries(VektRavRender PRIVATE
	VektRav
	juce::juce_audio_processors
	juce::juce_audio_basics
	juce::juce_gui_basics
	vekt_project_options)
