#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Text.h"
#include "Theme.h"
#include "AudioEngine.h"

// ============================================================================
// Detección de tono (algoritmo YIN) — suficiente para guitarra y bajo (de ~28 Hz a ~1 kHz)
// ============================================================================
class PitchDetector
{
public:
    // Devuelve la frecuencia en Hz, o 0 si no hay una nota clara
    static float detect (const float* x, int n, double sampleRate)
    {
        const int tauMin = juce::jmax (2, (int) (sampleRate / 1100.0));
        const int tauMax = juce::jmin (n / 2, (int) (sampleRate / 27.0));
        const int window = n - tauMax;
        if (tauMax <= tauMin + 2 || window < 64)
            return 0.0f;

        std::vector<float> d ((size_t) tauMax + 1, 0.0f);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < window; ++j)
            {
                const float diff = x[j] - x[j + tau];
                sum += diff * diff;
            }
            d[(size_t) tau] = sum;
        }

        // Diferencia normalizada acumulada
        d[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += d[(size_t) tau];
            d[(size_t) tau] = running > 0.0f ? d[(size_t) tau] * (float) tau / running : 1.0f;
        }

        // Primer mínimo por debajo del umbral
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (d[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
            return 0.0f;

        // Interpolación parabólica para afinar la medida
        float exact = (float) best;
        if (best > 1 && best < tauMax)
        {
            const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const float den = a + c - 2.0f * b;
            if (std::abs (den) > 1.0e-9f)
                exact += 0.5f * (a - c) / den;
        }
        return (float) (sampleRate / exact);
    }
};

// ============================================================================
// Afinador: entrada, modo (cromático / guitarra / bajo...), referencia del La y aguja
// ============================================================================
class TunerComponent : public juce::Component, private juce::Timer
{
public:
    TunerComponent (AudioEngine& e, std::function<juce::StringArray()> inputNames)
        : engine (e), getInputNames (std::move (inputNames))
    {
        modeBox.addItemList ({ tr8 ("Cromático"), tr8 ("Guitarra (estándar)"), tr8 ("Guitarra (Drop D)"),
                               tr8 ("Guitarra (medio tono abajo)"), tr8 ("Bajo 4 cuerdas"), tr8 ("Bajo 5 cuerdas") }, 1);
        modeBox.setSelectedId (2, juce::dontSendNotification);
        modeBox.onChange = [this] { repaint(); };
        addAndMakeVisible (modeBox);

        inputBox.onChange = [this] { engine.tunerInput = isShowing() ? inputBox.getSelectedId() - 1 : -1; };
        addAndMakeVisible (inputBox);

        refSlider.setRange (430.0, 450.0, 1.0);
        refSlider.setValue (440.0, juce::dontSendNotification);
        refSlider.setSliderStyle (juce::Slider::IncDecButtons);
        refSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 22);
        refSlider.setTextValueSuffix (" Hz");
        refSlider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (refSlider);

        muteToggle.setButtonText (tr8 ("Silenciar la salida mientras afino"));
        muteToggle.setToggleState (true, juce::dontSendNotification);
        muteToggle.onClick = [this] { engine.muteForTuner = isShowing() && muteToggle.getToggleState(); };
        addAndMakeVisible (muteToggle);

        for (auto* l : { &inputLabel, &modeLabel, &refLabel })
        {
            l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, Theme::textDim);
            addAndMakeVisible (l);
        }

        refreshInputs();
        setSize (440, 400);
    }

    ~TunerComponent() override { deactivate(); }

    void setPreferredInput (int channel) { preferredInput = channel; refreshInputs(); }

    void refreshInputs()
    {
        const auto names = getInputNames();
        const int current = inputBox.getSelectedId();
        inputBox.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            inputBox.addItem (names[i], i + 1);

        int want = current > 0 ? current : preferredInput + 1;
        if (want < 1 || want > names.size())
            want = names.isEmpty() ? 0 : 1;
        inputBox.setSelectedId (want, juce::dontSendNotification);
        if (isShowing())
            engine.tunerInput = want - 1;
    }

    void activate()
    {
        engine.tunerInput = inputBox.getSelectedId() - 1;
        engine.muteForTuner = muteToggle.getToggleState();
        analysis.clear();
        startTimerHz (25);
    }

    void deactivate()
    {
        stopTimer();
        engine.tunerInput = -1;
        engine.muteForTuner = false;
        frequency = 0.0f;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);

        auto meter = meterArea.toFloat();
        Theme::fillInset (g, meter, Theme::screen, 10.0f);

        const bool hasNote = frequency > 0.0f;
        const auto target = hasNote ? targetFor (frequency) : Target {};
        const bool inTune = hasNote && std::abs (target.cents) < 3.0f;
        const auto noteColour = ! hasNote ? Theme::textDim : inTune ? Theme::playGreen : Theme::text;

        // Escala de -50 a +50 cents en un arco
        const auto centre = juce::Point<float> (meter.getCentreX(), meter.getBottom() - 34.0f);
        const float radius = juce::jmin (meter.getWidth() * 0.42f, meter.getHeight() - 70.0f);
        const float span = juce::MathConstants<float>::pi * 0.36f;   // ±65°
        auto angleFor = [span] (float cents) { return juce::jlimit (-1.0f, 1.0f, cents / 50.0f) * span; };

        juce::Path zone;   // zona "afinado" (±3 cents)
        zone.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, angleFor (-3.0f), angleFor (3.0f), true);
        g.setColour (Theme::playGreen.withAlpha (0.35f));
        g.strokePath (zone, juce::PathStrokeType (10.0f));

        for (int c = -50; c <= 50; c += 5)
        {
            const float a = angleFor ((float) c);
            const float len = c % 25 == 0 ? 14.0f : (c % 10 == 0 ? 9.0f : 5.0f);
            const auto p1 = centre.getPointOnCircumference (radius - 6.0f, a);
            const auto p2 = centre.getPointOnCircumference (radius - 6.0f - len, a);
            g.setColour (c == 0 ? Theme::text : Theme::textDim.withAlpha (0.7f));
            g.drawLine ({ p1, p2 }, c == 0 ? 2.0f : 1.0f);
        }
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        for (int c : { -50, -25, 25, 50 })
        {
            const auto p = centre.getPointOnCircumference (radius + 10.0f, angleFor ((float) c));
            g.drawText ((c > 0 ? "+" : "") + juce::String (c), juce::Rectangle<float> (40.0f, 14.0f).withCentre (p),
                        juce::Justification::centred);
        }

        // Aguja
        if (hasNote)
        {
            const float a = angleFor (displayCents);
            const auto tip = centre.getPointOnCircumference (radius - 4.0f, a);
            g.setColour (inTune ? Theme::playGreen : Theme::soloYellow);
            g.drawLine ({ centre, tip }, 3.0f);
        }
        g.setColour (Theme::panelLight);
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (centre));

        // Nota
        auto noteArea = juce::Rectangle<float> (meter.getX(), centre.y - radius * 0.62f - 30.0f, meter.getWidth(), 64.0f);
        g.setColour (noteColour);
        g.setFont (juce::FontOptions (58.0f, juce::Font::bold));
        g.drawText (hasNote ? target.name : "-", noteArea, juce::Justification::centred);
        if (hasNote)
        {
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.setColour (noteColour.withAlpha (0.8f));
            g.drawText (target.solfege + "  " + juce::String (target.octave),
                        noteArea.translated (0.0f, 42.0f).withHeight (20.0f), juce::Justification::centred);
        }

        // Indicación y frecuencia
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        juce::String hint;
        if (! hasNote)              hint = tr8 ("Toca una cuerda...");
        else if (inTune)            hint = tr8 ("¡Afinada!");
        else if (target.cents < 0)  hint = tr8 ("Baja: tensa la cuerda  ") + juce::String (target.cents, 1) + " ct";
        else                        hint = tr8 ("Alta: afloja la cuerda  +") + juce::String (target.cents, 1) + " ct";
        g.setColour (! hasNote ? Theme::textDim : inTune ? Theme::playGreen : Theme::soloYellow);
        g.drawText (hint, meter.withTrimmedTop (meter.getHeight() - 30.0f), juce::Justification::centred);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (hasNote ? juce::String (frequency, 1) + " Hz" : juce::String(), meter.reduced (12.0f, 8.0f),
                    juce::Justification::topRight);

        // Cuerdas del modo elegido
        const auto strings = stringsForMode();
        if (! strings.empty())
        {
            auto row = stringsArea.toFloat();
            const float w = row.getWidth() / (float) strings.size();
            for (size_t i = 0; i < strings.size(); ++i)
            {
                auto cell = row.removeFromLeft (w).reduced (3.0f, 0.0f);
                const bool active = hasNote && target.midi == strings[i];
                Theme::fillRaised (g, cell, active ? (inTune ? Theme::playGreen : Theme::soloYellow) : Theme::panelLight, 5.0f);
                g.setColour (active ? juce::Colours::black : Theme::text);
                g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                g.drawText (noteName (strings[i]) + juce::String (strings[i] / 12 - 1), cell, juce::Justification::centred);
            }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        auto row = [&r] (int h) { auto a = r.removeFromTop (h); r.removeFromTop (6); return a; };

        auto r1 = row (24);
        inputLabel.setBounds (r1.removeFromLeft (64));
        inputBox.setBounds (r1);
        auto r2 = row (24);
        modeLabel.setBounds (r2.removeFromLeft (64));
        modeBox.setBounds (r2);

        auto bottom = r.removeFromBottom (24);
        refLabel.setBounds (bottom.removeFromLeft (64));
        refSlider.setBounds (bottom.removeFromLeft (130));
        bottom.removeFromLeft (10);
        muteToggle.setBounds (bottom);
        r.removeFromBottom (8);

        stringsArea = r.removeFromBottom (30);
        r.removeFromBottom (8);
        meterArea = r;
    }

private:
    struct Target
    {
        int midi = 0, octave = 0;
        float cents = 0.0f;
        juce::String name, solfege;
    };

    static juce::String noteName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((midi % 12) + 12) % 12];
    }

    static juce::String solfegeName (int midi)
    {
        static const char* names[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        return names[((midi % 12) + 12) % 12];
    }

    std::vector<int> stringsForMode() const
    {
        switch (modeBox.getSelectedId())
        {
            case 2: return { 40, 45, 50, 55, 59, 64 };   // E A D G B E
            case 3: return { 38, 45, 50, 55, 59, 64 };   // D A D G B E
            case 4: return { 39, 44, 49, 54, 58, 63 };   // Eb Ab Db Gb Bb Eb
            case 5: return { 28, 33, 38, 43 };           // E A D G
            case 6: return { 23, 28, 33, 38, 43 };       // B E A D G
            default: return {};
        }
    }

    Target targetFor (float freq) const
    {
        const float a4 = (float) refSlider.getValue();
        const float midiFloat = 69.0f + 12.0f * std::log2 (freq / a4);

        Target t;
        const auto strings = stringsForMode();
        if (strings.empty())
            t.midi = (int) std::lround (midiFloat);
        else
        {
            t.midi = strings.front();   // la cuerda más cercana
            for (int s : strings)
                if (std::abs (midiFloat - (float) s) < std::abs (midiFloat - (float) t.midi))
                    t.midi = s;
        }
        t.cents = (midiFloat - (float) t.midi) * 100.0f;
        t.name = noteName (t.midi);
        t.solfege = solfegeName (t.midi);
        t.octave = t.midi / 12 - 1;
        return t;
    }

    void timerCallback() override
    {
        // Acumular la entrada y analizar los últimos ~85 ms
        float temp[8192];
        int got;
        while ((got = engine.readTunerSamples (temp, 8192)) > 0)
            analysis.insert (analysis.end(), temp, temp + got);

        const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
        const int factor = juce::jmax (1, (int) std::lround (sr / 24000.0));   // se analiza a ~24 kHz
        const int needed = 2048 * factor;
        if ((int) analysis.size() > needed * 2)
            analysis.erase (analysis.begin(), analysis.end() - needed);
        if ((int) analysis.size() < needed)
            return;

        std::vector<float> x (2048);
        const float* src = analysis.data() + analysis.size() - (size_t) needed;
        float energy = 0.0f;
        for (int i = 0; i < 2048; ++i)
        {
            float acc = 0.0f;
            for (int k = 0; k < factor; ++k)
                acc += src[i * factor + k];
            x[(size_t) i] = acc / (float) factor;
            energy += x[(size_t) i] * x[(size_t) i];
        }

        const float rms = std::sqrt (energy / 2048.0f);
        float f = rms > 0.003f ? PitchDetector::detect (x.data(), 2048, sr / factor) : 0.0f;

        if (f > 0.0f)
        {
            // Suavizado: si la nota es la misma, se promedia; si cambia, se salta directamente
            if (frequency > 0.0f && std::abs (f / frequency - 1.0f) < 0.03f)
                frequency = frequency * 0.6f + f * 0.4f;
            else
                frequency = f;
            silentFrames = 0;
            const float targetCents = targetFor (frequency).cents;
            displayCents += (targetCents - displayCents) * 0.5f;
        }
        else if (++silentFrames > 12)   // medio segundo sin nota: se borra
            frequency = 0.0f;

        repaint();
    }

    AudioEngine& engine;
    std::function<juce::StringArray()> getInputNames;
    juce::ComboBox inputBox, modeBox;
    juce::Slider refSlider;
    juce::ToggleButton muteToggle;
    juce::Label inputLabel { {}, "Entrada" }, modeLabel { {}, "Modo" }, refLabel { {}, "La (A4)" };
    juce::Rectangle<int> meterArea, stringsArea;

    std::vector<float> analysis;
    float frequency = 0.0f, displayCents = 0.0f;
    int silentFrames = 0, preferredInput = 0;
};

// Ventana flotante del afinador
class TunerWindow : public juce::DocumentWindow
{
public:
    TunerWindow (AudioEngine& e, std::function<juce::StringArray()> inputNames)
        : juce::DocumentWindow ("Afinador", Theme::panel, juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        tuner = new TunerComponent (e, std::move (inputNames));
        setContentOwned (tuner, true);
        setAlwaysOnTop (true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
    }

    TunerComponent& getTuner() { return *tuner; }

    void show (int preferredInput)
    {
        tuner->setPreferredInput (preferredInput);
        setVisible (true);
        toFront (true);
        tuner->activate();
    }

    void closeButtonPressed() override
    {
        tuner->deactivate();
        setVisible (false);
    }

private:
    TunerComponent* tuner = nullptr;
};
