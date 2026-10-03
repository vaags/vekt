#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace vekt::dsp
{
// The linear parameter ramp every per-sample smoother uses. Double whatever the audio type: at high internal rates a
// float ramp's step falls below rounding, so it holds still and then jumps at its end (ARCHITECTURE.md, DSP Contracts).
using LinearRamp = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear>;
}
