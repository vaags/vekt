vekt_add_factory_presets(
	TARGET KobberFactoryData
	NAMESPACE KobberFactoryData
	HEADER_NAME MonoBinaryData.h
	PRESET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets"
	ORDER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.txt"
	LOCK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.lock"
	MANIFEST_HEADER KobberFactoryPresetManifest.h
	MANIFEST_NAMESPACE vekt::mono::factory_presets)

add_library(KobberCore STATIC
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

target_include_directories(KobberCore PUBLIC
	include
	Source)
target_link_libraries(KobberCore PUBLIC
	KobberFactoryData
	vekt_project_options
	vekt::juce_headers
	vekt::ui
	vekt::dsp
	vekt::state
	vekt::presets
	vekt::preset_ui
	vekt::plugin_support)

set(kobber_formats Standalone VST3 AU)

juce_add_plugin(Kobber
	VERSION ${PROJECT_VERSION}
	COMPANY_NAME "${VEKT_MAKER_NAME}"
	BUNDLE_ID ${VEKT_BUNDLE_ID_PREFIX}.kobber
	PLUGIN_MANUFACTURER_CODE ${VEKT_MAKER_CODE}
	PLUGIN_CODE Kobr
	FORMATS ${kobber_formats}
	PRODUCT_NAME "Kobber"
	DESCRIPTION "A three-oscillator ladder synthesizer"
	IS_SYNTH TRUE
	NEEDS_MIDI_INPUT TRUE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	AU_MAIN_TYPE kAudioUnitType_MusicDevice
	NEEDS_WEB_BROWSER FALSE
	NEEDS_CURL FALSE
	HARDENED_RUNTIME_ENABLED TRUE)
target_sources(Kobber PRIVATE Source/PluginEntryPoint.cpp)
target_link_libraries(Kobber PRIVATE
	KobberCore
	juce::juce_audio_utils
	juce::juce_audio_formats
	juce::juce_dsp)

vekt_adhoc_seal_bundle(Kobber_AU)
vekt_adhoc_seal_bundle(Kobber_VST3)
vekt_adhoc_seal_bundle(Kobber_Standalone)
