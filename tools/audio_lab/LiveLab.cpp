#include "SignalSources.h"
#include "FileSource.h"

#include <PluginProcessor.h>
#include <PluginEditor.h>

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
constexpr int labHeight = vekt::ui::ScalableEditor::logicalHeight + 244;
constexpr float modelSwitchFadeSeconds = 0.012f;

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
        sourceBox.addItem("Sine", 1);
        sourceBox.addItem("Sawtooth", 2);
        sourceBox.addItem("Sweep", 3);
		sourceBox.addItem("Impulse", 4);
		sourceBox.addItem("Noise", 5);
        sourceBox.addItem("Kick", 6);
		sourceBox.addItem("Unison", 7);
		sourceBox.addItem("Two Tone", 8);
		sourceBox.addItem("Audio File", 9);
        sourceBox.setSelectedId(1, juce::dontSendNotification);
        for (auto *component : {static_cast<juce::Component *>(&sourceBox),
                                static_cast<juce::Component *>(&octaveDownButton), static_cast<juce::Component *>(&octaveUpButton),
                                static_cast<juce::Component *>(&armButton),
                                static_cast<juce::Component *>(&muteButton), static_cast<juce::Component *>(&restartButton),
                                static_cast<juce::Component *>(&statusLabel),
			static_cast<juce::Component *>(&openFileButton), static_cast<juce::Component *>(&restartFileButton),
			static_cast<juce::Component *>(&fileLabel), static_cast<juce::Component *>(&positionLabel),
			static_cast<juce::Component *>(&modelLabel), static_cast<juce::Component *>(&modelAbButton)})
            addAndMakeVisible(*component);

		sourceBox.onChange = [this]
		{
			const auto fileMode = sourceBox.getSelectedId() == 9;
			requestedSource.store(sourceBox.getSelectedId() - 1);
			octaveDownButton.setEnabled(!fileMode);
			octaveUpButton.setEnabled(!fileMode);
			restartFileButton.setEnabled(fileMode && fileLoaded);
		};
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
		armButton.onClick = [this]
		{
			outputArmed.store(armButton.getToggleState());
			armButton.setButtonText(outputArmed.load() ? "Output armed" : "Arm output");
		};
		muteButton.onClick = [this] { outputArmed.store(false); armButton.setToggleState(false, juce::dontSendNotification); armButton.setButtonText("Arm output"); };
		modelLabel.setText("DEV MODEL — not saved", juce::dontSendNotification);
		modelLabel.setJustificationType(juce::Justification::centredRight);
		modelAbButton.setTooltip("A/B switches between Legacy and the experimental Fuzz Circuit model. The switch fades output briefly and is not saved.");
		modelAbButton.onClick = [this]
		{
			const auto next = requestedModel.load() == vekt::rav::RavProcessingModel::legacy
				? vekt::rav::RavProcessingModel::fuzzCircuitCandidate
				: vekt::rav::RavProcessingModel::legacy;
			requestedModel.store(next);
		};
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
		editor.reset(processor.createEditor());
		if (editor != nullptr)
		{
			addAndMakeVisible(*editor);
			if (auto* scalableEditor = dynamic_cast<vekt::ui::ScalableEditor*>(editor.get()))
			{
				scalableEditor->setResizeHandleVisible(false);
				scalableEditor->setResizable(false, false);
			}
		}
		setSize(labWidth, labHeight);
		setAudioChannels(0, 2);
		addChangeListener(this);
		refreshOutputDeviceName();
		startTimerHz(15);
	}

	~LiveLab() override
	{
		stopTimer();
		removeChangeListener(this);
		chooser.reset();
		loader.removeAllJobs(true, -1);
		shutdownAudio();
	}

	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
	{
		processor.prepareToPlay(sampleRate, samplesPerBlockExpected);
		activeModel = requestedModel.load();
		processor.setDevelopmentProcessingModel(activeModel);
		modelSwitchState = ModelSwitchState::steady;
		modelFadeSamples = std::max(1, static_cast<int>(std::round(sampleRate * modelSwitchFadeSeconds)));
		modelFadeGain = 1.0f;
		modelFadeSamplesRemaining = 0;
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
		beginModelSwitchIfRequested();

		if (requestedSource.load() == 8)
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

		juce::MidiBuffer midi;
		juce::AudioBuffer<float> block(info.buffer->getArrayOfWritePointers(),
			info.buffer->getNumChannels(), info.startSample, info.numSamples);
		const auto startTicks = juce::Time::getHighResolutionTicks();
		processor.processBlock(block, midi);
		applyModelSwitchFade(block);
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
		processor.releaseResources();
	}

	void paint(juce::Graphics& graphics) override
	{
		graphics.fillAll(juce::Colour::fromRGB(20, 24, 28));
		graphics.setColour(juce::Colours::white);
		graphics.setFont(juce::FontOptions(22.0f).withStyle("Bold"));
		graphics.drawText("VEKT RAV AUDIO LAB", 16, 12, 440, 32, juce::Justification::centredLeft);
		graphics.setColour(juce::Colour::fromRGB(54, 65, 70));
		graphics.drawLine(16.0f, 212.0f, static_cast<float>(getWidth() - 16), 212.0f);
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
        openFileButton.setBounds(16, 112, 120, 40);
		restartFileButton.setBounds(144, 112, 120, 40);
		fileLabel.setBounds(280, 112, getWidth() - 500, 40);
		positionLabel.setBounds(getWidth() - 212, 112, 196, 40);
		modelLabel.setBounds(16, 160, 200, 40);
		modelAbButton.setBounds(224, 160, 180, 40);
        if (editor != nullptr)
		{
			const auto editorArea = getLocalBounds().withTop(228).withTrimmedBottom(16).reduced(16, 0);
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
	void loadFile(const juce::File& file)
	{
		const auto generation = ++loadGeneration;
		fileLabel.setText("Loading " + file.getFileName() + "...", juce::dontSendNotification);
		loader.addJob([safe = juce::Component::SafePointer<LiveLab>(this), file, generation]
		{
			auto result = std::make_shared<vekt::audio_lab::FileLoadResult>(vekt::audio_lab::openAudioFile(file));
			juce::MessageManager::callAsync([safe, result, generation]
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
				safe->fileLabel.setText(result->file.getFileName(), juce::dontSendNotification);
				safe->fileLabel.setTooltip(result->file.getFullPathName());
				safe->sourceBox.setSelectedId(9, juce::dontSendNotification);
				safe->sourceBox.onChange();
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

	void timerCallback() override
	{
		if (deviceRefreshPending.exchange(false))
			followSystemDefaultOutput();
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
			+ juce::String(processor.getLatencySamples()) + "  CPU "
			+ juce::String(pluginCpuLoadPercent.load(), 1) + "%  Output: " + outputDeviceName,
            juce::dontSendNotification);
		const auto model = activeModel.load();
		modelAbButton.setButtonText(model == vekt::rav::RavProcessingModel::legacy
			? "A/B: Legacy" : "A/B: Fuzz Circuit");
    }

	void changeListenerCallback(juce::ChangeBroadcaster*) override
	{
		// Core Audio notifies JUCE when the macOS device list/default route changes.
		// Switch on the message thread, never from the audio callback.
		deviceRefreshPending.store(true);
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
		closeAudioDevice();
		const auto error = initialise(0, 2, nullptr, true);
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

	void beginModelSwitchIfRequested() noexcept
	{
		if (modelSwitchState != ModelSwitchState::steady || requestedModel.load() == activeModel.load())
			return;
		modelFadeSamplesRemaining = modelFadeSamples;
		modelSwitchState = ModelSwitchState::fadingOut;
	}

	void applyModelSwitchFade(juce::AudioBuffer<float>& block) noexcept
	{
		for (auto sample = 0; sample < block.getNumSamples(); ++sample)
		{
			if (modelSwitchState == ModelSwitchState::fadingOut)
				modelFadeGain = static_cast<float>(modelFadeSamplesRemaining)
					/ static_cast<float>(modelFadeSamples);
			else if (modelSwitchState == ModelSwitchState::fadingIn)
				modelFadeGain = 1.0f - static_cast<float>(modelFadeSamplesRemaining)
					/ static_cast<float>(modelFadeSamples);

			for (auto channel = 0; channel < block.getNumChannels(); ++channel)
				block.setSample(channel, sample, block.getSample(channel, sample) * modelFadeGain);

			if (modelSwitchState == ModelSwitchState::steady)
				continue;
			if (--modelFadeSamplesRemaining > 0)
				continue;

			if (modelSwitchState == ModelSwitchState::fadingOut)
			{
				activeModel.store(requestedModel.load());
				processor.setDevelopmentProcessingModel(activeModel.load());
				modelSwitchState = ModelSwitchState::fadingIn;
				modelFadeSamplesRemaining = modelFadeSamples;
			}
			else
			{
				modelSwitchState = ModelSwitchState::steady;
				modelFadeGain = 1.0f;
			}
		}
	}

	vekt::audio_lab::FileSource fileSource;
	juce::ThreadPool loader { 1 };
	std::unique_ptr<juce::FileChooser> chooser;
	juce::TextButton openFileButton { "Open File..." };
	juce::TextButton restartFileButton { "Restart File" };
	juce::Label fileLabel;
	juce::Label positionLabel;
	bool fileLoaded {};
	bool draggingFile {};
	unsigned int loadGeneration {};
	vekt::rav::PluginProcessor processor;
	std::unique_ptr<juce::AudioProcessorEditor> editor;
	juce::ComboBox sourceBox;
    juce::TextButton octaveDownButton{"-"};
    juce::TextButton octaveUpButton{"+"};
    juce::ToggleButton armButton { "Arm output" };
	juce::TextButton muteButton { "MUTE" };
	juce::TextButton restartButton { "Restart App" };
	juce::Label modelLabel;
	juce::TextButton modelAbButton { "A/B: Legacy" };
	juce::Label statusLabel;
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
	enum class ModelSwitchState
	{
		steady,
		fadingOut,
		fadingIn
	};
	std::atomic<vekt::rav::RavProcessingModel> requestedModel { vekt::rav::RavProcessingModel::legacy };
	std::atomic<vekt::rav::RavProcessingModel> activeModel { vekt::rav::RavProcessingModel::legacy };
	ModelSwitchState modelSwitchState { ModelSwitchState::steady };
	int modelFadeSamples {};
	int modelFadeSamplesRemaining {};
	float modelFadeGain { 1.0f };
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
		: DocumentWindow("Vekt Rav Audio Lab", juce::Colours::black, closeButton)
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
	const juce::String getApplicationName() override { return "Vekt Rav Audio Lab"; }
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
