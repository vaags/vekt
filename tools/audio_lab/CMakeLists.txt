add_executable(VektRender
	main.cpp)

add_executable(VektAudioLab
	LiveLab.cpp)
set_target_properties(VektAudioLab PROPERTIES MACOSX_BUNDLE TRUE)

add_executable(VektKobberRender
	KobberRender.cpp
	KobberRenderMain.cpp)

add_executable(VektKobberQCompMatrix
	KobberRender.cpp
	KobberQCompMatrixMain.cpp)

add_executable(VektKobberQCompListening
	KobberRender.cpp
	KobberQCompListeningMain.cpp)

target_link_libraries(VektKobberQCompListening PRIVATE
	KobberCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberIncrementalResonance KobberIncrementalResonanceMain.cpp)
target_link_libraries(VektKobberIncrementalResonance PRIVATE
	KobberCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberAcPumpResponse KobberAcPumpResponseMain.cpp)
target_link_libraries(VektKobberAcPumpResponse PRIVATE
	KobberCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberResonanceOnset KobberResonanceOnsetMain.cpp)
target_link_libraries(VektKobberResonanceOnset PRIVATE
	KobberCore
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberResonanceMatrix KobberResonanceMatrixMain.cpp)
target_link_libraries(VektKobberResonanceMatrix PRIVATE
	KobberCore
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberResonanceVoice KobberRender.cpp KobberResonanceVoiceMain.cpp)
target_link_libraries(VektKobberResonanceVoice PRIVATE
	KobberCore
	vekt::audio_analysis
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberWidthPitchLevelListening KobberWidthPitchLevelListeningMain.cpp)
target_link_libraries(VektKobberWidthPitchLevelListening PRIVATE
	KobberCore
	vekt::dsp
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

add_executable(VektKobberAdditiveCost KobberAdditiveCostMain.cpp)
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

add_executable(VektKobberProcessorCost KobberProcessorCostMain.cpp CallbackAllocationProbe.cpp)

add_executable(VektFlintCost FlintCostMain.cpp CallbackAllocationProbe.cpp)
target_link_libraries(VektFlintCost PRIVATE
	FlintCore
	vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektRender PRIVATE
	RavCore
	GlimmerCore
    vekt::juce_headers
	vekt::juce_runtime
	vekt_project_options)

target_link_libraries(VektAudioLab PRIVATE
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
