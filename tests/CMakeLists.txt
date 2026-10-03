add_executable(vekt_dsp_tests
    TestMain.cpp
    dsp/DcBlockerTests.cpp
	dsp/DisplayHistoryTests.cpp
    dsp/LatencyAlignedBypassTests.cpp
    dsp/LatencyAlignedMixerTests.cpp
    dsp/MatchedToneStageTests.cpp
	dsp/MatchedToneStageRampTests.cpp
	dsp/ThreeBandCrossoverTests.cpp
    dsp/ControlTransitionTests.cpp
    dsp/RotorMotionTests.cpp
    dsp/RotaryEngineTests.cpp
    dsp/AutoSpeedDetectorTests.cpp
    dsp/RavModeStageTests.cpp
	dsp/RavPostStageTests.cpp
    dsp/OversamplingBankTests.cpp
	 dsp/StereoPeakMeterTests.cpp
	dsp/ScopeTapTests.cpp
    presets/PresetTests.cpp
    presets/FrameworkPresetTests.cpp
	presets/GlimmerPresetTests.cpp
	presets/FlintPresetTests.cpp
    processor/RavProcessorTests.cpp
	processor/RavStageChainTests.cpp
    processor/AdaptiveAutoGainTests.cpp
	processor/ParametersTests.cpp
    processor/GlimmerParametersTests.cpp
    processor/GlimmerProcessorTests.cpp
	processor/MonoParametersTests.cpp
	processor/MonoProcessorTests.cpp
	processor/MonoLfoTests.cpp
	processor/MonoLfoRoutingTests.cpp
	processor/MonoOscillatorTests.cpp
	processor/MonoWidthRendererComparisonTests.cpp
	processor/MonoUnisonTests.cpp
	processor/MonoReproducibilityTests.cpp
	processor/MonoLadderBatchTests.cpp
	processor/MonoLadderPoleMixTests.cpp
	processor/MonoLadderHighPassTests.cpp
	processor/MonoSvfTests.cpp
	processor/MonoNonlinearSvfTests.cpp
	processor/MonoKorg35Tests.cpp
	processor/MonoFilterTypeTests.cpp
	processor/MonoPrecisionTests.cpp
	processor/EditorLayoutTests.cpp
	processor/ValueFormatTests.cpp
	audio_lab/FileSourceTests.cpp
    audio_lab/LabSettingsTests.cpp
	audio_lab/RackRoutingTests.cpp
	audio_lab/MeasurementsTests.cpp
	audio_lab/MonoRenderTests.cpp
	../tools/audio_lab/MonoRender.cpp
	audio_lab/LadderPrototypeTests.cpp
	../tools/audio_lab/LadderPrototype.cpp
    dsp/TanhStageTests.cpp
    ui/StereoMeterBallisticsTests.cpp
    ui/LevelMeterTests.cpp
    ui/OscilloscopeTests.cpp
    ui/RotaryControlTests.cpp
	ui/QualitySettingsTests.cpp
	compat/StateFixtureTests.cpp
	compat/ReferenceRenderTests.cpp
	compat/RenderCorpusTests.cpp
	compat/ParameterManifestTests.cpp
	plugin_support/PluginSupportTests.cpp
	flint/AllocationCounter.cpp
	flint/AllocationCounterTests.cpp
	flint/FlintAuditionTests.cpp
	flint/DriveStageTests.cpp
	flint/FlintEngineHostTests.cpp
	flint/FlintProcessorTests.cpp
	flint/KickClassicAnalogTests.cpp
	flint/MalletBarTests.cpp
	flint/ModalBankTests.cpp
	flint/VariationTests.cpp)

target_link_libraries(vekt_dsp_tests PRIVATE
    vekt::dsp
	vekt::audio_analysis
    vekt::ui
	vekt::plugin_support
    VektRavCore
	VektGlimmerCore
	VektMonoCore
	FlintCore
    vekt::juce_headers
    vekt::juce_runtime
    Catch2::Catch2)

if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
    add_custom_command(TARGET vekt_dsp_tests POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - "$<TARGET_FILE:vekt_dsp_tests>"
        COMMENT "Ad-hoc signing vekt_dsp_tests for local execution"
        VERBATIM)
endif()

# Tests of the Audio Lab include its headers as <audio_lab/...>.
target_include_directories(vekt_dsp_tests PRIVATE "${PROJECT_SOURCE_DIR}/tools")

# Compatibility fixtures live in the source tree; failing renders are written to the build tree.
target_compile_definitions(vekt_dsp_tests PRIVATE
	VEKT_COMPAT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
	VEKT_COMPAT_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/compat-actual")

vekt_strict_warnings(vekt_dsp_tests)

# Every test translation unit parses the JUCE and Catch2 headers; precompile them once (docs/VERIFICATION_SPEED.md).
target_precompile_headers(vekt_dsp_tests PRIVATE
	<juce_audio_processors/juce_audio_processors.h>
	<juce_dsp/juce_dsp.h>
	<juce_gui_basics/juce_gui_basics.h>
	<catch2/catch_approx.hpp>
	<catch2/catch_test_macros.hpp>)
if(VEKT_USE_CCACHE AND VEKT_CCACHE)
	target_compile_options(vekt_dsp_tests PRIVATE -Xclang -fno-pch-timestamp) # cacheable by ccache
endif()

include(Catch)
catch_discover_tests(vekt_dsp_tests ADD_TAGS_AS_LABELS)

if(APPLE)
	add_test(NAME "macOS plugin workflow scripts"
		COMMAND ${CMAKE_COMMAND}
			-DVEKT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
			-DVEKT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
			-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PluginWorkflowTests.cmake")
	set_tests_properties("macOS plugin workflow scripts" PROPERTIES LABELS "cmake;scripts")
endif()

add_test(NAME "Every test case carries a product or framework tag"
	COMMAND ${CMAKE_COMMAND} -DVEKT_TESTS_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestTagPolicy.cmake")
set_tests_properties("Every test case carries a product or framework tag" PROPERTIES LABELS "cmake;scripts;compat")

function(add_factory_preset_configure_test name fixtureCase)
    add_test(NAME "Factory preset configure: ${name}"
        COMMAND ${CMAKE_COMMAND}
            -S "${CMAKE_CURRENT_SOURCE_DIR}/cmake/factory_presets"
            -B "${CMAKE_CURRENT_BINARY_DIR}/cmake/factory_presets/${fixtureCase}"
            -DVEKT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DCASE=${fixtureCase})
    set_tests_properties("Factory preset configure: ${name}" PROPERTIES LABELS "cmake;presets;scripts")
endfunction()

add_factory_preset_configure_test("valid manifest" valid)
add_factory_preset_configure_test("missing locked entry" missing-lock-entry)
set_tests_properties("Factory preset configure: missing locked entry" PROPERTIES WILL_FAIL TRUE)
add_factory_preset_configure_test("reordered locked entry" reordered-lock-entry)
set_tests_properties("Factory preset configure: reordered locked entry" PROPERTIES WILL_FAIL TRUE)
add_factory_preset_configure_test("unsafe discovered path" unsafe-discovered-path)
set_tests_properties("Factory preset configure: unsafe discovered path" PROPERTIES WILL_FAIL TRUE)
