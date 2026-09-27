add_executable(VektRavRender
	main.cpp)

add_executable(VektRavAudioLab
	LiveLab.cpp)
set_target_properties(VektRavAudioLab PROPERTIES MACOSX_BUNDLE TRUE)

option(VEKT_MONO_AUDIO_LAB_COUPLED_PREVIEW "Use the development-only coupled 1x Mono ladder in Audio Lab" OFF)
if(VEKT_MONO_AUDIO_LAB_COUPLED_PREVIEW)
	if(NOT VEKT_MONO_LADDER_DEVELOPMENT)
		message(FATAL_ERROR "VEKT_MONO_AUDIO_LAB_COUPLED_PREVIEW requires VEKT_MONO_LADDER_DEVELOPMENT")
	endif()
	target_compile_definitions(VektRavAudioLab PRIVATE VEKT_MONO_AUDIO_LAB_COUPLED_PREVIEW=1)
endif()

add_executable(VektMonoRender
	MonoRender.cpp
	MonoRenderMain.cpp)

add_executable(VektLadderPrototype
	LadderPrototype.cpp
	LadderPrototypeMain.cpp)

add_executable(VektLadderCost
	LadderCostMain.cpp)

add_executable(VektMonoProcessorCost MonoProcessorCostMain.cpp CallbackAllocationProbe.cpp)

target_link_libraries(VektRavRender PRIVATE
	VektRavCore
	VektGlimmerCore
    vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektRavAudioLab PRIVATE
	VektRavCore
	VektGlimmerCore
	VektMonoCore
    vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektMonoRender PRIVATE
	VektMonoCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektLadderPrototype PRIVATE
	VektMonoCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektLadderCost PRIVATE
	VektMonoCore
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektMonoProcessorCost PRIVATE
	VektMonoCore
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)
