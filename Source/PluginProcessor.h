#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class SliceReverseProcessor : public juce::AudioProcessor
{
public:
    SliceReverseProcessor();
    ~SliceReverseProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SliceReverse"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Length of one slice in quarter notes for the "Slice Size" choice.
    static double sliceLengthQuarters (int choice, double beatsPerBar);

    // Two ping-pong buffers: one being recorded, one being played back.
    juce::AudioBuffer<float> capture, playback;
    int captureCount = 0;
    int playbackLen = 0;
    bool captureValid = false;
    bool playbackReversed = true;
    juce::int64 lastSliceIndex = std::numeric_limits<juce::int64>::min();

    double freeRunPpq = 0.0; // used when the host is stopped / gives no position
    double sampleRateHz = 44100.0;
    int reportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceReverseProcessor)
};
