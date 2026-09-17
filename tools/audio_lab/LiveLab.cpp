#include "SignalSources.h"

#include <PluginProcessor.h>
#include <PluginEditor.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace
{
class LiveLab final : public juce::AudioAppComponent,
						 private juce::Timer
{
public:
	LiveLab()
	{
        sourceBox.addItem("Sine", 1);
        sourceBox.addItem("Sawtooth", 2);
        sourceBox.addItem("Sweep", 3);
		sourceBox.addItem("Impulse", 4);
		sourceBox.addItem("Noise", 5);
        sourceBox.addItem("Kick", 6);
        sourceBox.setSelectedId(1, juce::dontSendNotification);
        for (auto *component : {static_cast<juce::Component *>(&sourceBox),
                                static_cast<juce::Component *>(&octaveDownButton), static_cast<juce::Component *>(&octaveUpButton),
                                static_cast<juce::Component *>(&armButton),
                                static_cast<juce::Component *>(&muteButton), static_cast<juce::Component *>(&restartButton),
                                static_cast<juce::Component *>(&statusLabel)})
            addAndMakeVisible(*component);

		sourceBox.onChange = [this] { requestedSource.store(sourceBox.getSelectedId() - 1); };
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
		muteButton.onClick = [this] { outputArmed.store(false); armButton.setToggleState(false, juce::dontSendNotification); };
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
		setSize(800, 640);
		setAudioChannels(0, 2);
		startTimerHz(15);
	}

	~LiveLab() override
	{
		stopTimer();
		shutdownAudio();
	}

	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
	{
		processor.prepareToPlay(sampleRate, samplesPerBlockExpected);
		source.prepare(vekt::audio_lab::Source::sine, sampleRate);
		sampleRateHz = sampleRate;
		sampleIndex = 0;
	}

	void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
	{
		const auto sourceType = static_cast<vekt::audio_lab::Source>(requestedSource.load());
		if (sourceType != currentSource)
		{
			currentSource = sourceType;
			source.prepare(sourceType, sampleRateHz);
            source.setOctave(requestedOctave.load());
            sampleIndex = 0;
		}
        source.setOctave(requestedOctave.load());

        for (auto sample = 0; sample < info.numSamples; ++sample)
		{
			const auto value = source.next(sampleIndex++);
			generatedPeak.store(std::max(generatedPeak.load(), std::abs(value)));
			for (auto channel = 0; channel < info.buffer->getNumChannels(); ++channel)
				info.buffer->setSample(channel, info.startSample + sample, value);
		}

		juce::MidiBuffer midi;
		juce::AudioBuffer<float> block(info.buffer->getArrayOfWritePointers(),
			info.buffer->getNumChannels(), info.startSample, info.numSamples);
		processor.processBlock(block, midi);
		if (!outputArmed.load())
			info.clearActiveBufferRegion();
		else
			publishPeak(block);
	}

	void releaseResources() override
	{
		processor.releaseResources();
	}

	void paint(juce::Graphics& graphics) override
	{
		graphics.fillAll(juce::Colour::fromRGB(20, 24, 28));
		graphics.setColour(juce::Colours::white);
		graphics.setFont(juce::FontOptions(22.0f).withStyle("Bold"));
		graphics.drawText("VEKT RAV AUDIO LAB", 20, 4, 440, 24, juce::Justification::centredLeft);
		graphics.setColour(juce::Colour::fromRGB(54, 65, 70));
		graphics.drawLine(20.0f, 64.0f, 780.0f, 64.0f);
	}

	void resized() override
	{
		sourceBox.setBounds(20, 33, 180, 26);
        octaveDownButton.setBounds(215, 33, 34, 26);
        octaveUpButton.setBounds(253, 33, 34, 26);
        armButton.setBounds(295, 33, 130, 26);
        muteButton.setBounds(435, 33, 100, 26);
        restartButton.setBounds(545, 33, 115, 26);
        statusLabel.setBounds(670, 33, 100, 26);
        if (editor != nullptr)
		{
			const auto editorArea = getLocalBounds().withTop(75).reduced(20, 0);
			const auto scale = std::min(
				static_cast<float>(editorArea.getWidth()) / 720.0f,
				static_cast<float>(editorArea.getHeight()) / 480.0f);
			const auto editorWidth = static_cast<int>(720.0f * scale);
			const auto editorHeight = static_cast<int>(480.0f * scale);
			editor->setBounds(editorArea.withSizeKeepingCentre(editorWidth, editorHeight));
		}
	}

private:
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
		statusLabel.setText(
			(outputArmed.load() ? "OUTPUT ARMED" : "Muted") + juce::String("  In ")
			+ juce::String(generatedPeak.load(), 3) + "  Out "
			+ juce::String(outputPeak.load(), 3) + "  Latency "
			+ juce::String(processor.getLatencySamples()),
            juce::dontSendNotification);
    }

	vekt::rav::PluginProcessor processor;
	std::unique_ptr<juce::AudioProcessorEditor> editor;
	juce::ComboBox sourceBox;
    juce::TextButton octaveDownButton{"-"};
    juce::TextButton octaveUpButton{"+"};
    juce::ToggleButton armButton { "Arm output" };
	juce::TextButton muteButton { "MUTE" };
	juce::TextButton restartButton { "Restart App" };
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
};

class MainWindow final : public juce::DocumentWindow
{
public:
	MainWindow()
		: DocumentWindow("Vekt Rav Audio Lab", juce::Colours::black, closeButton)
	{
		setUsingNativeTitleBar(true);
		constrainer.setMinimumSize(800, 620);
		constrainer.setMaximumSize(1'600, 1'200);
		setResizable(true, true);
		setConstrainer(&constrainer);
		setContentOwned(new LiveLab(), true);
		centreWithSize(840, 680);
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
