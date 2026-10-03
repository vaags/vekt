add_executable(VektRavRender
	main.cpp)

add_executable(VektRavAudioLab
	LiveLab.cpp)
set_target_properties(VektRavAudioLab PROPERTIES MACOSX_BUNDLE TRUE)

add_executable(VektKobberRender
	MonoRender.cpp
	MonoRenderMain.cpp)

add_executable(VektKobberQCompMatrix
	MonoRender.cpp
	MonoQCompMatrixMain.cpp)

add_executable(VektKobberQCompListening
	MonoRender.cpp
	MonoQCompListeningMain.cpp)

target_link_libraries(VektKobberQCompListening PRIVATE
	KobberCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberIncrementalResonance MonoIncrementalResonanceMain.cpp)
target_link_libraries(VektKobberIncrementalResonance PRIVATE
	KobberCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberAcPumpResponse MonoAcPumpResponseMain.cpp)
target_link_libraries(VektKobberAcPumpResponse PRIVATE
	KobberCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberResonanceOnset MonoResonanceOnsetMain.cpp)
target_link_libraries(VektKobberResonanceOnset PRIVATE
	KobberCore
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberResonanceMatrix MonoResonanceMatrixMain.cpp)
target_link_libraries(VektKobberResonanceMatrix PRIVATE
	KobberCore
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberResonanceVoice MonoRender.cpp MonoResonanceVoiceMain.cpp)
target_link_libraries(VektKobberResonanceVoice PRIVATE
	KobberCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberWidthPitchLevelListening MonoWidthPitchLevelListeningMain.cpp)
target_link_libraries(VektKobberWidthPitchLevelListening PRIVATE
	KobberCore
	vekt::dsp
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberAdditiveCost MonoAdditiveCostMain.cpp)
target_link_libraries(VektKobberAdditiveCost PRIVATE
	KobberCore
	vekt::dsp
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektLadderPrototype
	LadderPrototype.cpp
	LadderPrototypeMain.cpp)

add_executable(VektLadderCost
	LadderCostMain.cpp)

add_executable(VektKobberProcessorCost MonoProcessorCostMain.cpp CallbackAllocationProbe.cpp)

add_executable(VektFlintCost FlintCostMain.cpp CallbackAllocationProbe.cpp)
target_link_libraries(VektFlintCost PRIVATE
	FlintCore
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektRavRender PRIVATE
	RavCore
	GlimmerCore
    vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektRavAudioLab PRIVATE
	RavCore
	GlimmerCore
	KobberCore
	FlintCore
    vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektKobberRender PRIVATE
	KobberCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektKobberQCompMatrix PRIVATE
	KobberCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektLadderPrototype PRIVATE
	KobberCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektLadderCost PRIVATE
	KobberCore
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektKobberProcessorCost PRIVATE
	KobberCore
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

# Every Audio Lab executable is built from Vekt sources.
get_directory_property(vekt_audio_lab_targets BUILDSYSTEM_TARGETS)
vekt_strict_warnings(${vekt_audio_lab_targets})
