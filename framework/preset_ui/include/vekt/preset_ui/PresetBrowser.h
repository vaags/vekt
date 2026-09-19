#pragma once

#include <vekt/presets/PresetSession.h>
#include <vekt/presets/PresetBrowserModel.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace vekt::preset_ui
{
class PresetBrowser final : public juce::Component, private juce::ListBoxModel
{
public:
	explicit PresetBrowser(presets::PresetSession& session);
	~PresetBrowser() override;
	void refresh();
	void showResult(const juce::Result&);
	void resized() override;
	void paint(juce::Graphics&) override;
	bool keyPressed(const juce::KeyPress&) override;
	std::function<void()> onClose;
	std::function<void()> onSoundChanged;

private:
	int getNumRows() override;
	void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
	void selectedRowsChanged(int) override;
	void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override;
	void returnKeyPressed(int) override;
	void filter();
	void load();
	void save(bool replace);
	[[nodiscard]] std::optional<presets::PresetEntry> selected() const;

	presets::PresetSession& session;
	presets::PresetBrowserModel model;
	std::vector<presets::PresetEntry> rows;
	juce::ListBox list { "Presets", this };
	juce::TextEditor search, tagFilter, name, destination, tags;
	juce::ComboBox folders;
	juce::Label status, heading;
	juce::TextButton close { "Close" }, loadButton { "Load" }, saveButton { "Save As" },
		replaceButton { "Replace…" }, newFolder { "New folder" }, moveButton { "Move to folder" },
		deleteButton { "Delete…" }, refreshButton { "Refresh" }, updateTags { "Update tags" },
		importButton { "Import…" }, exportButton { "Export…" };
	std::unique_ptr<juce::FileChooser> chooser;
};
}