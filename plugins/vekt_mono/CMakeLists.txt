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
	Source/LadderPoleMix.h
	Source/LadderResonance.h
	Source/FilterModeEase.h
	Source/SvfResponse.h
	Source/Lfo.h
	Source/NonlinearTptLadder.cpp
	Source/NonlinearTptLadder.h
	Source/NonlinearTptSvf.h
	Source/NonlinearTptKorg35.h
	Source/Korg35Response.h
	Source/SimdLanes.h
	Source/WidthOscillator.cpp
	Source/WidthOscillator.h
	Source/MonoRenderWorkers.cpp
	Source/MonoRenderWorkers.h
	Source/MonoSettingsSnapshot.cpp
	Source/MonoSettingsSnapshot.h
	Source/MonoVoiceAllocator.h
	Source/MonoRenderPlan.h
	Source/FactoryPresets.cpp
	Source/MonoVoice.cpp
	Source/MonoVoice.h
	Source/Parameters.cpp
	Source/PluginEditor.cpp
	Source/PluginProcessor.cpp)

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
	vekt::preset_ui
	vekt::plugin_support)

set(vekt_mono_formats Standalone VST3 AU)

juce_add_plugin(VektMono
	VERSION ${PROJECT_VERSION}
	COMPANY_NAME "${VEKT_MAKER_NAME}"
	BUNDLE_ID ${VEKT_BUNDLE_ID_PREFIX}.mono
	PLUGIN_MANUFACTURER_CODE ${VEKT_MAKER_CODE}
	PLUGIN_CODE Kobr
	FORMATS ${vekt_mono_formats}
	PRODUCT_NAME "Mono"
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

vekt_adhoc_seal_bundle(VektMono_AU)
vekt_adhoc_seal_bundle(VektMono_VST3)
vekt_adhoc_seal_bundle(VektMono_Standalone)
