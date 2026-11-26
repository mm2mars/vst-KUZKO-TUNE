#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <cstdint>
#include <vector>

// Kuzko Tune : correction de hauteur adaptative pour voix seule.
// Principe : analyse en avance (latence annoncée à la DAW), détection de hauteur YIN,
// choix de la note cible, correction partielle selon l'écart mesuré, puis
// décalage de hauteur par PSOLA (le timbre est conservé).
class KuzkoTuneProcessor : public juce::AudioProcessor
{
public:
    KuzkoTuneProcessor();
    ~KuzkoTuneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Kuzko Tune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::StringArray getKeyNames();
    static juce::StringArray getScaleNames();

    juce::AudioProcessorValueTreeState apvts;

    // Valeurs lues par l'interface (affichage)
    std::atomic<float> dispHz { 0.f };
    std::atomic<float> dispMidi { 0.f };
    std::atomic<float> dispDev { 0.f };      // écart en cents entre la hauteur détectée et la note cible
    std::atomic<float> dispTarget { -1.f };
    std::atomic<float> dispVoiced { 0.f };

private:
    struct FrameData
    {
        float wf = 0.f;        // part de signal corrigé (0 = signal d'origine)
        float cents = 0.f;     // décalage appliqué, en cents
        float period = 480.f;  // période de la voix en échantillons
        float hz = 0.f;
        float mInst = 0.f;     // hauteur détectée (MIDI, fractionnaire)
        float target = -1.f;   // note cible (MIDI)
        float voiced = 0.f;
    };

    void readParams();
    void processFrame (int64_t k);
    bool yin (float& tauOut);
    void updateKey();
    FrameData frameAt (double t) const;
    void placeGrain();
    float wetAt (int64_t m) const;

    std::atomic<float> *pAmount = nullptr, *pSpeed = nullptr, *pHuman = nullptr, *pFormant = nullptr,
                       *pMix = nullptr, *pTol = nullptr, *pVib = nullptr, *pKey = nullptr,
                       *pScale = nullptr, *pMode = nullptr;

    // Paramètres figés au début de chaque bloc
    float amount = 0.7f, speedMs = 20.f, human = 0.35f, formantSt = 0.f, mixv = 1.f, tolCents = 15.f;
    bool vib = true;
    int keySel = 0, scaleSel = 1, modeSel = 0;
    int scaleBits = 0xFFF;

    // Dimensions (dépendent de la fréquence d'échantillonnage)
    double sr = 48000.0;
    int L = 4096;      // latence en échantillons
    int hop = 256;     // pas d'analyse
    int Wa = 2048;     // fenêtre d'analyse
    int Wd = 512;      // fenêtre d'analyse décimée
    int dec = 4;
    int bufSize = 0;
    int mask = 0;
    double Tmin = 48.0, Tmax = 800.0;

    std::vector<float> inBuf[2], olaBuf[2], monoBuf, xd, cmnd;
    int64_t nWritten = 0, framesDone = 0, emitPos = 0;
    double tS = 0.0;

    static constexpr int FM = 4095;
    std::vector<FrameData> frames;

    // État de l'analyse
    float ctr = 60.f, smoothShift = 0.f, vwSmooth = 0.f, lastPeriod = 480.f, meanAbsErr = 20.f, lastM = 60.f;
    int curNote = -1, miss = 100, medCount = 0, autoKey = 0, keyTick = 0;
    bool wasVoiced = false;
    float medBuf[3] { 0.f, 0.f, 0.f };
    float hist[12] {};
    float histTotal = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuzkoTuneProcessor)
};
