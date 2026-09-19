#include "FactoryPresets.h"

#include <vekt/mono/Parameters.h>

namespace vekt::mono
{
namespace
{
struct Definition final
{
	const char* identifier;
	const char* name;
	const char* folder;
	const char* tags;
	float cutoff;
	float resonance;
	float ampAttack;
	float ampRelease;
	float osc2Level;
	float osc3Level;
	float noiseLevel;
	float filterEnvelope;
};

constexpr Definition definitions[] {
	{ "mono-lead-aurora", "Aurora Lead", "Lead", "Lead,Bright,Expressive", 4200, 22, .008f, .22f, 82, 28, 0, 52 },
	{ "mono-lead-circuit", "Circuit Lead", "Lead", "Lead,Driven,Mono", 2600, 38, .004f, .18f, 100, 44, 0, 64 },
	{ "mono-lead-glass", "Glass Lead", "Lead", "Lead,Sharp,Modern", 6200, 12, .003f, .12f, 74, 18, 0, 38 },
	{ "mono-lead-voyager", "Voyager Lead", "Lead", "Lead,Classic,Wide", 3300, 28, .01f, .32f, 90, 34, 0, 58 },
	{ "mono-bass-foundation", "Foundation Bass", "Bass", "Bass,Classic,Deep", 850, 26, .003f, .18f, 76, 20, 0, 61 },
	{ "mono-bass-rubber", "Rubber Bass", "Bass", "Bass,Resonant,Punchy", 620, 48, .002f, .14f, 92, 12, 0, 76 },
	{ "mono-bass-steel", "Steel Bass", "Bass", "Bass,Driven,Modern", 1200, 36, .003f, .2f, 100, 35, 0, 57 },
	{ "mono-bass-subway", "Subway Bass", "Bass", "Bass,Sub,Round", 420, 16, .006f, .28f, 58, 16, 0, 45 },
	{ "mono-pad-amber", "Amber Pad", "Pad", "Pad,Warm,Slow", 1800, 18, .42f, 1.8f, 80, 54, 6, 30 },
	{ "mono-pad-horizon", "Horizon Pad", "Pad", "Pad,Wide,Air", 3200, 12, .7f, 2.4f, 68, 62, 10, 22 },
	{ "mono-pad-nocturne", "Nocturne Pad", "Pad", "Pad,Dark,Texture", 980, 30, .55f, 2.8f, 75, 48, 14, 42 },
	{ "mono-pad-vapor", "Vapor Pad", "Pad", "Pad,Soft,Drift", 2400, 16, .8f, 3.2f, 60, 72, 8, 26 },
	{ "mono-pluck-copper", "Copper Pluck", "Pluck", "Pluck,Short,Bright", 3800, 20, .002f, .22f, 86, 25, 0, 72 },
	{ "mono-pluck-ember", "Ember Pluck", "Pluck", "Pluck,Wood,Resonant", 2100, 40, .002f, .3f, 72, 32, 0, 68 },
	{ "mono-pluck-needle", "Needle Pluck", "Pluck", "Pluck,Sharp,Fast", 7000, 10, .001f, .12f, 94, 16, 0, 52 },
	{ "mono-pluck-slate", "Slate Pluck", "Pluck", "Pluck,Muted,Dark", 1450, 28, .003f, .35f, 65, 30, 0, 64 },
	{ "mono-motion-current", "Current Motion", "Motion", "Motion,Animated,Filter", 1850, 52, .05f, .65f, 84, 48, 4, 74 },
	{ "mono-motion-orbit", "Orbit Motion", "Motion", "Motion,Wide,Drift", 2600, 34, .12f, 1.1f, 70, 65, 8, 48 },
	{ "mono-motion-pulse", "Pulse Motion", "Motion", "Motion,Resonant,Sequence", 1100, 58, .01f, .38f, 96, 30, 0, 82 },
	{ "mono-motion-tide", "Tide Motion", "Motion", "Motion,Slow,Organic", 1550, 26, .3f, 1.6f, 62, 58, 12, 54 },
	{ "mono-fx-comet", "Comet FX", "FX", "FX,Noise,Sweep", 5600, 66, .15f, 2.1f, 48, 70, 32, 86 },
	{ "mono-fx-entropy", "Entropy FX", "FX", "FX,Noise,Experimental", 900, 72, .4f, 3.8f, 30, 78, 55, 94 },
	{ "mono-fx-polaris", "Polaris FX", "FX", "FX,High,Resonant", 8200, 60, .08f, 1.4f, 78, 64, 18, 66 },
	{ "mono-fx-transmission", "Transmission FX", "FX", "FX,Radio,Texture", 1300, 45, .02f, .8f, 44, 38, 48, 78 }
};

juce::String makeDocument(const Definition& definition)
{
	auto parameters = std::make_unique<juce::DynamicObject>();
	const auto set = [&parameters](const char* id, float value) { parameters->setProperty(id, value); };
	for (const auto* id : parameters::soundParameterIds) set(id, 0.0f);
	set(parameters::performanceMode, 0); set(parameters::unison, 0); set(parameters::pitchBendRange, 2);
	set(parameters::osc1Range, 1); set(parameters::osc2Range, 1); set(parameters::osc3Range, 1);
	set(parameters::osc1Level, 100); set(parameters::osc2Level, definition.osc2Level); set(parameters::osc3Level, definition.osc3Level);
	set(parameters::osc1Morph, 2); set(parameters::osc2Morph, 2); set(parameters::osc3Morph, 1);
	set(parameters::osc1PulseWidth, 50); set(parameters::osc2PulseWidth, 50); set(parameters::osc3PulseWidth, 50);
	set(parameters::noiseType, definition.noiseLevel > 0 ? 1 : 0); set(parameters::noiseLevel, definition.noiseLevel);
	set(parameters::filterCutoff, definition.cutoff); set(parameters::filterResonance, definition.resonance);
	set(parameters::filterKeyTracking, 50); set(parameters::filterEnvelopeAmount, definition.filterEnvelope);
	set(parameters::ampAttack, definition.ampAttack); set(parameters::ampDecay, .35f); set(parameters::ampSustain, 72); set(parameters::ampRelease, definition.ampRelease); set(parameters::ampVelocity, 55);
	set(parameters::filterAttack, .005f); set(parameters::filterDecay, .45f); set(parameters::filterSustain, 28); set(parameters::filterRelease, .5f); set(parameters::filterVelocity, 55);
	set(parameters::masterOutput, -5);
	auto root = std::make_unique<juce::DynamicObject>();
	root->setProperty("format", "vekt.preset"); root->setProperty("schemaVersion", 2);
	root->setProperty("id", definition.identifier); root->setProperty("product", parameters::presetProductIdentifier);
	root->setProperty("soundSchemaVersion", 1); root->setProperty("name", definition.name);
	juce::Array<juce::var> tags;
	for (const auto& tag : juce::StringArray::fromTokens(definition.tags, ",", {})) tags.add(tag);
	root->setProperty("tags", tags); root->setProperty("parameters", juce::var(parameters.release()));
	return juce::JSON::toString(juce::var(root.release()));
}
}

juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	for (const auto& definition : definitions)
		if (const auto result = catalog.addFactoryPreset(makeDocument(definition), definition.folder); result.failed())
			return result;
	return juce::Result::ok();
}
}