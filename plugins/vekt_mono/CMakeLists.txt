juce_add_binary_data(VektMonoFactoryData
	NAMESPACE VektMonoFactoryData
	HEADER_NAME MonoBinaryData.h
	SOURCES
		"Resources/Presets/Lead/Aurora Lead.vektpreset"
		"Resources/Presets/Lead/Circuit Lead.vektpreset"
		"Resources/Presets/Lead/Glass Lead.vektpreset"
		"Resources/Presets/Lead/Voyager Lead.vektpreset"
		"Resources/Presets/Bass/Foundation Bass.vektpreset"
		"Resources/Presets/Bass/Rubber Bass.vektpreset"
		"Resources/Presets/Bass/Steel Bass.vektpreset"
		"Resources/Presets/Bass/Subway Bass.vektpreset"
		"Resources/Presets/Pad/Amber Pad.vektpreset"
		"Resources/Presets/Pad/Horizon Pad.vektpreset"
		"Resources/Presets/Pad/Nocturne Pad.vektpreset"
		"Resources/Presets/Pad/Vapor Pad.vektpreset"
		"Resources/Presets/Pluck/Copper Pluck.vektpreset"
		"Resources/Presets/Pluck/Ember Pluck.vektpreset"
		"Resources/Presets/Pluck/Needle Pluck.vektpreset"
		"Resources/Presets/Pluck/Slate Pluck.vektpreset"
		"Resources/Presets/Motion/Current Motion.vektpreset"
		"Resources/Presets/Motion/Orbit Motion.vektpreset"
		"Resources/Presets/Motion/Pulse Motion.vektpreset"
		"Resources/Presets/Motion/Tide Motion.vektpreset"
		"Resources/Presets/FX/Comet FX.vektpreset"
		"Resources/Presets/FX/Entropy FX.vektpreset"
		"Resources/Presets/FX/Polaris FX.vektpreset"
		"Resources/Presets/FX/Transmission FX.vektpreset")

add_library(VektMonoCore STATIC
	Source/FactoryPresets.cpp
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
target_link_libraries(VektMono PRIVATE VektMonoCore)