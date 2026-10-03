#pragma once

#include <vekt/dsp/OversamplingChoices.h>

#include <array>

// The Tracking Oversampling choices (vekt::dsp::trackingQualityChoices()) Mono's processor tests sweep: Off, 2x IIR,
// 4x FIR, 8x FIR and 16x FIR, each factor with the filter Mono used before the choices became shared (ADR 0010).
inline constexpr std::array monoQualitySweep { 0.0f, 1.0f, 4.0f, 5.0f, 6.0f };
