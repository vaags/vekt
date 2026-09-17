#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <memory>

namespace vekt::dsp
{
template <typename Sample>
class LatencyAlignedMixer final
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec, int maximumWetLatencySamples)
    {
        maximumLatency = std::max(0, maximumWetLatencySamples);
        mixer = std::make_unique<juce::dsp::DryWetMixer<Sample>>(maximumLatency);
        mixer->setMixingRule(juce::dsp::DryWetMixingRule::linear);
        mixer->prepare(spec);
        mixer->setWetMixProportion(wetProportion);
        mixer->setWetLatency(static_cast<Sample>(wetLatency));
    }

    void reset()
    {
        if (mixer != nullptr)
            mixer->reset();
    }

    void setWetProportion(Sample newWetProportion)
    {
        wetProportion = std::clamp(newWetProportion, Sample {}, Sample { 1 });

        if (mixer != nullptr)
            mixer->setWetMixProportion(wetProportion);
    }

    void setRampLength(double seconds)
    {
        if (mixer != nullptr)
            mixer->setRampLength(juce::Seconds { seconds });
    }

    void setWetLatency(int newWetLatencySamples)
    {
        wetLatency = std::clamp(newWetLatencySamples, 0, maximumLatency);

        if (mixer != nullptr)
            mixer->setWetLatency(static_cast<Sample>(wetLatency));
    }

    [[nodiscard]] int getWetLatency() const noexcept
    {
        return wetLatency;
    }

    void pushDrySamples(const juce::dsp::AudioBlock<const Sample>& drySamples)
    {
        jassert(mixer != nullptr);
        mixer->pushDrySamples(drySamples);
    }

    void mixWetSamples(juce::dsp::AudioBlock<Sample> wetSamples)
    {
        jassert(mixer != nullptr);
        mixer->mixWetSamples(wetSamples);
    }

private:
    std::unique_ptr<juce::dsp::DryWetMixer<Sample>> mixer;
    Sample wetProportion { 1 };
    int wetLatency {};
    int maximumLatency {};
};
}
