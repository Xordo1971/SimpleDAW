#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Text.h"
#include "AudioEngine.h"
#include "Theme.h"
#include "Timeline.h"
#include "MixerComponent.h"
#include "Transpose.h"

// Diálogo para elegir cuántos semitonos (y cents) transponer
class TransposeDialog : public juce::Component
{
public:
    TransposeDialog (bool onSelection, std::function<void (double)> onApply) : apply (std::move (onApply))
    {
        semis.setRange (-12.0, 12.0, 1.0);
        semis.setValue (0.0, juce::dontSendNotification);
        semis.setSliderStyle (juce::Slider::LinearHorizontal);
        semis.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
        semis.setDoubleClickReturnValue (true, 0.0);
        semis.onValueChange = [this] { repaint(); };
        addAndMakeVisible (semis);

        cents.setRange (-50.0, 50.0, 1.0);
        cents.setValue (0.0, juce::dontSendNotification);
        cents.setSliderStyle (juce::Slider::LinearHorizontal);
        cents.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
        cents.setDoubleClickReturnValue (true, 0.0);
        cents.onValueChange = [this] { repaint(); };
        addAndMakeVisible (cents);

        for (auto* l : { &semisLabel, &centsLabel })
        {
            l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, Theme::textDim);
            addAndMakeVisible (l);
        }

        scope = onSelection ? tr8 ("Se transpondrá la selección.") : tr8 ("Se transpondrá toda la región.");

        okButton.onClick = [this] { finish (true); };
        cancelButton.onClick = [this] { finish (false); };
        okButton.setColour (juce::TextButton::buttonColourId, Theme::accent.darker (0.3f));
        addAndMakeVisible (okButton);
        addAndMakeVisible (cancelButton);
        setSize (420, 200);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        const double total = semis.getValue() + cents.getValue() / 100.0;
        static const char* names[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        const int st = (int) semis.getValue();
        juce::String text = total == 0.0 ? tr8 ("Sin cambio de tono")
                                         : (total > 0 ? "+" : "") + juce::String (total, cents.getValue() != 0.0 ? 2 : 0)
                                             + tr8 (" semitonos  ·  p. ej. La → ") + names[((9 + st) % 12 + 12) % 12];
        g.drawText (text, summaryArea, juce::Justification::centredLeft);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (scope + tr8 (" La duración no cambia."), scopeArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        auto row1 = r.removeFromTop (26); r.removeFromTop (8);
        semisLabel.setBounds (row1.removeFromLeft (90)); semis.setBounds (row1);
        auto row2 = r.removeFromTop (26); r.removeFromTop (10);
        centsLabel.setBounds (row2.removeFromLeft (90)); cents.setBounds (row2);
        summaryArea = r.removeFromTop (20);
        scopeArea = r.removeFromTop (18);
        auto buttons = r.removeFromBottom (30);
        okButton.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (100));
    }

private:
    void finish (bool ok)
    {
        const double total = semis.getValue() + cents.getValue() / 100.0;
        auto callback = apply;
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
        if (ok && total != 0.0 && callback)
            juce::MessageManager::callAsync ([callback, total] { callback (total); });
    }

    std::function<void (double)> apply;
    juce::Slider semis, cents;
    juce::Label semisLabel { {}, "Semitonos" }, centsLabel { {}, tr8 ("Afinación (ct)") };
    juce::String scope;
    juce::Rectangle<int> summaryArea, scopeArea;
    juce::TextButton okButton { "Transponer" }, cancelButton { "Cancelar" };
};

// Diálogo de velocidad (sin cambiar el tono)
class SpeedDialog : public juce::Component
{
public:
    SpeedDialog (bool onSelection, double currentBpm, std::function<void (double, bool)> onApply)
        : apply (std::move (onApply)), bpm (currentBpm), selection (onSelection)
    {
        speed.setRange (50.0, 150.0, 1.0);
        speed.setValue (100.0, juce::dontSendNotification);
        speed.setTextValueSuffix (" %");
        speed.setSliderStyle (juce::Slider::LinearHorizontal);
        speed.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
        speed.setDoubleClickReturnValue (true, 100.0);
        speed.onValueChange = [this] { repaint(); };
        addAndMakeVisible (speed);

        speedLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        speedLabel.setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (speedLabel);

        for (int p : { 50, 60, 70, 75, 80, 90 })
        {
            auto* b = presets.add (new juce::TextButton (juce::String (p) + " %"));
            b->onClick = [this, p] { speed.setValue (p); };
            addAndMakeVisible (b);
        }

        tempoToggle.setButtonText (tr8 ("Ajustar también el tempo del proyecto (metrónomo y compases)"));
        tempoToggle.setToggleState (! selection, juce::dontSendNotification);
        tempoToggle.setVisible (! selection);
        addAndMakeVisible (tempoToggle);

        okButton.onClick = [this] { finish (true); };
        cancelButton.onClick = [this] { finish (false); };
        okButton.setColour (juce::TextButton::buttonColourId, Theme::accent.darker (0.3f));
        addAndMakeVisible (okButton);
        addAndMakeVisible (cancelButton);
        setSize (460, 230);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        const double v = speed.getValue() / 100.0;
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        juce::String t = v == 1.0 ? tr8 ("Velocidad original")
                                  : juce::String (v < 1.0 ? tr8 ("Más lento: ") : tr8 ("Más rápido: ")) + juce::String (v * 100.0, 0)
                                      + tr8 (" % · el tono no cambia");
        g.drawText (t, summaryArea, juce::Justification::centredLeft);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ((selection ? tr8 ("Se aplicará a la selección.") : tr8 ("Se aplicará a toda la región."))
                      + (selection ? juce::String() : "  " + juce::String (bpm, 1) + " BPM -> " + juce::String (bpm * v, 1) + " BPM"),
                    scopeArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        auto row = r.removeFromTop (26); r.removeFromTop (6);
        speedLabel.setBounds (row.removeFromLeft (80)); speed.setBounds (row);
        auto pr = r.removeFromTop (24); r.removeFromTop (10);
        const int w = pr.getWidth() / presets.size();
        for (auto* b : presets) b->setBounds (pr.removeFromLeft (w).reduced (2, 0));
        summaryArea = r.removeFromTop (20);
        scopeArea = r.removeFromTop (18);
        r.removeFromTop (4);
        tempoToggle.setBounds (r.removeFromTop (24));
        auto buttons = r.removeFromBottom (30);
        okButton.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (100));
    }

private:
    void finish (bool ok)
    {
        const double v = speed.getValue() / 100.0;
        const bool adjust = tempoToggle.isVisible() && tempoToggle.getToggleState();
        auto callback = apply;
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
        if (ok && v != 1.0 && callback)
            juce::MessageManager::callAsync ([callback, v, adjust] { callback (v, adjust); });
    }

    std::function<void (double, bool)> apply;
    double bpm;
    bool selection;
    juce::Slider speed;
    juce::Label speedLabel { {}, "Velocidad" };
    juce::OwnedArray<juce::TextButton> presets;
    juce::ToggleButton tempoToggle;
    juce::Rectangle<int> summaryArea, scopeArea;
    juce::TextButton okButton { "Aplicar" }, cancelButton { "Cancelar" };
};

// Procesado en segundo plano con barra de progreso (transponer, velocidad...). Se destruye solo.
// El tramo [a, b) se sustituye por lo procesado (que puede tener otra duración), con fundidos en las uniones.
class ProcessJob : public juce::ThreadWithProgressWindow
{
public:
    using Op = std::function<bool (const float* in, int n, std::vector<float>& out, const std::function<bool (double)>& progress)>;
    using Done = std::function<void (std::shared_ptr<Clip> result)>;

    ProcessJob (const juce::String& title, std::shared_ptr<Clip> sourceClip, int from, int to, Op operation, Done d)
        : juce::ThreadWithProgressWindow (title, true, true),
          source (std::move (sourceClip)), a (from), b (to), op (std::move (operation)), done (std::move (d)) {}

    void run() override
    {
        const int nch = source->getNumChannels();
        const int len = (int) source->getLength();
        const int n = b - a;
        std::vector<std::vector<float>> processed ((size_t) nch);

        for (int ch = 0; ch < nch; ++ch)
        {
            const bool ok = op (source->audio.getReadPointer (ch, a), n, processed[(size_t) ch],
                [this, ch, nch] (double p)
                {
                    setProgress ((ch + p) / nch);
                    return ! threadShouldExit();
                });
            if (! ok)
                return;   // cancelado
        }

        const int m = (int) processed[0].size();
        auto out = source->cloneEmpty (nch, a + m + (len - b));
        const int fade = juce::jmin (juce::jmin (n, m) / 4, 240);
        for (int ch = 0; ch < nch; ++ch)
        {
            const auto* orig = source->audio.getReadPointer (ch);
            auto& proc = processed[(size_t) ch];
            auto* d = out->audio.getWritePointer (ch);
            std::copy (orig, orig + a, d);
            std::copy (proc.begin(), proc.end(), d + a);
            std::copy (orig + b, orig + len, d + a + m);

            // Fundidos cortos en las uniones para que no haya clics
            for (int i = 0; i < fade; ++i)
            {
                const float w = (float) i / (float) fade;
                if (a > 0)    d[a + i] = orig[a + i] * (1.0f - w) + proc[(size_t) i] * w;
                if (b < len)  d[a + m - fade + i] = proc[(size_t) (m - fade + i)] * (1.0f - w) + orig[b - fade + i] * w;
            }
        }
        result = out;
    }

    void threadComplete (bool userPressedCancel) override
    {
        done (userPressedCancel ? nullptr : result);
        delete this;
    }

private:
    std::shared_ptr<Clip> source, result;
    int a, b;
    Op op;
    Done done;
};

class AudioEditor : public juce::Component, private juce::ScrollBar::Listener
{
public:
    AudioEditor (MixerHost& h, AudioEngine& e) : host (h), engine (e)
    {
        setWantsKeyboardFocus (true);

        auto add = [this] (juce::TextButton& b, const juce::String& tip, std::function<void()> fn)
        {
            b.setTooltip (tip);
            b.setWantsKeyboardFocus (false);
            b.onClick = std::move (fn);
            addAndMakeVisible (b);
        };

        const juce::String cmd = (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0 ? "Cmd" : "Ctrl";

        add (undoButton,    tr8 ("Deshacer (") + cmd + "+Z)",                [this] { undo(); });
        add (redoButton,    tr8 ("Rehacer (") + cmd + "+Shift+Z)",          [this] { redo(); });
        add (cutButton,     tr8 ("Cortar la selección (") + cmd + "+X)",     [this] { cut(); });
        add (copyButton,    tr8 ("Copiar la selección (") + cmd + "+C)",     [this] { copy(); });
        add (pasteButton,   tr8 ("Pegar en el cursor o sobre la selección (") + cmd + "+V)", [this] { paste(); });
        add (deleteButton,  tr8 ("Borrar la selección (Supr)"),             [this] { deleteSelection(); });
        add (trimButton,    tr8 ("Recortar: conservar solo la selección"), [this] { trim(); });
        add (silenceButton, tr8 ("Silenciar la selección"),                [this] { silence(); });
        add (fadeInButton,  tr8 ("Fade in sobre la selección (o el primer segundo)"),  [this] { fade (true); });
        add (fadeOutButton, tr8 ("Fade out sobre la selección (o el último segundo)"), [this] { fade (false); });
        add (normButton,    tr8 ("Normalizar a -0,3 dB (selección o todo)"), [this] { normalise(); });
        add (reverseButton, tr8 ("Invertir en el tiempo (selección o todo)"), [this] { reverse(); });
        add (transposeButton, tr8 ("Transponer (cambiar el tono sin cambiar la duración): selección o todo"), [this] { showTransposeDialog(); });
        add (speedButton, tr8 ("Cambiar la velocidad sin cambiar el tono (para practicar): selección o todo"), [this] { showSpeedDialog(); });
        add (takeButton, tr8 ("Comping: sustituye la selección por el mismo tramo de otra toma"), [this] { showTakeMenu(); });
        add (zoomOutButton, tr8 ("Alejar (rueda + ") + cmd + ")",            [this] { zoomBy (2.0); });
        add (zoomInButton,  tr8 ("Acercar (rueda + ") + cmd + ")",           [this] { zoomBy (0.5); });
        add (fitButton,     tr8 ("Ver el clip completo"),                   [this] { zoomToFit(); });
        add (closeButton,   tr8 ("Cerrar el editor (E)"),                   [this] { if (onClose) onClose(); });

        hScroll.setAutoHide (false);
        hScroll.addListener (this);
        addAndMakeVisible (hScroll);
        updateButtons();
    }

    ~AudioEditor() override { hScroll.removeListener (this); }

    std::function<void()> onClose;

    // ---------- Qué se edita ----------
    void setRegion (Track* t, juce::int64 regionId)
    {
        if (t == track && regionId == region)
            return;
        track = t;
        region = regionId;
        selStart = selEnd = cursor = 0;
        zoomToFit();
        updateButtons();
        repaint();
    }

    // La pista va a desaparecer del proyecto: dejar de mostrarla
    void forgetTrack (Track* t)
    {
        if (track == t) { track = nullptr; region = -1; }
        updateButtons();
        repaint();
    }

    // Llamar cuando cambian las regiones desde fuera (borrar, mover, grabar...)
    void refresh()
    {
        if (track != nullptr && findRegion() == nullptr)
        {
            region = -1;
            selStart = selEnd = cursor = 0;
        }
        clampView();
        updateButtons();
        repaint();
    }

    // ---------- Transponer ----------
    void showTransposeDialog()
    {
        if (findRegion() == nullptr || engine.isRecording())
            return;

        juce::Component::SafePointer<AudioEditor> self (this);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new TransposeDialog (hasSelection(), [self] (double st) { if (self != nullptr) self->transpose (st); }));
        o.dialogTitle = "Transponer";
        o.dialogBackgroundColour = Theme::panel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    void transpose (double semitones)
    {
        runProcess (tr8 ("Transponiendo..."),
            [semitones] (const float* in, int n, std::vector<float>& out, const std::function<bool (double)>& progress)
            {
                out.resize ((size_t) n);
                return PitchShifter::process (in, n, out.data(), semitones, progress);
            });
    }

    // ---------- Cambiar velocidad (sin cambiar el tono) ----------
    std::function<void (double)> onScaleTempo;   // al cambiar la velocidad de toda la región, ajustar el tempo

    void showSpeedDialog()
    {
        if (findRegion() == nullptr || engine.isRecording())
            return;

        juce::Component::SafePointer<AudioEditor> self (this);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new SpeedDialog (hasSelection(), engine.bpm.load(), [self] (double speed, bool adjustTempo)
        {
            if (self == nullptr)
                return;
            self->runProcess (tr8 ("Cambiando la velocidad..."),
                [speed] (const float* in, int n, std::vector<float>& out, const std::function<bool (double)>& progress)
                {
                    return PitchShifter::changeSpeed (in, n, speed, out, progress);
                },
                [self, speed, adjustTempo]   // solo si el proceso termina (no al cancelar)
                {
                    if (self != nullptr && adjustTempo && self->onScaleTempo)
                        self->onScaleTempo (speed);
                });
        }));
        o.dialogTitle = "Cambiar velocidad";
        o.dialogBackgroundColour = Theme::panel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    // Procesa la selección (o toda la región) en segundo plano y la sustituye al terminar
    void runProcess (const juce::String& title, ProcessJob::Op op, std::function<void()> onSuccess = {})
    {
        auto* r = findRegion();
        if (r == nullptr)
            return;

        const auto range = rangeOrAll (*r->clip);
        auto source = r->clip;
        Track* t = track;
        const auto regionId = region;
        juce::Component::SafePointer<AudioEditor> self (this);

        auto* job = new ProcessJob (title, source, range.first, range.second, std::move (op),
            [self, source, t, regionId, onSuccess] (std::shared_ptr<Clip> result)
            {
                if (self == nullptr || result == nullptr || self->track != t || self->region != regionId)
                    return;
                auto* reg = self->findRegion();
                if (reg == nullptr || reg->clip != source)
                    return;   // la región cambió mientras se procesaba
                self->applyEdit ([result] (const Clip&) { return result; });
                if (onSuccess)
                    onSuccess();
            });
        job->launchThread();
    }

    // 30 veces por segundo desde la ventana principal (cursor de reproducción)
    void tick()
    {
        if (engine.getState() != AudioEngine::stopped)
        {
            if (auto* r = findRegion())
            {
                const auto p = engine.getPosition();
                if (p >= r->start - (juce::int64) (spp * 2) && p <= r->getEnd() + (juce::int64) (spp * 2))
                    repaint (waveArea);
            }
        }
    }

    // ---------- Dibujo ----------
    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.setGradientFill (juce::ColourGradient (Theme::panelLight, 0, 0, Theme::panel, 0, 34, false));
        g.fillRect (bounds.removeFromTop (34));
        g.setColour (Theme::border);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
        g.drawHorizontalLine (33, 0.0f, (float) getWidth());

        g.setColour (Theme::panelDark);
        g.fillRect (bounds);

        auto* r = findRegion();

        // Título e información
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (Theme::text);
        juce::String title = "EDITOR";
        if (r != nullptr)
            title << "  " << track->name << tr8 (" · ") << r->clip->name;
        g.drawFittedText (title, titleArea, juce::Justification::centredLeft, 1, 0.8f);

        g.setFont (juce::FontOptions (11.0f));
        g.setColour (Theme::textDim);
        g.drawFittedText (infoText (r), infoArea, juce::Justification::centredRight, 1, 0.8f);

        // Zona de onda
        auto wa = waveArea.toFloat();
        Theme::fillInset (g, wa.expanded (1.0f), juce::Colour (0xff17181b), 3.0f);

        if (r == nullptr)
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (tr8 ("Selecciona una región en una pista (o haz doble clic en ella) para editarla aquí."),
                        waveArea, juce::Justification::centred);
            return;
        }

        drawRuler (g, *r);

        const auto& clip = *r->clip;
        const int nch = clip.getNumChannels();
        const float laneH = wa.getHeight() / (float) nch;

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (waveArea);

        // Selección
        if (hasSelection())
        {
            const float x0 = sampleToX (selStart), x1 = sampleToX (selEnd);
            g.setColour (Theme::accent.withAlpha (0.22f));
            g.fillRect (juce::Rectangle<float> (x0, wa.getY(), x1 - x0, wa.getHeight()));
            g.setColour (Theme::accent.withAlpha (0.8f));
            g.drawVerticalLine ((int) x0, wa.getY(), wa.getBottom());
            g.drawVerticalLine ((int) x1, wa.getY(), wa.getBottom());
        }

        for (int ch = 0; ch < nch; ++ch)
        {
            auto lane = juce::Rectangle<float> (wa.getX(), wa.getY() + ch * laneH, wa.getWidth(), laneH);
            drawChannel (g, clip, ch, lane);

            g.setColour (Theme::textDim.withAlpha (0.7f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            if (nch > 1)
                g.drawText (ch == 0 ? "L" : "R", lane.removeFromLeft (16.0f).withHeight (14.0f).translated (4.0f, 2.0f),
                            juce::Justification::centredLeft);
            if (ch > 0)
            {
                g.setColour (Theme::border);
                g.drawHorizontalLine ((int) (wa.getY() + ch * laneH), wa.getX(), wa.getRight());
            }
        }

        // Cursor de edición
        if (! hasSelection())
        {
            g.setColour (Theme::soloYellow);
            g.drawVerticalLine ((int) sampleToX (cursor), wa.getY(), wa.getBottom());
        }

        // Cursor de reproducción
        const auto p = engine.getPosition() - r->start;
        if (p >= 0 && p <= r->getLength())
        {
            g.setColour (Theme::playhead);
            g.drawVerticalLine ((int) sampleToX (p), wa.getY(), wa.getBottom());
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (34).reduced (8, 5);

        closeButton.setBounds (bar.removeFromRight (26));
        bar.removeFromRight (10);
        fitButton.setBounds (bar.removeFromRight (56));
        bar.removeFromRight (3);
        zoomInButton.setBounds (bar.removeFromRight (26));
        bar.removeFromRight (3);
        zoomOutButton.setBounds (bar.removeFromRight (26));
        bar.removeFromRight (10);

        // Los botones tienen prioridad; el título usa lo que sobre (entre 120 y 230 px)
        constexpr int buttonsWidth = 1110;
        const double scale = juce::jlimit (0.6, 1.0, (bar.getWidth() - 128) / (double) buttonsWidth);
        titleArea = bar.removeFromLeft (juce::jlimit (0, 230, bar.getWidth() - (int) (buttonsWidth * scale) - 8));
        bar.removeFromLeft (8);

        auto place = [&bar, scale] (juce::TextButton& b, int w, int gapAfter)
        {
            b.setBounds (bar.removeFromLeft ((int) (w * scale)));
            bar.removeFromLeft ((int) (gapAfter * scale));
        };
        place (undoButton, 74, 3);   place (redoButton, 70, 10);
        place (cutButton, 58, 3);    place (copyButton, 60, 3);    place (pasteButton, 54, 3);
        place (deleteButton, 54, 3); place (trimButton, 70, 3);    place (silenceButton, 66, 10);
        place (fadeInButton, 64, 3); place (fadeOutButton, 70, 10);
        place (normButton, 80, 3);   place (reverseButton, 66, 3);   place (transposeButton, 88, 3);   place (speedButton, 78, 3);   place (takeButton, 86, 10);
        infoArea = bar;

        r.reduce (8, 0);
        auto scrollRow = r.removeFromBottom (16);
        rulerArea = r.removeFromTop (20).withTrimmedTop (2);
        r.removeFromBottom (4);
        waveArea = r;
        hScroll.setBounds (scrollRow.reduced (0, 2));

        clampView();
    }

    // ---------- Ratón ----------
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto* r = findRegion();
        if (r == nullptr || ! (waveArea.contains (e.getPosition()) || rulerArea.contains (e.getPosition())))
            return;

        const auto s = xToSample ((float) e.x);
        if (e.mods.isShiftDown() && (hasSelection() || cursor != s))
        {
            const auto anchor = hasSelection() ? (std::abs (s - selStart) < std::abs (s - selEnd) ? selEnd : selStart) : cursor;
            dragAnchor = anchor;
            setSelection (anchor, s);
        }
        else
        {
            dragAnchor = s;
            cursor = s;
            selStart = selEnd = s;
            if (! engine.isRecording())
                engine.setPosition (r->start + s);
        }
        dragging = true;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || findRegion() == nullptr)
            return;

        // Desplazamiento automático al arrastrar fuera de la vista
        if (e.x > waveArea.getRight())     scrollBy ((e.x - waveArea.getRight()) * spp * 0.5);
        else if (e.x < waveArea.getX())    scrollBy ((e.x - waveArea.getX()) * spp * 0.5);

        setSelection (dragAnchor, xToSample ((float) e.x));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
        if (hasSelection())
            cursor = selStart;
        updateButtons();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (auto* r = findRegion(); r != nullptr && waveArea.contains (e.getPosition()))
        {
            setSelection (0, r->getLength());
            updateButtons();
            repaint();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (findRegion() == nullptr)
            return;

        if (e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isAltDown())
        {
            const auto anchor = (double) xToSample ((float) e.x);
            const double factor = w.deltaY > 0 ? 0.8 : 1.25;
            const double newSpp = juce::jlimit (minSpp, maxSpp(), spp * factor);
            viewStart = anchor - (e.x - waveArea.getX()) * newSpp;
            spp = newSpp;
            clampView();
        }
        else
        {
            const float delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
            scrollBy (-delta * waveArea.getWidth() * spp * 0.5);
        }
        repaint();
    }

    // ---------- Teclado ----------
    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        const int code = key.getKeyCode();
        const auto c = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getTextCharacter());
        const bool cmd = mods.isCommandDown();
        const auto letter = c != 0 ? c : juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);

        if (cmd && letter == 'z' && mods.isShiftDown()) { redo(); return true; }
        if (cmd && letter == 'z') { undo();  return true; }
        if (cmd && letter == 'y') { redo();  return true; }
        if (cmd && letter == 'x') { cut();   return true; }
        if (cmd && letter == 'c') { copy();  return true; }
        if (cmd && letter == 'v') { paste(); return true; }
        if (cmd && letter == 'a')
        {
            if (auto* r = findRegion()) { setSelection (0, r->getLength()); updateButtons(); repaint(); }
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
        if (key == juce::KeyPress::escapeKey) { selStart = selEnd = cursor; updateButtons(); repaint(); return true; }

        return host.handleKey (key);   // espacio, R, etc.
    }

private:

    // ---------- Utilidades ----------
    Region* findRegion() const
    {
        if (track == nullptr || region < 0)
            return nullptr;
        for (auto& r : track->regions)
            if (r.id == region)
                return &r;
        return nullptr;
    }

    bool hasSelection() const { return selEnd > selStart; }

    void setSelection (juce::int64 a, juce::int64 b)
    {
        auto* r = findRegion();
        const auto len = r != nullptr ? r->getLength() : 0;
        selStart = juce::jlimit ((juce::int64) 0, len, juce::jmin (a, b));
        selEnd   = juce::jlimit ((juce::int64) 0, len, juce::jmax (a, b));
    }

    double sampleRate() const { return engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0; }

    float sampleToX (juce::int64 s) const { return (float) (waveArea.getX() + ((double) s - viewStart) / spp); }

    juce::int64 xToSample (float x) const
    {
        auto* r = findRegion();
        const auto len = r != nullptr ? r->getLength() : 0;
        const auto s = (juce::int64) std::llround (viewStart + (x - (float) waveArea.getX()) * spp);
        return juce::jlimit ((juce::int64) 0, len, s);
    }

    double maxSpp() const
    {
        auto* r = findRegion();
        const double len = r != nullptr ? (double) r->getLength() : 48000.0;
        return juce::jmax (minSpp, len / juce::jmax (50, waveArea.getWidth()) * 1.5);
    }

    void zoomToFit()
    {
        if (auto* r = findRegion())
            spp = juce::jmax (minSpp, (double) r->getLength() / juce::jmax (50, waveArea.getWidth()));
        viewStart = 0.0;
        clampView();
        repaint();
    }

    void zoomBy (double factor)
    {
        const double centre = hasSelection() ? (double) (selStart + selEnd) * 0.5
                                             : viewStart + waveArea.getWidth() * spp * 0.5;
        spp = juce::jlimit (minSpp, maxSpp(), spp * factor);
        viewStart = centre - waveArea.getWidth() * spp * 0.5;
        clampView();
        repaint();
    }

    void scrollBy (double samples)
    {
        viewStart += samples;
        clampView();
    }

    void clampView()
    {
        auto* r = findRegion();
        const double len = r != nullptr ? (double) r->getLength() : 0.0;
        const double visible = juce::jmax (1, waveArea.getWidth()) * spp;
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, len - visible * 0.5), viewStart);
        hScroll.setRangeLimits (0.0, juce::jmax (len, viewStart + visible), juce::dontSendNotification);
        hScroll.setCurrentRange (viewStart, visible, juce::dontSendNotification);
    }

    void scrollBarMoved (juce::ScrollBar*, double newStart) override
    {
        viewStart = newStart;
        repaint();
    }

    juce::String formatSamples (juce::int64 s) const
    {
        const double sec = (double) s / sampleRate();
        const int m = (int) (sec / 60.0);
        return juce::String::formatted ("%d:%06.3f", m, sec - m * 60.0);
    }

    juce::String infoText (const Region* r) const
    {
        if (r == nullptr)
            return {};
        juce::String t;
        if (hasSelection())
            t << tr8 ("Selección ") << formatSamples (selStart) << tr8 (" – ") << formatSamples (selEnd)
              << "  (" << juce::String ((double) (selEnd - selStart) / sampleRate(), 3) << " s)";
        else
            t << "Cursor " << formatSamples (cursor);
        t << tr8 ("   ·   ") << (r->clip->getNumChannels() > 1 ? tr8 ("Estéreo") : "Mono")
          << tr8 (" · ") << formatSamples (r->getLength());
        if (clipboard.getNumSamples() > 0)
            t << tr8 ("   ·   Portapapeles ") << juce::String ((double) clipboard.getNumSamples() / sampleRate(), 2) << " s";
        return t;
    }

    void drawRuler (juce::Graphics& g, const Region& r)
    {
        juce::ignoreUnused (r);
        const double sr = sampleRate();
        const double secsPerPixel = spp / sr;
        static const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300 };
        double step = 300;
        for (auto s : steps) if (s / secsPerPixel >= 90.0) { step = s; break; }

        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        const double startSec = viewStart / sr;
        const double endSec = startSec + waveArea.getWidth() * secsPerPixel;
        for (double t = std::floor (startSec / step) * step; t <= endSec; t += step)
        {
            if (t < 0) continue;
            const float x = sampleToX ((juce::int64) std::llround (t * sr));
            if (x < waveArea.getX() - 1) continue;
            g.setColour (Theme::textDim.withAlpha (0.6f));
            g.drawVerticalLine ((int) x, (float) rulerArea.getBottom() - 6.0f, (float) rulerArea.getBottom());
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.drawVerticalLine ((int) x, (float) waveArea.getY(), (float) waveArea.getBottom());
            g.setColour (Theme::textDim);
            const int m = (int) (t / 60.0);
            const double sec = t - m * 60.0;
            const juce::String label = step < 1.0 ? juce::String::formatted ("%d:%0*.*f", m, step < 0.01 ? 6 : 5, step < 0.01 ? 3 : 2, sec)
                                                  : juce::String::formatted ("%d:%02d", m, (int) std::llround (sec));
            g.drawText (label, (int) x + 3, rulerArea.getY(), 70, rulerArea.getHeight() - 4, juce::Justification::centredLeft);
        }
    }

    void drawChannel (juce::Graphics& g, const Clip& clip, int ch, juce::Rectangle<float> lane)
    {
        const float mid = lane.getCentreY();
        const float half = lane.getHeight() * 0.46f;

        // Líneas de referencia (0 y -6 dB)
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) mid, lane.getX(), lane.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        g.drawHorizontalLine ((int) (mid - half * 0.5f), lane.getX(), lane.getRight());
        g.drawHorizontalLine ((int) (mid + half * 0.5f), lane.getX(), lane.getRight());

        const auto len = clip.getLength();
        const float* data = clip.audio.getReadPointer (ch);
        const auto& peaks = clip.peaks[ch];
        const auto colour = track->colour.brighter (0.5f);
        const int x0 = waveArea.getX(), x1 = waveArea.getRight();

        if (spp < 1.5)
        {
            // Muy cerca: dibujamos las muestras como una línea
            juce::Path p;
            bool started = false;
            const auto first = juce::jmax ((juce::int64) 0, (juce::int64) std::floor (viewStart) - 1);
            const auto last  = juce::jmin (len - 1, (juce::int64) std::ceil (viewStart + (x1 - x0) * spp) + 1);
            for (auto s = first; s <= last; ++s)
            {
                const float x = sampleToX (s);
                const float y = mid - juce::jlimit (-1.0f, 1.0f, data[s]) * half;
                if (! started) { p.startNewSubPath (x, y); started = true; }
                else           p.lineTo (x, y);
            }
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.3f));
            if (spp < 0.15)
                for (auto s = first; s <= last; ++s)
                    g.fillEllipse (sampleToX (s) - 2.0f, mid - data[s] * half - 2.0f, 4.0f, 4.0f);
            return;
        }

        g.setColour (colour.withAlpha (0.9f));
        for (int x = x0; x < x1; ++x)
        {
            const double s0 = viewStart + (x - x0) * spp;
            const double s1 = s0 + spp;
            if (s1 < 0 || s0 >= (double) len)
                continue;

            float lo, hi;
            if (spp >= Clip::samplesPerPeak && ! peaks.empty())
            {
                int i0 = (int) (s0 / Clip::samplesPerPeak);
                int i1 = juce::jmax (i0 + 1, (int) (s1 / Clip::samplesPerPeak));
                i0 = juce::jlimit (0, (int) peaks.size(), i0);
                i1 = juce::jlimit (0, (int) peaks.size(), i1);
                float pk = 0.0f;
                for (int k = i0; k < i1; ++k) pk = juce::jmax (pk, peaks[(size_t) k]);
                lo = -pk; hi = pk;
            }
            else
            {
                const auto a = juce::jlimit ((juce::int64) 0, len - 1, (juce::int64) s0);
                const auto b = juce::jlimit (a + 1, len, (juce::int64) std::ceil (s1));
                auto range = juce::FloatVectorOperations::findMinAndMax (data + a, (int) (b - a));
                lo = range.getStart(); hi = range.getEnd();
            }
            g.drawVerticalLine (x, mid - juce::jlimit (-1.0f, 1.0f, hi) * half, mid - juce::jlimit (-1.0f, 1.0f, lo) * half + 1.0f);
        }
    }

    void updateButtons()
    {
        const bool has = findRegion() != nullptr;
        const bool sel = has && hasSelection();
        undoButton.setEnabled (host.canUndo());
        redoButton.setEnabled (host.canRedo());
        cutButton.setEnabled (sel);
        copyButton.setEnabled (sel);
        deleteButton.setEnabled (sel);
        trimButton.setEnabled (sel);
        silenceButton.setEnabled (sel);
        pasteButton.setEnabled (has && clipboard.getNumSamples() > 0);
        auto* reg = findRegion();
        takeButton.setEnabled (sel && reg != nullptr && reg->takes.size() > 1);
        for (auto* b : { &fadeInButton, &fadeOutButton, &normButton, &reverseButton, &transposeButton, &speedButton, &zoomInButton, &zoomOutButton, &fitButton })
            b->setEnabled (has);
    }

    // ---------- Edición ----------
    // Aplica una operación que devuelve un clip nuevo; guarda el antiguo para deshacer
    // Aplica una operación que devuelve un clip nuevo y la registra en el historial global.
    // startDelta: cuánto se desplaza el inicio de la región (p. ej. al recortar)
    bool applyEdit (const std::function<std::shared_ptr<Clip> (const Clip&)>& op, juce::int64 startDelta = 0,
                    const juce::String& name = tr8 ("Edición de audio"))
    {
        auto* r = findRegion();
        if (r == nullptr)
            return false;

        auto oldClip = r->clip;
        auto newClip = op (*oldClip);
        if (newClip == nullptr)
            return false;

        if (newClip->getLength() <= 0)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Editor",
                tr8 ("No se puede dejar la región vacía. Para quitarla entera, selecciónala en la pista y pulsa Supr."));
            return false;
        }

        newClip->computePeaks();
        const auto oldStart = r->start, newStart = r->start + startDelta;
        Track* t = track;
        const auto id = region;
        auto& eng = engine;
        eng.replaceClip (*t, id, newClip);
        eng.setRegionStart (*t, id, newStart);

        host.recordUndo (name,
                         [&eng, t, id, oldClip, oldStart] { eng.replaceClip (*t, id, oldClip); eng.setRegionStart (*t, id, oldStart); },
                         [&eng, t, id, newClip, newStart] { eng.replaceClip (*t, id, newClip); eng.setRegionStart (*t, id, newStart); });
        afterEdit();
        return true;
    }

    void afterEdit()
    {
        if (auto* r = findRegion())
        {
            setSelection (selStart, selEnd);
            cursor = juce::jlimit ((juce::int64) 0, r->getLength(), cursor);
        }
        clampView();
        updateButtons();
        repaint();
        host.regionsChanged();
    }

    void undo() { host.undo(); }
    void redo() { host.redo(); }

    // Rango afectado: la selección o, si no hay, todo el clip
    std::pair<int, int> rangeOrAll (const Clip& c) const
    {
        if (hasSelection())
            return { (int) selStart, (int) selEnd };
        return { 0, (int) c.getLength() };
    }

    void copy()
    {
        auto* r = findRegion();
        if (r == nullptr || ! hasSelection())
            return;
        const int n = (int) (selEnd - selStart);
        clipboard.setSize (r->clip->getNumChannels(), n);
        for (int ch = 0; ch < r->clip->getNumChannels(); ++ch)
            clipboard.copyFrom (ch, 0, r->clip->audio, ch, (int) selStart, n);
        updateButtons();
        repaint();
    }

    void deleteSelection()
    {
        if (! hasSelection())
            return;
        const int a = (int) selStart, b = (int) selEnd;
        const bool deleted = applyEdit ([a, b] (const Clip& c)
        {
            const int len = (int) c.getLength();
            auto out = c.cloneEmpty (c.getNumChannels(), len - (b - a));
            for (int ch = 0; ch < c.getNumChannels(); ++ch)
            {
                out->audio.copyFrom (ch, 0, c.audio, ch, 0, a);
                out->audio.copyFrom (ch, a, c.audio, ch, b, len - b);
            }
            return out;
        });
        if (deleted)
        {
            cursor = selStart;
            selEnd = selStart;
            afterEdit();
        }
    }

    void cut()
    {
        if (! hasSelection())
            return;
        copy();
        deleteSelection();
    }

    void paste()
    {
        if (clipboard.getNumSamples() == 0 || findRegion() == nullptr)
            return;

        const int a = (int) (hasSelection() ? selStart : cursor);
        const int b = (int) (hasSelection() ? selEnd : cursor);
        const int pasteLen = clipboard.getNumSamples();

        applyEdit ([a, b, pasteLen] (const Clip& c)
        {
            const auto& clip = clipboard;
            const int len = (int) c.getLength();
            const int nch = c.getNumChannels();
            auto out = c.cloneEmpty (nch, len - (b - a) + pasteLen);
            for (int ch = 0; ch < nch; ++ch)
            {
                out->audio.copyFrom (ch, 0, c.audio, ch, 0, a);
                // Adaptar canales: mono -> estéreo duplica; estéreo -> mono mezcla
                if (clip.getNumChannels() == nch)
                    out->audio.copyFrom (ch, a, clip, ch, 0, pasteLen);
                else if (clip.getNumChannels() == 1)
                    out->audio.copyFrom (ch, a, clip, 0, 0, pasteLen);
                else
                {
                    out->audio.copyFrom (ch, a, clip, 0, 0, pasteLen);
                    out->audio.addFrom  (ch, a, clip, 1, 0, pasteLen);
                    out->audio.applyGain (ch, a, pasteLen, 0.5f);
                }
                out->audio.copyFrom (ch, a + pasteLen, c.audio, ch, b, len - b);
            }
            return out;
        });
        selStart = selEnd = cursor = a + pasteLen;
        afterEdit();
    }

    void trim()
    {
        if (! hasSelection())
            return;
        const int a = (int) selStart, b = (int) selEnd;
        // La región empieza ahora donde empezaba la selección: así el audio no se desplaza en la canción
        if (applyEdit ([a, b] (const Clip& c)
            {
                auto out = c.cloneEmpty (c.getNumChannels(), b - a);
                for (int ch = 0; ch < c.getNumChannels(); ++ch)
                    out->audio.copyFrom (ch, 0, c.audio, ch, a, b - a);
                return out;
            }, a, "Recortar"))
        {
            setSelection (0, b - a);
            cursor = 0;
            afterEdit();
        }
    }

    void silence()
    {
        if (! hasSelection())
            return;
        const int a = (int) selStart, b = (int) selEnd;
        applyEdit ([a, b] (const Clip& c)
        {
            auto out = c.cloneEmpty (c.getNumChannels(), (int) c.getLength());
            out->audio.makeCopyOf (c.audio);
            out->audio.clear (a, b - a);
            return out;
        });
    }

    void fade (bool fadeIn)
    {
        auto* r = findRegion();
        if (r == nullptr)
            return;

        int a, b;
        if (hasSelection()) { a = (int) selStart; b = (int) selEnd; }
        else
        {
            const int oneSec = (int) juce::jmin ((juce::int64) sampleRate(), r->getLength());
            a = fadeIn ? 0 : (int) r->getLength() - oneSec;
            b = fadeIn ? oneSec : (int) r->getLength();
        }

        applyEdit ([a, b, fadeIn] (const Clip& c)
        {
            auto out = c.cloneEmpty (c.getNumChannels(), (int) c.getLength());
            out->audio.makeCopyOf (c.audio);
            const int n = juce::jmax (1, b - a);
            for (int ch = 0; ch < c.getNumChannels(); ++ch)
            {
                auto* d = out->audio.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    // Curva suave (coseno): sin escalones audibles
                    const float t = (float) i / (float) n;
                    const float g = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (fadeIn ? t : 1.0f - t));
                    d[a + i] *= g;
                }
            }
            return out;
        });
    }

    // Comping: el tramo seleccionado pasa a sonar con el mismo tramo de otra toma
    void showTakeMenu()
    {
        auto* r = findRegion();
        if (r == nullptr || r->takes.size() < 2 || ! hasSelection())
            return;
        juce::PopupMenu m;
        m.addSectionHeader (tr8 ("Usar en la selección:"));
        for (int k = 0; k < (int) r->takes.size(); ++k)
            m.addItem (k + 1, "Toma " + juce::String (k + 1), k != r->activeTake);
        juce::Component::SafePointer<AudioEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&takeButton), [self] (int result)
        {
            if (self != nullptr && result > 0)
                self->useTake (result - 1);
        });
    }

    void useTake (int take)
    {
        auto* r = findRegion();
        if (r == nullptr || take < 0 || take >= (int) r->takes.size() || ! hasSelection())
            return;
        auto source = r->takes[(size_t) take];
        const int a = (int) selStart, b = (int) selEnd;
        applyEdit ([source, a, b] (const Clip& c)
        {
            auto out = c.cloneEmpty (c.getNumChannels(), (int) c.getLength());
            out->audio.makeCopyOf (c.audio);
            const int to = juce::jmin (b, (int) source->getLength());
            const int n = to - a;
            if (n <= 0)
                return out;
            const int fade = juce::jmin (n / 4, 240);   // fundidos cortos: sin clics en las uniones
            for (int ch = 0; ch < c.getNumChannels(); ++ch)
            {
                const int srcCh = juce::jmin (ch, source->getNumChannels() - 1);
                auto* d = out->audio.getWritePointer (ch);
                const auto* src = source->audio.getReadPointer (srcCh);
                for (int i = 0; i < n; ++i)
                {
                    float w = 1.0f;
                    if (i < fade)       w = (float) i / (float) fade;
                    if (i >= n - fade)  w = juce::jmin (w, (float) (n - 1 - i) / (float) fade);
                    d[a + i] = d[a + i] * (1.0f - w) + src[a + i] * w;
                }
            }
            return out;
        });
    }

    void normalise()
    {
        auto* r = findRegion();
        if (r == nullptr)
            return;
        const auto range = rangeOrAll (*r->clip);
        applyEdit ([a = range.first, b = range.second] (const Clip& c) -> std::shared_ptr<Clip>
        {
            const float peak = c.audio.getMagnitude (a, b - a);
            if (peak < 1.0e-6f)
                return nullptr;
            auto out = c.cloneEmpty (c.getNumChannels(), (int) c.getLength());
            out->audio.makeCopyOf (c.audio);
            out->audio.applyGain (a, b - a, juce::Decibels::decibelsToGain (-0.3f) / peak);
            return out;
        });
    }

    void reverse()
    {
        auto* r = findRegion();
        if (r == nullptr)
            return;
        const auto range = rangeOrAll (*r->clip);
        applyEdit ([a = range.first, b = range.second] (const Clip& c)
        {
            auto out = c.cloneEmpty (c.getNumChannels(), (int) c.getLength());
            out->audio.makeCopyOf (c.audio);
            out->audio.reverse (a, b - a);
            return out;
        });
    }

    // ---------- Estado ----------
    MixerHost& host;
    AudioEngine& engine;
    Track* track = nullptr;
    juce::int64 region = -1;

    juce::int64 selStart = 0, selEnd = 0, cursor = 0, dragAnchor = 0;
    bool dragging = false;
    double viewStart = 0.0, spp = 100.0;
    static constexpr double minSpp = 0.05;

    static inline juce::AudioBuffer<float> clipboard;   // compartido entre pistas

    juce::Rectangle<int> titleArea, infoArea, rulerArea, waveArea;
    juce::ScrollBar hScroll { false };

    juce::TextButton undoButton { "Deshacer" }, redoButton { "Rehacer" },
                     cutButton { "Cortar" }, copyButton { "Copiar" }, pasteButton { "Pegar" },
                     deleteButton { "Borrar" }, trimButton { "Recortar" }, silenceButton { "Silencio" },
                     fadeInButton { "Fade in" }, fadeOutButton { "Fade out" },
                     normButton { "Normalizar" }, reverseButton { "Invertir" }, transposeButton { "Transponer" }, takeButton { "Usar toma..." }, speedButton { "Velocidad" },
                     zoomOutButton { "-" }, zoomInButton { "+" }, fitButton { "Ajustar" }, closeButton { "x" };
};
