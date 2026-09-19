juce_add_binary_data(VektRavFactoryData
	NAMESPACE VektRavFactoryData
	SOURCES
		"Resources/Presets/Clean Heat.vektpreset"
		"Resources/Presets/Warm Push.vektpreset"
		"Resources/Presets/Parallel Grit.vektpreset"
		"Resources/Presets/Bass/Deep Foundation.vektpreset"
		"Resources/Presets/Bass/Grind Line.vektpreset"
		"Resources/Presets/Bass/Parallel Punch.vektpreset"
		"Resources/Presets/Guitar/Edge Breakup.vektpreset"
		"Resources/Presets/Guitar/Lead Bite.vektpreset"
		"Resources/Presets/Guitar/Velvet Drive.vektpreset"
		"Resources/Presets/Keys/Analog Glow.vektpreset"
		"Resources/Presets/Keys/Crushed Chords.vektpreset"
		"Resources/Presets/Keys/Velvet Electric.vektpreset"
		"Resources/Presets/Drums/Drum Bus Glue.vektpreset"
		"Resources/Presets/Drums/Snare Crack.vektpreset"
		"Resources/Presets/Drums/Transient Smash.vektpreset"
		"Resources/Presets/Mastering/Low End Focus.vektpreset"
		"Resources/Presets/Mastering/Parallel Sheen.vektpreset"
		"Resources/Presets/Mastering/Transparent Polish.vektpreset"
		"Resources/Presets/Bass/Circuit Grind.vektpreset"
		"Resources/Presets/Guitar/Circuit Sustain.vektpreset"
		"Resources/Presets/Keys/Circuit Bloom.vektpreset"
		"Resources/Presets/Drums/Circuit Parallel.vektpreset")

add_library(VektRavCore STATIC
	Source/FactoryPresets.cpp
	Source/Parameters.cpp
	Source/PluginEditor.cpp
	Source/PluginProcessor.cpp
	Source/UserPresetPaths.cpp)

target_include_directories(VektRavCore PUBLIC Source)

target_compile_definitions(VektRavCore PUBLIC
	VEKT_AUV3_APP_GROUP_ID="${VEKT_AUV3_APP_GROUP_ID}"
	JUCE_STRICT_REFCOUNTEDPOINTER=1
	JUCE_WEB_BROWSER=0
	JUCE_USE_CURL=0
	JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VektRavCore
	PUBLIC
		juce::juce_audio_utils
		vekt::ui
		VektRavFactoryData
		vekt::dsp
		vekt::presets
		vekt::preset_ui
		vekt::state
		vekt_project_options)

set(vekt_rav_formats Standalone VST3)

if(CMAKE_GENERATOR STREQUAL "Xcode")
	list(APPEND vekt_rav_formats AUv3)
endif()

juce_add_plugin(VektRav
	VERSION ${PROJECT_VERSION}
	COMPANY_NAME Vekt
	BUNDLE_ID com.vekt.rav
	PLUGIN_MANUFACTURER_CODE Vekt
	PLUGIN_CODE Ravv
	FORMATS ${vekt_rav_formats}
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

target_sources(VektRav PRIVATE Source/PluginEntryPoint.cpp)
target_link_libraries(VektRav PRIVATE VektRavCore)
