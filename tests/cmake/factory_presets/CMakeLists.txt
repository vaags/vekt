cmake_minimum_required(VERSION 3.25)

project(VektFactoryPresetFixture LANGUAGES NONE)

if(NOT DEFINED VEKT_SOURCE_DIR)
	message(FATAL_ERROR "VEKT_SOURCE_DIR is required")
endif()

function(juce_add_binary_data target)
	add_library(${target} INTERFACE)
endfunction()

include("${VEKT_SOURCE_DIR}/cmake/VektFactoryPresets.cmake")

set(presetRoot "${CMAKE_BINARY_DIR}/Presets")
file(MAKE_DIRECTORY "${presetRoot}/Bass")
file(WRITE "${presetRoot}/Bass/Alpha.vektpreset" "{}")
file(WRITE "${presetRoot}/Bass/Beta.vektpreset" "{}")
set(orderFile "${CMAKE_BINARY_DIR}/factory-order.txt")
set(lockFile "${CMAKE_BINARY_DIR}/factory-order.lock")

if(CASE STREQUAL "missing-lock-entry")
	file(WRITE "${orderFile}" "Bass/Alpha.vektpreset\n")
	file(WRITE "${lockFile}" "Bass/Alpha.vektpreset\nBass/Beta.vektpreset\n")
elseif(CASE STREQUAL "reordered-lock-entry")
	file(WRITE "${orderFile}" "Bass/Beta.vektpreset\nBass/Alpha.vektpreset\n")
	file(WRITE "${lockFile}" "Bass/Alpha.vektpreset\nBass/Beta.vektpreset\n")
elseif(CASE STREQUAL "unsafe-discovered-path")
	file(MAKE_DIRECTORY "${presetRoot}/Unsafe\"Folder")
	file(WRITE "${presetRoot}/Unsafe\"Folder/Unsafe.vektpreset" "{}")
	file(WRITE "${orderFile}" "Bass/Alpha.vektpreset\nBass/Beta.vektpreset\n")
	file(WRITE "${lockFile}" "Bass/Alpha.vektpreset\nBass/Beta.vektpreset\n")
elseif(CASE STREQUAL "valid")
	file(WRITE "${orderFile}" "Bass/Alpha.vektpreset\nBass/Beta.vektpreset\n")
	file(WRITE "${lockFile}" "Bass/Alpha.vektpreset\nBass/Beta.vektpreset\n")
else()
	message(FATAL_ERROR "Unknown fixture case: ${CASE}")
endif()

vekt_add_factory_presets(
	TARGET FixtureFactoryData
	NAMESPACE FixtureFactoryData
	HEADER_NAME FixtureFactoryData.h
	PRESET_ROOT "${presetRoot}"
	ORDER_FILE "${orderFile}"
	LOCK_FILE "${lockFile}"
	MANIFEST_HEADER FixtureFactoryPresetManifest.h
	MANIFEST_NAMESPACE fixture::factory_presets)