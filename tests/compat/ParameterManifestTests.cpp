// Frozen parameter manifests: what hosts use to find automation and saved values. Each line pins a
// parameter's ID, version hint, kind, range, default and choices. JUCE addresses parameters by ID
// (hashed for VST3/AU), so order is not pinned. See tests/fixtures/README.md.

#include "CompatFixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <iomanip>
#include <iostream>

namespace
{
using namespace vekt::compat;

std::filesystem::path manifestPath(const Product& product)
{
	return fixtureDirectory() / "parameters" / (product.name + ".txt");
}

std::string describe(const juce::RangedAudioParameter& parameter)
{
	std::ostringstream text;
	// Seven significant digits is float precision; more would freeze conversion noise.
	text << std::setprecision(7) << parameter.paramID << "\tv" << parameter.getVersionHint() << '\t';
	const auto& range = parameter.getNormalisableRange();
	// Snap the default to the parameter's interval so normalisation noise (e.g. -5e-7 for 0 dB) is not frozen.
	auto defaultValue = static_cast<double>(parameter.convertFrom0to1(parameter.getDefaultValue()));
	if (range.interval > 0.0f)
		defaultValue = std::round(defaultValue / range.interval) * range.interval + 0.0; // + 0.0 turns -0 into 0
	if (const auto* choice = dynamic_cast<const juce::AudioParameterChoice*>(&parameter))
		text << "choice\t" << choice->choices.joinIntoString("|") << "\tdefault=" << defaultValue;
	else if (dynamic_cast<const juce::AudioParameterBool*>(&parameter) != nullptr)
		text << "bool\tdefault=" << defaultValue;
	else
		text << (dynamic_cast<const juce::AudioParameterInt*>(&parameter) != nullptr ? "int" : "float")
			 << "\trange=" << range.start << ':' << range.end << "\tinterval=" << range.interval
			 << "\tskew=" << range.skew << (range.symmetricSkew ? " symmetric" : "") << "\tdefault=" << defaultValue;
	text << '\t' << (parameter.isAutomatable() ? "automatable" : "fixed");
	return text.str();
}

std::string identifierOf(const std::string& line) { return line.substr(0, line.find('\t')); }

// Current parameters, keyed by ID, in processor order.
std::vector<std::pair<std::string, std::string>> currentManifest(const Product& product)
{
	auto processor = product.create();
	std::vector<std::pair<std::string, std::string>> lines;
	for (const auto* parameter : rangedParameters(*processor))
		lines.emplace_back(parameter->paramID.toStdString(), describe(*parameter));
	return lines;
}

std::vector<std::string> frozenManifest(const Product& product)
{
	std::vector<std::string> lines;
	std::ifstream stream(manifestPath(product));
	for (std::string line; std::getline(stream, line);)
		if (!line.empty()) lines.push_back(line);
	return lines;
}

// Frozen parameters that were removed or changed since they were frozen.
std::vector<std::string> breakingChanges(const Product& product)
{
	const auto current = currentManifest(product);
	std::vector<std::string> problems;
	for (const auto& frozen : frozenManifest(product))
	{
		const auto found = std::find_if(current.begin(), current.end(),
			[&](const auto& entry) { return entry.first == identifierOf(frozen); });
		if (found == current.end())
			problems.push_back("removed: " + frozen);
		else if (found->second != frozen)
			problems.push_back("was: " + frozen + "\n  now: " + found->second);
	}
	return problems;
}

std::vector<std::string> unfrozen(const Product& product)
{
	const auto frozen = frozenManifest(product);
	std::vector<std::string> lines;
	for (const auto& [identifier, line] : currentManifest(product))
		if (std::none_of(frozen.begin(), frozen.end(), [&](const auto& entry) { return identifierOf(entry) == identifier; }))
			lines.push_back(line);
	return lines;
}

std::string joined(const std::vector<std::string>& lines)
{
	std::string text;
	for (const auto& line : lines) text += "\n  " + line;
	return text;
}
}

TEST_CASE("Frozen parameters keep their identity, range and default", "[compat][parameters]")
{
	for (const auto& product : products())
	{
		INFO(product.name);
		REQUIRE_FALSE(frozenManifest(product).empty());
		const auto problems = breakingChanges(product);
		INFO("Hosts would lose automation or saved values. Before release, edit tests/fixtures/parameters/"
			<< product.name << ".txt by hand if the change is deliberate:" << joined(problems));
		CHECK(problems.empty());
	}
}

TEST_CASE("Every parameter is frozen in the manifest", "[compat][parameters]")
{
	for (const auto& product : products())
	{
		const auto missing = unfrozen(product);
		INFO(product.name << ": freeze new parameters with: vekt_dsp_tests \"[.capture-parameters]\"" << joined(missing));
		CHECK(missing.empty());
	}
}

TEST_CASE("Capture new parameters into the manifests", "[.capture-parameters]")
{
	for (const auto& product : products())
	{
		// Only appends: a changed or removed frozen parameter must be resolved by hand.
		const auto problems = breakingChanges(product);
		INFO(product.name << " has changed frozen parameters:" << joined(problems));
		REQUIRE(problems.empty());
		const auto additions = unfrozen(product);
		if (additions.empty()) continue;
		std::filesystem::create_directories(manifestPath(product).parent_path());
		std::ofstream stream(manifestPath(product), std::ios::app);
		for (const auto& line : additions) stream << line << '\n';
		std::cout << "Froze " << additions.size() << " parameters in " << manifestPath(product).string() << '\n';
	}
}
