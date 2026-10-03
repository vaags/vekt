add_library(vekt_ui STATIC
	src/LevelMeter.cpp
	src/ModulationRing.cpp
	src/Oscilloscope.cpp
	src/Panel.cpp
	src/QualitySettings.cpp
	src/RotaryControl.cpp
	src/ScalableEditor.cpp
	src/VektLookAndFeel.cpp)
add_library(vekt::ui ALIAS vekt_ui)

target_include_directories(vekt_ui PUBLIC include)
target_link_libraries(vekt_ui PUBLIC
	vekt_project_options
	vekt::dsp
    vekt::juce_headers)
