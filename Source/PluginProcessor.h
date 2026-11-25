#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Separator.h"

// Nombres e identificadores de las piezas
inline const char* const kStemIds[pb::kStems]   = { "kick", "snare", "toms", "hat", "cym", "oth" };
inline const char* const kStemNames[pb::kStems] = { "KICK", "SNARE", "TOMS", "HI-HAT", "CYMBALS", "OTHER" };

class ParaBataProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kMaxSamples = 4;        // sonidos por pieza (capas de velocidad)

    ParaBataProcessor();
    ~ParaBataProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ParaBata"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Samples propios para reemplazar o reforzar cada pieza (0 Kick .. 4 Cymbals)
    void loadSamples (int stem, const juce::Array<juce::File>& files, bool append);
    void clearSamples (int stem);
    juce::StringArray getSampleNames (int stem) const;

    juce::AudioProcessorValueTreeState apvts;

    // Indicadores para la interfaz
    std::atomic<float> activity[pb::kStems];
    std::atomic<int> hitCount[pb::kStems];

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void reloadSamplesFromState();
    std::shared_ptr<juce::AudioBuffer<float>> readSample (const juce::File&) const;
    void processChunk (int start, int n, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    struct Slot
    {
        std::vector<std::shared_ptr<juce::AudioBuffer<float>>> bufs;
        juce::StringArray paths;
    };
    struct Voice { std::shared_ptr<juce::AudioBuffer<float>> buf; int pos = 0; int delay = 0; float gain = 0.0f; bool active = false; };
    static constexpr int kVoices = 8;

    pb::Separator sep;
    mutable juce::AudioFormatManager formats;
    mutable juce::SpinLock slotLock;
    Slot slots[pb::kHitStems];
    Voice voices[pb::kHitStems][kVoices];
    int rr[pb::kHitStems] = { 0 };
    double sr = 48000.0;
    static constexpr int kChunk = 2048;
    std::vector<float> inL, inR, stemL[pb::kStems], stemR[pb::kStems], smpL[pb::kHitStems], smpR[pb::kHitStems];
    struct NoteOff { int64_t at; int note; };
    std::vector<NoteOff> pendingOffs;
    int64_t samplesDone = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaBataProcessor)
};
