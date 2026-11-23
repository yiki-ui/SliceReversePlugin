#include "PluginProcessor.h"

namespace
{
    const juce::StringArray kSliceNames { "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars" };
}

juce::AudioProcessorValueTreeState::ParameterLayout SliceReverseProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "slice", 1 }, "Slice Size", kSliceNames, 3));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "every", 1 }, "Reverse Every Nth Slice", 1, 8, 1));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
                                                        NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "fade", 1 }, "Edge Fade (ms)",
                                                        NormalisableRange<float> (0.0f, 20.0f, 0.1f), 3.0f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "pdc", 1 }, "Delay Compensation", true));

    return { p.begin(), p.end() };
}

SliceReverseProcessor::SliceReverseProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

double SliceReverseProcessor::sliceLengthQuarters (int choice, double beatsPerBar)
{
    switch (choice)
    {
        case 0: return 0.125;
        case 1: return 0.25;
        case 2: return 0.5;
        case 3: return 1.0;
        case 4: return 2.0;
        case 5: return beatsPerBar;
        default: return beatsPerBar * 2.0;
    }
}

bool SliceReverseProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void SliceReverseProcessor::prepareToPlay (double sr, int)
{
    sampleRateHz = sr;
    const int channels = juce::jmax (1, getTotalNumInputChannels());
    const int capacity = (int) (sr * 20.0); // enough for 2 bars at ~24 BPM
    capture.setSize (channels, capacity);
    playback.setSize (channels, capacity);
    capture.clear();
    playback.clear();

    captureCount = playbackLen = 0;
    captureValid = false;
    lastSliceIndex = std::numeric_limits<juce::int64>::min();
    freeRunPpq = 0.0;
    reportedLatency = -1;
}

void SliceReverseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), capture.getNumChannels());
    const int capacity = capture.getNumSamples();

    // ---- host timing -------------------------------------------------------
    double bpm = 120.0, beatsPerBar = 4.0, startPpq = freeRunPpq;
    bool hostPlaying = false;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto t = pos->getBpm())
                bpm = juce::jlimit (20.0, 400.0, *t);
            if (auto ts = pos->getTimeSignature())
                beatsPerBar = ts->numerator * 4.0 / (double) juce::jmax (1, ts->denominator);
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition())
                {
                    startPpq = *ppq;
                    hostPlaying = true;
                }
        }

    const double ppqPerSample = bpm / 60.0 / sampleRateHz;
    const double sliceQ = sliceLengthQuarters ((int) apvts.getRawParameterValue ("slice")->load(), beatsPerBar);
    const int every = juce::jmax (1, (int) apvts.getRawParameterValue ("every")->load());
    const float mix = apvts.getRawParameterValue ("mix")->load();
    const int fadeSamples = (int) (apvts.getRawParameterValue ("fade")->load() * 0.001 * sampleRateHz);
    const bool pdc = apvts.getRawParameterValue ("pdc")->load() > 0.5f;

    // Reversing needs the whole slice first, so the plugin is one slice late.
    // Telling the host lets it shift the result back onto the exact bar/beat.
    const int latency = pdc ? juce::jmin (capacity - 1, (int) std::lround (sliceQ / ppqPerSample)) : 0;
    if (latency != reportedLatency)
    {
        reportedLatency = latency;
        setLatencySamples (latency);
    }

    // ---- per-sample slicing -----------------------------------------------
    for (int i = 0; i < numSamples; ++i)
    {
        // epsilon stops float rounding from firing the slice boundary a sample late
        const double x = (startPpq + i * ppqPerSample) / sliceQ + 1.0e-9;
        const auto idx = (juce::int64) std::floor (x);
        const double frac = x - (double) idx;

        if (idx != lastSliceIndex)
        {
            // A new slice started: the one we just recorded becomes the playback slice.
            if (captureValid && idx == lastSliceIndex + 1 && captureCount > 0)
            {
                std::swap (capture, playback);
                playbackLen = captureCount;

                const auto playedIdx = idx - 1;
                playbackReversed = (((playedIdx % every) + every) % every) == 0;
            }
            else
            {
                playbackLen = 0; // transport jumped / first slice: nothing valid to play
            }

            captureCount = 0;
            captureValid = true;
            lastSliceIndex = idx;
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const float in = data[i];

            if (captureCount < capacity)
                capture.setSample (ch, captureCount, in);

            float wet = 0.0f, dry = 0.0f;
            if (playbackLen > 0)
            {
                const int fwd = juce::jmin (playbackLen - 1, (int) (frac * playbackLen));
                const int rev = playbackLen - 1 - fwd;

                dry = playback.getSample (ch, fwd);
                if (playbackReversed)
                {
                    wet = playback.getSample (ch, rev);
                    if (fadeSamples > 0)
                    {
                        const float g = juce::jmin (1.0f,
                                                    (float) fwd / (float) fadeSamples,
                                                    (float) (playbackLen - 1 - fwd) / (float) fadeSamples);
                        wet *= g;
                    }
                }
                else
                {
                    wet = dry;
                }
            }

            data[i] = dry * (1.0f - mix) + wet * mix;
        }

        if (captureCount < capacity)
            ++captureCount;
    }

    for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    freeRunPpq = startPpq + numSamples * ppqPerSample;
    juce::ignoreUnused (hostPlaying);
}

juce::AudioProcessorEditor* SliceReverseProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void SliceReverseProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SliceReverseProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SliceReverseProcessor();
}
