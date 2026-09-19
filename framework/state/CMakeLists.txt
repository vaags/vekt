add_library(vekt_state INTERFACE)
add_library(vekt::state ALIAS vekt_state)

target_include_directories(vekt_state INTERFACE include)
target_link_libraries(vekt_state INTERFACE
	vekt_project_options
    vekt::juce_headers)
