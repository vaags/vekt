#include "LabSettings.h"
#include "RackRouting.h"

#include <vekt/rav/PluginProcessor.h>
#include <vekt/rav/PluginEditor.h>
#include <vekt/glimmer/PluginProcessor.h>
#include <vekt/kobber/PluginProcessor.h>
#include <vekt/flint/PluginProcessor.h>

#include <juce_audio_utils/juce_audio_utils.h>

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace
{
// Space around the hosted editor: side margins, compact controls and keyboard above it.
constexpr int labHorizontalChrome = 32;
constexpr int labVerticalChrome = 168;
constexpr int editorTop = 152;

juce::String getSystemDefaultOutputName()
{
#if JUCE_MAC
	AudioDeviceID device {};
	UInt32 size = sizeof(device);
	const AudioObjectPropertyAddress defaultDeviceProperty {
		kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
		kAudioObjectPropertyElementMain
	};
	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultDeviceProperty, 0, nullptr, &size, &device) != noErr)
		return {};
	CFStringRef name {};
	size = sizeof(name);
	const AudioObjectPropertyAddress nameProperty {
		kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
	};
	if (AudioObjectGetPropertyData(device, &nameProperty, 0, nullptr, &size, &name) != noErr || name == nullptr)
		return {};
	const auto result = juce::String::fromCFString(name);
	CFRelease(name);
	return result;
#else
	return {};
#endif
}

#if JUCE_MAC
class DefaultOutputListener final
{
public:
	explicit DefaultOutputListener(std::atomic<bool>& pendingRefresh)
		: pending(pendingRefresh)
	{
		const AudioObjectPropertyAddress property {
			kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
			kAudioObjectPropertyElementMain
		};
		installed = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &property, callback, this) == noErr;
	}
	~DefaultOutputListener()
	{
		if (!installed)
			return;
		const AudioObjectPropertyAddress property {
			kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
			kAudioObjectPropertyElementMain
		};
		AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &property, callback, this);
	}

private:
	static OSStatus callback(AudioObjectID, UInt32, const AudioObjectPropertyAddress[], void* context)
	{
		static_cast<DefaultOutputListener*>(context)->pending.store(true);
		return noErr;
	}
	std::atomic<bool>& pending;
	bool installed {};
};
#endif

class LiveLab final : private juce::AudioDeviceManager,
						 public juce::AudioAppComponent,
						 private juce::Timer,
						 private juce::ChangeListener
{
public:
	LiveLab()
		: AudioAppComponent(static_cast<juce::AudioDeviceManager&>(*this))
		#if JUCE_MAC
		, defaultOutputListener(deviceRefreshPending)
		#endif
	{
		restoreRackState();
		for (auto* component : { static_cast<juce::Component*>(&restartButton),
			static_cast<juce::Component*>(&statusLabel), static_cast<juce::Component*>(&productTabs),
			static_cast<juce::Component*>(&orderButton), static_cast<juce::Component*>(&midiInputBox),
			static_cast<juce::Component*>(&instrumentBox) })
			addAndMakeVisible(*component);
		addAndMakeVisible(keyboard);
		orderButton.setTooltip("Process the instrument through this rack route.");
		productTabs.addItem("Kobber", 1);
		productTabs.addItem("RAV", 2);
		productTabs.addItem("Glimmer", 3);
		productTabs.addItem("Flint", 4);
		// One instrument plays at a time; the one left is silenced (RackRouting.h).
		instrumentBox.addItem("Play: Mono", 1);
		instrumentBox.addItem("Play: Flint", 2);
		instrumentBox.setTooltip("The instrument the keyboard and MIDI input play, before the rack.");
		instrumentBox.setSelectedId(instrument.load() + 1, juce::dontSendNotification);
		instrumentBox.onChange = [this]
		{
			instrument.store(instrumentBox.getSelectedId() - 1);
			// Show the instrument that now plays.
			productTabs.setSelectedId(instrument.load() == static_cast<int>(vekt::audio_lab::Instrument::flint) ? 4 : 1);
		};
		productTabs.setSelectedId(restoredSelectedTab, juce::dontSendNotification);
		productTabs.onChange = [this]
		{
			showProductEditor(productTabs.getSelectedId());
		};
		orderButton.onClick = [this]
		{
			const auto next = (rackRoute.load() + 1) % 5;
			rackRoute.store(next);
			static constexpr std::array<const char*, 5> routeNames {
				"Bypass", "RAV", "Glimmer", "RAV -> Glimmer", "Glimmer -> RAV" };
			orderButton.setButtonText(routeNames[static_cast<std::size_t>(next)]);
		};
		static constexpr std::array<const char*, 5> routeNames {
			"Bypass", "RAV", "Glimmer", "RAV -> Glimmer", "Glimmer -> RAV" };
		orderButton.setButtonText(routeNames[static_cast<std::size_t>(rackRoute.load())]);
		midiInputBox.onChange = [this] { selectMidiInput(midiInputBox.getSelectedItemIndex() - 1); };
		restartButton.onClick = []
		{
			const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
			const auto appBundle = executable.getParentDirectory().getParentDirectory().getParentDirectory();
			juce::Timer::callAfterDelay(250, [appBundle]
			{
				juce::ChildProcess launcher;
				launcher.start({ "/usr/bin/open", "-n", appBundle.getFullPathName() });
				juce::JUCEApplication::getInstance()->quit();
			});
		};
		ravEditor.reset(ravProcessor.createEditor());
		glimmerEditor.reset(glimmerProcessor.createEditor());
		kobberEditor.reset(kobberProcessor.createEditor());
		flintEditor.reset(flintProcessor.createEditor());
		silenceMidi.ensureSize(vekt::audio_lab::silenceEventBytes);
		keyboard.setKeyPressBaseOctave(4);
		keyboard.setOctaveForMiddleC(4);
		keyboard.setWantsKeyboardFocus(true);
		keyboardState.addListener(&midiCollector);
		for (auto* editor : { kobberEditor.get(), ravEditor.get(), glimmerEditor.get(), flintEditor.get() })
		{
			addAndMakeVisible(*editor);
			if (auto* scalableEditor = dynamic_cast<vekt::ui::ScalableEditor*>(editor))
			{
				scalableEditor->setResizeHandleVisible(false);
				scalableEditor->setResizable(false, false);
			}
		}
		showProductEditor(productTabs.getSelectedId());
		// Fit the largest product editor at its native size (Mono is wider than Rav and Glimmer).
		int editorWidth = vekt::ui::ScalableEditor::logicalWidth, editorHeight = vekt::ui::ScalableEditor::logicalHeight;
		for (auto* editor : { kobberEditor.get(), ravEditor.get(), glimmerEditor.get(), flintEditor.get() })
			if (const auto* scalable = dynamic_cast<vekt::ui::ScalableEditor*>(editor))
			{
				editorWidth = std::max(editorWidth, scalable->getLogicalWidth());
				editorHeight = std::max(editorHeight, scalable->getLogicalHeight());
			}
		setSize(editorWidth + labHorizontalChrome, editorHeight + labVerticalChrome);
		openInitialOutput();
		setAudioChannels(0, 2);
		refreshMidiInputs();
		addChangeListener(this);
		startTimerHz(15);
	}

	~LiveLab() override
	{
		saveRackState();
		stopTimer();
		removeChangeListener(this);
		keyboardState.removeListener(&midiCollector);
		if (selectedMidiInputIdentifier.isNotEmpty())
		{
			removeMidiInputDeviceCallback(selectedMidiInputIdentifier, &midiCollector);
			setMidiInputDeviceEnabled(selectedMidiInputIdentifier, false);
		}
		shutdownAudio();
	}

	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
	{
		ravProcessor.prepareToPlay(sampleRate, samplesPerBlockExpected);
		glimmerProcessor.prepareToPlay(sampleRate, samplesPerBlockExpected);
		kobberProcessor.prepareToPlay(sampleRate, samplesPerBlockExpected);
		flintProcessor.prepareToPlay(sampleRate, samplesPerBlockExpected);
		// A plugin host passes its audio workgroup; this lab hosts Mono directly, so forward the device's.
		if (auto* device = getCurrentAudioDevice()) kobberProcessor.audioWorkgroupContextChanged(device->getWorkgroup());
		midiCollector.reset(sampleRate);
		midiCollector.ensureStorageAllocated(4096);
		sampleRateHz = sampleRate;
	}

	void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
	{
		juce::MidiBuffer midi;
		midiCollector.removeNextBlockOfMessages(midi, info.numSamples);
		info.clearActiveBufferRegion();
		juce::AudioBuffer<float> sourceBlock(info.buffer->getArrayOfWritePointers(),
			info.buffer->getNumChannels(), info.startSample, info.numSamples);
		const auto selected = instrument.load();
		if (selected != playingInstrument)
		{
			vekt::audio_lab::silenceInstrument(instrumentProcessor(playingInstrument), sourceBlock, silenceMidi);
			playingInstrument = selected;
		}
		const auto kobberTicks = vekt::audio_lab::processInstrument(sourceBlock, midi, instrumentProcessor(selected));
		auto inputPeak = 0.0f;
		for (auto channel = 0; channel < info.buffer->getNumChannels(); ++channel)
			inputPeak = std::max(inputPeak, info.buffer->getMagnitude(channel, info.startSample, info.numSamples));
		generatedPeak.store(inputPeak);

		juce::AudioBuffer<float> block(info.buffer->getArrayOfWritePointers(),
			info.buffer->getNumChannels(), info.startSample, info.numSamples);
		const auto startTicks = juce::Time::getHighResolutionTicks();
		vekt::audio_lab::processRackRoute(rackRoute.load(), block, midi, ravProcessor, glimmerProcessor);
		const auto elapsedTicks = kobberTicks + juce::Time::getHighResolutionTicks() - startTicks;
		const auto blockDurationTicks = static_cast<double>(info.numSamples)
			* static_cast<double>(juce::Time::getHighResolutionTicksPerSecond()) / sampleRateHz;
		const auto instantaneousLoad = static_cast<float>(100.0 * static_cast<double>(elapsedTicks)
			/ blockDurationTicks);
		const auto previousLoad = pluginCpuLoadPercent.load();
		pluginCpuLoadPercent.store(previousLoad + 0.1f * (instantaneousLoad - previousLoad));
		publishPeak(block);
	}

	void releaseResources() override
	{
		kobberProcessor.releaseResources();
		flintProcessor.releaseResources();
		ravProcessor.releaseResources();
		glimmerProcessor.releaseResources();
	}

	bool keyPressed(const juce::KeyPress& key) override
	{
		// Keep the Audio Lab's QWERTY keyboard active while another product editor
		// owns focus. MidiKeyboardComponent normally receives keys only when the
		// on-screen keyboard itself is focused.
		return keyboard.keyPressed(key);
	}

	bool keyStateChanged(bool isKeyDown) override
	{
		return keyboard.keyStateChanged(isKeyDown);
	}

	void paint(juce::Graphics& graphics) override
	{
		graphics.fillAll(juce::Colour::fromRGB(20, 24, 28));
		graphics.setColour(juce::Colours::white);
		graphics.setFont(juce::FontOptions(22.0f).withStyle("Bold"));
		graphics.drawText("VEKT AUDIO LAB", 16, 12, 440, 32, juce::Justification::centredLeft);
		graphics.setColour(juce::Colour::fromRGB(54, 65, 70));
		graphics.drawLine(16.0f, 100.0f, static_cast<float>(getWidth() - 16), 100.0f);
	}

	void resized() override
	{
		midiInputBox.setBounds(16, 58, 220, 32);
		instrumentBox.setBounds(244, 56, 140, 36);
		productTabs.setBounds(392, 56, 120, 36);
		orderButton.setBounds(520, 56, 152, 36);
		restartButton.setBounds(680, 56, 132, 36);
		statusLabel.setBounds(824, 56, getWidth() - 840, 36);
		keyboard.setBounds(16, 108, getWidth() - 32, 36);
		for (auto* editor : { kobberEditor.get(), ravEditor.get(), glimmerEditor.get(), flintEditor.get() })
		{
			const auto editorArea = getLocalBounds().withTop(editorTop).withTrimmedBottom(16).reduced(16, 0);
			// Each product may use its own logical size (Mono is wider), so fit each editor by its own aspect.
			const auto* scalable = dynamic_cast<vekt::ui::ScalableEditor*>(editor);
			const auto logicalWidth = static_cast<float>(scalable != nullptr ? scalable->getLogicalWidth() : vekt::ui::ScalableEditor::logicalWidth);
			const auto logicalHeight = static_cast<float>(scalable != nullptr ? scalable->getLogicalHeight() : vekt::ui::ScalableEditor::logicalHeight);
			const auto scale = std::min(static_cast<float>(editorArea.getWidth()) / logicalWidth,
				static_cast<float>(editorArea.getHeight()) / logicalHeight);
			const auto editorWidth = static_cast<int>(logicalWidth * scale);
			const auto editorHeight = static_cast<int>(logicalHeight * scale);
			editor->setBounds(editorArea.withSizeKeepingCentre(editorWidth, editorHeight));
		}
	}

private:

	[[nodiscard]] juce::AudioProcessor& instrumentProcessor(int index) noexcept
	{
		if (index == static_cast<int>(vekt::audio_lab::Instrument::flint)) return flintProcessor;
		return kobberProcessor;
	}

	[[nodiscard]] const juce::AudioProcessor& instrumentProcessor(int index) const noexcept
	{
		if (index == static_cast<int>(vekt::audio_lab::Instrument::flint)) return flintProcessor;
		return kobberProcessor;
	}

	void showProductEditor(int tab)
	{
		kobberEditor->setVisible(tab == 1);
		ravEditor->setVisible(tab == 2);
		glimmerEditor->setVisible(tab == 3);
		flintEditor->setVisible(tab == 4);
		auto* editor = tab == 1 ? kobberEditor.get() : tab == 2 ? ravEditor.get() : tab == 3 ? glimmerEditor.get()
			: flintEditor.get();
		editor->resized();
		editor->toFront(false);
		editor->repaint();
	}

	[[nodiscard]] static juce::File rackStateFile()
	{
		return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
			.getChildFile("Vekt").getChildFile("Audio Lab").getChildFile("rack-state.xml");
	}

	static void restoreProcessorState(juce::AudioProcessor& processor, const juce::String& encoded)
	{
		if (encoded.isEmpty())
			return;
		juce::MemoryBlock state;
		if (state.fromBase64Encoding(encoded))
			processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	}

	void restoreRackState()
	{
		const auto file = rackStateFile();
		if (!file.existsAsFile())
			return;
		juce::XmlDocument document(file);
		const auto xml = document.getDocumentElement();
		if (xml == nullptr)
			return;
		const auto state = juce::ValueTree::fromXml(*xml);
		if (!state.hasType("VektAudioLabRackState")
			|| static_cast<int>(state.getProperty("schemaVersion", 0)) != 1)
			return;
		restoredSettings = vekt::audio_lab::readLabSettings(state);
		restoredSelectedTab = restoredSettings.selectedTab;
		rackRoute.store(restoredSettings.rackRoute);
		instrument.store(restoredSettings.instrument);
		playingInstrument = restoredSettings.instrument;
		restoreProcessorState(kobberProcessor, state.getProperty("kobberState", {}).toString());
		restoreProcessorState(ravProcessor, state.getProperty("ravState", {}).toString());
		restoreProcessorState(glimmerProcessor, state.getProperty("glimmerState", {}).toString());
		restoreProcessorState(flintProcessor, state.getProperty("flintState", {}).toString());
		restoredMidiInputIdentifier = restoredSettings.midiInputIdentifier;
	}

	void saveRackState()
	{
		juce::ValueTree state("VektAudioLabRackState");
		state.setProperty("schemaVersion", 1, nullptr);
		vekt::audio_lab::writeLabSettings(state, {
			9,
			0,
			productTabs.getSelectedId(),
			rackRoute.load(),
			restoredMidiInputIdentifier,
			{},
			true,
			instrument.load()
		});
		const auto capture = [](juce::AudioProcessor& processor)
		{
			juce::MemoryBlock data;
			processor.getStateInformation(data);
			return data.toBase64Encoding();
		};
		state.setProperty("kobberState", capture(kobberProcessor), nullptr);
		state.setProperty("ravState", capture(ravProcessor), nullptr);
		state.setProperty("glimmerState", capture(glimmerProcessor), nullptr);
		state.setProperty("flintState", capture(flintProcessor), nullptr);
		const auto file = rackStateFile();
		file.getParentDirectory().createDirectory();
		juce::TemporaryFile temporary(file);
		if (const auto xml = state.createXml(); xml != nullptr
			&& temporary.getFile().replaceWithText(xml->toString()))
			juce::ignoreUnused(temporary.overwriteTargetFileWithTemporary());
	}

	void publishPeak(const juce::AudioBuffer<float>& buffer) noexcept
	{
		auto peak = 0.0f;
		for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
			for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
				peak = std::max(peak, std::abs(buffer.getSample(channel, sample)));
		outputPeak.store(peak);
	}

	void refreshMidiInputs()
	{
		const auto wantedIdentifier = restoredMidiInputIdentifier;
		midiInputDevices = juce::MidiInput::getAvailableDevices();
		midiInputBox.clear(juce::dontSendNotification);
		midiInputBox.addItem("MIDI Input: Off", 1);
		for (int index = 0; index < midiInputDevices.size(); ++index)
			midiInputBox.addItem("MIDI: " + midiInputDevices.getReference(index).name, index + 2);
		for (int index = 0; index < midiInputDevices.size(); ++index)
			if (midiInputDevices.getReference(index).identifier == wantedIdentifier)
			{
				midiInputBox.setSelectedId(index + 2, juce::dontSendNotification);
				selectMidiInput(index);
				return;
			}
		disconnectMidiInput();
		midiInputBox.setSelectedId(1, juce::dontSendNotification);
	}

	[[nodiscard]] int rackLatencySamples() const noexcept
	{
		const auto route = rackRoute.load();
		const auto sourceLatency = instrumentProcessor(instrument.load()).getLatencySamples();
		const auto ravLatency = route == 1 || route == 3 || route == 4 ? ravProcessor.getLatencySamples() : 0;
		const auto glimmerLatency = route == 2 || route == 3 || route == 4 ? glimmerProcessor.getLatencySamples() : 0;
		return sourceLatency + ravLatency + glimmerLatency;
	}

	void selectMidiInput(int index)
	{
		disconnectMidiInput();
		if (!juce::isPositiveAndBelow(index, midiInputDevices.size()))
		{
			restoredMidiInputIdentifier.clear();
			return;
		}
		selectedMidiInputIdentifier = midiInputDevices.getReference(index).identifier;
		restoredMidiInputIdentifier = selectedMidiInputIdentifier;
		setMidiInputDeviceEnabled(selectedMidiInputIdentifier, true);
		addMidiInputDeviceCallback(selectedMidiInputIdentifier, &midiCollector);
	}

	void disconnectMidiInput()
	{
		if (selectedMidiInputIdentifier.isEmpty())
			return;
		removeMidiInputDeviceCallback(selectedMidiInputIdentifier, &midiCollector);
		setMidiInputDeviceEnabled(selectedMidiInputIdentifier, false);
		selectedMidiInputIdentifier.clear();
	}

	void timerCallback() override
	{
		if (deviceRefreshPending.exchange(false))
			followSystemDefaultOutput();
		if (const auto availableMidiInputs = juce::MidiInput::getAvailableDevices(); availableMidiInputs != midiInputDevices)
			refreshMidiInputs();
		const auto diagnostics = juce::String(instrument.load() == static_cast<int>(vekt::audio_lab::Instrument::flint)
			? "Flint  In " : "Mono  In ")
			+ juce::String(generatedPeak.load(), 3) + "  Out "
			+ juce::String(outputPeak.load(), 3) + "  Lat "
			+ juce::String(rackLatencySamples()) + "  CPU "
			+ juce::String(pluginCpuLoadPercent.load(), 1) + "%  Output: " + outputDeviceName;
		statusLabel.setText(diagnostics, juce::dontSendNotification);
		statusLabel.setTooltip(diagnostics);
    }

	void changeListenerCallback(juce::ChangeBroadcaster*) override
	{
		// Core Audio notifies JUCE when the macOS device list/default route changes.
		// Switch on the message thread, never from the audio callback.
		deviceRefreshPending.store(true);
	}

	void openInitialOutput()
	{
		const auto defaultName = getSystemDefaultOutputName();
		auto error = defaultName.isEmpty()
			? initialiseWithDefaultDevices(0, 2)
			: initialise(0, 2, nullptr, true, "*" + defaultName + "*");
		if (error.isNotEmpty())
			error = initialiseWithDefaultDevices(0, 2);
		if (error.isEmpty())
			refreshOutputDeviceName();
		else
			outputDeviceName = "Unavailable";
	}

	void followSystemDefaultOutput()
	{
		const auto defaultName = getSystemDefaultOutputName();
		auto* current = getCurrentAudioDevice();
		if (defaultName.isEmpty() || (current != nullptr && current->getName() == defaultName))
		{
			refreshOutputDeviceName();
			return;
		}
		auto setup = getAudioDeviceSetup();
		setup.outputDeviceName = defaultName;
		auto error = setAudioDeviceSetup(setup, false);
		if (error.isNotEmpty())
			error = initialise(0, 2, nullptr, true, "*" + defaultName + "*");
		if (error.isNotEmpty())
			error = initialiseWithDefaultDevices(0, 2);
		if (error.isEmpty())
			refreshOutputDeviceName();
		else
			outputDeviceName = "Unavailable";
	}

	void refreshOutputDeviceName()
	{
		if (auto* current = getCurrentAudioDevice())
			outputDeviceName = current->getName();
		else
			outputDeviceName = "Unavailable";
	}

	vekt::rav::PluginProcessor ravProcessor;
	vekt::glimmer::PluginProcessor glimmerProcessor;
	vekt::kobber::PluginProcessor kobberProcessor;
	vekt::flint::PluginProcessor flintProcessor;
	juce::MidiKeyboardState keyboardState;
	juce::MidiMessageCollector midiCollector;
	juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
	std::unique_ptr<juce::AudioProcessorEditor> kobberEditor;
	std::unique_ptr<juce::AudioProcessorEditor> ravEditor;
	std::unique_ptr<juce::AudioProcessorEditor> glimmerEditor;
	std::unique_ptr<juce::AudioProcessorEditor> flintEditor;
	juce::TextButton restartButton { "Restart App" };
	juce::Label statusLabel;
	juce::ComboBox productTabs;
	juce::ComboBox midiInputBox;
	juce::ComboBox instrumentBox;
	juce::TextButton orderButton { "RAV -> Glimmer" };
	double sampleRateHz { 48'000.0 };
	std::atomic<float> outputPeak {};
	std::atomic<float> generatedPeak {};
	std::atomic<float> pluginCpuLoadPercent {};
	std::atomic<int> rackRoute { 3 };
	std::atomic<int> instrument {}; // Instrument (RackRouting.h), chosen on the message thread
	int playingInstrument {}; // audio thread: the instrument the last block played
	juce::MidiBuffer silenceMidi;
	vekt::audio_lab::LabSettings restoredSettings;
	int restoredSelectedTab { 1 };
	juce::Array<juce::MidiDeviceInfo> midiInputDevices;
	juce::String selectedMidiInputIdentifier;
	juce::String restoredMidiInputIdentifier;
	std::atomic<bool> deviceRefreshPending {};
	juce::String outputDeviceName { "Unavailable" };
	#if JUCE_MAC
	DefaultOutputListener defaultOutputListener;
	#endif
};

class MainWindow final : public juce::DocumentWindow
{
public:
	MainWindow()
		: DocumentWindow("Vekt Audio Lab", juce::Colours::black, closeButton)
	{
		setUsingNativeTitleBar(true);
		setContentOwned(new LiveLab(), true);
		// The lab sizes itself to its widest editor; never let the window shrink below that.
		const auto* lab = getContentComponent();
		constrainer.setMinimumSize(lab->getWidth(), lab->getHeight());
		constrainer.setMaximumSize(lab->getWidth() * 2, lab->getHeight() * 2);
		setResizable(true, true);
		setConstrainer(&constrainer);
		centreWithSize(getWidth(), getHeight());
		setVisible(true);
	}

	void closeButtonPressed() override
	{
		juce::JUCEApplication::getInstance()->systemRequestedQuit();
	}

private:
	juce::ComponentBoundsConstrainer constrainer;
};

class Application final : public juce::JUCEApplication
{
public:
	const juce::String getApplicationName() override { return "Vekt Audio Lab"; }
	const juce::String getApplicationVersion() override { return "0.1.0"; }
	bool moreThanOneInstanceAllowed() override { return true; }

	void initialise(const juce::String&) override
	{
		mainWindow = std::make_unique<MainWindow>();
		juce::Process::makeForegroundProcess();
		mainWindow->toFront(true);
		mainWindow->grabKeyboardFocus();
	}

	void shutdown() override
	{
		if (mainWindow != nullptr)
			mainWindow->setVisible(false);
		mainWindow.reset();
	}
	void systemRequestedQuit() override { quit(); }
	void anotherInstanceStarted(const juce::String&) override {}

private:
	std::unique_ptr<MainWindow> mainWindow;
};
}

START_JUCE_APPLICATION(Application)
