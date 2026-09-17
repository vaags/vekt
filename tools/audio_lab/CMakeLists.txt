add_executable(VektRavRender
	main.cpp)

add_executable(VektRavAudioLab
	LiveLab.cpp)
set_target_properties(VektRavAudioLab PROPERTIES MACOSX_BUNDLE TRUE)

target_link_libraries(VektRavRender PRIVATE
	VektRav
	juce::juce_audio_processors
	juce::juce_audio_basics
	juce::juce_gui_basics
	vekt_project_options)

target_link_libraries(VektRavAudioLab PRIVATE
	VektRav
	juce::juce_audio_utils
	juce::juce_gui_basics
	vekt::ui
	vekt_project_options)
