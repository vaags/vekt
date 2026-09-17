set(vekt_saturator_formats Standalone VST3)

if(CMAKE_GENERATOR STREQUAL "Xcode")
	list(APPEND vekt_saturator_formats AUv3)
endif()

juce_add_plugin(VektSaturator
	VERSION ${PROJECT_VERSION}
	COMPANY_NAME Vekt
	BUNDLE_ID com.vekt.saturator
	PLUGIN_MANUFACTURER_CODE Vekt
	PLUGIN_CODE Vsat
	FORMATS ${vekt_saturator_formats}
	PRODUCT_NAME "Vekt Saturator"
	DESCRIPTION "A quality-first tanh saturation effect"
	IS_SYNTH FALSE
	NEEDS_MIDI_INPUT FALSE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	AU_MAIN_TYPE kAudioUnitType_Effect
	NEEDS_WEB_BROWSER FALSE
	NEEDS_CURL FALSE
	HARDENED_RUNTIME_ENABLED TRUE)

target_sources(VektSaturator PRIVATE
	Source/Parameters.cpp
	Source/PluginProcessor.cpp)

target_include_directories(VektSaturator PUBLIC Source)

target_compile_definitions(VektSaturator PUBLIC
	JUCE_WEB_BROWSER=0
	JUCE_USE_CURL=0
	JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VektSaturator
	PRIVATE
		vekt::dsp
		juce::juce_audio_utils
	PUBLIC
		vekt_project_options)
