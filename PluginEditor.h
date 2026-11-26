#pragma once

#include "PluginProcessor.h"
#include <functional>
#include <memory>

class KuzkoLnF : public juce::LookAndFeel_V4
{
public:
    KuzkoLnF();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

class KuzkoTuneEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit KuzkoTuneEditor (KuzkoTuneProcessor&);
    ~KuzkoTuneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::String label;
        juce::Point<float> centre;
        float radius = 60.f;
        juce::Rectangle<float> labelRect;
        juce::Rectangle<float> valueRect;
        bool boxed = true;
        std::function<juce::String (double)> fmt;
        std::unique_ptr<SliderAtt> att;
    };

    void timerCallback() override;
    void setupKnob (int i, const char* paramId, const juce::String& label,
                    juce::Point<float> centre, float radius,
                    juce::Rectangle<float> labelRect, juce::Rectangle<float> valueRect, bool boxed,
                    std::function<juce::String (double)> fmt);

    void drawGauge (juce::Graphics&);
    void drawKeyboard (juce::Graphics&);
    static juce::Font F (float size, bool bold = false);

    KuzkoTuneProcessor& proc;
    KuzkoLnF lnf;

    Knob knobs[6];

    juce::ComboBox keyBox, scaleBox;
    std::unique_ptr<ComboAtt> keyAtt, scaleAtt;

    juce::ToggleButton vibToggle;
    std::unique_ptr<ButtonAtt> vibAtt;

    juce::TextButton modeBtn[3];
    std::unique_ptr<juce::ParameterAttachment> modeAtt;
    int modeIndex = 0;

    // état d'affichage
    float devS = 0.f, midiS = 60.f, hzS = 0.f;
    float targetNote = -1.f;
    bool voicedNow = false;
    float activity[128] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuzkoTuneEditor)
};
