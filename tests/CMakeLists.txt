add_executable(vekt_dsp_tests
    dsp/DcBlockerTests.cpp
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
    presets/PresetTests.cpp
    presets/FrameworkPresetTests.cpp
	presets/GlimmerPresetTests.cpp
    processor/RavProcessorTests.cpp
	processor/RavStageChainTests.cpp
    processor/AdaptiveAutoGainTests.cpp
	processor/ParametersTests.cpp
    processor/GlimmerParametersTests.cpp
    processor/GlimmerProcessorTests.cpp
	processor/MonoParametersTests.cpp
	processor/MonoProcessorTests.cpp
	processor/EditorLayoutTests.cpp
	processor/ValueFormatTests.cpp
	audio_lab/FileSourceTests.cpp
    audio_lab/LabSettingsTests.cpp
    dsp/TanhStageTests.cpp
    ui/StereoMeterBallisticsTests.cpp
    ui/LevelMeterTests.cpp)

target_link_libraries(vekt_dsp_tests PRIVATE
    vekt::dsp
    vekt::ui
    VektRavCore
	VektGlimmerCore
	VektMonoCore
    vekt::juce_headers
    vekt::juce_runtime
    Catch2::Catch2WithMain)

if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
    add_custom_command(TARGET vekt_dsp_tests POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - "$<TARGET_FILE:vekt_dsp_tests>"
        COMMENT "Ad-hoc signing vekt_dsp_tests for local execution"
        VERBATIM)
endif()

include(Catch)
catch_discover_tests(vekt_dsp_tests)

function(add_factory_preset_configure_test name fixtureCase)
    add_test(NAME "Factory preset configure: ${name}"
        COMMAND ${CMAKE_COMMAND}
            -S "${CMAKE_CURRENT_SOURCE_DIR}/cmake/factory_presets"
            -B "${CMAKE_CURRENT_BINARY_DIR}/cmake/factory_presets/${fixtureCase}"
            -DVEKT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DCASE=${fixtureCase})
endfunction()

add_factory_preset_configure_test("valid manifest" valid)
add_factory_preset_configure_test("missing locked entry" missing-lock-entry)
set_tests_properties("Factory preset configure: missing locked entry" PROPERTIES WILL_FAIL TRUE)
add_factory_preset_configure_test("reordered locked entry" reordered-lock-entry)
set_tests_properties("Factory preset configure: reordered locked entry" PROPERTIES WILL_FAIL TRUE)
add_factory_preset_configure_test("unsafe discovered path" unsafe-discovered-path)
set_tests_properties("Factory preset configure: unsafe discovered path" PROPERTIES WILL_FAIL TRUE)
