add_executable(vekt_dsp_tests
    dsp/DcBlockerTests.cpp
    dsp/LatencyAlignedBypassTests.cpp
    dsp/LatencyAlignedMixerTests.cpp
    dsp/MatchedToneStageTests.cpp
	dsp/MatchedToneStageRampTests.cpp
	dsp/ThreeBandCrossoverTests.cpp
    dsp/ControlTransitionTests.cpp
    dsp/RavModeStageTests.cpp
	dsp/RavPostStageTests.cpp
    dsp/OversamplingBankTests.cpp
	 dsp/StereoPeakMeterTests.cpp
    presets/PresetTests.cpp
    processor/RavProcessorTests.cpp
    processor/AdaptiveAutoGainTests.cpp
    dsp/TanhStageTests.cpp)

target_link_libraries(vekt_dsp_tests PRIVATE
    vekt::dsp
    VektRav
    Catch2::Catch2WithMain)

if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
    add_custom_command(TARGET vekt_dsp_tests POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - "$<TARGET_FILE:vekt_dsp_tests>"
        COMMENT "Ad-hoc signing vekt_dsp_tests for local execution"
        VERBATIM)
endif()

include(Catch)
catch_discover_tests(vekt_dsp_tests)
