#include <vekt/plugin_support/ChoiceTable.h>
#include <vekt/plugin_support/PresetHost.h>
#include <vekt/plugin_support/QualitySelection.h>
#include <vekt/presets/PresetSchema.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

namespace
{
constexpr auto product = "test.host";
constexpr std::array soundParameterIds { "gain" };

// The smallest processor that can own an APVTS: one sound parameter and the two quality choices.
class TestProcessor final : public juce::AudioProcessor
{
public:
	TestProcessor()
		: parameters(*this, nullptr, "TestState", layout()),
		  presets(parameters, { product, "Test", 1 }, {
			  [this](const juce::String& name)
			  {
				  return vekt::presets::PresetSchema::create(product, name, parameters, soundParameterIds);
			  },
			  [](vekt::presets::Preset&) { return juce::Result::ok(); },
			  [this](const vekt::presets::Preset& preset)
			  {
				  return vekt::presets::PresetSchema::validate(preset, product, parameters, soundParameterIds);
			  },
			  [this](const vekt::presets::Preset& preset)
			  {
				  const auto result = vekt::presets::PresetSchema::apply(preset, product, parameters, soundParameterIds);
				  if (result.wasOk()) presets.session().clear();
				  return result;
			  },
			  [this](const vekt::presets::Preset& preset)
			  {
				  return vekt::presets::PresetSchema::matches(preset, product, parameters, soundParameterIds);
			  } })
	{
		for (const auto* json : {
				 R"({"format":"vekt.preset","schemaVersion":2,"id":"test-first","product":"test.host","soundSchemaVersion":1,"name":"First","tags":[],"parameters":{"gain":0.25}})",
				 R"({"format":"vekt.preset","schemaVersion":2,"id":"test-second","product":"test.host","soundSchemaVersion":1,"name":"Second","tags":[],"parameters":{"gain":0.75}})" })
			REQUIRE(presets.catalog().addFactoryPreset(json).wasOk());
	}

	void setValue(const char* identifier, float value)
	{
		auto* parameter = parameters.getParameter(identifier);
		REQUIRE(parameter != nullptr);
		parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
	}
	[[nodiscard]] float value(const char* identifier) const { return parameters.getRawParameterValue(identifier)->load(); }

	const juce::String getName() const override { return "Test"; }
	void prepareToPlay(double, int) override {}
	void releaseResources() override {}
	void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
	double getTailLengthSeconds() const override { return 0.0; }
	bool acceptsMidi() const override { return false; }
	bool producesMidi() const override { return false; }
	juce::AudioProcessorEditor* createEditor() override { return nullptr; }
	bool hasEditor() const override { return false; }
	int getNumPrograms() override { return presets.numPrograms(); }
	int getCurrentProgram() override { return presets.currentProgram(); }
	void setCurrentProgram(int index) override { presets.selectProgram(index); }
	const juce::String getProgramName(int index) override { return presets.programName(index); }
	void changeProgramName(int, const juce::String&) override {}
	void getStateInformation(juce::MemoryBlock& destination) override { presets.save(destination); }
	void setStateInformation(const void* data, int size) override { juce::ignoreUnused(presets.restore(data, size)); }

	juce::AudioProcessorValueTreeState parameters;
	vekt::plugin_support::PresetHost presets;

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout layout()
	{
		// Tracking defaults to 2x IIR, Offline to 4x FIR.
		return {
			std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 0.5f),
			vekt::plugin_support::QualitySelection::makeTrackingParameter("tracking", 1, 1),
			vekt::plugin_support::QualitySelection::makeOfflineParameter("offline", 1, 2)
		};
	}
};

struct Directory
{
	juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
		.getChildFile("vekt-plugin-support-" + juce::Uuid().toString()); // unique across parallel test processes
	~Directory() { root.deleteRecursively(); }
};

std::string text(const juce::MemoryBlock& block)
{
	return { static_cast<const char*>(block.getData()), block.getSize() };
}
}

TEST_CASE("Preset hosts expose the factory presets as the host program bank", "[plugin-support][presets]")
{
	Directory directory;
	TestProcessor processor;
	REQUIRE(processor.presets.configureUserPresetDirectory(directory.root).wasOk());
	REQUIRE(processor.getNumPrograms() == 2);
	REQUIRE(processor.getProgramName(1) == "Second");
	REQUIRE(processor.getProgramName(2).isEmpty());
	REQUIRE(processor.getProgramName(-1).isEmpty());
	REQUIRE(processor.getCurrentProgram() == 0);

	processor.setCurrentProgram(1);
	REQUIRE(processor.getCurrentProgram() == 1);
	REQUIRE(processor.value("gain") == Catch::Approx(0.75f));
	processor.setCurrentProgram(2);
	processor.setCurrentProgram(-1);
	REQUIRE(processor.getCurrentProgram() == 1);

	// A user preset is not in the bank.
	processor.setValue("gain", 0.1f);
	REQUIRE(processor.presets.session().save("Mine", {}, {}).wasOk());
	REQUIRE(processor.getCurrentProgram() == 0);
	REQUIRE(processor.getNumPrograms() == 2);
}

TEST_CASE("Preset hosts step through the catalog and wrap", "[plugin-support][presets]")
{
	TestProcessor processor;
	REQUIRE(processor.presets.loadAdjacentPreset(false).wasOk());
	REQUIRE(processor.getCurrentProgram() == 1);
	REQUIRE(processor.presets.loadAdjacentPreset(true).wasOk());
	REQUIRE(processor.getCurrentProgram() == 0);
	REQUIRE(processor.presets.loadAdjacentPreset(false).wasOk());
	REQUIRE(processor.getCurrentProgram() == 1);

	TestProcessor fresh;
	REQUIRE(fresh.presets.loadAdjacentPreset(true).wasOk());
	REQUIRE(fresh.getCurrentProgram() == 0);
	REQUIRE(fresh.value("gain") == Catch::Approx(0.25f));
}

TEST_CASE("Preset hosts restore the selection and drop the legacy factory key", "[plugin-support][presets][state]")
{
	TestProcessor source;
	source.setCurrentProgram(1);
	source.setValue("gain", 0.5f);
	source.presets.setMetadataValue(vekt::plugin_support::PresetHost::legacyFactoryPresetProperty, "First");
	source.presets.setMetadataValue("editorWidth", 900);
	juce::MemoryBlock state;
	source.getStateInformation(state);
	REQUIRE(text(state).find("currentFactoryPreset") != std::string::npos); // saved as an older project would be

	TestProcessor restored;
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(restored.getCurrentProgram() == 1);
	REQUIRE(restored.presets.session().modified());
	REQUIRE(restored.value("gain") == Catch::Approx(0.5f));
	REQUIRE(static_cast<int>(restored.presets.metadataValue("editorWidth")) == 900);
	REQUIRE_FALSE(restored.presets.metadataCopy().hasProperty(vekt::plugin_support::PresetHost::legacyFactoryPresetProperty));
	juce::MemoryBlock resaved;
	restored.getStateInformation(resaved);
	REQUIRE(text(resaved).find("currentFactoryPreset") == std::string::npos);
}

TEST_CASE("Preset hosts ignore the legacy factory key when a project has no selection", "[plugin-support][presets][state]")
{
	// A project that names a factory preset only in the legacy key selects nothing (ADR 0010).
	const std::string legacy = R"({"format":"vekt.project","product":"test.host","schemaVersion":1,)"
		R"("parameters":{"gain":0.75},"metadata":{"currentFactoryPreset":"Second"}})";
	TestProcessor processor;
	processor.setCurrentProgram(1);
	REQUIRE(processor.presets.session().loaded().has_value());
	REQUIRE(processor.presets.restore(legacy.data(), static_cast<int>(legacy.size())));
	REQUIRE_FALSE(processor.presets.session().loaded().has_value());
	REQUIRE(processor.getCurrentProgram() == 0);
	REQUIRE(processor.value("gain") == Catch::Approx(0.75f));
	REQUIRE_FALSE(processor.presets.metadataCopy().hasProperty(vekt::plugin_support::PresetHost::legacyFactoryPresetProperty));
}

TEST_CASE("Preset hosts leave everything unchanged when a project cannot be restored", "[plugin-support][presets][state]")
{
	TestProcessor processor;
	processor.setCurrentProgram(1);
	processor.presets.setMetadataValue("editorWidth", 900);
	const std::string foreign = R"({"format":"vekt.project","product":"other","schemaVersion":1,"parameters":{}})";
	REQUIRE_FALSE(processor.presets.restore(foreign.data(), static_cast<int>(foreign.size())));
	REQUIRE_FALSE(processor.presets.restore(nullptr, 0));
	REQUIRE(processor.getCurrentProgram() == 1);
	REQUIRE(processor.value("gain") == Catch::Approx(0.75f));
	REQUIRE(static_cast<int>(processor.presets.metadataValue("editorWidth")) == 900);
	REQUIRE(processor.presets.configureUserPresetDirectory({}).failed());
}

TEST_CASE("Quality selections apply a request at the next block once prepared", "[plugin-support][quality]")
{
	using vekt::dsp::OversamplingFactor;
	TestProcessor processor;
	vekt::plugin_support::QualitySelection quality(processor.parameters, "tracking", "offline");
	REQUIRE(quality.active().factor == OversamplingFactor::x2);

	// Before prepare a request waits; prepare picks the quality for the processing mode and drops the request.
	processor.setValue("tracking", 0.0f);
	REQUIRE(quality.pending());
	REQUIRE_FALSE(quality.takeRequest(false).has_value());
	REQUIRE(quality.prepare(true).factor == OversamplingFactor::x4);
	REQUIRE(quality.active().factor == OversamplingFactor::x4);
	REQUIRE(quality.prepare(false).factor == OversamplingFactor::off);
	REQUIRE_FALSE(quality.pending());
	REQUIRE_FALSE(quality.takeRequest(false).has_value());

	// Each request is taken once, at the next block; a later request is not lost.
	processor.setValue("tracking", 1.0f);
	REQUIRE(quality.pending());
	const auto taken = quality.takeRequest(false);
	REQUIRE(taken.has_value());
	REQUIRE(taken->factor == OversamplingFactor::x2);
	REQUIRE(quality.active().factor == OversamplingFactor::x2);
	REQUIRE_FALSE(quality.pending());
	REQUIRE_FALSE(quality.takeRequest(false).has_value());
	processor.setValue("tracking", 2.0f);
	REQUIRE(quality.takeRequest(false)->factor == OversamplingFactor::x4);

	// A request for the active quality, or for the other processing mode, changes nothing.
	processor.setValue("offline", 1.0f);
	REQUIRE_FALSE(quality.takeRequest(false).has_value());
	REQUIRE(quality.active().factor == OversamplingFactor::x4);

	// After release requests wait for the next prepare.
	quality.release();
	processor.setValue("tracking", 0.0f);
	REQUIRE_FALSE(quality.takeRequest(false).has_value());
	REQUIRE(quality.pending());
}

TEST_CASE("Choice tables name every value and decode to the nearest choice, clamped", "[plugin-support]")
{
	enum class Speed { slow, fast, automatic };
	using vekt::plugin_support::Choice;
	static constexpr vekt::plugin_support::ChoiceTable speeds { Choice { Speed::slow, "Slow" }, Choice { Speed::fast, "Fast" },
		Choice { Speed::automatic, "Auto" } };
	static_assert(speeds.size() == 3);

	REQUIRE(speeds.names() == juce::StringArray { "Slow", "Fast", "Auto" });
	REQUIRE(speeds.at(0.0f) == Speed::slow);
	REQUIRE(speeds.at(1.0f) == Speed::fast);
	REQUIRE(speeds.at(2.0f) == Speed::automatic);
	REQUIRE(speeds.at(1.4f) == Speed::fast); // nearest choice
	REQUIRE(speeds.at(0.6f) == Speed::fast);
	REQUIRE(speeds.at(-1.0f) == Speed::slow); // clamped to the table
	REQUIRE(speeds.at(3.0f) == Speed::automatic);
	REQUIRE(speeds.at(1.0e9f) == Speed::automatic);
	REQUIRE(speeds.indexOf(Speed::automatic) == 2);
	REQUIRE(juce::String(speeds.nameOf(Speed::fast)) == "Fast");

	// Values need not be enums: counts and offsets decode the same way.
	static constexpr vekt::plugin_support::ChoiceTable counts { Choice { 1, "1x" }, Choice { 2, "2x" }, Choice { 4, "4x" } };
	REQUIRE(counts.at(2.0f) == 4);
	REQUIRE(counts[1].value == 2);
}

TEST_CASE("Shared quality choices name the quality they select", "[plugin-support][quality]")
{
	// Editors label the active quality with qualityName, so every choice must map to the quality its text names.
	const auto& tracking = vekt::dsp::trackingQualityChoices();
	for (int index = 0; index < tracking.size(); ++index)
		REQUIRE(vekt::dsp::qualityName(vekt::dsp::trackingQualityFrom(static_cast<float>(index))) == tracking[index]);
	const auto& offline = vekt::dsp::offlineQualityChoices();
	for (int index = 0; index < offline.size(); ++index)
		REQUIRE(vekt::dsp::qualityName(vekt::dsp::offlineQualityFrom(static_cast<float>(index))) == offline[index]);
	REQUIRE(tracking.size() == offline.size());
}
