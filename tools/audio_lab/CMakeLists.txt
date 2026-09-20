add_executable(VektRavRender
	main.cpp)

add_executable(VektRavAudioLab
	LiveLab.cpp)
set_target_properties(VektRavAudioLab PROPERTIES MACOSX_BUNDLE TRUE)

target_link_libraries(VektRavRender PRIVATE
	VektRavCore
	VektGlimmerCore
    vekt::juce_headers
	vekt_project_options)

target_link_libraries(VektRavAudioLab PRIVATE
	VektRavCore
	VektGlimmerCore
	VektMonoCore
    vekt::juce_headers
	vekt_project_options)
