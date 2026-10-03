# Warnings as errors for targets compiled from Vekt sources (docs/CODING_STANDARDS.md). JUCE's own module sources and
# third-party code are excluded: the flag is added PRIVATE to our targets only, never through vekt_project_options,
# which also reaches the targets that compile JUCE. On in the development presets, off for release builds.
option(VEKT_WARNINGS_AS_ERRORS "Treat compiler warnings in Vekt sources as errors" OFF)

function(vekt_strict_warnings)
	if(NOT VEKT_WARNINGS_AS_ERRORS)
		return()
	endif()
	foreach(target IN LISTS ARGN)
		if(TARGET ${target})
			target_compile_options(${target} PRIVATE -Werror)
		endif()
	endforeach()
endfunction()

# For targets that also compile JUCE's module sources (the juce_add_plugin shared code): only the given sources of
# <target> are strict.
function(vekt_strict_warnings_on_sources target)
	if(NOT VEKT_WARNINGS_AS_ERRORS)
		return()
	endif()
	set_source_files_properties(${ARGN} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS -Werror)
endfunction()
