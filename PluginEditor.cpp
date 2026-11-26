#include "PluginEditor.h"
#include <cmath>

using namespace juce;

namespace
{
    const Colour kRed     (0xffff2a2a);
    const Colour kDarkRed (0xffa01414);
    const Colour kPanel   (0xff100c0c);
    const Colour kStroke  (0xff3a1414);

    const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const bool isBlackPc[12]  = { false, true, false, true, false, false, true, false, true, false, true, false };

    constexpr int kFirstNote = 48;   // C3
    constexpr int kNumNotes  = 36;   // 3 octaves
    constexpr int kNumWhite  = 21;
}

// ---------------------------------------------------------------------------
KuzkoLnF::KuzkoLnF()
{
    setColour (ComboBox::backgroundColourId, Colour (0xff0c0909));
    setColour (ComboBox::outlineColourId, Colour (0xff3a1a1a));
    setColour (ComboBox::textColourId, Colours::white);
    setColour (ComboBox::arrowColourId, kRed);
    setColour (PopupMenu::backgroundColourId, Colour (0xff140d0d));
    setColour (PopupMenu::textColourId, Colours::white);
    setColour (PopupMenu::highlightedBackgroundColourId, kDarkRed);
    setColour (TextButton::buttonColourId, Colour (0xff110c0c));
    setColour (TextButton::buttonOnColourId, Colour (0xffc01818));
    setColour (TextButton::textColourOffId, Colours::white.withAlpha (0.85f));
    setColour (TextButton::textColourOnId, Colours::white);
}

void KuzkoLnF::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                 float startAngle, float endAngle, Slider&)
{
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.f);
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float trackR = r - 3.f;
    const float thick = jmax (3.f, r * 0.11f);

    Path track;
    track.addCentredArc (c.x, c.y, trackR, trackR, 0.f, startAngle, endAngle, true);
    g.setColour (Colour (0xff2b1b1b));
    g.strokePath (track, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

    const float angle = startAngle + pos * (endAngle - startAngle);
    Path val;
    val.addCentredArc (c.x, c.y, trackR, trackR, 0.f, startAngle, angle, true);
    g.setColour (kRed.withAlpha (0.25f));
    g.strokePath (val, PathStrokeType (thick * 2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (kRed);
    g.strokePath (val, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

    const float br = r * 0.68f;
    g.setGradientFill (ColourGradient (Colour (0xff3d3d3d), c.x, c.y - br, Colour (0xff0b0b0b), c.x, c.y + br, false));
    g.fillEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f);
    g.setColour (Colour (0xff050505));
    g.drawEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f, 1.5f);

    const float s = std::sin (angle), co = -std::cos (angle);
    g.setColour (Colours::white.withAlpha (0.92f));
    g.drawLine (c.x + s * br * 0.35f, c.y + co * br * 0.35f,
                c.x + s * br * 0.85f, c.y + co * br * 0.85f, jmax (2.f, r * 0.05f));
}

void KuzkoLnF::drawToggleButton (Graphics& g, ToggleButton& b, bool, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.f);
    const bool on = b.getToggleState();
    g.setColour (on ? Colour (0xffc01818) : Colour (0xff2a1c1c));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    const float d = r.getHeight() - 4.f;
    const float x = on ? r.getRight() - d - 2.f : r.getX() + 2.f;
    g.setColour (Colours::white);
    g.fillEllipse (x, r.getY() + 2.f, d, d);
}

// ---------------------------------------------------------------------------
Font KuzkoTuneEditor::F (float size, bool bold)
{
    return Font (FontOptions (size).withStyle (bold ? "Bold" : "Regular"));
}

void KuzkoTuneEditor::setupKnob (int i, const char* paramId, const String& label,
                                 Point<float> centre, float radius,
                                 Rectangle<float> labelRect, Rectangle<float> valueRect, bool boxed,
                                 std::function<String (double)> fmt)
{
    auto& k = knobs[i];
    k.label = label;
    k.centre = centre;
    k.radius = radius;
    k.labelRect = labelRect;
    k.valueRect = valueRect;
    k.boxed = boxed;
    k.fmt = std::move (fmt);

    k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    k.slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (k.slider);
    k.att = std::make_unique<SliderAtt> (proc.apvts, paramId, k.slider);
}

KuzkoTuneEditor::KuzkoTuneEditor (KuzkoTuneProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    auto pct = [] (double v) { return String ((int) std::lround (v)) + " %"; };

    setupKnob (0, "amount", "Amount", { 180.f, 318.f }, 92.f, { 297.f, 262.f, 220.f, 40.f }, { 295.f, 316.f, 128.f, 44.f }, true, pct);
    setupKnob (1, "speed", "Retune Speed", { 167.f, 492.f }, 68.f, { 283.f, 444.f, 230.f, 40.f }, { 283.f, 492.f, 141.f, 48.f }, true,
               [] (double v) { return String ((int) std::lround (v)) + " ms"; });
    setupKnob (2, "humanize", "Humanize", { 167.f, 658.f }, 68.f, { 284.f, 608.f, 200.f, 40.f }, { 283.f, 658.f, 141.f, 48.f }, true, pct);
    setupKnob (3, "formant", "Formant", { 1216.f, 495.f }, 62.f, { 1320.f, 444.f, 150.f, 40.f }, { 1319.f, 492.f, 121.f, 46.f }, true,
               [] (double v) { return String (v, 1); });
    setupKnob (4, "mix", "Mix", { 1235.f, 688.f }, 56.f, { 1320.f, 632.f, 150.f, 40.f }, { 1319.f, 677.f, 121.f, 46.f }, true, pct);
    setupKnob (5, "tolerance", "Tolerance", { 100.f, 890.f }, 46.f, { 64.f, 812.f, 220.f, 36.f }, { 170.f, 858.f, 170.f, 36.f }, false,
               [] (double v) { return String ((int) std::lround (v)) + " cents"; });

    keyBox.addItemList (KuzkoTuneProcessor::getKeyNames(), 1);
    scaleBox.addItemList (KuzkoTuneProcessor::getScaleNames(), 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    keyAtt = std::make_unique<ComboAtt> (proc.apvts, "key", keyBox);
    scaleAtt = std::make_unique<ComboAtt> (proc.apvts, "scale", scaleBox);

    addAndMakeVisible (vibToggle);
    vibAtt = std::make_unique<ButtonAtt> (proc.apvts, "vibrato", vibToggle);

    const char* modeNames[3] = { "Auto", "Soft", "Hard" };
    for (int i = 0; i < 3; ++i)
    {
        modeBtn[i].setButtonText (modeNames[i]);
        modeBtn[i].setClickingTogglesState (false);
        modeBtn[i].onClick = [this, i]
        {
            if (modeAtt != nullptr) modeAtt->setValueAsCompleteGesture ((float) i);
        };
        addAndMakeVisible (modeBtn[i]);
    }
    modeAtt = std::make_unique<ParameterAttachment> (*proc.apvts.getParameter ("mode"),
        [this] (float v)
        {
            modeIndex = roundToInt (v);
            for (int i = 0; i < 3; ++i)
                modeBtn[i].setToggleState (i == modeIndex, dontSendNotification);
        });
    modeAtt->sendInitialUpdate();

    setSize (960, 640);
    setResizable (true, true);
    setResizeLimits (768, 512, 1920, 1280);
    getConstrainer()->setFixedAspectRatio (1.5);

    startTimerHz (30);
}

KuzkoTuneEditor::~KuzkoTuneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KuzkoTuneEditor::resized()
{
    const float sc = (float) getWidth() / 1536.f;
    auto S = [sc] (float x, float y, float w, float h)
    {
        return Rectangle<int> (roundToInt (x * sc), roundToInt (y * sc), roundToInt (w * sc), roundToInt (h * sc));
    };

    for (auto& k : knobs)
        k.slider.setBounds (S (k.centre.x - k.radius, k.centre.y - k.radius, k.radius * 2.f, k.radius * 2.f));

    keyBox.setBounds (S (1265.f, 230.f, 212.f, 45.f));
    scaleBox.setBounds (S (1265.f, 291.f, 212.f, 45.f));
    vibToggle.setBounds (S (308.f, 902.f, 50.f, 26.f));
    modeBtn[0].setBounds (S (1193.f, 851.f, 102.f, 56.f));
    modeBtn[1].setBounds (S (1295.f, 851.f, 97.f, 56.f));
    modeBtn[2].setBounds (S (1392.f, 851.f, 98.f, 56.f));
}

void KuzkoTuneEditor::timerCallback()
{
    voicedNow = proc.dispVoiced.load() > 0.5f;
    const float tgt = proc.dispTarget.load();
    targetNote = voicedNow ? tgt : -1.f;

    const float dev = voicedNow ? jlimit (-50.f, 50.f, proc.dispDev.load()) : 0.f;
    devS += (dev - devS) * 0.4f;

    if (voicedNow)
    {
        midiS = proc.dispMidi.load();
        hzS = proc.dispHz.load();
    }

    for (auto& a : activity) a *= 0.93f;
    if (voicedNow)
    {
        const int n = roundToInt (midiS);
        const int folded = kFirstNote + (((n - kFirstNote) % kNumNotes) + kNumNotes) % kNumNotes;
        activity[folded] = jmin (1.f, activity[folded] + 0.25f);
    }

    repaint();
}

// ---------------------------------------------------------------------------
void KuzkoTuneEditor::drawGauge (Graphics& g)
{
    const float cx = 770.f, cy = 410.f, R = 255.f;

    g.setColour (Colour (0xff0b0808));
    g.fillEllipse (cx - R - 18.f, cy - R - 18.f, (R + 18.f) * 2.f, (R + 18.f) * 2.f);
    g.setColour (kStroke);
    g.drawEllipse (cx - R - 18.f, cy - R - 18.f, (R + 18.f) * 2.f, (R + 18.f) * 2.f, 2.f);

    const float s0 = -MathConstants<float>::pi * 0.75f;
    const float s1 = MathConstants<float>::pi * 0.75f;
    const float arcR = R - 14.f;

    Path bg;
    bg.addCentredArc (cx, cy, arcR, arcR, 0.f, s0, s1, true);
    g.setColour (Colour (0xff3a3535));
    g.strokePath (bg, PathStrokeType (26.f));

    if (voicedNow)
    {
        const float a = s0 + ((devS + 50.f) / 100.f) * (s1 - s0);
        Path fill;
        fill.addCentredArc (cx, cy, arcR, arcR, 0.f, s0, jmax (s0 + 0.01f, a), true);
        g.setColour (kRed.withAlpha (0.28f));
        g.strokePath (fill, PathStrokeType (40.f));
        g.setColour (kRed);
        g.strokePath (fill, PathStrokeType (26.f));
    }

    // graduations
    for (int i = 0; i <= 40; ++i)
    {
        const float a = s0 + (float) i / 40.f * (s1 - s0);
        const bool major = (i % 10) == 0;
        const float r0 = R - 62.f, r1 = R - (major ? 38.f : 50.f);
        g.setColour (major ? kRed : kRed.withAlpha (0.55f));
        g.drawLine (cx + std::sin (a) * r0, cy - std::cos (a) * r0,
                    cx + std::sin (a) * r1, cy - std::cos (a) * r1, major ? 3.f : 1.5f);
    }

    g.setColour (Colour (0xff120c0c));
    g.fillEllipse (cx - 140.f, cy - 140.f, 280.f, 280.f);
    g.setColour (kStroke);
    g.drawEllipse (cx - 140.f, cy - 140.f, 280.f, 280.f, 2.f);

    const int pc = ((roundToInt (midiS) % 12) + 12) % 12;
    g.setColour (voicedNow ? kRed : Colour (0xff5a3a3a));
    g.setFont (F (150.f, true));
    g.drawText (voicedNow ? String (noteNames[pc]) : String ("-"),
                Rectangle<float> (cx - 130.f, cy - 100.f, 260.f, 170.f), Justification::centred, false);

    g.setColour (Colours::white.withAlpha (0.9f));
    g.setFont (F (36.f));
    g.drawText (voicedNow ? String (hzS, 1) + " Hz" : String ("-- Hz"),
                Rectangle<float> (cx - 130.f, cy + 62.f, 260.f, 50.f), Justification::centred, false);

    String status = "Idle";
    if (voicedNow)
        status = std::abs (devS) < 10.f ? "In Tune" : (devS > 0.f ? "Sharp" : "Flat");

    const Rectangle<float> pill (685.f, 590.f, 166.f, 44.f);
    g.setColour (Colour (0xff1a0b0b));
    g.fillRoundedRectangle (pill, 22.f);
    g.setColour (kRed);
    g.drawRoundedRectangle (pill, 22.f, 2.f);
    g.setFont (F (30.f));
    g.drawText (status, pill, Justification::centred, false);
}

void KuzkoTuneEditor::drawKeyboard (Graphics& g)
{
    const float x0 = 405.f, y0 = 838.f, w = 725.f, h = 97.f;
    const float ww = w / (float) kNumWhite;
    const float bw = ww * 0.62f, bh = h * 0.6f;

    auto fold = [] (int note) { return kFirstNote + (((note - kFirstNote) % kNumNotes) + kNumNotes) % kNumNotes; };
    const int det = voicedNow ? fold (roundToInt (midiS)) : -1;
    const int tgt = targetNote >= 0.f ? fold (roundToInt (targetNote)) : -1;

    g.setColour (Colour (0xff0b0808));
    g.fillRoundedRectangle (x0 - 8.f, 770.f, w + 16.f, 175.f, 14.f);

    // barres d'activité au-dessus des touches
    int whiteCount = 0;
    for (int n = kFirstNote; n < kFirstNote + kNumNotes; ++n)
    {
        const bool black = isBlackPc[n % 12];
        const float cx = black ? x0 + (float) whiteCount * ww : x0 + ((float) whiteCount + 0.5f) * ww;
        const float a = activity[n];
        if (a > 0.02f)
        {
            g.setColour (kRed.withAlpha (0.35f + 0.65f * a));
            g.fillRect (cx - 4.f, 826.f - a * 40.f, 8.f, a * 40.f);
        }
        if (! black) ++whiteCount;
    }

    // touches blanches
    whiteCount = 0;
    for (int n = kFirstNote; n < kFirstNote + kNumNotes; ++n)
    {
        if (isBlackPc[n % 12]) continue;
        const Rectangle<float> r (x0 + (float) whiteCount * ww, y0, ww - 1.5f, h);
        g.setGradientFill (ColourGradient (Colour (0xfff2f2f2), 0.f, r.getY(), Colour (0xffc9c9c9), 0.f, r.getBottom(), false));
        g.fillRect (r);
        if (n == tgt && n != det) { g.setColour (kRed.withAlpha (0.45f)); g.fillRect (r); }
        if (n == det) { g.setColour (kRed); g.fillRect (r); }
        ++whiteCount;
    }

    // touches noires
    whiteCount = 0;
    for (int n = kFirstNote; n < kFirstNote + kNumNotes; ++n)
    {
        if (! isBlackPc[n % 12]) { ++whiteCount; continue; }
        const Rectangle<float> r (x0 + (float) whiteCount * ww - bw * 0.5f, y0, bw, bh);
        g.setColour (Colour (0xff0a0a0a));
        g.fillRect (r);
        if (n == tgt && n != det) { g.setColour (kRed.withAlpha (0.55f)); g.fillRect (r); }
        if (n == det) { g.setColour (kRed); g.fillRect (r); }
    }
}

void KuzkoTuneEditor::paint (Graphics& g)
{
    const float sc = (float) getWidth() / 1536.f;
    g.addTransform (AffineTransform::scale (sc));

    g.setGradientFill (ColourGradient (Colour (0xff1c1011), 0.f, 0.f, Colour (0xff050505), 0.f, 1024.f, false));
    g.fillRect (0.f, 0.f, 1536.f, 1024.f);

    g.setColour (kRed.withAlpha (0.25f));
    g.drawRoundedRectangle (6.f, 6.f, 1524.f, 1012.f, 40.f, 7.f);
    g.setColour (kRed.withAlpha (0.85f));
    g.drawRoundedRectangle (8.f, 8.f, 1520.f, 1008.f, 40.f, 2.5f);

    // en-tête
    g.setColour (kRed);
    g.setFont (F (120.f, true));
    g.drawText ("K", Rectangle<float> (48.f, 0.f, 130.f, 120.f), Justification::centred, false);
    g.setColour (Colours::white);
    g.setFont (F (76.f, true));
    g.drawText ("KUZKO", Rectangle<float> (212.f, 18.f, 290.f, 80.f), Justification::centredLeft, false);
    g.setColour (kRed);
    g.drawText ("TUNE", Rectangle<float> (492.f, 18.f, 220.f, 80.f), Justification::centredLeft, false);

    g.setColour (kStroke);
    g.drawLine (30.f, 128.f, 1506.f, 128.f, 1.5f);

    // panneaux
    auto panel = [&] (Rectangle<float> r)
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (r, 26.f);
        g.setColour (kStroke);
        g.drawRoundedRectangle (r, 26.f, 2.f);
    };
    panel ({ 30.f, 150.f, 440.f, 600.f });
    panel ({ 1050.f, 150.f, 460.f, 600.f });
    panel ({ 30.f, 765.f, 340.f, 185.f });
    panel ({ 1170.f, 770.f, 320.f, 160.f });

    // titres de sections
    g.setColour (Colours::white);
    g.setFont (F (32.f));
    g.drawText ("PITCH CORRECTION", Rectangle<float> (140.f, 160.f, 320.f, 40.f), Justification::centredLeft, false);
    g.drawText ("KEY & SCALE", Rectangle<float> (1208.f, 160.f, 280.f, 40.f), Justification::centredLeft, false);
    g.setFont (F (28.f));
    g.drawText ("FORMANT", Rectangle<float> (1206.f, 382.f, 250.f, 36.f), Justification::centredLeft, false);
    g.drawText ("MIX", Rectangle<float> (1226.f, 588.f, 200.f, 36.f), Justification::centredLeft, false);
    g.drawText ("ADVANCED", Rectangle<float> (68.f, 772.f, 240.f, 36.f), Justification::centredLeft, false);
    g.drawText ("MODE", Rectangle<float> (1262.f, 782.f, 200.f, 36.f), Justification::centredLeft, false);

    g.setColour (kStroke);
    g.drawLine (1100.f, 367.f, 1480.f, 367.f, 1.5f);
    g.drawLine (1100.f, 572.f, 1480.f, 572.f, 1.5f);

    g.setColour (Colours::white.withAlpha (0.92f));
    g.setFont (F (28.f));
    g.drawText ("Key", Rectangle<float> (1145.f, 230.f, 110.f, 45.f), Justification::centredLeft, false);
    g.drawText ("Scale", Rectangle<float> (1145.f, 291.f, 110.f, 45.f), Justification::centredLeft, false);

    // potards : libellés et valeurs
    for (int i = 0; i < 6; ++i)
    {
        const auto& k = knobs[i];
        g.setColour (Colours::white.withAlpha (0.92f));
        g.setFont (F (i == 5 ? 24.f : 28.f));
        g.drawText (k.label, k.labelRect, Justification::centredLeft, false);

        if (k.boxed)
        {
            g.setColour (Colour (0xff090606));
            g.fillRoundedRectangle (k.valueRect, 10.f);
            g.setColour (Colour (0xff2e1c1c));
            g.drawRoundedRectangle (k.valueRect, 10.f, 1.5f);
        }
        g.setColour (Colours::white);
        g.setFont (F (i == 5 ? 30.f : 32.f));
        g.drawText (k.fmt ((double) k.slider.getValue()), k.valueRect,
                    k.boxed ? Justification::centred : Justification::centredLeft, false);
    }

    g.setColour (Colours::white.withAlpha (0.92f));
    g.setFont (F (24.f));
    g.drawText ("Vibrato Preserve", Rectangle<float> (64.f, 900.f, 240.f, 30.f), Justification::centredLeft, false);

    drawGauge (g);
    drawKeyboard (g);

    g.setColour (kRed.withAlpha (0.85f));
    g.setFont (F (28.f));
    g.drawText ("K U Z K O   T U N E", Rectangle<float> (540.f, 968.f, 460.f, 36.f), Justification::centred, false);
    g.setColour (kStroke);
    g.drawLine (200.f, 986.f, 530.f, 986.f, 2.f);
    g.drawLine (1010.f, 986.f, 1340.f, 986.f, 2.f);
}
