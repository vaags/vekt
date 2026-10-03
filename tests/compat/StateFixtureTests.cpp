// Frozen host-state fixtures: the exact bytes each product's getStateInformation wrote on the day the
// fixture was captured, plus what a restore of them must produce. Fixtures are never regenerated;
// a format or framework change has to keep restoring every one of them. See tests/fixtures/README.md.

#include "CompatFixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace
{
using namespace vekt::compat;

struct Expectation
{
	std::map<std::string, float> parameters;
	std::map<std::string, std::string> extras;
};

std::filesystem::path stateDirectory(const Product& product) { return fixtureDirectory() / "state" / product.name; }

// Moves every parameter away from its default (and, with a different offset, away from a capture),
// so a restore that silently skips a parameter cannot pass.
void applyPattern(juce::AudioProcessor& processor, int offset)
{
	auto index = 0;
	for (auto* parameter : rangedParameters(processor))
	{
		const auto defaultValue = parameter->getDefaultValue();
		auto target = 0.0f;
		if (parameter->isDiscrete() || parameter->isBoolean())
		{
			const auto steps = parameter->getNumSteps();
			if (steps < 2) continue;
			const auto defaultStep = static_cast<int>(std::lround(defaultValue * static_cast<float>(steps - 1)));
			// Never the default step, whatever the offset: two-state parameters would otherwise land on
			// their default for even offsets and could not show a restore that keeps a live value.
			const auto step = (defaultStep + 1 + (offset - 1) % (steps - 1)) % steps;
			target = static_cast<float>(step) / static_cast<float>(steps - 1);
		}
		else
		{
			target = static_cast<float>(std::fmod(0.137 + 0.618 * (index + offset * 7), 1.0));
			if (std::abs(target - defaultValue) < 0.05f)
				target = std::fmod(target + 0.3f, 1.0f);
		}
		parameter->setValueNotifyingHost(target);
		++index;
	}
}

std::string formatExpectation(juce::AudioProcessor& processor, const Product& product)
{
	std::ostringstream text;
	text << std::setprecision(9);
	for (const auto& [key, value] : product.describeExtras(processor))
		text << '#' << key << '\t' << value << '\n';
	for (auto* parameter : rangedParameters(processor))
		text << parameter->paramID << '\t' << plainValue(*parameter) << '\n';
	return text.str();
}

Expectation parseExpectation(const std::filesystem::path& path)
{
	Expectation expectation;
	std::ifstream stream(path);
	std::string line;
	while (std::getline(stream, line))
	{
		const auto tab = line.find('\t');
		if (tab == std::string::npos) continue;
		const auto key = line.substr(0, tab);
		const auto value = line.substr(tab + 1);
		if (key.starts_with('#'))
			expectation.extras[key.substr(1)] = value;
		else
			expectation.parameters[key] = std::stof(value);
	}
	return expectation;
}

void restore(juce::AudioProcessor& processor, const std::vector<char>& bytes)
{
	processor.setStateInformation(bytes.data(), static_cast<int>(bytes.size()));
}

// Normalised comparison so the tolerance means the same thing for Hz, dB and choice indices.
bool matches(const juce::RangedAudioParameter& parameter, float expectedPlain)
{
	return std::abs(parameter.getValue() - parameter.convertTo0to1(expectedPlain)) <= 1.0e-6f;
}

std::vector<std::string> mismatches(juce::AudioProcessor& processor, const Product& product, const Expectation& expectation)
{
	std::vector<std::string> problems;
	for (const auto& [identifier, value] : expectation.parameters)
	{
		const auto* parameter = findParameter(processor, identifier);
		if (parameter == nullptr)
			problems.push_back(identifier + ": parameter no longer exists");
		else if (!matches(*parameter, value))
			problems.push_back(identifier + ": expected " + std::to_string(value) + ", restored " + std::to_string(plainValue(*parameter)));
	}
	const auto extras = product.describeExtras(processor);
	for (const auto& [key, value] : expectation.extras)
		if (const auto found = extras.find(key); found == extras.end() || found->second != value)
			problems.push_back("#" + key + ": expected " + value + ", restored " + (found == extras.end() ? "<missing>" : found->second));
	return problems;
}

std::string joined(const std::vector<std::string>& lines)
{
	std::string text;
	for (const auto& line : lines) text += "\n  " + line;
	return text;
}

// Fixtures are named <date> or, for later captures that day, <date>-<n>. Plain filename order puts
// "2026-10-01-2" before "2026-10-01", so order by date, then by capture number.
std::filesystem::path newestFixture(std::vector<std::filesystem::path> fixtures)
{
	const auto key = [](const std::filesystem::path& path)
	{
		const auto stem = path.stem().string();
		const auto date = stem.substr(0, 10);
		const auto capture = stem.size() > 11 ? std::stoi(stem.substr(11)) : 1;
		return std::pair { date, capture };
	};
	return *std::max_element(fixtures.begin(), fixtures.end(),
		[&](const auto& first, const auto& second) { return key(first) < key(second); });
}

std::string today()
{
	const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::ostringstream text;
	text << std::put_time(std::localtime(&now), "%Y-%m-%d");
	return text.str();
}
}

TEST_CASE("Every frozen project state restores what it held", "[compat][state]")
{
	for (const auto& product : products())
	{
		const auto fixtures = filesWithExtension(stateDirectory(product), ".state");
		INFO(product.name);
		REQUIRE_FALSE(fixtures.empty());
		for (const auto& fixture : fixtures)
		{
			INFO(fixture.filename().string());
			const auto bytes = readBytes(fixture);
			const auto expectation = parseExpectation(std::filesystem::path(fixture).replace_extension(".expected"));
			REQUIRE_FALSE(expectation.parameters.empty());

			auto fresh = product.create();
			restore(*fresh, bytes);
			const auto freshProblems = mismatches(*fresh, product, expectation);
			INFO("restored into a new instance:" << joined(freshProblems));
			CHECK(freshProblems.empty());

			// Restoring over a modified instance must give the same result, and parameters the project
			// predates must take their defaults rather than keep whatever was live.
			auto modified = product.create();
			applyPattern(*modified, 2);
			restore(*modified, bytes);
			auto modifiedProblems = mismatches(*modified, product, expectation);
			for (auto* parameter : rangedParameters(*modified))
				if (!expectation.parameters.contains(parameter->paramID.toStdString())
					&& std::abs(parameter->getValue() - parameter->getDefaultValue()) > 1.0e-6f)
					modifiedProblems.push_back(parameter->paramID.toStdString() + ": added after this project, kept "
						+ std::to_string(plainValue(*parameter)) + " instead of its default");
			INFO("restored into a modified instance:" << joined(modifiedProblems));
			CHECK(modifiedProblems.empty());

			// What a restored project saves again must restore identically (no drift across sessions).
			juce::MemoryBlock resaved;
			fresh->getStateInformation(resaved);
			// Older Rav and Glimmer projects also named the factory preset in a metadata key; the selection now
			// comes from the preset session alone, so restoring drops the key (ADR 0010).
			const std::string resavedText(static_cast<const char*>(resaved.getData()), resaved.getSize());
			CHECK(resavedText.find("\"currentFactoryPreset\"") == std::string::npos);
			auto reopened = product.create();
			reopened->setStateInformation(resaved.getData(), static_cast<int>(resaved.getSize()));
			const auto reopenedProblems = mismatches(*reopened, product, expectation);
			INFO("saved again and reopened:" << joined(reopenedProblems));
			CHECK(reopenedProblems.empty());
		}
	}
}

TEST_CASE("The newest frozen project state covers every current parameter", "[compat][state]")
{
	for (const auto& product : products())
	{
		const auto fixtures = filesWithExtension(stateDirectory(product), ".expected");
		INFO(product.name);
		REQUIRE_FALSE(fixtures.empty());
		const auto newestPath = newestFixture(fixtures);
		const auto newest = parseExpectation(newestPath);
		auto processor = product.create();
		std::vector<std::string> uncovered;
		for (auto* parameter : rangedParameters(*processor))
			if (!newest.parameters.contains(parameter->paramID.toStdString()))
				uncovered.push_back(parameter->paramID.toStdString());
		INFO("Parameters added since " << newestPath.filename().string()
			<< "; freeze the current format with: vekt_dsp_tests \"[.capture-state]\"" << joined(uncovered));
		CHECK(uncovered.empty());
	}
}

TEST_CASE("The modification patterns move every parameter off its default", "[compat][state]")
{
	for (const auto& product : products())
		for (const auto offset : { 1, 2 })
		{
			auto processor = product.create();
			applyPattern(*processor, offset);
			std::vector<std::string> atDefault;
			for (auto* parameter : rangedParameters(*processor))
				if (parameter->getNumSteps() >= 2 && std::abs(parameter->getValue() - parameter->getDefaultValue()) <= 1.0e-6f)
					atDefault.push_back(parameter->paramID.toStdString());
			INFO(product.name << " offset " << offset << ":" << joined(atDefault));
			CHECK(atDefault.empty());
		}
}

// VST3 hosts automate booleans with fractional values that JUCE stores unsnapped; a restore of the
// same logical state must still leave the saved value, not the fractional one, for the host to read.
TEST_CASE("Project recall restores booleans after fractional host automation for every product", "[compat][state]")
{
	for (const auto& product : products())
	{
		INFO(product.name);
		auto processor = product.create();
		auto booleans = 0;
		for (auto* parameter : rangedParameters(*processor))
		{
			if (!parameter->isBoolean())
				continue;
			++booleans;
			for (const auto value : { 0.0f, 1.0f })
			{
				INFO(parameter->paramID << " = " << value);
				parameter->setValueNotifyingHost(value);
				juce::MemoryBlock state;
				processor->getStateInformation(state);
				parameter->setValueNotifyingHost(value < 0.5f ? 0.280552f : 0.719448f);
				processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
				CHECK(parameter->getValue() == value);
			}

			// A project that predates the parameter restores its default the same way.
			juce::MemoryBlock state;
			processor->getStateInformation(state);
			auto document = juce::JSON::parse(state.toString());
			document["parameters"].getDynamicObject()->removeProperty(parameter->paramID);
			const auto text = juce::JSON::toString(document);
			const auto defaultValue = parameter->getDefaultValue();
			INFO(parameter->paramID << " omitted, default " << defaultValue);
			parameter->setValueNotifyingHost(defaultValue < 0.5f ? 0.280552f : 0.719448f);
			processor->setStateInformation(text.toRawUTF8(), static_cast<int>(text.getNumBytesAsUTF8()));
			CHECK(parameter->getValue() == defaultValue);
		}
		REQUIRE(booleans > 0);
	}
}

TEST_CASE("The newest frozen project state is chosen by date and capture number", "[compat][state]")
{
	REQUIRE(newestFixture({ "2026-10-01.expected", "2026-10-01-2.expected" }).filename() == "2026-10-01-2.expected");
	REQUIRE(newestFixture({ "2026-10-01-2.expected", "2026-10-01-10.expected" }).filename() == "2026-10-01-10.expected");
	REQUIRE(newestFixture({ "2026-10-01-3.expected", "2026-11-02.expected" }).filename() == "2026-11-02.expected");
}

TEST_CASE("Capture today's project state for every product", "[.capture-state]")
{
	for (const auto& product : products())
	{
		auto source = product.create();
		if (source->getNumPrograms() > 1)
			source->setCurrentProgram(1);
		applyPattern(*source, 1);
		product.customiseExtras(*source);
		juce::MemoryBlock bytes;
		source->getStateInformation(bytes);

		// The fixture records what a restore produces, so first prove that is what was saved.
		auto restored = product.create();
		restored->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
		std::vector<std::string> lost;
		for (auto* parameter : rangedParameters(*source))
			if (const auto* copy = findParameter(*restored, parameter->paramID.toStdString());
				copy == nullptr || !matches(*copy, plainValue(*parameter)))
				lost.push_back(parameter->paramID.toStdString());
		if (product.describeExtras(*source) != product.describeExtras(*restored))
			lost.push_back("product extras");
		INFO(product.name << " does not round-trip; not freezing a lossy state:" << joined(lost));
		REQUIRE(lost.empty());

		// A product whose state has not changed since its newest fixture gains nothing from a copy of it.
		const auto expectation = formatExpectation(*restored, product);
		if (const auto frozen = filesWithExtension(stateDirectory(product), ".state"); !frozen.empty())
		{
			const auto newest = newestFixture(frozen);
			const auto frozenBytes = readBytes(newest);
			const auto frozenExpectation = readBytes(std::filesystem::path(newest).replace_extension(".expected"));
			const auto* data = static_cast<const char*>(bytes.getData());
			if (std::equal(frozenBytes.begin(), frozenBytes.end(), data, data + bytes.getSize())
				&& std::equal(frozenExpectation.begin(), frozenExpectation.end(), expectation.begin(), expectation.end()))
			{
				std::cout << "Unchanged since " << newest.string() << "; not freezing " << product.name << "\n";
				continue;
			}
		}

		// Fixtures are permanent: never overwrite one, add another.
		auto stem = stateDirectory(product) / today();
		for (auto suffix = 2; std::filesystem::exists(std::filesystem::path(stem).replace_extension(".state")); ++suffix)
			stem = stateDirectory(product) / (today() + "-" + std::to_string(suffix));
		writeBytes(std::filesystem::path(stem).replace_extension(".state"), bytes.getData(), bytes.getSize());
		writeBytes(std::filesystem::path(stem).replace_extension(".expected"), expectation.data(), expectation.size());
		std::cout << "Froze " << stem.string() << ".state\n";
	}
}
