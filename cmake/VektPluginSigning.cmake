include_guard(GLOBAL)

# The linker signs only the AU binary, leaving the bundle unsealed so strict
# verification fails. JUCE seals VST3 bundles after writing moduleinfo.json but
# applies the same step to other formats only with its install/copy step, which
# Vekt leaves disabled. Reuse JUCE's script: it ad-hoc signs only an invalid
# bundle. Release signing with an identity remains a separate approved step.
function(vekt_adhoc_seal_bundle target)
	if(NOT APPLE OR NOT TARGET "${target}")
		return()
	endif()
	add_custom_command(TARGET "${target}" POST_BUILD
		COMMAND "${CMAKE_COMMAND}"
			"-Dsrc=$<TARGET_BUNDLE_DIR:${target}>"
			-P "${JUCE_CMAKE_UTILS_DIR}/checkBundleSigning.cmake"
		VERBATIM)
endfunction()
