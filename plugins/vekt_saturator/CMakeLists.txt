set(vekt_saturator_formats Standalone VST3)

if(CMAKE_GENERATOR STREQUAL "Xcode")
	list(APPEND vekt_saturator_formats AUv3)
endif()

juce_add_plugin(VektSaturator
	VERSION ${PROJECT_VERSION}
	COMPANY_NAME Vekt
	BUNDLE_ID com.vekt.rav
	PLUGIN_MANUFACTURER_CODE Vekt
	PLUGIN_CODE Ravv
	FORMATS ${vekt_saturator_formats}
	PRODUCT_NAME "Vekt Rav"
	DESCRIPTION "A six-mode multiband saturation effect"
	IS_SYNTH FALSE
	NEEDS_MIDI_INPUT FALSE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	AU_MAIN_TYPE kAudioUnitType_Effect
	NEEDS_WEB_BROWSER FALSE
	NEEDS_CURL FALSE
	HARDENED_RUNTIME_ENABLED TRUE)

juce_add_binary_data(VektSaturatorFactoryData
	NAMESPACE VektSaturatorFactoryData
	SOURCES
		"Resources/Presets/Clean Heat.vektpreset"
		"Resources/Presets/Warm Push.vektpreset"
		"Resources/Presets/Parallel Grit.vektpreset")

target_sources(VektSaturator PRIVATE
	Source/FactoryPresets.cpp
	Source/Parameters.cpp
	Source/PluginEditor.cpp
	Source/PluginProcessor.cpp
	Source/UserPresetPaths.cpp)

target_include_directories(VektSaturator PUBLIC Source)

target_compile_definitions(VektSaturator PUBLIC
	VEKT_AUV3_APP_GROUP_ID="${VEKT_AUV3_APP_GROUP_ID}"
	JUCE_STRICT_REFCOUNTEDPOINTER=1
	JUCE_WEB_BROWSER=0
	JUCE_USE_CURL=0
	JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VektSaturator
	PRIVATE
		juce::juce_audio_utils
		vekt::ui
		VektSaturatorFactoryData
	PUBLIC
		vekt::dsp
		vekt::presets
		vekt::state
		vekt_project_options)
