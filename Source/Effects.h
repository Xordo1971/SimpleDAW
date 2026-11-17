#pragma once
#include "Text.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>   // IR del ampli (leer el archivo)
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
        cabinet  = choice ("cabinet", "Pantalla", { "Sin pantalla", "1x12", "2x12", "4x12", "IR (archivo)" }, 3);
        // Micrófono delante de la pantalla (solo suena si hay pantalla): modelo, posición y distancia
        mic      = choice ("mic", "Micro", { "Condenser 87", "Condenser 414", "Dynamic 20", "Dynamic 57",
                                              "Dynamic 421", "Dynamic 609", "Ribbon 121" }, 3);
        micPos   = param ("micPos", tr8 ("Posición del micro"), 0.0f, 1.0f, 0.3f);   // 0 = centro del cono, 1 = borde
        micDist  = param ("micDist", "Distancia del micro", 0.0f, 1.0f, 0.2f); // 0 = pegado, 1 = lejos
        gate     = param ("gate", "Puerta de ruido", -90.0f, -30.0f, -72.0f, "dB");
        // Canal limpio con sus propios controles (como un ampli de dos canales)
        cleanVol    = param ("cleanVol", "Volumen (limpio)", 0.0f, 10.0f, 5.0f);
        cleanTreble = param ("cleanTreble", "Agudos (limpio)", 0.0f, 10.0f, 5.0f);
        cleanBass   = param ("cleanBass", "Graves (limpio)", 0.0f, 10.0f, 5.0f);
        // Interruptores
        fat    = toggle ("fat", "Fat (más graves)", false);
        c45    = toggle ("c45", "Bite (más brillo y mordida)", false);   // id antiguo, por compatibilidad
        sat    = toggle ("sat", "Sat (más saturación)", false);
        voice  = toggle ("voice", "Voice (limpio con más medios)", false);
        bright = toggle ("bright", "Bright (limpio más brillante)", false);
        power  = toggle ("power", "Encendido", true);
    }

   #if JUCE_MODULE_AVAILABLE_juce_audio_processors
    juce::AudioProcessorEditor* createEditor() override;   // frontal del ampli (AmpEditor.h)
   #endif

    juce::String presetName;   // último preset cargado (se guarda con el proyecto)

    // Pantalla real: respuesta al impulso (IR) en un archivo de audio (.wav, .aiff, .flac...). Se usa con
    // la opción "IR (archivo)" de la pantalla. Devuelve false si el archivo no se puede leer.
    static constexpr int irCabinetIndex = 4;
    bool loadIr (const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples < 16)
            return false;
        convolution.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes,
                                         0, juce::dsp::Convolution::Normalise::yes);
        irFile = file;
        irLoaded = true;
        return true;
    }
    bool hasIr() const                { return irLoaded.load(); }
    juce::File getIrFile() const      { return irFile; }

protected:
    void writeExtraState (juce::XmlElement& x) override
    {
        if (presetName.isNotEmpty()) x.setAttribute ("presetName", presetName);
        if (irLoaded.load()) x.setAttribute ("irFile", irFile.getFullPathName());
    }
    void readExtraState (const juce::XmlElement& x) override
    {
        presetName = x.getStringAttribute ("presetName");
        const auto path = x.getStringAttribute ("irFile");
        if (path.isNotEmpty())
            loadIr (juce::File (path));   // si el archivo ya no está, la pantalla queda sin IR
    }
public:

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefs  = juce::dsp::IIR::Coefficients<float>;
    static constexpr int maxCab = 6;

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversampling->initProcessing (spec.maximumBlockSize);
        convolution.prepare (spec);
        convolution.reset();
        for (auto& ch : chains) { for (auto& f : ch.pre) f.reset(); for (auto& f : ch.inter) f.reset(); for (auto& f : ch.tone) f.reset(); for (auto& f : ch.cab) f.reset(); for (auto& f : ch.mic) f.reset(); }
        gateEnv[0] = gateEnv[1] = 0.0f;
        gateGain[0] = gateGain[1] = 1.0f;
        lastKey = {};
        updateFilters();
    }

    // Recalcula los filtros solo cuando cambian los controles
    void updateFilters()
    {
        const std::array<float, 16> key { (float) channel->getIndex(), bass->get(), mid->get(), treble->get(), presence->get(),
                                          (float) cabinet->getIndex(), (float) sampleRate, cleanBass->get(), cleanTreble->get(),
                                          (float) fat->get(), (float) c45->get(), (float) voice->get(), (float) bright->get(),
                                          (float) mic->getIndex(), micPos->get(), micDist->get() };
        if (key == lastKey)
            return;
        lastKey = key;

        const double sr = sampleRate, os = sampleRate * 2.0;
        const int ch = channel->getIndex();
        auto db = [] (float v) { return juce::Decibels::decibelsToGain (v); };

        // Antes de saturar: quitar graves que embarran y (crunch/lead) realzar medios para que "muerda"
        // Interruptores: Voice/Bright en el limpio; Fat/Bite en los canales saturados
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

        // Micro: cada modelo es una curva sencilla (graves, medios, brillo) y el sitio donde se pone
        // la modifica: hacia el borde del cono se pierden agudos; lejos se pierden graves de
        // cercanía y agudos, y se oye más la caja. Son aproximaciones, no medidas reales.
        struct MicCurve { float lowHz, lowDb, midHz, midQ, midDb, hiHz, hiDb, brightHz; };
        static const MicCurve curves[] = {
            // 0 Condenser 87: amplio y abierto, algo de graves
            { 100.0f,  1.5f, 3500.0f, 0.8f,  1.5f, 6500.0f,  2.5f, 1.15f },
            // 1 Condenser 414: brillante y suave
            { 120.0f,  1.0f, 4500.0f, 0.8f,  2.0f, 7000.0f,  3.0f, 1.25f },
            // 2 Dynamic 20: grueso, sin brillo, muy graves
            {  90.0f,  3.0f,  400.0f, 1.0f, -1.5f, 4500.0f, -2.0f, 0.90f },
            // 3 Dynamic 57: el clásico, medios con mordiente
            { 100.0f,  0.5f, 4800.0f, 1.0f,  3.5f, 6000.0f,  0.0f, 1.00f },
            // 4 Dynamic 421: pegada, graves llenos y medios algo ahuecados
            { 100.0f,  2.0f,  600.0f, 1.0f, -2.0f, 3800.0f,  3.0f, 1.05f },
            // 5 Dynamic 609: brillante y rápido
            { 110.0f, -1.0f, 4200.0f, 1.0f,  4.0f, 6500.0f,  1.5f, 1.15f },
            // 6 Ribbon 121: oscuro y redondo
            { 150.0f,  3.0f, 3000.0f, 0.8f, -1.0f, 5500.0f, -4.0f, 0.85f },
        };
        const auto& mc = curves[juce::jlimit (0, 6, mic->getIndex())];
        const float pos = micPos->get(), dist = micDist->get();
        auto m0 = Coefs::makeLowShelf  (sr, mc.lowHz, 0.7f, db (mc.lowDb + (1.0f - dist) * 3.0f - dist * 3.0f));   // efecto de cercanía
        auto m1 = Coefs::makePeakFilter (sr, mc.midHz, mc.midQ, db (mc.midDb * (1.0f - 0.7f * pos)));              // el borde del cono suena menos "mordiente"
        auto m2 = Coefs::makeHighShelf (sr, mc.hiHz, 0.7f, db (mc.hiDb - pos * 4.5f - dist * 2.5f));
        auto m3 = Coefs::makeLowPass   (sr, juce::jlimit (3000.0, 0.45 * sr, 6200.0 * mc.brightHz * (1.0 - 0.35 * pos) * (1.0 - 0.2 * dist)), 0.7f);

        for (auto& c : chains)
        {
            *c.mic[0].coefficients = *m0; *c.mic[1].coefficients = *m1;
            *c.mic[2].coefficients = *m2; *c.mic[3].coefficients = *m3;
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
        const bool useIr = cabinet->getIndex() == irCabinetIndex && irLoaded.load();
        const bool useCab = cabinet->getIndex() > 0 && cabinet->getIndex() < irCabinetIndex;

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
                {
                    for (auto& f : chain.cab) x = f.processSample (x);
                    for (auto& f : chain.mic) x = f.processSample (x);
                }
                d[i] = useIr ? x : x * out;   // con IR, el volumen se aplica después de la convolución
            }
        }

        if (useIr)
        {
            juce::dsp::AudioBlock<float> irBlock (buffer.getArrayOfWritePointers(), 2, (size_t) n);
            convolution.process (juce::dsp::ProcessContextReplacing<float> (irBlock));
            buffer.applyGain (out);
        }
    }

    double getLatencySeconds() const { return 0.0; }

    struct Chain
    {
        std::array<Filter, 4> pre;
        std::array<Filter, 2> inter;
        std::array<Filter, 4> tone;
        std::array<Filter, maxCab> cab;
        std::array<Filter, 4> mic;
        Chain()
        {
            auto init = [] (auto& arr) { for (auto& f : arr) f.coefficients = Coefs::makeAllPass (44100.0, 1000.0); };
            init (pre); init (inter); init (tone); init (cab); init (mic);
        }
    };

    juce::AudioParameterChoice *channel, *cabinet, *mic;
    juce::AudioParameterFloat *gain, *bass, *mid, *treble, *presence, *master, *micPos, *micDist, *gate, *cleanVol, *cleanTreble, *cleanBass;
    juce::AudioParameterBool *fat, *c45, *sat, *voice, *bright, *power;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::Convolution convolution;
    juce::File irFile;
    std::atomic<bool> irLoaded { false };
    std::array<Chain, 2> chains;
    std::array<float, 16> lastKey {};
    float gateEnv[2] {}, gateGain[2] { 1.0f, 1.0f };
    bool gateOpen[2] { true, true };
    int gateHold[2] {};
};

// ---------------------------------------------------------------- Trémolo
class TremoloEffect : public BuiltInEffect
{
public:
    TremoloEffect() : BuiltInEffect ("Tremolo")
    {
        rate   = param ("rate", "Velocidad", 0.1f, 15.0f, 4.5f, "Hz", 4.0f);
        depth  = param ("depth", "Profundidad", 0.0f, 1.0f, 0.6f);
        shape  = choice ("shape", "Forma", { "Seno", "Triangular", "Cuadrada suave" }, 0);
        stereo = toggle ("stereo", tr8 ("Estéreo (auto-pan)"), false);
    }

private:
    void prepare (const juce::dsp::ProcessSpec&) override { phase = 0.0f; }

    // fase 0..1 -> 0..1
    static float wave (int kind, float ph)
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        switch (kind)
        {
            case 1:  return ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f;
            case 2:  return 0.5f + 0.5f * std::tanh (4.0f * std::sin (twoPi * ph)) / std::tanh (4.0f);
            default: return 0.5f - 0.5f * std::cos (twoPi * ph);
        }
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        const float inc = rate->get() / (float) sampleRate;
        const float d = depth->get();
        const int kind = shape->getIndex();
        const bool wide = stereo->get();
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float a = wave (kind, phase);
            const float b = wide ? wave (kind, phase >= 0.5f ? phase - 0.5f : phase + 0.5f) : a;   // auto-pan: un lado sube cuando el otro baja
            l[i] *= 1.0f - d * a;
            r[i] *= 1.0f - d * b;
            phase += inc;
            if (phase >= 1.0f)
                phase -= 1.0f;
        }
    }

    juce::AudioParameterFloat *rate, *depth;
    juce::AudioParameterChoice* shape;
    juce::AudioParameterBool* stereo;
    float phase = 0.0f;
};

// ---------------------------------------------------------------- Wah
// Filtro paso banda que barre las frecuencias: con el pedal a mano (parámetro Pedal), solo (LFO)
// o siguiendo cómo se toca (más fuerte = más abierto).
class WahEffect : public BuiltInEffect
{
public:
    WahEffect() : BuiltInEffect ("Wah")
    {
        mode  = choice ("mode", "Modo", { "Pedal", "Auto LFO", "Auto ataque" }, 0);
        pedal = param ("pedal", "Pedal", 0.0f, 1.0f, 0.5f);
        rate  = param ("rate", "Velocidad (Auto LFO)", 0.1f, 8.0f, 1.5f, "Hz", 1.5f);
        sens  = param ("sens", "Sensibilidad (Auto ataque)", 0.0f, 1.0f, 0.6f);
        q     = param ("q", "Resonancia", 1.0f, 10.0f, 5.0f);
        mix   = param ("mix", "Mezcla", 0.0f, 1.0f, 1.0f);
    }

private:
    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        auto mono = spec;
        mono.numChannels = 1;
        for (auto& f : filt)
        {
            f.prepare (mono);
            f.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
            f.reset();
        }
        lfo = 0.0f;
        env = 0.0f;
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        const int kind = mode->getIndex();
        const float qv = q->get();
        const float wet = mix->get();
        const float lfoInc = rate->get() / (float) sampleRate;
        const float sensV = sens->get();
        const float envAtk = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
        const float envRel = 1.0f - std::exp (-1.0f / (0.12f * (float) sampleRate));
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float pos = pedal->get();
            if (kind == 1)
            {
                pos = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * lfo);
                lfo += lfoInc;
                if (lfo >= 1.0f) lfo -= 1.0f;
            }
            else if (kind == 2)
            {
                const float level = juce::jmax (std::abs (l[i]), std::abs (r[i]));
                env += (level > env ? envAtk : envRel) * (level - env);
                pos = juce::jlimit (0.0f, 1.0f, env * sensV * 6.0f);
            }

            // Cada 8 muestras se recalcula la frecuencia (de ~350 Hz a ~2,2 kHz, a lo largo de la carrera del pedal)
            if ((i & 7) == 0)
            {
                const float freq = 350.0f * std::pow (2200.0f / 350.0f, pos);
                for (auto& f : filt)
                {
                    f.setCutoffFrequency (freq);
                    f.setResonance (qv);
                }
            }

            const float wl = filt[0].processSample (0, l[i]) / qv;   // /Q: el paso banda de JUCE puede pasar de 0 dB
            const float wr = filt[1].processSample (0, r[i]) / qv;
            l[i] = mixDryWet (l[i], wl * 1.5f, wet);
            r[i] = mixDryWet (r[i], wr * 1.5f, wet);
        }
    }

    juce::AudioParameterChoice* mode;
    juce::AudioParameterFloat *pedal, *rate, *sens, *q, *mix;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> filt;
    float lfo = 0.0f, env = 0.0f;
};

// ---------------------------------------------------------------- Puerta de ruido
class GateEffect : public BuiltInEffect
{
public:
    GateEffect() : BuiltInEffect ("Puerta de ruido")
    {
        threshold = param ("threshold", "Umbral", -80.0f, -20.0f, -55.0f, "dB");
        attack    = param ("attack", "Ataque", 0.1f, 50.0f, 2.0f, "ms", 5.0f);
        hold      = param ("hold", "Mantener", 0.0f, 500.0f, 60.0f, "ms", 80.0f);
        release   = param ("release", tr8 ("Relajación"), 5.0f, 1000.0f, 120.0f, "ms", 150.0f);
        range     = param ("range", "Reduccion", -80.0f, 0.0f, -80.0f, "dB");
    }

private:
    void prepare (const juce::dsp::ProcessSpec&) override { env = 0.0f; gain = 1.0f; open = true; holdLeft = 0; }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        const float thr = juce::Decibels::decibelsToGain (threshold->get());
        const float floorGain = range->get() <= -79.0f ? 0.0f : juce::Decibels::decibelsToGain (range->get());
        const float atk = std::exp (-1.0f / (juce::jmax (0.1f, attack->get()) * 0.001f * (float) sampleRate));
        const float rel = std::exp (-1.0f / (juce::jmax (5.0f, release->get()) * 0.001f * (float) sampleRate));
        const float envRel = std::exp (-1.0f / (0.05f * (float) sampleRate));
        const int holdSamples = (int) (hold->get() * 0.001f * (float) sampleRate);
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            // Una sola puerta para los dos canales, para no descentrar la imagen estéreo
            const float level = juce::jmax (std::abs (l[i]), std::abs (r[i]));
            env = level > env ? level : env * envRel;
            if (env >= thr)
            {
                open = true;
                holdLeft = holdSamples;
            }
            else if (env < thr * 0.5f)   // histéresis de 6 dB: no "tartamudea"
            {
                if (holdLeft > 0) --holdLeft;
                else open = false;
            }
            const float target = open ? 1.0f : floorGain;
            gain = target + (gain - target) * (target > gain ? atk : rel);
            l[i] *= gain;
            r[i] *= gain;
        }
    }

    juce::AudioParameterFloat *threshold, *attack, *hold, *release, *range;
    float env = 0.0f, gain = 1.0f;
    bool open = true;
    int holdLeft = 0;
};

// ---------------------------------------------------------------- Catálogo
namespace BuiltInEffects
{
    inline juce::StringArray names()
    {
        return { "Delay", "Reverb", "Chorus", "Flanger", "Phaser", "Pitch", "Ecualizador", "Compresor", "Overdrive", "Ampli", "Tremolo", "Wah", "Puerta de ruido" };
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
            case 10: return std::make_unique<TremoloEffect>();
            case 11: return std::make_unique<WahEffect>();
            case 12: return std::make_unique<GateEffect>();
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
