#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/Engine.h"
#include "Parameters.h"

class StompBoxProcessor : public juce::AudioProcessor
{
public:
    StompBoxProcessor();
    ~StompBoxProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return stompbox::IRData::maxSeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    // Cab IR (message thread)
    juce::Result loadIRFile (const juce::File& file);
    void clearIR();
    stompbox::IRData getIR() const;
    int getIRVersion() const noexcept { return irVersion.load(); }

    // Live/Studio: a global setting, remembered across instances and projects.
    using Quality = stompbox::Engine::Quality;
    void setQuality (Quality q);
    Quality getQuality() const noexcept { return engine.getQuality(); }

    juce::File getLastIRFolder() const;

private:
    void applyIR (stompbox::IRData ir);
    juce::PropertiesFile& globalSettings();

    stompbox::Engine engine;

    std::atomic<float>* pChannel = nullptr;
    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pTreble = nullptr;
    std::atomic<float>* pBass = nullptr;
    std::atomic<float>* pCut = nullptr;
    std::atomic<float>* pMaster = nullptr;
    std::atomic<float>* pOutput = nullptr;

    mutable juce::CriticalSection irLock;
    stompbox::IRData currentIR;
    std::atomic<int> irVersion { 0 };

    std::unique_ptr<juce::PropertiesFile> settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StompBoxProcessor)
};
