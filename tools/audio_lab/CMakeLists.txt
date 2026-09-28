add_executable(VektRavRender
	main.cpp)

add_executable(VektRavAudioLab
	LiveLab.cpp)
set_target_properties(VektRavAudioLab PROPERTIES MACOSX_BUNDLE TRUE)

add_executable(VektMonoRender
	MonoRender.cpp
	MonoRenderMain.cpp)

add_executable(VektMonoQCompMatrix
	MonoRender.cpp
	MonoQCompMatrixMain.cpp)

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

target_link_libraries(VektMonoQCompMatrix PRIVATE
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
