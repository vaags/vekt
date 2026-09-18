add_library(vekt_ui STATIC
	src/LevelMeter.cpp
	src/Panel.cpp
	src/RotaryControl.cpp
	src/ScalableEditor.cpp
	src/VektLookAndFeel.cpp)
add_library(vekt::ui ALIAS vekt_ui)

target_include_directories(vekt_ui PUBLIC include)
target_link_libraries(vekt_ui PUBLIC
	vekt_project_options
	juce::juce_audio_basics
	juce::juce_gui_basics)
