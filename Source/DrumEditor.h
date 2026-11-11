#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Text.h"
#include "Theme.h"
#include "AudioEngine.h"
#include "MixerComponent.h"

namespace DrumColours
{
    inline juce::Colour forPattern (int p)
    {
        static const juce::uint32 c[] = { 0xff5b9bd5, 0xffed9a4c, 0xffe06c9f, 0xff6cc48a };
        return juce::Colour (c[juce::jlimit (0, 3, p)]);
    }
    inline juce::String letter (int p) { return juce::String::charToString ((juce::juce_wchar) ('A' + p)); }
}

// Rejilla de pasos: filas = instrumentos, columnas = semicorcheas del compás.
// Clic: nada -> normal -> acento -> nada.  Alt+clic: suave.  Arrastrar: pinta el mismo valor.
class DrumGrid : public juce::Component, private juce::Timer
{
public:
    DrumGrid (Track& t, AudioEngine& e, MixerHost& h) : track (t), engine (e), host (h) { startTimerHz (30); }

    std::function<void()> onEdited;

    int stepsPerBar() const
    {
        return juce::jlimit (1, Drums::maxSteps, engine.sigNum.load() * 16 / juce::jmax (1, engine.sigDen.load()));
    }

    void paint (juce::Graphics& g) override
    {
        auto& d = *track.drum;
        const auto& pat = d.patterns[(size_t) d.currentPattern];
        const int steps = stepsPerBar();
        const int stepsPerBeat = juce::jmax (1, 16 / juce::jmax (1, engine.sigDen.load()));
        const auto colour = DrumColours::forPattern (d.currentPattern);
        g.fillAll (Theme::panelDark);

        for (int inst = 0; inst < Drums::numInstruments; ++inst)
        {
            for (int s = 0; s < steps; ++s)
            {
                auto cell = cellBounds (inst, s).toFloat().reduced (1.5f);
                const bool beatStart = s % stepsPerBeat == 0;
                const auto v = pat.steps[(size_t) inst][(size_t) s];
                const bool beatShade = (s / stepsPerBeat) % 2 == 0;
                Theme::fillInset (g, cell, beatShade ? juce::Colour (0xff2a2c31) : juce::Colour (0xff24262a), 3.0f);
                if (v > 0)
                {
                    const float alpha = v == 1 ? 0.45f : v == 2 ? 0.75f : 1.0f;
                    Theme::fillRaised (g, cell.reduced (v == 1 ? 4.0f : 1.0f), colour.withAlpha (alpha), 3.0f);
                }
                if (s == playingStep)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.18f));
                    g.fillRoundedRectangle (cell, 3.0f);
                }
                if (beatStart && s > 0)
                {
                    g.setColour (Theme::border);
                    g.drawVerticalLine ((int) cell.getX() - 2, cell.getY(), cell.getBottom());
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        before = *track.drum;
        const auto hit = cellAt (e.getPosition());
        if (hit.first < 0)
            return;
        const auto current = track.drum->patterns[(size_t) track.drum->currentPattern].steps[(size_t) hit.first][(size_t) hit.second];
        paintValue = e.mods.isAltDown() ? (current == 1 ? 0 : 1)
                   : current == 0 ? 2 : current == 2 ? 3 : 0;
        setCell (hit.first, hit.second);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto hit = cellAt (e.getPosition());
        if (hit.first >= 0)
            setCell (hit.first, hit.second);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        commitUndo (track, engine, host, before, tr8 ("Editar patrón de batería"));
        if (onEdited) onEdited();
    }

    // Registra en el historial global un cambio de la batería (antes -> ahora)
    static void commitUndo (Track& t, AudioEngine& e, MixerHost& h, const Drums::DrumData& before, const juce::String& name)
    {
        const auto after = *t.drum;
        if (std::memcmp (&before.patterns, &after.patterns, sizeof (before.patterns)) == 0
             && std::memcmp (&before.arrangement, &after.arrangement, sizeof (before.arrangement)) == 0
             && before.levels == after.levels && before.swing == after.swing && before.humanize == after.humanize)
            return;
        Track* tp = &t;
        AudioEngine* ep = &e;
        auto set = [tp, ep] (const Drums::DrumData& v)
        {
            const juce::ScopedLock sl (ep->getLock());
            const int keep = tp->drum->currentPattern;   // el patrón que se está viendo no forma parte del deshacer
            *tp->drum = v;
            tp->drum->currentPattern = keep;
        };
        h.recordUndo (name, [set, before] { set (before); }, [set, after] { set (after); });
    }

private:
    juce::Rectangle<int> cellBounds (int inst, int step) const
    {
        const int steps = stepsPerBar();
        const float w = (float) getWidth() / (float) steps, h = (float) getHeight() / (float) Drums::numInstruments;
        return juce::Rectangle<float> (step * w, inst * h, w, h).toNearestInt();
    }

    std::pair<int, int> cellAt (juce::Point<int> p) const
    {
        if (! getLocalBounds().contains (p))
            return { -1, -1 };
        const int steps = stepsPerBar();
        const int s = juce::jlimit (0, steps - 1, p.x * steps / juce::jmax (1, getWidth()));
        const int i = juce::jlimit (0, Drums::numInstruments - 1, p.y * Drums::numInstruments / juce::jmax (1, getHeight()));
        return { i, s };
    }

    void setCell (int inst, int step)
    {
        auto& v = track.drum->patterns[(size_t) track.drum->currentPattern].steps[(size_t) inst][(size_t) step];
        if (v == paintValue)
            return;
        {
            const juce::ScopedLock sl (engine.getLock());
            v = paintValue;
        }
        if (paintValue > 0)
            track.drumPreview = inst;   // se oye al marcarlo
        repaint();
    }

    void timerCallback() override
    {
        // Paso que está sonando (si el compás actual usa este patrón)
        int step = -1;
        if (engine.getState() != AudioEngine::stopped && engine.getPosition() >= 0)
        {
            const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
            const double sixteenth = sr * 60.0 / engine.bpm.load() / 4.0;
            const auto g = (juce::int64) std::floor ((double) engine.getPosition() / sixteenth);
            const int steps = stepsPerBar();
            const auto bar = g / steps;
            if (bar < Drums::maxBars && track.drum->arrangement[(size_t) bar] == track.drum->currentPattern)
                step = (int) (g % steps);
        }
        if (step != playingStep) { playingStep = step; repaint(); }
    }

    Track& track;
    AudioEngine& engine;
    MixerHost& host;
    Drums::DrumData before;
    uint8_t paintValue = 2;
    int playingStep = -1;
};

// Ventana del editor de batería: patrones A-D, estilos, swing, volúmenes y rejilla
class DrumEditorComponent : public juce::Component
{
public:
    DrumEditorComponent (Track& t, AudioEngine& e, MixerHost& h) : track (t), engine (e), host (h), grid (t, e, h)
    {
        for (int p = 0; p < Drums::numPatterns; ++p)
        {
            auto* b = patternButtons.add (new juce::TextButton (tr8 ("Patrón ") + DrumColours::letter (p)));
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, DrumColours::forPattern (p));
            b->onClick = [this, p] { track.drum->currentPattern = p; refresh(); };
            b->setTooltip (p == 0 ? tr8 ("A: el ritmo principal") : p == 1 ? tr8 ("B: estribillo")
                                  : p == 2 ? tr8 ("C: redoble / relleno") : tr8 ("D: libre"));
            addAndMakeVisible (b);
        }

        for (auto& st : Drums::styles())
            styleBox.addItem (tr8 (st.name), styleBox.getNumItems() + 1);
        styleBox.setSelectedId (defaultStyle() + 1, juce::dontSendNotification);
        addAndMakeVisible (styleBox);
        loadStyleButton.setTooltip (tr8 ("Sustituye los patrones A, B y C por el estilo elegido"));
        loadStyleButton.onClick = [this]
        {
            const auto previous = *track.drum;
            {
                const juce::ScopedLock sl (engine.getLock());
                Drums::applyStyle (*track.drum, styleBox.getSelectedId() - 1, grid.stepsPerBar());
            }
            DrumGrid::commitUndo (track, engine, host, previous, tr8 ("Cargar estilo de batería"));
            refresh();
        };
        addAndMakeVisible (loadStyleButton);

        clearButton.onClick = [this]
        {
            const auto previous = *track.drum;
            {
                const juce::ScopedLock sl (engine.getLock());
                track.drum->patterns[(size_t) track.drum->currentPattern].clear();
            }
            DrumGrid::commitUndo (track, engine, host, previous, tr8 ("Vaciar patrón"));
            refresh();
        };
        addAndMakeVisible (clearButton);

        copyButton.onClick = [this]
        {
            juce::PopupMenu m;
            for (int p = 0; p < Drums::numPatterns; ++p)
                if (p != track.drum->currentPattern)
                    m.addItem (p + 1, tr8 ("Copiar en el patrón ") + DrumColours::letter (p));
            juce::Component::SafePointer<DrumEditorComponent> self (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&copyButton), [self] (int r)
            {
                if (self == nullptr || r == 0) return;
                auto& d = *self->track.drum;
                const auto before = d;
                {
                    const juce::ScopedLock sl (self->engine.getLock());
                    d.patterns[(size_t) r - 1] = d.patterns[(size_t) d.currentPattern];
                }
                DrumGrid::commitUndo (self->track, self->engine, self->host, before, tr8 ("Copiar patrón"));
            });
        };
        addAndMakeVisible (copyButton);

        swing.setRange (0.0, 100.0, 1.0);
        swing.setTextValueSuffix (" %");
        swing.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
        swing.onDragStart = [this] { before = *track.drum; };
        swing.onValueChange = [this] { track.drum->swing = (float) swing.getValue() / 100.0f; };
        swing.onDragEnd = [this] { DrumGrid::commitUndo (track, engine, host, before, "Swing"); };
        swing.setTooltip (tr8 ("Swing: retrasa las semicorcheas a contratiempo (100 % = tresillo, como un shuffle)"));
        addAndMakeVisible (swing);

        humanize.setButtonText ("Humanizar");
        humanize.setTooltip (tr8 ("Varía un poco la fuerza de cada golpe para que suene menos mecánico"));
        humanize.onClick = [this]
        {
            const auto previous = *track.drum;
            track.drum->humanize = humanize.getToggleState();
            DrumGrid::commitUndo (track, engine, host, previous, "Humanizar");
        };
        addAndMakeVisible (humanize);

        for (int i = 0; i < Drums::numInstruments; ++i)
        {
            auto* lvl = levels.add (new juce::Slider());
            lvl->setSliderStyle (juce::Slider::LinearHorizontal);
            lvl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            lvl->setRange (0.0, 1.0, 0.01);
            lvl->setDoubleClickReturnValue (true, 0.8);
            lvl->setTooltip (tr8 ("Volumen de ") + tr8 (Drums::instrumentName (i)) + tr8 (" (clic en el nombre: escucharlo)"));
            lvl->onDragStart = [this] { before = *track.drum; };
            lvl->onValueChange = [this, i, lvl] { track.drum->levels[(size_t) i] = (float) lvl->getValue(); };
            lvl->onDragEnd = [this] { DrumGrid::commitUndo (track, engine, host, before, tr8 ("Volumen de batería")); };
            addAndMakeVisible (lvl);

            auto* nameButton = names.add (new juce::TextButton (tr8 (Drums::instrumentName (i))));
            nameButton->onClick = [this, i] { track.drumPreview = i; };
            addAndMakeVisible (nameButton);
        }

        grid.onEdited = [this] { refresh(); };
        addAndMakeVisible (grid);

        hint.setText (tr8 ("Clic: normal → acento → nada · Alt+clic: suave · arrastra para pintar. "
                           "En la pista, clic en un compás para ponerle el patrón elegido (doble clic: este editor)."),
                      juce::dontSendNotification);
        hint.setFont (juce::FontOptions (11.0f));
        hint.setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (hint);

        refresh();
        setSize (860, 420);
    }

    void refresh()
    {
        auto& d = *track.drum;
        for (int p = 0; p < patternButtons.size(); ++p)
            patternButtons[p]->setToggleState (p == d.currentPattern, juce::dontSendNotification);
        swing.setValue (d.swing * 100.0, juce::dontSendNotification);
        humanize.setToggleState (d.humanize, juce::dontSendNotification);
        for (int i = 0; i < levels.size(); ++i)
            levels[i]->setValue (d.levels[(size_t) i], juce::dontSendNotification);
        grid.repaint();
        if (auto* top = getTopLevelComponent())
            top->repaint();
    }

    void paint (juce::Graphics& g) override { g.fillAll (Theme::panel); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto top = r.removeFromTop (28);
        for (auto* b : patternButtons) { b->setBounds (top.removeFromLeft (92)); top.removeFromLeft (4); }
        top.removeFromLeft (8);
        copyButton.setBounds (top.removeFromLeft (70)); top.removeFromLeft (4);
        clearButton.setBounds (top.removeFromLeft (70));

        auto second = (r.removeFromTop (6), r.removeFromTop (26));
        styleBox.setBounds (second.removeFromLeft (150)); second.removeFromLeft (4);
        loadStyleButton.setBounds (second.removeFromLeft (110)); second.removeFromLeft (20);
        humanize.setBounds (second.removeFromRight (110)); second.removeFromRight (10);
        swing.setBounds (second.removeFromRight (220));

        hint.setBounds (r.removeFromBottom (20));
        r.removeFromBottom (4);
        r.removeFromTop (10);
        auto labels = r.removeFromLeft (200);
        const float h = (float) labels.getHeight() / Drums::numInstruments;
        for (int i = 0; i < Drums::numInstruments; ++i)
        {
            auto row = juce::Rectangle<float> ((float) labels.getX(), labels.getY() + i * h, (float) labels.getWidth(), h).toNearestInt().reduced (0, 2);
            names[i]->setBounds (row.removeFromLeft (118));
            row.removeFromLeft (4);
            levels[i]->setBounds (row.reduced (0, 4));
        }
        r.removeFromLeft (8);
        grid.setBounds (r);
    }

private:
    int defaultStyle() const
    {
        const int num = engine.sigNum.load(), den = engine.sigDen.load();
        return num == 3 && den == 4 ? 7 : (den == 8 && num % 3 == 0) ? 8 : 0;
    }

    Track& track;
    AudioEngine& engine;
    MixerHost& host;
    DrumGrid grid;
    Drums::DrumData before;
    juce::OwnedArray<juce::TextButton> patternButtons, names;
    juce::OwnedArray<juce::Slider> levels;
    juce::ComboBox styleBox;
    juce::TextButton loadStyleButton { "Cargar estilo" }, clearButton { "Vaciar" }, copyButton { "Copiar..." };
    juce::Slider swing { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton humanize;
    juce::Label hint;
};

class DrumEditorWindow : public juce::DocumentWindow
{
public:
    DrumEditorWindow (Track& t, AudioEngine& e, MixerHost& h)
        : juce::DocumentWindow (tr8 ("Batería · ") + t.name, Theme::panel, juce::DocumentWindow::closeButton), track (&t)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new DrumEditorComponent (t, e, h), true);
        setResizable (true, false);
        setResizeLimits (720, 360, 2000, 900);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    Track* getTrack() const { return track; }
    void refresh() { if (auto* c = dynamic_cast<DrumEditorComponent*> (getContentComponent())) c->refresh(); }
    void closeButtonPressed() override { setVisible (false); }

private:
    Track* track;
};
