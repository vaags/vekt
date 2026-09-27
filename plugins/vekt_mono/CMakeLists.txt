vekt_add_factory_presets(
	TARGET VektMonoFactoryData
	NAMESPACE VektMonoFactoryData
	HEADER_NAME MonoBinaryData.h
	PRESET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets"
	ORDER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.txt"
	LOCK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.lock"
	MANIFEST_HEADER VektMonoFactoryPresetManifest.h
	MANIFEST_NAMESPACE vekt::mono::factory_presets)

add_library(VektMonoCore STATIC
	Source/DelayedFeedbackLadder.h
	Source/NonlinearTptLadder.cpp
	Source/NonlinearTptLadder.h
	Source/FactoryPresets.cpp
	Source/MonoVoice.h
	Source/Parameters.cpp
	Source/PluginEditor.cpp
	Source/PluginProcessor.cpp)

option(VEKT_MONO_LADDER_DEVELOPMENT "Enable candidate ladder in local C++ development harnesses" OFF)
if(VEKT_MONO_LADDER_DEVELOPMENT)
	target_compile_definitions(VektMonoCore PUBLIC VEKT_MONO_LADDER_DEVELOPMENT=1)
endif()

target_include_directories(VektMonoCore PUBLIC
	include
	Source)
target_link_libraries(VektMonoCore PUBLIC
	VektMonoFactoryData
	vekt_project_options
	vekt::juce_headers
	vekt::ui
	vekt::dsp
	vekt::state
	vekt::presets
	vekt::preset_ui)

set(vekt_mono_formats Standalone VST3)
if(CMAKE_GENERATOR STREQUAL "Xcode")
	list(APPEND vekt_mono_formats AUv3)
endif()

juce_add_plugin(VektMono
	VERSION ${PROJECT_VERSION}
	COMPANY_NAME Vekt
	BUNDLE_ID com.vekt.mono
	PLUGIN_MANUFACTURER_CODE Vekt
	PLUGIN_CODE Mono
	FORMATS ${vekt_mono_formats}
	PRODUCT_NAME "Vekt Mono"
	DESCRIPTION "A three-oscillator ladder synthesizer"
	IS_SYNTH TRUE
	NEEDS_MIDI_INPUT TRUE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	AU_MAIN_TYPE kAudioUnitType_MusicDevice
	NEEDS_WEB_BROWSER FALSE
	NEEDS_CURL FALSE
	HARDENED_RUNTIME_ENABLED TRUE)
target_sources(VektMono PRIVATE Source/PluginEntryPoint.cpp)
target_link_libraries(VektMono PRIVATE
	VektMonoCore
	juce::juce_audio_utils
	juce::juce_audio_formats
	juce::juce_dsp)

if(VEKT_MONO_LADDER_DEVELOPMENT)
	juce_add_plugin(VektMonoLadderPreview
		VERSION ${PROJECT_VERSION}
		COMPANY_NAME Vekt
		BUNDLE_ID com.vekt.mono.ladderpreview
		PLUGIN_MANUFACTURER_CODE Vekt
		PLUGIN_CODE MnPr
		FORMATS Standalone
		PRODUCT_NAME "Vekt Mono Ladder Preview"
		DESCRIPTION "Development-only 1x nonlinear ladder preview"
		IS_SYNTH TRUE
		NEEDS_MIDI_INPUT TRUE
		NEEDS_MIDI_OUTPUT FALSE
		IS_MIDI_EFFECT FALSE
		NEEDS_WEB_BROWSER FALSE
		NEEDS_CURL FALSE
		HARDENED_RUNTIME_ENABLED TRUE)
	target_sources(VektMonoLadderPreview PRIVATE Source/PluginEntryPoint.cpp)
	target_compile_definitions(VektMonoLadderPreview PRIVATE VEKT_MONO_LADDER_PREVIEW=1)
	target_link_libraries(VektMonoLadderPreview PRIVATE
		VektMonoCore
		juce::juce_audio_utils
		juce::juce_audio_formats
		juce::juce_dsp)
endif()
