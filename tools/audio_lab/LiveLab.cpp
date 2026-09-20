#include "SignalSources.h"
#include "FileSource.h"
#include "LabSettings.h"

#include <PluginProcessor.h>
#include <PluginEditor.h>
#include <vekt/glimmer/PluginProcessor.h>
#include <vekt/mono/PluginProcessor.h>

#include <juce_audio_utils/juce_audio_utils.h>

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>
#endif

#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace
{
constexpr int labWidth = vekt::ui::ScalableEditor::logicalWidth + 32;
constexpr int labHeight = vekt::ui::ScalableEditor::logicalHeight + 272;

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
						 public juce::FileDragAndDropTarget,
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
		requestedOctave.store(restoredSettings.octave);
        sourceBox.addItem("Sine", 1);
        sourceBox.addItem("Sawtooth", 2);
        sourceBox.addItem("Sweep", 3);
		sourceBox.addItem("Impulse", 4);
		sourceBox.addItem("Noise", 5);
        sourceBox.addItem("Kick", 6);
		sourceBox.addItem("Unison", 7);
		sourceBox.addItem("Two Tone", 8);
		sourceBox.addItem("Audio File", 9);
		sourceBox.addItem("Vekt Mono", 10);
		sourceBox.setSelectedId(restoredSettings.source + 1, juce::dontSendNotification);
        for (auto *component : {static_cast<juce::Component *>(&sourceBox),
                                static_cast<juce::Component *>(&octaveDownButton), static_cast<juce::Component *>(&octaveUpButton),
                                static_cast<juce::Component *>(&armButton),
                                static_cast<juce::Component *>(&muteButton), static_cast<juce::Component *>(&restartButton),
                                static_cast<juce::Component *>(&statusLabel),
			static_cast<juce::Component *>(&openFileButton), static_cast<juce::Component *>(&restartFileButton),
			static_cast<juce::Component *>(&fileLabel), static_cast<juce::Component *>(&positionLabel),
			static_cast<juce::Component *>(&productTabs), static_cast<juce::Component *>(&orderButton),
			static_cast<juce::Component *>(&midiInputBox),
								})
            addAndMakeVisible(*component);
		addAndMakeVisible(keyboard);

		sourceBox.onChange = [this]
		{
			const auto fileMode = sourceBox.getSelectedId() == 9;
			const auto monoMode = sourceBox.getSelectedId() == 10;
			requestedSource.store(sourceBox.getSelectedId() - 1);
			octaveDownButton.setEnabled(!fileMode && !monoMode);
			octaveUpButton.setEnabled(!fileMode && !monoMode);
			restartFileButton.setEnabled(fileMode && fileLoaded);
			keyboard.setVisible(monoMode);
			if (monoMode)
				productTabs.setSelectedId(1, juce::sendNotification);
			orderButton.setTooltip("Process the selected source through this rack route.");
		};
		sourceBox.onChange();
		fileLabel.setText("Drop WAV, MP3, FLAC or AIFF/AIF here", juce::dontSendNotification);
		fileLabel.setTooltip("Drop one mono or stereo audio file anywhere in Audio Lab. Files loop at their original level.");
		restartFileButton.setEnabled(false);
		restartFileButton.onClick = [this] { fileSource.restart(); };
		openFileButton.onClick = [this]
		{
			chooser = std::make_unique<juce::FileChooser>("Open audio file", juce::File(), "*.wav;*.mp3;*.flac;*.aiff;*.aif");
			chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
				[safe = juce::Component::SafePointer<LiveLab>(this)](const juce::FileChooser& dialog)
				{
					if (safe != nullptr && dialog.getResult() != juce::File())
						safe->loadFile(dialog.getResult());
				});
		};
        octaveDownButton.onClick = [this]
        { requestedOctave.store(std::max(-3, requestedOctave.load() - 1)); };
        octaveUpButton.onClick = [this]
        { requestedOctave.store(std::min(3, requestedOctave.load() + 1)); };
        armButton.setClickingTogglesState(true);
		setOutputArmed(restoredSettings.outputArmed);
		armButton.onClick = [this]
		{
			setOutputArmed(armButton.getToggleState());
		};
		productTabs.addItem("Mono", 1);
		productTabs.addItem("RAV", 2);
		productTabs.addItem("Glimmer", 3);
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
		muteButton.onClick = [this] { setOutputArmed(false); };
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
		monoEditor.reset(monoProcessor.createEditor());
		if (restoredSettings.audioFilePath.isNotEmpty())
			loadFile(juce::File(restoredSettings.audioFilePath), restoredSettings.source == 8);
		keyboard.setKeyPressBaseOctave(4);
		keyboard.setOctaveForMiddleC(4);
		keyboard.setWantsKeyboardFocus(true);
		keyboardState.addListener(&midiCollector);
		for (auto* editor : { monoEditor.get(), ravEditor.get(), glimmerEditor.get() })
		{
			addAndMakeVisible(*editor);
			if (auto* scalableEditor = dynamic_cast<vekt::ui::ScalableEditor*>(editor))
			{
				scalableEditor->setResizeHandleVisible(false);
				scalableEditor->setResizable(false, false);
			}
		}
		showProductEditor(productTabs.getSelectedId());
		keyboard.setVisible(false);
		setSize(labWidth, labHeight);
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
		chooser.reset();
		loader.removeAllJobs(true, -1);
		shutdownAudio();
	}

	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
	{
		ravProcessor.prepareToPlay(sampleRate, samplesPerBlockExpected);
		glimmerProcessor.prepareToPlay(sampleRate, samplesPerBlockExpected);
		monoProcessor.prepareToPlay(sampleRate, samplesPerBlockExpected);
		midiCollector.reset(sampleRate);
		midiCollector.ensureStorageAllocated(4096);
		fileSource.prepare(samplesPerBlockExpected, sampleRate);
		currentSource = static_cast<vekt::audio_lab::Source>(-1);
		source.prepare(vekt::audio_lab::Source::sine, sampleRate);
		sampleRateHz = sampleRate;
		sampleIndex = 0;
	}

	void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
	{
		if (!outputArmed.load())
		{
			info.clearActiveBufferRegion();
			generatedPeak.store(0.0f);
			outputPeak.store(0.0f);
			pluginCpuLoadPercent.store(0.0f);
			return;
		}
		const auto monoMode = requestedSource.load() == 9;
		juce::MidiBuffer midi;
		midiCollector.removeNextBlockOfMessages(midi, info.numSamples);
		if (monoMode)
		{
			info.clearActiveBufferRegion();
			juce::AudioBuffer<float> monoBlock(info.buffer->getArrayOfWritePointers(),
				info.buffer->getNumChannels(), info.startSample, info.numSamples);
			monoProcessor.processBlock(monoBlock, midi);
		}
		else if (requestedSource.load() == 8)
			fileSource.render(info);
		else
		{
			const auto sourceType = static_cast<vekt::audio_lab::Source>(requestedSource.load());
			if (sourceType != currentSource)
			{
				currentSource = sourceType;
				source.prepare(sourceType, sampleRateHz);
				sampleIndex = 0;
			}
			source.setOctave(requestedOctave.load());
			for (auto sample = 0; sample < info.numSamples; ++sample)
			{
				const auto sourceSample = sampleIndex++;
				for (auto channel = 0; channel < info.buffer->getNumChannels(); ++channel)
					info.buffer->setSample(channel, info.startSample + sample, source.next(sourceSample, channel));
			}
		}
		auto inputPeak = 0.0f;
		for (auto channel = 0; channel < info.buffer->getNumChannels(); ++channel)
			inputPeak = std::max(inputPeak, info.buffer->getMagnitude(channel, info.startSample, info.numSamples));
		generatedPeak.store(inputPeak);

		juce::AudioBuffer<float> block(info.buffer->getArrayOfWritePointers(),
			info.buffer->getNumChannels(), info.startSample, info.numSamples);
		const auto startTicks = juce::Time::getHighResolutionTicks();
		switch (rackRoute.load())
		{
		case 1: ravProcessor.processBlock(block, midi); break;
		case 2: glimmerProcessor.processBlock(block, midi); break;
		case 3: ravProcessor.processBlock(block, midi); glimmerProcessor.processBlock(block, midi); break;
		case 4: glimmerProcessor.processBlock(block, midi); ravProcessor.processBlock(block, midi); break;
		default: break;
		}
		const auto elapsedTicks = juce::Time::getHighResolutionTicks() - startTicks;
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
		fileSource.release();
		monoProcessor.releaseResources();
		ravProcessor.releaseResources();
		glimmerProcessor.releaseResources();
	}

	bool keyPressed(const juce::KeyPress& key) override
	{
		// Keep the Audio Lab's QWERTY keyboard active while another product editor
		// owns focus. MidiKeyboardComponent normally receives keys only when the
		// on-screen keyboard itself is focused.
		return requestedSource.load() == 9 && keyboard.keyPressed(key);
	}

	bool keyStateChanged(bool isKeyDown) override
	{
		return requestedSource.load() == 9 && keyboard.keyStateChanged(isKeyDown);
	}

	void paint(juce::Graphics& graphics) override
	{
		graphics.fillAll(juce::Colour::fromRGB(20, 24, 28));
		graphics.setColour(juce::Colours::white);
		graphics.setFont(juce::FontOptions(22.0f).withStyle("Bold"));
		graphics.drawText("VEKT AUDIO LAB", 16, 12, 440, 32, juce::Justification::centredLeft);
		graphics.setColour(juce::Colour::fromRGB(54, 65, 70));
		graphics.drawLine(16.0f, 160.0f, static_cast<float>(getWidth() - 16), 160.0f);
		if (draggingFile)
		{
			graphics.setColour(juce::Colour::fromRGB(123, 191, 173));
			graphics.drawRoundedRectangle(fileLabel.getBounds().toFloat().expanded(2.0f), 4.0f, 2.0f);
		}
	}

	void resized() override
	{
		sourceBox.setBounds(16, 56, 180, 44);
		octaveDownButton.setBounds(212, 56, 44, 44);
		octaveUpButton.setBounds(264, 56, 44, 44);
		armButton.setBounds(316, 56, 140, 44);
		muteButton.setBounds(464, 56, 100, 44);
		restartButton.setBounds(572, 56, 132, 44);
		statusLabel.setBounds(720, 56, getWidth() - 736, 44);
		productTabs.setBounds(712, 112, 150, 40);
		orderButton.setBounds(872, 112, 152, 40);
		midiInputBox.setBounds(16, 168, 260, 32);
        openFileButton.setBounds(16, 112, 120, 40);
		restartFileButton.setBounds(144, 112, 120, 40);
		fileLabel.setBounds(280, 112, getWidth() - 500, 40);
		positionLabel.setBounds(getWidth() - 212, 112, 196, 40);
		keyboard.setBounds(284, 168, getWidth() - 300, 72);
		for (auto* editor : { monoEditor.get(), ravEditor.get(), glimmerEditor.get() })
		{
			const auto editorArea = getLocalBounds().withTop(256).withTrimmedBottom(16).reduced(16, 0);
			const auto scale = std::min(
				static_cast<float>(editorArea.getWidth()) / vekt::ui::ScalableEditor::logicalWidth,
				static_cast<float>(editorArea.getHeight()) / vekt::ui::ScalableEditor::logicalHeight);
			const auto editorWidth = static_cast<int>(vekt::ui::ScalableEditor::logicalWidth * scale);
			const auto editorHeight = static_cast<int>(vekt::ui::ScalableEditor::logicalHeight * scale);
			editor->setBounds(editorArea.withSizeKeepingCentre(editorWidth, editorHeight));
		}
	}

	bool isInterestedInFileDrag(const juce::StringArray& files) override
	{
		return files.size() == 1 && vekt::audio_lab::acceptsAudioFile(juce::File(files[0]));
	}
	void fileDragEnter(const juce::StringArray&, int, int) override { draggingFile = true; repaint(); }
	void fileDragExit(const juce::StringArray&) override { draggingFile = false; repaint(); }
	void filesDropped(const juce::StringArray& files, int, int) override
	{
		draggingFile = false;
		repaint();
		if (files.size() == 1)
			loadFile(juce::File(files[0]));
	}

private:
	void setOutputArmed(bool shouldArm)
	{
		outputArmed.store(shouldArm);
		armButton.setToggleState(shouldArm, juce::dontSendNotification);
		armButton.setButtonText(shouldArm ? "Output armed" : "Arm output");
	}

	void showProductEditor(int tab)
	{
		monoEditor->setVisible(tab == 1);
		ravEditor->setVisible(tab == 2);
		glimmerEditor->setVisible(tab == 3);
		auto* editor = tab == 1 ? monoEditor.get() : tab == 2 ? ravEditor.get() : glimmerEditor.get();
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
		restoreProcessorState(monoProcessor, state.getProperty("monoState", {}).toString());
		restoreProcessorState(ravProcessor, state.getProperty("ravState", {}).toString());
		restoreProcessorState(glimmerProcessor, state.getProperty("glimmerState", {}).toString());
		restoredMidiInputIdentifier = restoredSettings.midiInputIdentifier;
	}

	void saveRackState()
	{
		juce::ValueTree state("VektAudioLabRackState");
		state.setProperty("schemaVersion", 1, nullptr);
		vekt::audio_lab::writeLabSettings(state, {
			requestedSource.load(),
			requestedOctave.load(),
			productTabs.getSelectedId(),
			rackRoute.load(),
			restoredMidiInputIdentifier,
			loadedAudioFile.getFullPathName(),
			outputArmed.load()
		});
		const auto capture = [](juce::AudioProcessor& processor)
		{
			juce::MemoryBlock data;
			processor.getStateInformation(data);
			return data.toBase64Encoding();
		};
		state.setProperty("monoState", capture(monoProcessor), nullptr);
		state.setProperty("ravState", capture(ravProcessor), nullptr);
		state.setProperty("glimmerState", capture(glimmerProcessor), nullptr);
		const auto file = rackStateFile();
		file.getParentDirectory().createDirectory();
		juce::TemporaryFile temporary(file);
		if (const auto xml = state.createXml(); xml != nullptr
			&& temporary.getFile().replaceWithText(xml->toString()))
			juce::ignoreUnused(temporary.overwriteTargetFileWithTemporary());
	}

	void loadFile(const juce::File& file, bool selectSource = true)
	{
		const auto generation = ++loadGeneration;
		loadedAudioFile = file;
		fileLabel.setText("Loading " + file.getFileName() + "...", juce::dontSendNotification);
		loader.addJob([safe = juce::Component::SafePointer<LiveLab>(this), file, generation, selectSource]
		{
			auto result = std::make_shared<vekt::audio_lab::FileLoadResult>(vekt::audio_lab::openAudioFile(file));
			juce::MessageManager::callAsync([safe, result, generation, selectSource]
			{
				if (safe == nullptr || safe->loadGeneration != generation)
					return;
				if (!result->reader)
				{
					safe->fileLabel.setText(result->error, juce::dontSendNotification);
					safe->fileLabel.setTooltip(result->error);
					return;
				}
				safe->fileSource.install(std::move(result->reader));
				safe->fileLoaded = true;
				safe->loadedAudioFile = result->file;
				safe->fileLabel.setText(result->file.getFileName(), juce::dontSendNotification);
				safe->fileLabel.setTooltip(result->file.getFullPathName());
				if (selectSource)
				{
					safe->sourceBox.setSelectedId(9, juce::dontSendNotification);
					safe->sourceBox.onChange();
				}
			});
		});
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
		const auto sourceLatency = requestedSource.load() == 9 ? monoProcessor.getLatencySamples() : 0;
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
		if (fileLoaded)
		{
			const auto time = [](double seconds)
			{
				const auto whole = static_cast<int>(seconds);
				return juce::String(whole / 60) + ":" + juce::String(whole % 60).paddedLeft('0', 2);
			};
			positionLabel.setText(time(fileSource.getPosition()) + " / " + time(fileSource.getDuration()) + "  Loop",
				juce::dontSendNotification);
		}
		statusLabel.setText(
			(outputArmed.load() ? "OUTPUT ARMED" : "Muted") + juce::String("  In ")
			+ juce::String(generatedPeak.load(), 3) + "  Out "
			+ juce::String(outputPeak.load(), 3) + "  Latency "
			+ juce::String(rackLatencySamples()) + "  CPU "
			+ juce::String(pluginCpuLoadPercent.load(), 1) + "%  Output: " + outputDeviceName,
            juce::dontSendNotification);
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

	vekt::audio_lab::FileSource fileSource;
	juce::ThreadPool loader { 1 };
	std::unique_ptr<juce::FileChooser> chooser;
	juce::TextButton openFileButton { "Open File..." };
	juce::TextButton restartFileButton { "Restart File" };
	juce::Label fileLabel;
	juce::Label positionLabel;
	bool fileLoaded {};
	juce::File loadedAudioFile;
	bool draggingFile {};
	unsigned int loadGeneration {};
	vekt::rav::PluginProcessor ravProcessor;
	vekt::glimmer::PluginProcessor glimmerProcessor;
	vekt::mono::PluginProcessor monoProcessor;
	juce::MidiKeyboardState keyboardState;
	juce::MidiMessageCollector midiCollector;
	juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
	std::unique_ptr<juce::AudioProcessorEditor> monoEditor;
	std::unique_ptr<juce::AudioProcessorEditor> ravEditor;
	std::unique_ptr<juce::AudioProcessorEditor> glimmerEditor;
	juce::ComboBox sourceBox;
    juce::TextButton octaveDownButton{"-"};
    juce::TextButton octaveUpButton{"+"};
    juce::ToggleButton armButton { "Arm output" };
	juce::TextButton muteButton { "MUTE" };
	juce::TextButton restartButton { "Restart App" };
	juce::Label statusLabel;
	juce::ComboBox productTabs;
	juce::ComboBox midiInputBox;
	juce::TextButton orderButton { "RAV -> Glimmer" };
	vekt::audio_lab::SignalSource source;
	vekt::audio_lab::Source currentSource { static_cast<vekt::audio_lab::Source>(-1) };
    std::atomic<int> requestedSource{};
    std::atomic<int> requestedOctave{};
    double sampleRateHz { 48'000.0 };
	std::int64_t sampleIndex {};
	std::atomic<bool> outputArmed {};
	std::atomic<float> outputPeak {};
	std::atomic<float> generatedPeak {};
	std::atomic<float> pluginCpuLoadPercent {};
	std::atomic<int> rackRoute { 3 };
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
		constrainer.setMinimumSize(labWidth, labHeight);
		constrainer.setMaximumSize(labWidth * 2, labHeight * 2);
		setResizable(true, true);
		setConstrainer(&constrainer);
		setContentOwned(new LiveLab(), true);
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
