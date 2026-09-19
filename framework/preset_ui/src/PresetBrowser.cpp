#include <vekt/preset_ui/PresetBrowser.h>

namespace vekt::preset_ui
{
namespace
{
juce::StringArray parseTags(const juce::String& text)
{
	juce::StringArray values;
	values.addTokens(text, ",", "");
	return presets::normaliseTags(values);
}
}

PresetBrowser::PresetBrowser(presets::PresetSession& controller) : session(controller)
{
	setTitle("Preset browser");
	setWantsKeyboardFocus(true);
	for (juce::Component* component : std::initializer_list<juce::Component*> {
		&list, &search, &tagFilter, &name, &destination, &tags, &folders, &status, &heading,
		&close, &loadButton, &saveButton, &replaceButton, &newFolder, &moveButton, &deleteButton, &refreshButton,
		&updateTags, &importButton, &exportButton })
		addAndMakeVisible(component);
	heading.setText(session.descriptor().displayName + " presets", juce::dontSendNotification);
	auto configure = [](juce::TextEditor& editor, const juce::String& title)
	{
		editor.setTitle(title);
		editor.setTextToShowWhenEmpty(title, juce::Colours::grey);
	};
	configure(search, "Search presets");
	configure(tagFilter, "Match all tags (comma separated)");
	configure(name, "Preset name");
	configure(destination, "User folder (empty = root)");
	configure(tags, "Preset tags (comma separated)");
	folders.setTitle("Library folder");
	search.onTextChange = tagFilter.onTextChange = [this] { filter(); };
	folders.onChange = [this] { filter(); };
	close.onClick = [this] { if (onClose) onClose(); };
	loadButton.onClick = [this] { load(); };
	saveButton.onClick = [this] { save(false); };
	replaceButton.onClick = [this] { save(true); };
	refreshButton.onClick = [this] { refresh(); };
	updateTags.onClick = [this]
	{
		if (const auto entry = selected())
		{
			const auto result = session.updateTags(entry->identifier, parseTags(tags.getText()));
			if (result.wasOk()) refresh();
			showResult(result);
		}
	};
	importButton.onClick = [this]
	{
		chooser = std::make_unique<juce::FileChooser>("Import a preset into the library", juce::File {}, "*.vektpreset");
		juce::Component::SafePointer<PresetBrowser> safe(this);
		const auto folder = destination.getText().trim();
		chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[safe, folder](const juce::FileChooser& dialog)
			{
				if (!safe || dialog.getResult() == juce::File {}) return;
				const auto result = safe->session.importFile(dialog.getResult(), folder);
				safe->refresh(); safe->showResult(result);
			});
	};
	exportButton.onClick = [this]
	{
		const auto entry = selected();
		if (!entry) return;
		chooser = std::make_unique<juce::FileChooser>("Export preset", juce::File {}, "*.vektpreset");
		juce::Component::SafePointer<PresetBrowser> safe(this);
		chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
			| juce::FileBrowserComponent::warnAboutOverwriting, [safe, entry](const juce::FileChooser& dialog)
			{
				if (!safe || dialog.getResult() == juce::File {}) return;
				safe->showResult(safe->session.exportFile(entry->identifier, entry->origin, dialog.getResult()));
			});
	};
	newFolder.onClick = [this]
	{
		if (auto* repository = session.library().repository())
		{
			const auto result = repository->createFolder(destination.getText().trim());
			refresh(); showResult(result);
		}
	};
	moveButton.onClick = [this]
	{
		const auto entry = selected();
		auto* repository = session.library().repository();
		if (!entry || entry->origin != presets::PresetOrigin::user || !repository) return;
		const auto folder = destination.getText().trim();
		const auto result = repository->move(entry->location, folder.isEmpty() ? entry->name : folder + "/" + entry->name);
		refresh(); showResult(result);
	};
	deleteButton.onClick = [this]
	{
		const auto entry = selected();
		if (!entry || entry->origin != presets::PresetOrigin::user) return;
		juce::Component::SafePointer<PresetBrowser> safe(this);
		juce::AlertWindow::showAsync(juce::MessageBoxOptions().withTitle("Delete preset?")
			.withMessage("Delete " + entry->location + "?").withButton("Delete").withButton("Cancel")
			.withAssociatedComponent(this), [safe, entry](int result)
		{
			if (!safe || result != 1) return;
			const auto removed = safe->session.library().removeUserPreset(entry->location);
			if (removed.wasOk() && safe->session.loaded() && safe->session.loaded()->identifier == entry->identifier)
				safe->session.clear();
			safe->refresh(); safe->showResult(removed);
			if (safe->onSoundChanged) safe->onSoundChanged();
		});
	};
	list.setRowHeight(34);
	setSize(780, 460);
}

PresetBrowser::~PresetBrowser() { list.setModel(nullptr); }

void PresetBrowser::refresh()
{
	const auto previous = folders.getText();
	session.library().refresh();
	folders.clear(juce::dontSendNotification);
	folders.addItem("All presets", 1);
	folders.addItem("Factory", 2);
	folders.addItem("User", 3);
	if (auto* repository = session.library().repository())
		for (const auto& folder : repository->folders()) folders.addItem("User / " + folder, folders.getNumItems() + 1);
	folders.setSelectedId(1, juce::dontSendNotification);
	for (int i = 0; i < folders.getNumItems(); ++i)
		if (folders.getItemText(i) == previous) folders.setSelectedItemIndex(i, juce::dontSendNotification);
	const auto writable = session.library().repository() != nullptr;
	saveButton.setEnabled(writable);
	newFolder.setEnabled(writable);
	importButton.setEnabled(writable);
	filter();
}

void PresetBrowser::filter()
{
	const auto before = selected();
	model.search = search.getText();
	model.tags = parseTags(tagFilter.getText());
	model.origin.reset(); model.folder.clear();
	if (folders.getSelectedId() == 2) model.origin = presets::PresetOrigin::factory;
	if (folders.getSelectedId() >= 3) model.origin = presets::PresetOrigin::user;
	if (folders.getSelectedId() > 3) model.folder = folders.getText().substring(7);
	rows = model.filter(session.library().entries());
	list.deselectAllRows(); list.updateContent();
	if (before)
		for (std::size_t i = 0; i < rows.size(); ++i)
			if (rows[i].identifier == before->identifier && rows[i].origin == before->origin)
				list.selectRow(static_cast<int>(i));
	selectedRowsChanged(list.getSelectedRow());
	status.setText(rows.empty() ? "No presets match. Clear search or change the folder/tags." : juce::String(rows.size()) + " presets", juce::dontSendNotification);
}

int PresetBrowser::getNumRows() { return static_cast<int>(rows.size()); }

void PresetBrowser::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selectedRow)
{
	if (row < 0 || row >= getNumRows()) return;
	const auto& entry = rows[static_cast<std::size_t>(row)];
	if (selectedRow) g.fillAll(findColour(juce::TextEditor::highlightColourId));
	g.setColour(findColour(juce::Label::textColourId));
	g.drawText(entry.name + (entry.error.isEmpty() ? "" : " (unavailable)"), 8, 0, width / 2 - 8, height, juce::Justification::centredLeft);
	g.drawText((entry.origin == presets::PresetOrigin::factory ? "Factory" : "User / " + entry.folder)
		+ "   " + entry.tags.joinIntoString(", "), width / 2, 0, width / 2 - 8, height, juce::Justification::centredLeft);
}

std::optional<presets::PresetEntry> PresetBrowser::selected() const
{
	const auto row = list.getSelectedRow();
	return row >= 0 && row < static_cast<int>(rows.size()) ? std::optional { rows[static_cast<std::size_t>(row)] } : std::nullopt;
}

void PresetBrowser::selectedRowsChanged(int)
{
	const auto entry = selected();
	loadButton.setEnabled(entry && entry->error.isEmpty());
	const auto user = entry && entry->origin == presets::PresetOrigin::user;
	deleteButton.setEnabled(user); moveButton.setEnabled(user); replaceButton.setEnabled(user);
	updateTags.setEnabled(user); exportButton.setEnabled(entry && entry->error.isEmpty());
	if (entry)
	{
		name.setText(entry->name, false);
		destination.setText(entry->folder, false);
		tags.setText(entry->tags.joinIntoString(", "), false);
	}
}

void PresetBrowser::load()
{
	if (const auto entry = selected())
	{
		showResult(session.load(entry->identifier, entry->origin));
		if (onSoundChanged) onSoundChanged();
	}
}

void PresetBrowser::save(bool replace)
{
	if (replace)
	{
		juce::Component::SafePointer<PresetBrowser> safe(this);
		const auto presetName = name.getText().trim(), folder = destination.getText().trim();
		const auto presetTags = parseTags(tags.getText());
		juce::AlertWindow::showAsync(juce::MessageBoxOptions().withTitle("Replace preset?")
			.withMessage("Replace " + presetName + " with the current sound?")
			.withButton("Replace").withButton("Cancel").withAssociatedComponent(this),
			[safe, presetName, folder, presetTags](int result)
		{
			if (!safe || result != 1) return;
			const auto saved = safe->session.save(presetName, folder, presetTags, presets::PresetSaveMode::replaceExisting);
			if (saved.wasOk()) safe->refresh();
			safe->showResult(saved);
			if (safe->onSoundChanged) safe->onSoundChanged();
		});
		return;
	}
	const auto result = session.save(name.getText(), destination.getText().trim(), parseTags(tags.getText()));
	if (result.wasOk()) refresh();
	showResult(result);
	if (onSoundChanged) onSoundChanged();
}

void PresetBrowser::showResult(const juce::Result& result)
{ status.setText(result.failed() ? result.getErrorMessage() : "", juce::dontSendNotification); }
void PresetBrowser::listBoxItemDoubleClicked(int, const juce::MouseEvent&) { load(); }
void PresetBrowser::returnKeyPressed(int) { load(); }
bool PresetBrowser::keyPressed(const juce::KeyPress& key)
{
	if (key == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
	return false;
}

void PresetBrowser::paint(juce::Graphics& g)
{
	g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
	g.setColour(findColour(juce::Label::textColourId).withAlpha(0.4f));
	g.drawRect(getLocalBounds());
}

void PresetBrowser::resized()
{
	auto area = getLocalBounds().reduced(12);
	auto header = area.removeFromTop(32);
	close.setBounds(header.removeFromRight(70)); heading.setBounds(header);
	area.removeFromTop(8);
	auto filters = area.removeFromTop(32);
	folders.setBounds(filters.removeFromLeft(180)); filters.removeFromLeft(8);
	search.setBounds(filters.removeFromLeft((filters.getWidth() - 8) / 2)); filters.removeFromLeft(8);
	tagFilter.setBounds(filters);
	area.removeFromTop(8);
	status.setBounds(area.removeFromBottom(28));
	auto actions = area.removeFromBottom(32);
	for (auto* button : { &loadButton, &saveButton, &replaceButton, &deleteButton, &refreshButton, &importButton, &exportButton })
	{ button->setBounds(actions.removeFromLeft((getWidth() - 24) / 7 - 8)); actions.removeFromLeft(8); }
	area.removeFromBottom(8);
	auto organisation = area.removeFromBottom(32);
	newFolder.setBounds(organisation.removeFromRight(100)); organisation.removeFromRight(8);
	moveButton.setBounds(organisation.removeFromRight(130)); organisation.removeFromRight(8);
	destination.setBounds(organisation);
	area.removeFromBottom(8);
	auto fields = area.removeFromBottom(32);
	updateTags.setBounds(fields.removeFromRight(108)); fields.removeFromRight(8);
	name.setBounds(fields.removeFromLeft(fields.getWidth() / 2)); fields.removeFromLeft(8); tags.setBounds(fields);
	area.removeFromBottom(8); list.setBounds(area);
}
}