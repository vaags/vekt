# Usage: cmake -Dbundle=<path> -P VektSealBundle.cmake
# Ad-hoc seals a plugin bundle that fails strict verification and fails the build if it still fails afterwards. A bundle
# that already verifies is left untouched, so an identity signature is never replaced.
if(NOT bundle)
	message(FATAL_ERROR "VektSealBundle.cmake: pass -Dbundle=<path>")
endif()
find_program(VEKT_CODESIGN codesign PATHS /usr/bin NO_DEFAULT_PATH)
if(NOT VEKT_CODESIGN)
	message(FATAL_ERROR "codesign not found: cannot seal ${bundle}")
endif()

execute_process(COMMAND "${VEKT_CODESIGN}" --verify --deep --strict "${bundle}"
	RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0)
	return()
endif()

message(STATUS "Ad-hoc sealing ${bundle}")
execute_process(COMMAND "${VEKT_CODESIGN}" --force --sign - "${bundle}" RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "Ad-hoc sealing failed for ${bundle}:\n${error}")
endif()
execute_process(COMMAND "${VEKT_CODESIGN}" --verify --deep --strict "${bundle}" RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "${bundle} still fails strict verification after sealing:\n${error}")
endif()
