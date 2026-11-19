#pragma once
// Editor gráfico del EQ de canal (ChannelEqEffect, Effects.h), al estilo del Channel EQ de Logic:
// curva de respuesta, un punto por banda que se arrastra (frecuencia y ganancia; rueda = Q;
// doble clic = activar o desactivar la banda) y debajo los valores de cada banda.
#include "Theme.h"

class ChannelEqEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ChannelEqEditor (ChannelEqEffect& p) : juce::AudioProcessorEditor (p), eq (p)
    {
        powerButton.setClickingTogglesState (true);
        powerButton.setTooltip ("Activar o desactivar el EQ");
        powerButton.onClick = [this] { setParam (eq.enabledParam(), powerButton.getToggleState() ? 1.0f : 0.0f); };
        addAndMakeVisible (powerButton);

        resetButton.setTooltip ("Poner todas las bandas a 0 dB con sus frecuencias de partida");
        resetButton.onClick = [this] { reset(); };
        addAndMakeVisible (resetButton);

        for (int b = 0; b < ChannelEqEffect::numBands; ++b)
        {
            auto& band = eq.band (b);
            auto* col = columns.add (new BandColumn (*this, b));
            col->onButton.onClick = [this, b] { auto& on = *eq.band (b).on; setParam (on, on.get() ? 0.0f : 1.0f); };
            col->freq.setup (*band.freq, [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; });
            col->gain.setup (*band.gain, [] (float v) { return juce::String (v, 1) + " dB"; });
            col->q.setup    (*band.q,    [] (float v) { return juce::String (v, 2); });
            const bool cut = ChannelEqEffect::kindOf (b) == ChannelEqEffect::lowCut || ChannelEqEffect::kindOf (b) == ChannelEqEffect::highCut;
            col->gain.setVisible (! cut);
            addAndMakeVisible (col);
        }

        outSlider.setSliderStyle (juce::Slider::LinearVertical);
        outSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        outSlider.setTooltip ("Ganancia de salida del EQ");
        outAttachment = std::make_unique<juce::SliderParameterAttachment> (eq.outputGain(), outSlider);
        outSlider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
        outSlider.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getDoubleValue(); };
        outSlider.updateText();
        outSlider.setDoubleClickReturnValue (true, 0.0);
        addAndMakeVisible (outSlider);

        setResizable (true, true);
        setResizeLimits (760, 380, 1600, 900);
        setSize (900, 440);
        startTimerHz (30);
    }

    ~ChannelEqEditor() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);

        // Cabecera
        g.setColour (Theme::strongText);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText ("EQ de canal", getLocalBounds().removeFromTop (headerH).withTrimmedLeft (110), juce::Justification::centredLeft);

        // Fondo y rejilla de la curva
        const auto r = graph.toFloat();
        g.setColour (Theme::screen);
        g.fillRoundedRectangle (r, 6.0f);

        g.setFont (juce::FontOptions (10.0f));
        for (float f : { 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 80.0f, 100.0f, 200.0f, 300.0f, 400.0f, 500.0f, 800.0f,
                         1000.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 20000.0f })
        {
            const float x = xForFreq (f);
            const bool major = f == 100.0f || f == 1000.0f || f == 10000.0f;
            g.setColour (Theme::screenText.withAlpha (major ? 0.18f : 0.08f));
            g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
            if (f == 20.0f || f == 50.0f || f == 100.0f || f == 200.0f || f == 500.0f || f == 1000.0f || f == 2000.0f
                || f == 5000.0f || f == 10000.0f || f == 20000.0f)
            {
                g.setColour (Theme::screenTextDim.withAlpha (0.8f));
                const auto label = f >= 1000.0f ? juce::String (juce::roundToInt (f / 1000.0f)) + "k" : juce::String (juce::roundToInt (f));
                const float lx = juce::jlimit (r.getX() + 2.0f, r.getRight() - 42.0f, x - 20.0f);
                g.drawText (label, juce::Rectangle<float> (lx, r.getBottom() - 15.0f, 40.0f, 13.0f),
                            f == 20.0f ? juce::Justification::centredLeft : f == 20000.0f ? juce::Justification::centredRight : juce::Justification::centred);
            }
        }
        for (int db = -maxDb; db <= maxDb; db += 6)
        {
            const float y = yForGain ((float) db);
            g.setColour (Theme::screenText.withAlpha (db == 0 ? 0.30f : 0.08f));
            g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
            if (db != -maxDb && db != maxDb)
            {
                g.setColour (Theme::screenTextDim.withAlpha (0.8f));
                g.drawText (juce::String (db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (r.getX() + 4.0f, y - 7.0f, 28.0f, 14.0f),
                            juce::Justification::centredLeft);
            }
        }

        // Curva total
        const bool on = eq.isEnabled();
        juce::Path curve;
        const int steps = juce::jmax (2, graph.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
            const float y = yForGain (totalDbAt (freqForX (x)));
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        const auto accent = on ? Theme::accent : Theme::screenTextDim;
        {
            juce::Path fill (curve);
            const float zero = yForGain (0.0f);
            fill.lineTo (r.getRight(), zero);
            fill.lineTo (r.getX(), zero);
            fill.closeSubPath();
            g.setColour (accent.withAlpha (0.16f));
            juce::Graphics::ScopedSaveState st (g);
            g.reduceClipRegion (graph);
            g.fillPath (fill);
        }
        {
            juce::Graphics::ScopedSaveState st (g);
            g.reduceClipRegion (graph);
            // Curva de la banda que se toca
            if (hotBand >= 0 && eq.band (hotBand).on->get())
            {
                juce::Path one;
                for (int i = 0; i <= steps; ++i)
                {
                    const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
                    const float y = yForGain (bandDbAt (hotBand, freqForX (x)));
                    if (i == 0) one.startNewSubPath (x, y); else one.lineTo (x, y);
                }
                g.setColour (bandColour (hotBand).withAlpha (0.7f));
                g.strokePath (one, juce::PathStrokeType (1.2f));
            }
            g.setColour (accent);
            g.strokePath (curve, juce::PathStrokeType (2.0f));
        }

        // Puntos de las bandas
        for (int b = 0; b < ChannelEqEffect::numBands; ++b)
        {
            const auto p = nodePosition (b);
            const auto c = bandColour (b);
            const bool bandOn = eq.band (b).on->get();
            const float rad = b == hotBand ? 8.0f : 6.5f;
            g.setColour (bandOn ? c : c.withAlpha (0.35f));
            if (bandOn) g.fillEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (p));
            g.setColour (bandOn ? Theme::screen : c.withAlpha (0.6f));
            g.drawEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (p), 1.5f);
            g.setColour (bandOn ? Theme::screen : c.withAlpha (0.7f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (rad * 2, rad * 2).withCentre (p), juce::Justification::centred);
        }

        if (! on)
        {
            g.setColour (Theme::screenTextDim);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("EQ desactivado", graph.reduced (10).removeFromTop (20), juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 0);
        auto header = area.removeFromTop (headerH);
        powerButton.setBounds (header.removeFromLeft (90).withSizeKeepingCentre (90, 26));
        resetButton.setBounds (header.removeFromRight (110).withSizeKeepingCentre (110, 26));

        area.removeFromBottom (10);
        auto bottom = area.removeFromBottom (78);
        auto right = area.removeFromRight (80);
        outSlider.setBounds (right.reduced (6, 0));
        graph = area.reduced (0, 4);

        bottom.removeFromRight (80);
        const int w = bottom.getWidth() / ChannelEqEffect::numBands;
        for (int b = 0; b < columns.size(); ++b)
            columns[b]->setBounds (bottom.getX() + b * w, bottom.getY(), w, bottom.getHeight());
    }

    // --- Ratón sobre la curva ---
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int b = bandAt (e.position);
        if (b != hotBand) { hotBand = b; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (dragBand < 0 && hotBand >= 0) { hotBand = -1; repaint(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragBand = bandAt (e.position);
        if (dragBand < 0)
            return;
        auto& band = eq.band (dragBand);
        band.freq->beginChangeGesture();
        band.gain->beginChangeGesture();
        if (! band.on->get())
            setParam (*band.on, 1.0f);   // arrastrar una banda apagada la enciende
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand < 0)
            return;
        auto& band = eq.band (dragBand);
        const auto p = e.position;
        const float f = juce::jlimit (20.0f, 20000.0f, freqForX (juce::jlimit ((float) graph.getX(), (float) graph.getRight(), p.x)));
        band.freq->setValueNotifyingHost (band.freq->convertTo0to1 (f));
        if (hasGain (dragBand))
        {
            const float g = juce::jlimit (-(float) maxDb, (float) maxDb, gainForY (p.y));
            band.gain->setValueNotifyingHost (band.gain->convertTo0to1 (e.mods.isShiftDown() ? 0.0f : g));
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragBand >= 0)
        {
            eq.band (dragBand).freq->endChangeGesture();
            eq.band (dragBand).gain->endChangeGesture();
        }
        dragBand = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int b = bandAt (e.position);
        if (b >= 0)
        {
            auto& on = *eq.band (b).on;
            setParam (on, on.get() ? 0.0f : 1.0f);
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int b = bandAt (e.position);
        if (b < 0)
            return;
        auto& q = *eq.band (b).q;
        const float factor = std::pow (1.15f, w.deltaY * (w.isReversed ? -1.0f : 1.0f) * 4.0f);
        setParam (q, q.convertTo0to1 (juce::jlimit (0.1f, 10.0f, q.get() * factor)));
    }

private:
    static constexpr int headerH = 44;
    static constexpr int maxDb = 24;

    // Un valor que se cambia arrastrando arriba/abajo (doble clic: escribirlo)
    struct DragValue : public juce::Label
    {
        void setup (juce::AudioParameterFloat& p, std::function<juce::String (float)> fmt)
        {
            param = &p;
            format = std::move (fmt);
            setJustificationType (juce::Justification::centred);
            setFont (juce::FontOptions (11.0f));
            setEditable (false, true);
            setColour (juce::Label::textColourId, Theme::text);
            onTextChange = [this]
            {
                const float v = getText().retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getFloatValue()
                                * (getText().containsIgnoreCase ("k") ? 1000.0f : 1.0f);
                param->setValueNotifyingHost (param->convertTo0to1 (juce::jlimit (param->range.start, param->range.end, v)));
                update();
            };
            update();
        }
        void update() { if (param != nullptr && ! isBeingEdited()) setText (format (param->get()), juce::dontSendNotification); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            juce::Label::mouseDown (e);
            startValue = param->convertTo0to1 (param->get());
            param->beginChangeGesture();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const float v = juce::jlimit (0.0f, 1.0f, startValue - (float) e.getDistanceFromDragStartY() * 0.004f);
            param->setValueNotifyingHost (v);
            update();
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            juce::Label::mouseUp (e);
            param->endChangeGesture();
        }
        juce::AudioParameterFloat* param = nullptr;
        std::function<juce::String (float)> format;
        float startValue = 0.0f;
    };

    struct BandColumn : public juce::Component
    {
        BandColumn (ChannelEqEditor& e, int b) : owner (e), band (b)
        {
            onButton.setTooltip ("Activar o desactivar la banda " + juce::String (b + 1));
            for (auto* c : std::initializer_list<juce::Component*> { &onButton, &freq, &gain, &q })
                addAndMakeVisible (c);
            onButton.setAlpha (0.0f);   // botón invisible: el dibujo del icono va encima
        }
        void paint (juce::Graphics& g) override
        {
            auto r = onButton.getBounds().toFloat();
            const bool on = owner.eq.band (band).on->get();
            const auto c = owner.bandColour (band);
            g.setColour (on ? c.withAlpha (0.25f) : Theme::panelDark);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? c : Theme::border);
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            // Icono de la forma del filtro
            juce::Path p;
            auto ic = r.reduced (r.getWidth() * 0.3f, 6.0f);
            const auto k = ChannelEqEffect::kindOf (band);
            const float x0 = ic.getX(), x1 = ic.getRight(), y0 = ic.getY(), y1 = ic.getBottom(), ym = ic.getCentreY();
            if (k == ChannelEqEffect::lowCut)        { p.startNewSubPath (x0, y1); p.quadraticTo (x0 + ic.getWidth() * 0.3f, y0, x1, y0); }
            else if (k == ChannelEqEffect::highCut)  { p.startNewSubPath (x0, y0); p.quadraticTo (x1 - ic.getWidth() * 0.3f, y0, x1, y1); }
            else if (k == ChannelEqEffect::lowShelf) { p.startNewSubPath (x0, y0); p.lineTo (ic.getCentreX() - 3, y0); p.lineTo (ic.getCentreX() + 3, ym); p.lineTo (x1, ym); }
            else if (k == ChannelEqEffect::highShelf){ p.startNewSubPath (x0, ym); p.lineTo (ic.getCentreX() - 3, ym); p.lineTo (ic.getCentreX() + 3, y0); p.lineTo (x1, y0); }
            else { p.startNewSubPath (x0, ym); p.lineTo (ic.getCentreX() - 5, ym); p.quadraticTo (ic.getCentreX(), y0 - 2, ic.getCentreX() + 5, ym); p.lineTo (x1, ym); }
            g.setColour (on ? Theme::strongText : Theme::textDim);
            g.strokePath (p, juce::PathStrokeType (1.4f));
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 0);
            onButton.setBounds (r.removeFromTop (22));
            r.removeFromTop (2);
            const int h = r.getHeight() / 3;
            freq.setBounds (r.removeFromTop (h));
            gain.setBounds (r.removeFromTop (h));
            q.setBounds (r);
        }
        ChannelEqEditor& owner;
        int band;
        juce::TextButton onButton;
        DragValue freq, gain, q;
    };

    void timerCallback() override
    {
        powerButton.setToggleState (eq.isEnabled(), juce::dontSendNotification);
        powerButton.setButtonText (eq.isEnabled() ? "Activo" : "Apagado");
        for (auto* c : columns)
        {
            c->freq.update(); c->gain.update(); c->q.update();
            c->repaint();
        }
        // Se repinta solo si algo cambió
        juce::String key;
        for (int b = 0; b < ChannelEqEffect::numBands; ++b)
        {
            auto& band = eq.band (b);
            key << (int) band.on->get() << band.freq->get() << band.gain->get() << band.q->get();
        }
        key << eq.outputGain().get() << (int) eq.isEnabled();
        if (key != lastKey) { lastKey = key; repaint(); }
    }

    static void setParam (juce::RangedAudioParameter& p, float normalised)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (normalised);
        p.endChangeGesture();
    }

    void reset()
    {
        ChannelEqEffect fresh;
        for (int i = 0; i < eq.getParameters().size() && i < fresh.getParameters().size(); ++i)
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (eq.getParameters()[i]))
                setParam (*p, fresh.getParameters()[i]->getValue());
    }

    static bool hasGain (int b)
    {
        const auto k = ChannelEqEffect::kindOf (b);
        return k != ChannelEqEffect::lowCut && k != ChannelEqEffect::highCut;
    }

    juce::Colour bandColour (int b) const
    {
        static const juce::uint32 c[] = { 0xffe5484d, 0xffe89a3c, 0xffe8c547, 0xff3fc46b, 0xff2fc7c9, 0xff4ea1ff, 0xff9b7bff, 0xffe86fb8 };
        return juce::Colour (c[juce::jlimit (0, 7, b)]);
    }

    float xForFreq (float f) const
    {
        const float t = std::log (f / 20.0f) / std::log (1000.0f);
        return (float) graph.getX() + t * (float) graph.getWidth();
    }
    float freqForX (float x) const
    {
        const float t = (x - (float) graph.getX()) / (float) juce::jmax (1, graph.getWidth());
        return 20.0f * std::pow (1000.0f, t);
    }
    float yForGain (float db) const
    {
        return (float) graph.getCentreY() - db / (float) maxDb * (float) graph.getHeight() * 0.5f * 0.92f;
    }
    float gainForY (float y) const
    {
        return ((float) graph.getCentreY() - y) / ((float) graph.getHeight() * 0.5f * 0.92f) * (float) maxDb;
    }

    float bandDbAt (int b, float f) const { return eq.bandResponseDb (b, f); }
    float totalDbAt (float f) const       { return eq.responseDb (f); }

    juce::Point<float> nodePosition (int b) const
    {
        auto& band = eq.band (b);
        const float x = xForFreq (band.freq->get());
        // Los cortes se dibujan sobre la curva a su frecuencia; el resto, a su ganancia
        const float y = hasGain (b) ? yForGain (band.gain->get()) : yForGain (juce::jlimit (-(float) maxDb, (float) maxDb, bandDbAt (b, band.freq->get())));
        return { x, y };
    }

    int bandAt (juce::Point<float> p) const
    {
        int best = -1;
        float bestD = 14.0f;
        for (int b = 0; b < ChannelEqEffect::numBands; ++b)
        {
            const float d = nodePosition (b).getDistanceFrom (p);
            if (d < bestD) { bestD = d; best = b; }
        }
        return best;
    }

    ChannelEqEffect& eq;
    juce::TextButton powerButton { "Activo" }, resetButton { "Restablecer" };
    juce::OwnedArray<BandColumn> columns;
    juce::Slider outSlider;
    std::unique_ptr<juce::SliderParameterAttachment> outAttachment;
    juce::Rectangle<int> graph;
    int hotBand = -1, dragBand = -1;
    juce::String lastKey;
};

inline juce::AudioProcessorEditor* ChannelEqEffect::createEditor() { return new ChannelEqEditor (*this); }
