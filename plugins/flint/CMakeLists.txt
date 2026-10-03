vekt_add_factory_presets(
	TARGET FlintFactoryData
	NAMESPACE FlintFactoryData
	HEADER_NAME FlintBinaryData.h
	PRESET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets"
	ORDER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.txt"
	LOCK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Presets/factory-order.lock"
	MANIFEST_HEADER FlintFactoryPresetManifest.h
	MANIFEST_NAMESPACE vekt::flint::factory_presets)

add_library(FlintCore STATIC
	include/vekt/flint/FactoryPresets.h
	include/vekt/flint/Models.h
	include/vekt/flint/Parameters.h
	include/vekt/flint/PluginProcessor.h
	Source/FactoryPresets.cpp
	Source/DriveStage.cpp
	Source/DriveStage.h
	Source/FlintEngine.h
	Source/FlintEngineHost.cpp
	Source/FlintEngineHost.h
	Source/FlintEngines.cpp
	Source/FlintEngines.h
	Source/FlintParameters.h
	Source/HitRandom.h
	Source/KickClassicAnalog.cpp
	Source/KickClassicAnalog.h
	Source/MalletBar.cpp
	Source/MalletBar.h
	Source/ModalBank.cpp
	Source/ModalBank.h
	Source/Parameters.cpp
	Source/PluginEditor.cpp
	Source/PluginProcessor.cpp
	Source/SafetyClip.h)

target_include_directories(FlintCore PUBLIC
	include
	Source)
target_link_libraries(FlintCore PUBLIC
	FlintFactoryData
	vekt_project_options
	vekt::juce_headers
	vekt::ui
	vekt::dsp
	vekt::state
	vekt::presets
	vekt::preset_ui
	vekt::plugin_support)

set(flint_formats Standalone VST3 AU)

juce_add_plugin(Flint
	VERSION ${PROJECT_VERSION}
	COMPANY_NAME "${VEKT_MAKER_NAME}"
	BUNDLE_ID ${VEKT_BUNDLE_ID_PREFIX}.flint
	PLUGIN_MANUFACTURER_CODE ${VEKT_MAKER_CODE}
	PLUGIN_CODE Flnt
	FORMATS ${flint_formats}
	PRODUCT_NAME "Flint"
	DESCRIPTION "A sample-free percussion synthesizer"
	IS_SYNTH TRUE
	NEEDS_MIDI_INPUT TRUE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	AU_MAIN_TYPE kAudioUnitType_MusicDevice
	VST3_CATEGORIES Instrument Drum
	NEEDS_WEB_BROWSER FALSE
	NEEDS_CURL FALSE
	HARDENED_RUNTIME_ENABLED TRUE)
target_sources(Flint PRIVATE Source/PluginEntryPoint.cpp)
target_link_libraries(Flint PRIVATE
	FlintCore
	juce::juce_audio_utils
	juce::juce_audio_formats
	juce::juce_dsp)

vekt_adhoc_seal_bundle(Flint_AU)
vekt_adhoc_seal_bundle(Flint_VST3)
vekt_adhoc_seal_bundle(Flint_Standalone)
