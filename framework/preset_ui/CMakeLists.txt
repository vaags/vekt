add_library(vekt_preset_ui STATIC src/PresetBrowser.cpp)
add_library(vekt::preset_ui ALIAS vekt_preset_ui)
target_include_directories(vekt_preset_ui PUBLIC include)
target_link_libraries(vekt_preset_ui PUBLIC vekt::presets vekt::ui)