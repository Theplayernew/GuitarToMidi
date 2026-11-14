#include "PluginProcessor.h"

GuitarToMidiProcessor::GuitarToMidiProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::mono(), false)
                          .withInput  ("Sidechain", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    thresholdP = apvts.getRawParameterValue ("threshold");
    trackingP  = apvts.getRawParameterValue ("tracking");
    stabilityP = apvts.getRawParameterValue ("stability");
    transposeP = apvts.getRawParameterValue ("transpose");
    channelP   = apvts.getRawParameterValue ("channel");
    monitorP   = apvts.getRawParameterValue ("monitor");
}

juce::AudioProcessorValueTreeState::ParameterLayout GuitarToMidiProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "threshold", 1 }, "Threshold (dB)",
                                                  NormalisableRange<float> (-80.0f, -10.0f, 0.1f), -50.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tracking", 1 }, "Pitch Tolerance",
                                                  NormalisableRange<float> (0.05f, 0.40f, 0.01f), 0.15f));
    l.add (std::make_unique<AudioParameterInt>   (ParameterID { "stability", 1 }, "Stability (hops)", 1, 4, 2));
    l.add (std::make_unique<AudioParameterInt>   (ParameterID { "transpose", 1 }, "Transpose", -24, 24, 0));
    l.add (std::make_unique<AudioParameterInt>   (ParameterID { "channel", 1 }, "MIDI Channel", 1, 16, 1));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "monitor", 1 }, "Monitor Dry Input", false));
    return l;
}

void GuitarToMidiProcessor::resetState()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    writePos = sinceAnalysis = decimPhase = 0;
    decimAccum = 0.0f;
    currentNote = candidate = -1;
    candCount = unvoiced = 0;
    pendingOnset = false;
    prevRms = 0.0f;
}

void GuitarToMidiProcessor::prepareToPlay (double sampleRate, int)
{
    decim = sampleRate >= 88200.0 ? 2 : 1;          // keep CPU bounded at 96k+
    const double fs = sampleRate / decim;
    detector.prepare (fs);
    ringSize = detector.requiredSamples();
    ring.assign ((size_t) ringSize, 0.0f);
    tmp.assign ((size_t) ringSize, 0.0f);
    hop = std::max (32, (int) std::lround (fs * 0.0029));   // ~2.9 ms analysis hop
    setLatencySamples (0);   // no lookahead / no buffering: MIDI is emitted in the same block
    resetState();
}

bool GuitarToMidiProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    for (const auto& in : l.inputBuses)
        if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()
            && in != juce::AudioChannelSet::disabled())
            return false;
    return true;
}

void GuitarToMidiProcessor::noteOff (int offset, juce::MidiBuffer& midi)
{
    if (currentNote >= 0)
    {
        midi.addEvent (juce::MidiMessage::noteOff (pChannel, sounding, (juce::uint8) 0), offset);
        currentNote = -1;
    }
}

void GuitarToMidiProcessor::pushSample (float s, int offset, juce::MidiBuffer& midi)
{
    if (decim == 2)
    {
        decimAccum += s;
        if (++decimPhase < 2) return;
        s = decimAccum * 0.5f;
        decimAccum = 0.0f;
        decimPhase = 0;
    }

    ring[(size_t) writePos] = s;
    if (++writePos >= ringSize) writePos = 0;

    if (++sinceAnalysis >= hop)
    {
        sinceAnalysis = 0;
        analyse (offset, midi);
    }
}

void GuitarToMidiProcessor::analyse (int offset, juce::MidiBuffer& midi)
{
    const int N = ringSize;
    for (int k = 0; k < N; ++k)
    {
        int idx = writePos + k;
        if (idx >= N) idx -= N;
        tmp[(size_t) k] = ring[(size_t) idx];
    }

    const int tail = 128;
    float e = 0.0f;
    for (int k = N - tail; k < N; ++k) e += tmp[(size_t) k] * tmp[(size_t) k];
    const float rms = std::sqrt (e / (float) tail);
    const float db  = 20.0f * std::log10 (rms + 1e-9f);

    if (db < pThreshold - 3.0f)                 // signal gone -> release
    {
        noteOff (offset, midi);
        candidate = -1; candCount = 0; pendingOnset = false; unvoiced = 0;
        prevRms = rms;
        return;
    }
    if (db < pThreshold) { prevRms = rms; return; }   // hysteresis band: hold

    if (rms > prevRms * 2.0f)                   // new pluck -> wait for fresh pitch
    {
        pendingOnset = true;
        candidate = -1; candCount = 0;
    }
    prevRms = rms;

    float freq = 0.0f, conf = 0.0f;
    if (! detector.detect (tmp.data(), pTracking, freq, conf))
    {
        candCount = 0;
        if (++unvoiced > 12) noteOff (offset, midi);
        return;
    }
    unvoiced = 0;

    const float midiF = 69.0f + 12.0f * std::log2 (freq / 440.0f);
    const int note = (int) std::lround (midiF);
    if (note < 28 || note > 100) return;

    if (note == candidate) ++candCount; else { candidate = note; candCount = 1; }

    if (candCount >= pStability && (note != currentNote || pendingOnset))
    {
        noteOff (offset, midi);
        const int out = juce::jlimit (0, 127, note + pTranspose);
        const int vel = juce::jlimit (1, 127, (int) std::lround (juce::jmap (db, pThreshold, pThreshold + 45.0f, 20.0f, 127.0f)));
        midi.addEvent (juce::MidiMessage::noteOn (pChannel, out, (juce::uint8) vel), offset);
        currentNote = note;
        sounding = out;
        pendingOnset = false;
    }
}

void GuitarToMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    pThreshold = thresholdP->load();
    pTracking  = trackingP->load();
    pStability = (int) stabilityP->load();
    pTranspose = (int) transposeP->load();
    pChannel   = (int) channelP->load();

    const int n = buffer.getNumSamples();
    // guitar comes in on the Sidechain bus (what Cubase exposes for instruments); fall back to main input
    const int inBus = getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled() ? 1 : 0;
    auto in = getBusBuffer (buffer, true, inBus);
    const int inCh = in.getNumChannels();

    if (inCh > 0)
    {
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < inCh; ++c) s += in.getReadPointer (c)[i];
            pushSample (s / (float) inCh, i, midi);
        }
    }

    auto out = getBusBuffer (buffer, false, 0);
    if (monitorP->load() < 0.5f || inCh == 0)
        out.clear();
    else if (out.getNumChannels() == 2 && inCh == 1)
        out.copyFrom (1, 0, out, 0, 0, n);
}

juce::AudioProcessorEditor* GuitarToMidiProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void GuitarToMidiProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void GuitarToMidiProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GuitarToMidiProcessor(); }
