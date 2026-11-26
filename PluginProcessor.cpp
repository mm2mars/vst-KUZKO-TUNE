#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

using namespace juce;

namespace
{
    int maskFromIntervals (std::initializer_list<int> iv)
    {
        int m = 0;
        for (int i : iv) m |= (1 << i);
        return m;
    }

    int scaleMaskFor (int idx)
    {
        switch (idx)
        {
            case 0:  return 0xFFF;                                          // Chromatic
            case 1:  return maskFromIntervals ({ 0, 2, 4, 5, 7, 9, 11 });   // Major
            case 2:  return maskFromIntervals ({ 0, 2, 3, 5, 7, 8, 10 });   // Minor
            case 3:  return maskFromIntervals ({ 0, 2, 3, 5, 7, 8, 11 });   // Harmonic Minor
            case 4:  return maskFromIntervals ({ 0, 2, 4, 7, 9 });          // Pentatonic Major
            case 5:  return maskFromIntervals ({ 0, 3, 5, 7, 10 });         // Pentatonic Minor
            case 6:  return maskFromIntervals ({ 0, 2, 3, 5, 7, 9, 10 });   // Dorian
            case 7:  return maskFromIntervals ({ 0, 2, 4, 5, 7, 9, 10 });   // Mixolydian
            default: return 0xFFF;
        }
    }

    inline float smoothstep01 (float x)
    {
        x = jlimit (0.f, 1.f, x);
        return x * x * (3.f - 2.f * x);
    }
}

StringArray KuzkoTuneProcessor::getKeyNames()
{
    return { "Auto", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

StringArray KuzkoTuneProcessor::getScaleNames()
{
    return { "Chromatic", "Major", "Minor", "Harmonic Minor", "Pentatonic Major",
             "Pentatonic Minor", "Dorian", "Mixolydian" };
}

AudioProcessorValueTreeState::ParameterLayout KuzkoTuneProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Amount",
                                                        NormalisableRange<float> (0.f, 100.f, 0.1f), 70.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Retune Speed",
                                                        NormalisableRange<float> (1.f, 200.f, 0.1f, 0.4f), 20.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "humanize", 1 }, "Humanize",
                                                        NormalisableRange<float> (0.f, 100.f, 0.1f), 35.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "formant", 1 }, "Formant",
                                                        NormalisableRange<float> (-12.f, 12.f, 0.1f), 0.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
                                                        NormalisableRange<float> (0.f, 100.f, 0.1f), 100.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tolerance", 1 }, "Tolerance",
                                                        NormalisableRange<float> (0.f, 50.f, 0.5f), 15.f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "vibrato", 1 }, "Vibrato Preserve", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", getKeyNames(), 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", getScaleNames(), 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                         StringArray { "Auto", "Soft", "Hard" }, 0));

    return { p.begin(), p.end() };
}

KuzkoTuneProcessor::KuzkoTuneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pAmount  = apvts.getRawParameterValue ("amount");
    pSpeed   = apvts.getRawParameterValue ("speed");
    pHuman   = apvts.getRawParameterValue ("humanize");
    pFormant = apvts.getRawParameterValue ("formant");
    pMix     = apvts.getRawParameterValue ("mix");
    pTol     = apvts.getRawParameterValue ("tolerance");
    pVib     = apvts.getRawParameterValue ("vibrato");
    pKey     = apvts.getRawParameterValue ("key");
    pScale   = apvts.getRawParameterValue ("scale");
    pMode    = apvts.getRawParameterValue ("mode");
}

bool KuzkoTuneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
}

void KuzkoTuneProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    Tmin = sr / 1000.0;   // 1000 Hz
    Tmax = sr / 60.0;     // 60 Hz
    L = roundToInt (0.0853 * sr);
    hop = jmax (64, roundToInt (0.00533 * sr));
    dec = jmax (1, roundToInt (sr / 12000.0));
    Wd = (roundToInt (0.0427 * sr) / dec) & ~1;
    Wa = Wd * dec;

    bufSize = nextPowerOfTwo (L * 2 + 65536);
    mask = bufSize - 1;

    for (int c = 0; c < 2; ++c)
    {
        inBuf[c].assign ((size_t) bufSize, 0.f);
        olaBuf[c].assign ((size_t) bufSize, 0.f);
    }
    monoBuf.assign ((size_t) bufSize, 0.f);
    xd.assign ((size_t) Wd + 4, 0.f);
    cmnd.assign ((size_t) Wd + 4, 1.f);
    frames.assign ((size_t) FM + 1, FrameData());

    nWritten = 0;
    framesDone = 0;
    emitPos = 0;
    tS = 0.0;

    ctr = 60.f; smoothShift = 0.f; vwSmooth = 0.f; lastPeriod = (float) (sr / 200.0);
    meanAbsErr = 20.f; lastM = 60.f;
    curNote = -1; miss = 100; medCount = 0; autoKey = 0; keyTick = 0;
    wasVoiced = false;
    for (auto& h : hist) h = 0.f;
    histTotal = 0.f;

    setLatencySamples (L);
}

void KuzkoTuneProcessor::readParams()
{
    amount    = *pAmount / 100.f;
    speedMs   = *pSpeed;
    human     = *pHuman / 100.f;
    formantSt = *pFormant;
    mixv      = *pMix / 100.f;
    tolCents  = *pTol;
    vib       = *pVib > 0.5f;
    keySel    = roundToInt (pKey->load());
    scaleSel  = roundToInt (pScale->load());
    modeSel   = roundToInt (pMode->load());
    scaleBits = scaleMaskFor (scaleSel);
}

// ---------------------------------------------------------------------------
// Détection de hauteur (YIN sur signal décimé)
// ---------------------------------------------------------------------------
bool KuzkoTuneProcessor::yin (float& tauOut)
{
    const int W2 = Wd / 2;
    const int tauMax = jmin (W2 - 1, (int) std::ceil (Tmax / dec) + 2);
    const int tauMin = jmax (2, (int) std::floor (Tmin / dec));
    if (tauMax <= tauMin + 2) return false;

    cmnd[0] = 1.f;
    float run = 0.f;
    for (int t = 1; t <= tauMax; ++t)
    {
        float d = 0.f;
        for (int j = 0; j < W2; ++j)
        {
            const float df = xd[(size_t) j] - xd[(size_t) (j + t)];
            d += df * df;
        }
        run += d;
        cmnd[(size_t) t] = run > 1e-12f ? d * (float) t / run : 1.f;
    }

    int found = -1;
    for (int t = tauMin; t <= tauMax; ++t)
    {
        if (cmnd[(size_t) t] < 0.15f)
        {
            while (t + 1 <= tauMax && cmnd[(size_t) (t + 1)] < cmnd[(size_t) t]) ++t;
            found = t;
            break;
        }
    }

    if (found < 0)
    {
        float mn = 1e9f;
        int mi = -1;
        for (int t = tauMin; t <= tauMax; ++t)
            if (cmnd[(size_t) t] < mn) { mn = cmnd[(size_t) t]; mi = t; }
        if (mi > 0 && mn < 0.3f) found = mi;
    }

    if (found < 0 || found >= tauMax) return false;

    const float s0 = cmnd[(size_t) (found - 1)];
    const float s1 = cmnd[(size_t) found];
    const float s2 = cmnd[(size_t) (found + 1)];
    const float den = s0 + s2 - 2.f * s1;
    float sh = std::abs (den) > 1e-9f ? 0.5f * (s0 - s2) / den : 0.f;
    sh = jlimit (-1.f, 1.f, sh);
    tauOut = (float) found + sh;
    return true;
}

void KuzkoTuneProcessor::updateKey()
{
    if (histTotal < 30.f) return;
    int best = autoKey;
    float bs = -1.f, cur = 0.f;
    for (int r = 0; r < 12; ++r)
    {
        float s = 0.f;
        for (int pc = 0; pc < 12; ++pc)
            if ((scaleBits >> ((pc - r + 12) % 12)) & 1) s += hist[pc];
        if (r == autoKey) cur = s;
        if (s > bs) { bs = s; best = r; }
    }
    if (cur < 0.93f * bs) autoKey = best;
}

// ---------------------------------------------------------------------------
// Une image d'analyse : détection, note cible, correction adaptative
// ---------------------------------------------------------------------------
void KuzkoTuneProcessor::processFrame (int64_t k)
{
    const int64_t p = k * (int64_t) hop;
    const int half = Wa / 2;

    double energy = 0.0;
    for (int i = 0; i < Wd; ++i)
    {
        float s = 0.f;
        const int64_t base = p - half + (int64_t) i * dec;
        for (int d = 0; d < dec; ++d)
            s += monoBuf[(size_t) ((base + d) & mask)];
        s /= (float) dec;
        xd[(size_t) i] = s;
        energy += (double) s * s;
    }
    const float rms = (float) std::sqrt (energy / Wd);

    bool ok = false;
    float tauF = 0.f;
    if (rms > 0.0015f) ok = yin (tauF);

    const double hopMs = 1000.0 * hop / sr;

    if (ok)
    {
        const float periodFull = tauF * (float) dec;
        const float hz = (float) (sr / periodFull);
        const float m = 69.f + 12.f * std::log2 (hz / 440.f);

        miss = 0;
        medBuf[0] = medBuf[1];
        medBuf[1] = medBuf[2];
        medBuf[2] = m;
        medCount = jmin (3, medCount + 1);
        if (medCount < 3)
            lastM = m;
        else
        {
            float a = medBuf[0], b = medBuf[1], c = medBuf[2];
            lastM = jmax (jmin (a, b), jmin (jmax (a, b), c));
        }

        if (! wasVoiced)
        {
            wasVoiced = true;
            ctr = lastM;
            curNote = -1;
            smoothShift = 0.f;
        }
    }
    else
    {
        ++miss;
        if (miss > 3)
        {
            wasVoiced = false;
            medCount = 0;
        }
    }

    float desired = 0.f;
    float target = -1.f;

    if (wasVoiced)
    {
        const float m = lastM;

        if (ok)
        {
            if (std::abs (m - ctr) > 0.8f)
                ctr = m;
            else
                ctr += (1.f - (float) std::exp (-hopMs / 120.0)) * (m - ctr);

            const int pc = ((roundToInt (m) % 12) + 12) % 12;
            for (auto& h : hist) h *= 0.9995f;
            hist[pc] += 1.f;
            histTotal = histTotal * 0.9995f + 1.f;
            if (++keyTick >= 64) { keyTick = 0; updateKey(); }
        }

        const float ref = vib ? ctr : m;
        const bool chromatic = (keySel == 0 && histTotal < 30.f);
        const int key = keySel == 0 ? autoKey : keySel - 1;
        const int bits = scaleBits;

        auto allowed = [&] (int note)
        {
            return chromatic || ((bits >> (((note - key) % 12 + 12) % 12)) & 1);
        };

        int cand = -1;
        float bd = 1e9f;
        const int r = roundToInt (ref);
        for (int nn = r - 7; nn <= r + 7; ++nn)
        {
            if (! allowed (nn)) continue;
            const float d = std::abs (ref - (float) nn);
            if (d < bd) { bd = d; cand = nn; }
        }
        if (cand < 0) { cand = r; bd = std::abs (ref - (float) r); }

        // hystérésis : on ne change de note cible que si l'autre est nettement plus proche
        if (curNote >= 0 && cand != curNote && allowed (curNote)
            && std::abs (ref - (float) curNote) <= bd + 0.2f)
            cand = curNote;

        curNote = cand;
        target = (float) cand;

        const float e = (target - ref) * 100.f;   // écart à corriger, en cents
        if (ok)
            meanAbsErr += (jmin (std::abs (e), 100.f) - meanAbsErr) * 0.002f;

        // Réglages selon le mode
        float strengthMul = 1.f, speedMul = 1.f, tolMul = 1.f;
        if (modeSel == 0)        // Auto : plus tu chantes juste, moins il corrige
            strengthMul = jlimit (0.35f, 1.f, 0.35f + meanAbsErr / 35.f);
        else if (modeSel == 1)   // Soft
        {
            strengthMul = 0.6f; speedMul = 2.f; tolMul = 1.5f;
        }
        else                     // Hard
        {
            strengthMul = 1.f; speedMul = 0.25f; tolMul = 0.5f;
        }

        // Correction partielle selon l'écart : rien sous la tolérance, complète au-delà de tolérance + 35 cents
        const float tolEff = tolCents * tolMul * (1.f + 0.5f * human);
        const float absE = std::abs (e);
        const float w = absE <= tolEff ? 0.f : smoothstep01 ((absE - tolEff) / 35.f);

        const float s = jlimit (0.f, 1.f, amount * strengthMul * (1.f - 0.25f * human) * w);
        desired = e * s;

        const double tMs = jmax (1.0, (double) speedMs * speedMul * (1.0 + 3.0 * human));
        const float alpha = 1.f - (float) std::exp (-hopMs / tMs);
        smoothShift += alpha * (desired - smoothShift);
    }
    else
    {
        smoothShift *= 0.5f;
    }

    vwSmooth += ((wasVoiced ? 1.f : 0.f) - vwSmooth) * 0.35f;

    // La part corrigée n'augmente que si le décalage est audible (évite de toucher une note déjà juste)
    const float gate = smoothstep01 (std::abs (smoothShift) / 2.f);

    FrameData fd;
    fd.cents = smoothShift;
    fd.wf = vwSmooth * gate;
    if (wasVoiced)
    {
        const float f0 = 440.f * std::pow (2.f, (lastM - 69.f) / 12.f);
        lastPeriod = jlimit ((float) Tmin, (float) Tmax, (float) (sr / f0));
        fd.hz = f0;
    }
    fd.period = lastPeriod;
    fd.mInst = lastM;
    fd.target = target;
    fd.voiced = wasVoiced ? 1.f : 0.f;

    frames[(size_t) (k & FM)] = fd;
}

KuzkoTuneProcessor::FrameData KuzkoTuneProcessor::frameAt (double t) const
{
    if (t < 0.0) t = 0.0;
    const int64_t k = (int64_t) (t / hop);
    const float fr = (float) (t / hop - (double) k);
    const auto& a = frames[(size_t) (k & FM)];
    const auto& b = frames[(size_t) ((k + 1) & FM)];

    FrameData o = a;
    o.wf     = a.wf + (b.wf - a.wf) * fr;
    o.cents  = a.cents + (b.cents - a.cents) * fr;
    o.period = a.period + (b.period - a.period) * fr;
    return o;
}

float KuzkoTuneProcessor::wetAt (int64_t m) const
{
    return frameAt ((double) m).wf;
}

// ---------------------------------------------------------------------------
// Synthèse PSOLA : un grain par période de sortie
// ---------------------------------------------------------------------------
void KuzkoTuneProcessor::placeGrain()
{
    const FrameData f = frameAt (tS);
    const double T = jlimit (Tmin, Tmax, (double) f.period);
    const double ratio = std::pow (2.0, (double) f.cents / 1200.0);
    const double Tout = T / ratio;
    const double fs = std::pow (2.0, (double) formantSt / 12.0);

    // On cale l'origine du grain sur le pic de la forme d'onde le plus proche
    const int64_t c = (int64_t) std::llround (tS);
    const int hs = (int) (T * 0.5);
    int64_t best = c;
    float bv = -1e9f;
    for (int64_t i = c - hs; i <= c + hs; ++i)
    {
        const float v = monoBuf[(size_t) (i & mask)];
        if (v > bv) { bv = v; best = i; }
    }

    const int nch = jmin (2, getTotalNumInputChannels());
    const double gain = Tout / T;
    const int64_t first = (int64_t) std::ceil (tS - T);
    const int64_t last = (int64_t) std::floor (tS + T);
    const int64_t minPos = jmax ((int64_t) 0, emitPos);

    for (int64_t i = first; i <= last; ++i)
    {
        if (i < minPos) continue;
        const double j = (double) i - tS;
        const float w = (float) (0.5 + 0.5 * std::cos (MathConstants<double>::pi * j / T));
        const double src = (double) best + j * fs;
        const int64_t i0 = (int64_t) std::floor (src);
        const float fr = (float) (src - (double) i0);

        for (int ch = 0; ch < nch; ++ch)
        {
            const float a = inBuf[ch][(size_t) (i0 & mask)];
            const float b = inBuf[ch][(size_t) ((i0 + 1) & mask)];
            olaBuf[ch][(size_t) (i & mask)] += (a + (b - a) * fr) * w * (float) gain;
        }
    }

    tS += Tout;

    dispHz.store (f.hz);
    dispMidi.store (f.mInst);
    dispTarget.store (f.target);
    dispVoiced.store (f.voiced);
    dispDev.store (f.target >= 0.f ? (f.mInst - f.target) * 100.f : 0.f);
}

// ---------------------------------------------------------------------------
void KuzkoTuneProcessor::processBlock (AudioBuffer<float>& buf, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const int n = buf.getNumSamples();
    const int nch = jmin (2, buf.getNumChannels());
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buf.clear (c, 0, n);

    if (inBuf[0].empty() || nch < 1) return;

    readParams();

    emitPos = nWritten - L;

    for (int i = 0; i < n; ++i)
    {
        const size_t idx = (size_t) ((nWritten + i) & mask);
        float sum = 0.f;
        for (int c = 0; c < nch; ++c)
        {
            const float v = buf.getSample (c, i);
            inBuf[c][idx] = v;
            sum += v;
        }
        monoBuf[idx] = sum / (float) nch;
    }
    nWritten += n;

    // Analyse : une image tous les "hop" échantillons, dès que la fenêtre est disponible
    while (framesDone * (int64_t) hop + Wa / 2 <= nWritten)
        processFrame (framesDone++);

    // Synthèse : grains placés tant que la source nécessaire est disponible
    const double needIn = 2.5 * Tmax + 3.0;
    for (;;)
    {
        const int64_t k = (int64_t) (tS / hop);
        if (k + 1 >= framesDone) break;
        if (tS + needIn > (double) nWritten) break;
        placeGrain();
    }

    // Sortie retardée de L échantillons
    for (int i = 0; i < n; ++i)
    {
        const int64_t m = nWritten - n + i - L;
        if (m < 0)
        {
            for (int c = 0; c < nch; ++c) buf.setSample (c, i, 0.f);
            continue;
        }

        const size_t idx = (size_t) (m & mask);
        const float wf = wetAt (m);
        for (int c = 0; c < nch; ++c)
        {
            const float dry = inBuf[c][idx];
            const float wet = olaBuf[c][idx];
            olaBuf[c][idx] = 0.f;
            const float corrected = dry + wf * (wet - dry);
            buf.setSample (c, i, dry + mixv * (corrected - dry));
        }
    }
}

AudioProcessorEditor* KuzkoTuneProcessor::createEditor()
{
    return new KuzkoTuneEditor (*this);
}

void KuzkoTuneProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void KuzkoTuneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KuzkoTuneProcessor();
}
