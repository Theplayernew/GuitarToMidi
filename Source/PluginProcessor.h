#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PitchDetector.h"

class GuitarToMidiProcessor : public juce::AudioProcessor
{
public:
    GuitarToMidiProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GuitarToMidi"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
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
    void pushSample (float s, int blockOffset, juce::MidiBuffer& midi);
    void analyse (int blockOffset, juce::MidiBuffer& midi);
    void noteOff (int blockOffset, juce::MidiBuffer& midi);
    void resetState();

    PitchDetector detector;
    std::vector<float> ring, tmp;
    int ringSize = 0, writePos = 0, hop = 128, sinceAnalysis = 0;
    int decim = 1, decimPhase = 0;
    float decimAccum = 0.0f;

    int currentNote = -1, candidate = -1, candCount = 0, unvoiced = 0, sounding = 0;
    bool pendingOnset = false;
    float prevRms = 0.0f;

    // per-block cached parameters
    float pThreshold = -50.0f, pTracking = 0.15f;
    int pStability = 2, pTranspose = 0, pChannel = 1;

    std::atomic<float> *thresholdP{}, *trackingP{}, *stabilityP{}, *transposeP{}, *channelP{}, *monitorP{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarToMidiProcessor)
};
