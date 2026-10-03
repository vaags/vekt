include_guard(GLOBAL)

# The arm64 linker ad-hoc signs at most the binary, never the bundle, so strict
# verification fails. JUCE 9.0.2 seals VST3 bundles before it writes
# moduleinfo.json, which then breaks the seal; it seals AU bundles only in its
# install/copy step, which Vekt leaves disabled, and never seals Standalone apps.
# Call this after juce_add_plugin so it runs after those steps. VektSealBundle
# ad-hoc signs only a bundle that fails strict verification and fails the build
# if it still fails afterwards (JUCE's checkBundleSigning.cmake verifies
# non-strictly and ignores signing errors). Release signing with an identity
# remains a separate approved step.
function(vekt_adhoc_seal_bundle target)
	if(NOT APPLE OR NOT TARGET "${target}")
		return()
	endif()
	add_custom_command(TARGET "${target}" POST_BUILD
		COMMAND "${CMAKE_COMMAND}"
			"-Dbundle=$<TARGET_BUNDLE_DIR:${target}>"
			-P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/VektSealBundle.cmake"
		VERBATIM)
endfunction()
