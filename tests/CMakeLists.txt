add_executable(vekt_dsp_tests
    dsp/DcBlockerTests.cpp
    dsp/LatencyAlignedBypassTests.cpp
    dsp/LatencyAlignedMixerTests.cpp
    dsp/MatchedToneStageTests.cpp
	dsp/MatchedToneStageRampTests.cpp
	dsp/ThreeBandCrossoverTests.cpp
    dsp/ControlTransitionTests.cpp
    dsp/RotorMotionTests.cpp
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
	processor/EditorLayoutTests.cpp
	processor/ValueFormatTests.cpp
	audio_lab/FileSourceTests.cpp
    dsp/TanhStageTests.cpp)

target_link_libraries(vekt_dsp_tests PRIVATE
    vekt::dsp
    vekt::ui
    VektRavCore
	VektGlimmerCore
	juce::juce_audio_utils
    Catch2::Catch2WithMain)

if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
    add_custom_command(TARGET vekt_dsp_tests POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - "$<TARGET_FILE:vekt_dsp_tests>"
        COMMENT "Ad-hoc signing vekt_dsp_tests for local execution"
        VERBATIM)
endif()

include(Catch)
catch_discover_tests(vekt_dsp_tests)
