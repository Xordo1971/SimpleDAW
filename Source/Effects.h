#pragma once
#include "Text.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

// ============================================================================
// Efectos integrados de SimpleDAW. Cada uno es un AudioProcessor estéreo con
// parámetros, así que se procesan igual que un plugin externo y su ventana
// de edición se genera automáticamente a partir de los parámetros.
// ============================================================================

class BuiltInEffect : public juce::AudioProcessor
{
public:
    explicit BuiltInEffect (const juce::String& effectName)
        : juce::AudioProcessor (BusesProperties()
                                    .withInput  ("Entrada", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Salida",  juce::AudioChannelSet::stereo(), true)),
          name (effectName) {}

    const juce::String getName() const override { return name; }

    void prepareToPlay (double sr, int block) override
    {
        sampleRate = sr;
        prepare ({ sr, (juce::uint32) juce::jmax (1, block), 2 });
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        if (buffer.getNumChannels() >= 2 && buffer.getNumSamples() > 0)
            process (buffer);
    }

   #if JUCE_MODULE_AVAILABLE_juce_audio_processors   // (las pruebas automáticas se compilan sin interfaz)
    bool hasEditor() const override                  { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
   #else
    bool hasEditor() const override                  { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
   #endif

    double getTailLengthSeconds() const override     { return 0.0; }
    bool acceptsMidi() const override                { return false; }
    bool producesMidi() const override               { return false; }
    int getNumPrograms() override                    { return 1; }
    int getCurrentProgram() override                 { return 0; }
    void setCurrentProgram (int) override            {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    // Estado = valor de cada parámetro por su identificador (para guardar proyectos)
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::XmlElement xml ("EFFECT");
        for (auto* p : getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                xml.setAttribute (withId->paramID, (double) withId->getValue());
        writeExtraState (xml);
        copyXmlToBinary (xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            for (auto* p : getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                    if (xml->hasAttribute (withId->paramID))
                        withId->setValueNotifyingHost ((float) xml->getDoubleAttribute (withId->paramID));
        if (auto xml = getXmlFromBinary (data, size))
            readExtraState (*xml);
    }

protected:
    virtual void writeExtraState (juce::XmlElement&) {}
    virtual void readExtraState (const juce::XmlElement&) {}
public:

protected:
    virtual void prepare (const juce::dsp::ProcessSpec&) = 0;
    virtual void process (juce::AudioBuffer<float>&) = 0;

    juce::AudioParameterFloat* param (const juce::String& id, const juce::String& label,
                                      float min, float max, float def, const juce::String& suffix = {},
                                      float centre = 0.0f)
    {
        juce::NormalisableRange<float> range (min, max);
        if (centre > min && centre < max)
            range.setSkewForCentre (centre);

        auto* p = new juce::AudioParameterFloat (juce::ParameterID { id, 1 }, label, range, def,
                                                 juce::AudioParameterFloatAttributes().withLabel (suffix));
        addParameter (p);
        return p;
    }

    juce::AudioParameterChoice* choice (const juce::String& id, const juce::String& label, const juce::StringArray& options, int def)
    {
        auto* p = new juce::AudioParameterChoice (juce::ParameterID { id, 1 }, label, options, def);
        addParameter (p);
        return p;
    }

    juce::AudioParameterBool* toggle (const juce::String& id, const juce::String& label, bool def)
    {
        auto* p = new juce::AudioParameterBool (juce::ParameterID { id, 1 }, label, def);
        addParameter (p);
        return p;
    }

    static float mixDryWet (float dry, float wet, float mix) { return dry * (1.0f - mix) + wet * mix; }

    double sampleRate = 44100.0;

private:
    juce::String name;
};

// ---------------------------------------------------------------- Delay
class DelayEffect : public BuiltInEffect
{
public:
    DelayEffect() : BuiltInEffect ("Delay")
    {
        time     = param ("time", "Tiempo", 10.0f, 2000.0f, 380.0f, "ms", 400.0f);
        feedback = param ("feedback", "Repeticiones", 0.0f, 0.95f, 0.35f);
        tone     = param ("tone", "Tono", 1000.0f, 18000.0f, 6000.0f, "Hz", 5000.0f);
        mix      = param ("mix", "Mezcla", 0.0f, 1.0f, 0.3f);
    }

    double getTailLengthSeconds() const override { return 4.0; }

private:
    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        const int size = (int) (spec.sampleRate * 2.1) + 4;
        for (auto& b : lines) b.assign ((size_t) size, 0.0f);
        writePos = 0;
        smoothedDelay.reset (spec.sampleRate, 0.08);
        smoothedDelay.setCurrentAndTargetValue (time->get() * 0.001f * (float) spec.sampleRate);
        lp[0] = lp[1] = 0.0f;
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        const int size = (int) lines[0].size();
        if (size < 4) return;

        smoothedDelay.setTargetValue (juce::jlimit (1.0f, (float) size - 2.0f, time->get() * 0.001f * (float) sampleRate));
        const float fb = feedback->get(), m = mix->get();
        const float lpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * tone->get() / (float) sampleRate);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float d = smoothedDelay.getNextValue();
            float readPos = (float) writePos - d;
            if (readPos < 0) readPos += (float) size;
            const int i0 = (int) readPos;
            const int i1 = (i0 + 1) % size;
            const float frac = readPos - (float) i0;

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                auto& line = lines[(size_t) ch];
                float wet = line[(size_t) i0] + frac * (line[(size_t) i1] - line[(size_t) i0]);
                lp[ch] += lpCoef * (wet - lp[ch]);
                wet = lp[ch];
                line[(size_t) writePos] = data[i] + wet * fb;
                data[i] = mixDryWet (data[i], wet, m);
            }
            if (++writePos >= size) writePos = 0;
        }
    }

    juce::AudioParameterFloat *time, *feedback, *tone, *mix;
    std::vector<float> lines[2];
    int writePos = 0;
    float lp[2] {};
    juce::SmoothedValue<float> smoothedDelay;
};

// ---------------------------------------------------------------- Reverb
class ReverbEffect : public BuiltInEffect
{
public:
    ReverbEffect() : BuiltInEffect ("Reverb")
    {
        size    = param ("size", tr8 ("Tamaño"), 0.0f, 1.0f, 0.6f);
        damping = param ("damping", tr8 ("Amortiguación"), 0.0f, 1.0f, 0.45f);
        width   = param ("width", "Amplitud", 0.0f, 1.0f, 1.0f);
        mix     = param ("mix", "Mezcla", 0.0f, 1.0f, 0.25f);
    }

    double getTailLengthSeconds() const override { return 6.0; }

private:
    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        reverb.setSampleRate (spec.sampleRate);
        reverb.reset();
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        juce::Reverb::Parameters p;
        p.roomSize = size->get();
        p.damping  = damping->get();
        p.width    = width->get();
        p.wetLevel = mix->get();
        p.dryLevel = 1.0f - mix->get();   // al 100 % solo suena la reverb (ideal en un bus)
        reverb.setParameters (p);
        reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples());
    }

    juce::AudioParameterFloat *size, *damping, *width, *mix;
    juce::Reverb reverb;
};

// ---------------------------------------------------------------- Chorus y Flanger (misma base)
class ModulationEffect : public BuiltInEffect
{
public:
    ModulationEffect (const juce::String& n, float defRate, float defDepth, float centreMs, float defFeedback, float defMix)
        : BuiltInEffect (n), centreDelayMs (centreMs)
    {
        rate     = param ("rate", "Velocidad", 0.05f, 10.0f, defRate, "Hz", 1.0f);
        depth    = param ("depth", "Profundidad", 0.0f, 1.0f, defDepth);
        feedback = param ("feedback", tr8 ("Realimentación"), -0.95f, 0.95f, defFeedback);
        mix      = param ("mix", "Mezcla", 0.0f, 1.0f, defMix);
    }

private:
    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        chorus.prepare (spec);
        chorus.setCentreDelay (centreDelayMs);
        chorus.reset();
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        chorus.setRate (rate->get());
        chorus.setDepth (depth->get());
        chorus.setFeedback (feedback->get());
        chorus.setMix (mix->get());
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) buffer.getNumSamples());
        chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    float centreDelayMs;
    juce::AudioParameterFloat *rate, *depth, *feedback, *mix;
    juce::dsp::Chorus<float> chorus;
};

struct ChorusEffect  : ModulationEffect { ChorusEffect()  : ModulationEffect ("Chorus",  0.8f, 0.35f, 8.0f, 0.0f, 0.5f) {} };
struct FlangerEffect : ModulationEffect { FlangerEffect() : ModulationEffect ("Flanger", 0.25f, 0.6f, 1.5f, 0.6f, 0.5f) {} };

// ---------------------------------------------------------------- Phaser
class PhaserEffect : public BuiltInEffect
{
public:
    PhaserEffect() : BuiltInEffect ("Phaser")
    {
        rate     = param ("rate", "Velocidad", 0.05f, 5.0f, 0.5f, "Hz", 1.0f);
        depth    = param ("depth", "Profundidad", 0.0f, 1.0f, 0.7f);
        centre   = param ("centre", "Frecuencia", 200.0f, 5000.0f, 1000.0f, "Hz", 1000.0f);
        feedback = param ("feedback", tr8 ("Realimentación"), -0.9f, 0.9f, 0.5f);
        mix      = param ("mix", "Mezcla", 0.0f, 1.0f, 0.5f);
    }

private:
    void prepare (const juce::dsp::ProcessSpec& spec) override { phaser.prepare (spec); phaser.reset(); }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        phaser.setRate (rate->get());
        phaser.setDepth (depth->get());
        phaser.setCentreFrequency (centre->get());
        phaser.setFeedback (feedback->get());
        phaser.setMix (mix->get());
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) buffer.getNumSamples());
        phaser.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    juce::AudioParameterFloat *rate, *depth, *centre, *feedback, *mix;
    juce::dsp::Phaser<float> phaser;
};

// ---------------------------------------------------------------- Pitch (dos cabezas de lectura con fundido)
class PitchEffect : public BuiltInEffect
{
public:
    PitchEffect() : BuiltInEffect ("Pitch")
    {
        semitones = param ("semitones", "Semitonos", -12.0f, 12.0f, 12.0f, "st");
        cents     = param ("cents", tr8 ("Afinación fina"), -50.0f, 50.0f, 0.0f, "ct");
        mix       = param ("mix", "Mezcla", 0.0f, 1.0f, 0.5f);
    }

private:
    static constexpr int bufferSize = 8192;

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        for (auto& b : lines) b.assign (bufferSize, 0.0f);
        writePos = 0;
        phase = 0.0;
        window = juce::jlimit (256.0, (double) bufferSize - 8.0, spec.sampleRate * 0.05);   // ventana de 50 ms
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        const double ratio = std::pow (2.0, (semitones->get() + cents->get() / 100.0) / 12.0);
        const double step = (1.0 - ratio) / window;
        const float m = mix->get();

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double p1 = phase;
            const double p2 = std::fmod (phase + 0.5, 1.0);
            const float g1 = (float) std::sin (juce::MathConstants<double>::pi * p1);
            const float g2 = (float) std::sin (juce::MathConstants<double>::pi * p2);

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                auto& line = lines[(size_t) ch];
                line[(size_t) writePos] = data[i];
                const float wet = read (line, p1 * window) * g1 + read (line, p2 * window) * g2;
                data[i] = mixDryWet (data[i], wet, m);
            }

            phase += step;
            phase -= std::floor (phase);
            writePos = (writePos + 1) % bufferSize;
        }
    }

    float read (const std::vector<float>& line, double delay) const
    {
        double pos = (double) writePos - delay - 1.0;
        while (pos < 0) pos += bufferSize;
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % bufferSize;
        const float frac = (float) (pos - i0);
        return line[(size_t) i0] + frac * (line[(size_t) i1] - line[(size_t) i0]);
    }

    juce::AudioParameterFloat *semitones, *cents, *mix;
    std::vector<float> lines[2];
    int writePos = 0;
    double phase = 0.0, window = 2048.0;
};

// ---------------------------------------------------------------- Ecualizador de 3 bandas
class EqEffect : public BuiltInEffect
{
public:
    EqEffect() : BuiltInEffect ("Ecualizador")
    {
        lowGain  = param ("low", "Graves", -15.0f, 15.0f, 0.0f, "dB");
        midFreq  = param ("midfreq", "Frecuencia medios", 200.0f, 5000.0f, 1000.0f, "Hz", 1000.0f);
        midGain  = param ("mid", "Medios", -15.0f, 15.0f, 0.0f, "dB");
        highGain = param ("high", "Agudos", -15.0f, 15.0f, 0.0f, "dB");
        lowCut   = param ("lowcut", "Corte de graves", 20.0f, 500.0f, 20.0f, "Hz", 100.0f);
    }

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefs  = juce::dsp::IIR::Coefficients<float>;

    void prepare (const juce::dsp::ProcessSpec&) override
    {
        for (auto& ch : filters) for (auto& f : ch) f.reset();
        lastValues.fill (-9999.0f);
        updateCoefficients();
    }

    void updateCoefficients()
    {
        const std::array<float, 5> v { lowGain->get(), midFreq->get(), midGain->get(), highGain->get(), lowCut->get() };
        if (v == lastValues) return;
        lastValues = v;

        const double sr = sampleRate;
        auto hp   = Coefs::makeHighPass  (sr, v[4], 0.707f);
        auto low  = Coefs::makeLowShelf  (sr, 120.0, 0.707f, juce::Decibels::decibelsToGain (v[0]));
        auto mid  = Coefs::makePeakFilter (sr, v[1], 0.9f,   juce::Decibels::decibelsToGain (v[2]));
        auto high = Coefs::makeHighShelf (sr, 6000.0, 0.707f, juce::Decibels::decibelsToGain (v[3]));

        for (auto& ch : filters)
        {
            *ch[0].coefficients = *hp;
            *ch[1].coefficients = *low;
            *ch[2].coefficients = *mid;
            *ch[3].coefficients = *high;
        }
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        updateCoefficients();
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float x = data[i];
                for (auto& f : filters[(size_t) ch])
                    x = f.processSample (x);
                data[i] = x;
            }
        }
    }

    juce::AudioParameterFloat *lowGain, *midFreq, *midGain, *highGain, *lowCut;
    std::array<std::array<Filter, 4>, 2> filters {{
        {{ Filter (Coefs::makeAllPass (44100.0, 1000.0)), Filter (Coefs::makeAllPass (44100.0, 1000.0)),
           Filter (Coefs::makeAllPass (44100.0, 1000.0)), Filter (Coefs::makeAllPass (44100.0, 1000.0)) }},
        {{ Filter (Coefs::makeAllPass (44100.0, 1000.0)), Filter (Coefs::makeAllPass (44100.0, 1000.0)),
           Filter (Coefs::makeAllPass (44100.0, 1000.0)), Filter (Coefs::makeAllPass (44100.0, 1000.0)) }} }};
    std::array<float, 5> lastValues {};
};

// ---------------------------------------------------------------- Compresor
class CompressorEffect : public BuiltInEffect
{
public:
    CompressorEffect() : BuiltInEffect ("Compresor")
    {
        threshold = param ("threshold", "Umbral", -60.0f, 0.0f, -18.0f, "dB");
        ratio     = param ("ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1", 4.0f);
        attack    = param ("attack", "Ataque", 0.1f, 100.0f, 10.0f, "ms", 10.0f);
        release   = param ("release", tr8 ("Relajación"), 10.0f, 1000.0f, 120.0f, "ms", 120.0f);
        makeup    = param ("makeup", "Ganancia", 0.0f, 24.0f, 3.0f, "dB");
    }

private:
    void prepare (const juce::dsp::ProcessSpec& spec) override { comp.prepare (spec); comp.reset(); }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        comp.setThreshold (threshold->get());
        comp.setRatio (ratio->get());
        comp.setAttack (attack->get());
        comp.setRelease (release->get());
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) buffer.getNumSamples());
        comp.process (juce::dsp::ProcessContextReplacing<float> (block));
        buffer.applyGain (juce::Decibels::decibelsToGain (makeup->get()));
    }

    juce::AudioParameterFloat *threshold, *ratio, *attack, *release, *makeup;
    juce::dsp::Compressor<float> comp;
};

// ---------------------------------------------------------------- Overdrive
class OverdriveEffect : public BuiltInEffect
{
public:
    OverdriveEffect() : BuiltInEffect ("Overdrive")
    {
        drive = param ("drive", tr8 ("Saturación"), 0.0f, 40.0f, 18.0f, "dB");
        tone  = param ("tone", "Tono", 800.0f, 12000.0f, 4000.0f, "Hz", 3000.0f);
        level = param ("level", "Volumen", -30.0f, 6.0f, -8.0f, "dB");
    }

private:
    void prepare (const juce::dsp::ProcessSpec&) override { lp[0] = lp[1] = 0.0f; }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        const float pre  = juce::Decibels::decibelsToGain (drive->get());
        const float post = juce::Decibels::decibelsToGain (level->get());
        const float coef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * tone->get() / (float) sampleRate);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float x = data[i] * pre;
                const float y = std::tanh (x + 0.1f * x * x) - std::tanh (0.0f);   // ligera asimetría, como un pedal
                lp[ch] += coef * (y - lp[ch]);
                data[i] = lp[ch] * post;
            }
        }
    }

    juce::AudioParameterFloat *drive, *tone, *level;
    float lp[2] {};
};

// ---------------------------------------------------------------- Ampli de guitarra
// Puerta de ruido -> previo (1 o 2 etapas de saturación, con sobremuestreo x2)
// -> ecualizador de tonos -> presencia -> simulación de pantalla -> volumen
class AmpEffect : public BuiltInEffect
{
public:
    AmpEffect() : BuiltInEffect ("Ampli")
    {
        channel  = choice ("channel", "Canal", { "Limpio", "Crunch", "Lead" }, 1);
        gain     = param ("gain", "Ganancia", 0.0f, 10.0f, 5.0f);
        bass     = param ("bass", "Graves", 0.0f, 10.0f, 5.0f);
        mid      = param ("mid", "Medios", 0.0f, 10.0f, 5.0f);
        treble   = param ("treble", "Agudos", 0.0f, 10.0f, 5.0f);
        presence = param ("presence", "Presencia", 0.0f, 10.0f, 5.0f);
        master   = param ("master", "Volumen", -30.0f, 6.0f, -3.0f, "dB");
        cabinet  = choice ("cabinet", "Pantalla", { "Sin pantalla", "1x12", "2x12", "4x12" }, 3);
        gate     = param ("gate", "Puerta de ruido", -90.0f, -30.0f, -72.0f, "dB");
        // Canal limpio con sus propios controles (como un ampli de dos canales)
        cleanVol    = param ("cleanVol", "Volumen (limpio)", 0.0f, 10.0f, 5.0f);
        cleanTreble = param ("cleanTreble", "Agudos (limpio)", 0.0f, 10.0f, 5.0f);
        cleanBass   = param ("cleanBass", "Graves (limpio)", 0.0f, 10.0f, 5.0f);
        // Interruptores
        fat    = toggle ("fat", "Fat (más graves)", false);
        c45    = toggle ("c45", "C45 (más brillo y mordida)", false);
        sat    = toggle ("sat", "Sat (más saturación)", false);
        voice  = toggle ("voice", "Voice (limpio con más medios)", false);
        bright = toggle ("bright", "Bright (limpio más brillante)", false);
        power  = toggle ("power", "Encendido", true);
    }

   #if JUCE_MODULE_AVAILABLE_juce_audio_processors
    juce::AudioProcessorEditor* createEditor() override;   // frontal del ampli (AmpEditor.h)
   #endif

    juce::String presetName;   // último preset cargado (se guarda con el proyecto)

protected:
    void writeExtraState (juce::XmlElement& x) override         { if (presetName.isNotEmpty()) x.setAttribute ("presetName", presetName); }
    void readExtraState (const juce::XmlElement& x) override    { presetName = x.getStringAttribute ("presetName"); }
public:

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefs  = juce::dsp::IIR::Coefficients<float>;
    static constexpr int maxCab = 6;

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversampling->initProcessing (spec.maximumBlockSize);
        for (auto& ch : chains) { for (auto& f : ch.pre) f.reset(); for (auto& f : ch.inter) f.reset(); for (auto& f : ch.tone) f.reset(); for (auto& f : ch.cab) f.reset(); }
        gateEnv[0] = gateEnv[1] = 0.0f;
        gateGain[0] = gateGain[1] = 1.0f;
        lastKey = {};
        updateFilters();
    }

    // Recalcula los filtros solo cuando cambian los controles
    void updateFilters()
    {
        const std::array<float, 13> key { (float) channel->getIndex(), bass->get(), mid->get(), treble->get(), presence->get(),
                                          (float) cabinet->getIndex(), (float) sampleRate, cleanBass->get(), cleanTreble->get(),
                                          (float) fat->get(), (float) c45->get(), (float) voice->get(), (float) bright->get() };
        if (key == lastKey)
            return;
        lastKey = key;

        const double sr = sampleRate, os = sampleRate * 2.0;
        const int ch = channel->getIndex();
        auto db = [] (float v) { return juce::Decibels::decibelsToGain (v); };

        // Antes de saturar: quitar graves que embarran y (crunch/lead) realzar medios para que "muerda"
        // Interruptores: Voice/Bright en el limpio; Fat/C45 en los canales saturados
        const bool clean = ch == 0;
        auto pre0 = Coefs::makeHighPass (sr, clean ? (voice->get() ? 90.0 : 60.0) : (fat->get() ? 70.0 : 110.0), 0.7f);
        auto pre1 = Coefs::makePeakFilter (sr, clean ? 650.0 : 800.0, 0.7f, db (clean ? (voice->get() ? 4.5f : 0.0f) : 6.0f));
        auto pre2 = Coefs::makeLowShelf (sr, 140.0, 0.7f, db (! clean && fat->get() ? 4.0f : 0.0f));
        auto pre3 = Coefs::makeHighShelf (sr, clean ? 2500.0 : 1800.0, 0.7f,
                                          db (clean ? (bright->get() ? 6.0f : 0.0f) : (c45->get() ? 4.0f : 0.0f)));
        // Entre etapas (a 2x): suavizar agudos ásperos y apretar los graves
        auto int0 = Coefs::makeLowPass (os, ch == 2 ? 6500.0 : 9000.0, 0.7f);
        auto int1 = Coefs::makeHighPass (os, ch == 2 ? 160.0 : 40.0, 0.7f);
        // Tonos (5 = plano; de -12 a +12 dB) y presencia
        const float b = clean ? cleanBass->get() : bass->get();
        const float m = clean ? 5.0f : mid->get();
        const float t = clean ? cleanTreble->get() : treble->get();
        auto t0 = Coefs::makeLowShelf  (sr, 120.0, 0.7f,  db ((b - 5.0f) * 2.4f));
        auto t1 = Coefs::makePeakFilter (sr, 700.0, 0.8f, db ((m - 5.0f) * 2.4f));
        auto t2 = Coefs::makeHighShelf (sr, 3000.0, 0.7f, db ((t - 5.0f) * 2.4f));
        auto t3 = Coefs::makeHighShelf (sr, 5500.0, 0.7f, db ((presence->get() - 5.0f) * 1.6f));

        // Pantalla: respuesta típica de un altavoz de guitarra (sin agudos por encima de ~5 kHz)
        std::array<juce::ReferenceCountedObjectPtr<Coefs>, maxCab> cab;
        switch (cabinet->getIndex())
        {
            case 1: cab = { Coefs::makeHighPass (sr, 90.0, 0.7f), Coefs::makePeakFilter (sr, 120.0, 1.0f, db (2.0f)),
                            Coefs::makePeakFilter (sr, 2200.0, 1.2f, db (3.0f)), Coefs::makePeakFilter (sr, 500.0, 1.0f, db (0.0f)),
                            Coefs::makeLowPass (sr, 5500.0, 0.7f), Coefs::makeLowPass (sr, 5500.0, 0.7f) }; break;
            case 2: cab = { Coefs::makeHighPass (sr, 80.0, 0.7f), Coefs::makePeakFilter (sr, 110.0, 1.0f, db (3.0f)),
                            Coefs::makePeakFilter (sr, 2000.0, 1.2f, db (3.0f)), Coefs::makePeakFilter (sr, 400.0, 1.0f, db (-2.0f)),
                            Coefs::makeLowPass (sr, 5000.0, 0.7f), Coefs::makeLowPass (sr, 5000.0, 0.7f) }; break;
            case 3: cab = { Coefs::makeHighPass (sr, 70.0, 0.7f), Coefs::makePeakFilter (sr, 100.0, 1.0f, db (4.0f)),
                            Coefs::makePeakFilter (sr, 1800.0, 1.2f, db (2.5f)), Coefs::makePeakFilter (sr, 450.0, 1.0f, db (-3.0f)),
                            Coefs::makeLowPass (sr, 4500.0, 0.6f), Coefs::makeLowPass (sr, 4500.0, 0.6f) }; break;
            default: break;
        }

        for (auto& c : chains)
        {
            *c.pre[0].coefficients = *pre0;  *c.pre[1].coefficients = *pre1;
            *c.pre[2].coefficients = *pre2;  *c.pre[3].coefficients = *pre3;
            *c.inter[0].coefficients = *int0; *c.inter[1].coefficients = *int1;
            *c.tone[0].coefficients = *t0; *c.tone[1].coefficients = *t1; *c.tone[2].coefficients = *t2; *c.tone[3].coefficients = *t3;
            for (int i = 0; i < maxCab; ++i)
                if (cab[(size_t) i] != nullptr)
                    *c.cab[(size_t) i].coefficients = *cab[(size_t) i];
        }
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        if (! power->get())
            return;   // apagado: la señal pasa tal cual
        updateFilters();
        const int n = buffer.getNumSamples();
        const int ch = channel->getIndex();
        const float g = ch == 0 ? cleanVol->get() : gain->get();
        const float satDb = sat->get() ? 6.0f : 0.0f;
        const bool useCab = cabinet->getIndex() > 0;

        // 1. Puerta de ruido y filtros previos
        const float threshold = gate->get() <= -89.0f ? 0.0f : juce::Decibels::decibelsToGain (gate->get());
        const float release = std::exp (-1.0f / (0.08f * (float) sampleRate));
        const float attack  = std::exp (-1.0f / (0.001f * (float) sampleRate));
        const int holdSamples = (int) (0.06 * sampleRate);
        for (int c = 0; c < 2; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                const float a = std::abs (d[i]);
                gateEnv[c] = a > gateEnv[c] ? a : gateEnv[c] * release;
                // Abre al pasar el umbral; para cerrar tiene que bajar 6 dB más y aguantar 60 ms:
                // así no "tartamudea" (eso suena a zumbido rasposo con mucha ganancia)
                if (gateEnv[c] >= threshold)
                {
                    gateOpen[c] = true;
                    gateHold[c] = holdSamples;
                }
                else if (gateEnv[c] < threshold * 0.5f)
                {
                    if (gateHold[c] > 0) --gateHold[c];
                    else gateOpen[c] = false;
                }
                const float target = gateOpen[c] ? 1.0f : 0.0f;
                gateGain[c] = target + (gateGain[c] - target) * (target > gateGain[c] ? attack : release);
                float x = d[i] * gateGain[c];
                for (auto& f : chains[(size_t) c].pre) x = f.processSample (x);
                d[i] = x;
            }
        }

        // 2. Saturación a doble frecuencia (menos aliasing: suena menos "digital")
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) n);
        auto up = oversampling->processSamplesUp (block);
        const float drive1 = ch == 0 ? 1.0f + g * 0.45f
                           : ch == 1 ? juce::Decibels::decibelsToGain (g * 3.4f + 2.0f + satDb)
                                     : juce::Decibels::decibelsToGain (g * 3.6f + 8.0f + satDb);
        const float bias = ch == 0 ? 0.05f : 0.18f;   // asimetría: armónicos pares, como una válvula
        const float makeup = ch == 0 ? 0.9f / std::tanh (drive1 * 0.5f + 0.2f) : ch == 1 ? 0.55f : 0.42f;
        const float tb = std::tanh (bias);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* d = up.getChannelPointer (c);
            auto& chain = chains[c];
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                float x = std::tanh (d[i] * drive1 + bias) - tb;
                if (ch == 2)   // segunda etapa del canal lead
                {
                    x = chain.inter[1].processSample (chain.inter[0].processSample (x));
                    x = std::tanh (x * 4.0f + 0.1f) - std::tanh (0.1f);
                }
                else
                {
                    x = chain.inter[0].processSample (x);
                }
                d[i] = x * makeup;
            }
        }
        oversampling->processSamplesDown (block);

        // 3. Tonos, presencia, pantalla y volumen
        const float out = juce::Decibels::decibelsToGain (master->get());
        for (int c = 0; c < 2; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            auto& chain = chains[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                float x = d[i];
                for (auto& f : chain.tone) x = f.processSample (x);
                if (useCab)
                    for (auto& f : chain.cab) x = f.processSample (x);
                d[i] = x * out;
            }
        }
    }

    double getLatencySeconds() const { return 0.0; }

    struct Chain
    {
        std::array<Filter, 4> pre;
        std::array<Filter, 2> inter;
        std::array<Filter, 4> tone;
        std::array<Filter, maxCab> cab;
        Chain()
        {
            auto init = [] (auto& arr) { for (auto& f : arr) f.coefficients = Coefs::makeAllPass (44100.0, 1000.0); };
            init (pre); init (inter); init (tone); init (cab);
        }
    };

    juce::AudioParameterChoice *channel, *cabinet;
    juce::AudioParameterFloat *gain, *bass, *mid, *treble, *presence, *master, *gate, *cleanVol, *cleanTreble, *cleanBass;
    juce::AudioParameterBool *fat, *c45, *sat, *voice, *bright, *power;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::array<Chain, 2> chains;
    std::array<float, 13> lastKey {};
    float gateEnv[2] {}, gateGain[2] { 1.0f, 1.0f };
    bool gateOpen[2] { true, true };
    int gateHold[2] {};
};

// ---------------------------------------------------------------- Catálogo
namespace BuiltInEffects
{
    inline juce::StringArray names()
    {
        return { "Delay", "Reverb", "Chorus", "Flanger", "Phaser", "Pitch", "Ecualizador", "Compresor", "Overdrive", "Ampli" };
    }

    inline std::unique_ptr<juce::AudioProcessor> create (int index)
    {
        switch (index)
        {
            case 0: return std::make_unique<DelayEffect>();
            case 1: return std::make_unique<ReverbEffect>();
            case 2: return std::make_unique<ChorusEffect>();
            case 3: return std::make_unique<FlangerEffect>();
            case 4: return std::make_unique<PhaserEffect>();
            case 5: return std::make_unique<PitchEffect>();
            case 6: return std::make_unique<EqEffect>();
            case 7: return std::make_unique<CompressorEffect>();
            case 8: return std::make_unique<OverdriveEffect>();
            case 9: return std::make_unique<AmpEffect>();
            default: return nullptr;
        }
    }

    inline std::unique_ptr<juce::AudioProcessor> createByName (const juce::String& name)
    {
        return create (names().indexOf (name));
    }
}

#if JUCE_MODULE_AVAILABLE_juce_audio_processors
 #include "AmpEditor.h"   // frontal gráfico del ampli
#endif
