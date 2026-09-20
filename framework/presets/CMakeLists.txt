add_library(vekt_presets STATIC
	src/EmbeddedFactoryPresets.cpp
	src/FilePresetRepository.cpp
	src/PresetCatalog.cpp
	src/PresetJsonCodec.cpp
	src/PresetSchema.cpp)
add_library(vekt::presets ALIAS vekt_presets)

target_include_directories(vekt_presets PUBLIC include)
target_link_libraries(vekt_presets PUBLIC
	vekt_project_options
    vekt::juce_headers)
