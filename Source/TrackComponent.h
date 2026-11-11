#pragma once
#include "Text.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Timeline.h"
#include "MixerComponent.h"
#include "DrumEditor.h"

// Una fila: cabecera con controles a la izquierda y carril con las regiones a la derecha
class TrackComponent : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    TrackComponent (Track& t, AudioEngine& e, Timeline& tl, MixerHost& host, std::function<void (TrackComponent&)> onDeleteRequested)
        : track (t), engine (e), timeline (tl), host (host), onDelete (std::move (onDeleteRequested)),
          pluginChip (host, t, Section::plugins), effectChip (host, t, Section::effects)
    {
        addAndMakeVisible (pluginChip);
        addAndMakeVisible (effectChip);

        // Pista de batería: sin entrada ni grabación; botón para abrir el editor de patrones
        editDrumsButton.onClick = [this] { this->host.openDrumEditor (track); };
        editDrumsButton.setTooltip (tr8 ("Editar los patrones de batería (o doble clic en la pista)"));
        addChildComponent (editDrumsButton);

        setWantsKeyboardFocus (true);

        nameLabel.setText (track.name, juce::dontSendNotification);
        nameLabel.setEditable (false, true, false);
        nameLabel.setTooltip ("Doble clic para renombrar");
        nameLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        nameLabel.onTextChange = [this] { track.name = nameLabel.getText(); };
        addAndMakeVisible (nameLabel);

        inputBox.setTextWhenNothingSelected ("Entrada");
        inputBox.onChange = [this] { track.inputChannel = juce::jmax (-1, inputBox.getSelectedId() - 2); };
        addAndMakeVisible (inputBox);

        armButton.fillWhenOn = true;
        setupToggle (armButton,     Theme::recordRed,     [this] (bool on) { track.armed = on; });
        setupToggle (muteButton,    Theme::muteBlue,      [this] (bool on) { track.mute = on; });
        setupToggle (soloButton,    Theme::soloYellow,    [this] (bool on) { track.solo = on; });
        setupToggle (monitorButton, Theme::monitorOrange, [this] (bool on) { track.monitor = on; });
        armButton.setTooltip ("Armar para grabar");
        muteButton.setTooltip ("Silenciar");
        soloButton.setTooltip ("Solo");
        monitorButton.setTooltip (tr8 ("Monitorizar la entrada: escuchar la entrada por la salida (cuidado con la realimentación)"));

        volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        volumeSlider.setRange (-60.0, 6.0, 0.1);
        volumeSlider.setTextValueSuffix (" dB");
        volumeSlider.setValue (0.0, juce::dontSendNotification);
        volumeSlider.setDoubleClickReturnValue (true, 0.0);
        volumeSlider.setWantsKeyboardFocus (false);
        volumeSlider.onValueChange = [this]
        {
            track.volumeDb = (float) volumeSlider.getValue();
        };
        addAndMakeVisible (volumeSlider);

        panSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        panSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        panSlider.setRange (-1.0, 1.0, 0.01);
        panSlider.setValue (0.0, juce::dontSendNotification);
        panSlider.setDoubleClickReturnValue (true, 0.0);
        panSlider.setTooltip ("Panorama (doble clic: centro)");
        panSlider.setWantsKeyboardFocus (false);
        panSlider.onValueChange = [this] { track.pan = (float) panSlider.getValue(); };
        addAndMakeVisible (panSlider);

        deleteButton.setTooltip ("Eliminar pista");
        deleteButton.setWantsKeyboardFocus (false);
        deleteButton.onClick = [this] { if (onDelete) onDelete (*this); };
        addAndMakeVisible (deleteButton);

        if (track.isDrum())
        {
            inputBox.setVisible (false);
            armButton.setVisible (false);
            monitorButton.setVisible (false);
            editDrumsButton.setVisible (true);
        }

        startTimerHz (30);
    }

    Track& getTrack() { return track; }

    // Vista de automatización sobre el carril: -1 = oculta, 0 = volumen, 1 = panorama
    void showAutomation (int param) { autoView = param; repaint(); }
    int getAutomationView() const    { return autoView; }

    void setSelection (bool isTrackSelected, juce::int64 regionId)
    {
        if (trackSelected == isTrackSelected && selectedRegion == regionId)
            return;
        trackSelected = isTrackSelected;
        selectedRegion = regionId;
        repaint();
    }

    // ---------- Arrastrar archivos de audio desde el Finder / Explorador ----------
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files)
            if (engine.canRead (juce::File (f)))
                return true;
        return false;
    }

    void fileDragEnter (const juce::StringArray&, int, int) override { dropHighlight = true;  repaint(); }
    void fileDragExit (const juce::StringArray&) override            { dropHighlight = false; repaint(); }

    void filesDropped (const juce::StringArray& files, int x, int) override
    {
        dropHighlight = false;
        repaint();
        const auto lane = getLaneBounds();
        const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
        const double sec = x >= lane.getX() ? juce::jmax (0.0, timeline.xToSeconds ((float) x, lane.getX())) : 0.0;
        host.importFiles (files, &track, (juce::int64) (sec * sr));
    }

    juce::Rectangle<int> getLaneBounds() const { return getLocalBounds().withTrimmedLeft (Timeline::headerWidth); }
    void repaintLane() { repaint (getLaneBounds()); }

    void setInputChannels (const juce::StringArray& names)
    {
        inputBox.clear (juce::dontSendNotification);
        inputBox.addItem ("Sin entrada", 1);
        for (int i = 0; i < names.size(); ++i)
            inputBox.addItem (names[i], i + 2);

        int current = track.inputChannel.load();
        if (current >= names.size())
            current = -1;
        track.inputChannel = current;
        inputBox.setSelectedId (current + 2, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        // ---------- Cabecera ----------
        auto header = getLocalBounds().withWidth (Timeline::headerWidth).toFloat();
        const auto top = trackSelected ? Theme::panelLight.brighter (0.12f) : Theme::panelLight;
        g.setGradientFill (juce::ColourGradient (top, 0, header.getY(),
                                                 trackSelected ? Theme::panel.brighter (0.08f) : Theme::panel, 0, header.getBottom(), false));
        g.fillRect (header);
        g.setColour (Theme::border);
        g.drawVerticalLine ((int) header.getRight() - 1, header.getY(), header.getBottom());
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine (0, 0.0f, header.getRight());

        // Franja e indicador de color de la pista
        g.setColour (track.colour);
        g.fillRect (header.withWidth (5.0f));

        // Medidor de entrada con degradado verde -> amarillo -> rojo
        auto m = meterBounds.toFloat();
        Theme::fillInset (g, m, juce::Colour (0xff121315), 2.0f);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float norm = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f));
        if (norm > 0.0f)
        {
            juce::ColourGradient grad (Theme::playGreen, m.getX(), 0, Theme::recordRed, m.getRight(), 0, false);
            grad.addColour (0.75, Theme::soloYellow);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (m.reduced (1.0f).withWidth ((m.getWidth() - 2.0f) * norm), 1.5f);
        }

        // Pico máximo de entrada en dB (lo que llega de la interfaz: lo importante al grabar)
        PeakReadout::draw (g, peakBounds.toFloat(), track.inputHold, true);

        // ---------- Carril ----------
        const auto lane = getLaneBounds();
        g.setColour (Theme::lane);
        g.fillRect (lane);

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (lane);

        // Rejilla de compases y partes, alineada con la regla
        timeline.forEachGridLine (lane.getWidth(), [&] (double sec, int, int beatInBar)
        {
            g.setColour (juce::Colours::white.withAlpha (beatInBar == 0 ? 0.075f : 0.03f));
            g.drawVerticalLine ((int) timeline.secondsToX (sec, lane.getX()), (float) lane.getY(), (float) lane.getBottom());
        });
        g.setColour (Theme::border);
        g.drawHorizontalLine (getHeight() - 1, (float) lane.getX(), (float) lane.getRight());

        const double sr = engine.getSampleRate();
        if (sr <= 0)
            return;

        // Marcadores
        for (size_t i = 0; i < timeline.markers.size(); ++i)
        {
            g.setColour (Timeline::markerColour ((int) i).withAlpha (0.22f));
            g.drawVerticalLine ((int) timeline.secondsToX ((double) timeline.markers[i].pos / sr, lane.getX()),
                                (float) lane.getY(), (float) lane.getBottom());
        }

        // Rango del ciclo / pinchado
        if (engine.hasValidLoop() && (engine.loopOn.load() || engine.punchOn.load()))
        {
            const float lx0 = timeline.secondsToX ((double) engine.loopStart.load() / sr, lane.getX());
            const float lx1 = timeline.secondsToX ((double) engine.loopEnd.load() / sr, lane.getX());
            const auto c = engine.punchOn.load() ? Theme::recordRed : juce::Colour (0xffffc801);
            g.setColour (c.withAlpha (0.045f));
            g.fillRect (juce::Rectangle<float> (lx0, (float) lane.getY(), lx1 - lx0, (float) lane.getHeight()));
            g.setColour (c.withAlpha (0.25f));
            g.drawVerticalLine ((int) lx0, (float) lane.getY(), (float) lane.getBottom());
            g.drawVerticalLine ((int) lx1, (float) lane.getY(), (float) lane.getBottom());
        }

        for (int i = 0; i < (int) track.regions.size(); ++i)
            drawRegion (g, track.regions[(size_t) i], track.regions[(size_t) i].id == selectedRegion, lane, sr);

        // Toma que se está grabando ahora mismo
        if (track.recorder != nullptr)
        {
            const float x0 = timeline.secondsToX ((double) track.recordStart / sr, lane.getX());
            const float x1 = timeline.secondsToX (engine.getPositionSeconds(), lane.getX());
            juce::Rectangle<float> r (x0, (float) lane.getY() + 3.0f, juce::jmax (1.0f, x1 - x0), (float) lane.getHeight() - 7.0f);
            g.setColour (Theme::recordRed.withAlpha (0.25f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Theme::recordRed);
            g.fillRoundedRectangle (r.withHeight (13.0f), 3.0f);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText ("Grabando...", r.withHeight (13.0f).reduced (5.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);

            // Forma de onda en directo de lo que se está grabando
            int count = track.livePeakCount.load (std::memory_order_acquire);
            int firstPeak = 0;
            if (engine.getRecordMode() == AudioEngine::RecordMode::cycle)
            {
                // En ciclo se graban todas las vueltas seguidas: se dibuja la vuelta en curso
                const auto loopLen = engine.loopEnd.load() - engine.loopStart.load();
                const int perPass = (int) juce::jmax ((juce::int64) 1, loopLen / Clip::samplesPerPeak);
                firstPeak = (count / perPass) * perPass;
            }
            if (count > firstPeak)
            {
                auto body = r.withTrimmedTop (13.0f);
                const float mid = body.getCentreY(), half = body.getHeight() * 0.46f;
                const double samplesPerPixel = sr / timeline.pixelsPerSecond;
                const int px0 = juce::jmax ((int) x0, lane.getX());
                const int px1 = juce::jmin ((int) x1, lane.getRight());
                g.setColour (juce::Colour (0xffff8f8f));
                for (int px = px0; px < px1; ++px)
                {
                    const double s0 = timeline.xToSeconds ((float) px, lane.getX()) * sr - (double) track.recordStart;
                    int i0 = (int) (s0 / Clip::samplesPerPeak);
                    int i1 = juce::jmax (i0 + 1, (int) ((s0 + samplesPerPixel) / Clip::samplesPerPeak));
                    i0 = juce::jlimit (0, count - firstPeak, i0);
                    i1 = juce::jlimit (0, count - firstPeak, i1);
                    float pk = 0.0f;
                    for (int k = i0; k < i1; ++k)
                        pk = juce::jmax (pk, track.livePeaks[(size_t) (firstPeak + k)]);
                    if (pk > 0.0f)
                        g.drawVerticalLine (px, mid - juce::jmin (1.0f, pk) * half, mid + juce::jmin (1.0f, pk) * half + 1.0f);
                }
            }
        }

        if (track.isDrum())
            drawDrumLane (g, lane, sr);

        if (autoView >= 0)
            drawAutomation (g, lane, sr);

        // Cursor de reproducción
        const float px = timeline.secondsToX (engine.getPositionSeconds(), lane.getX());
        g.setColour (Theme::playhead.withAlpha (0.25f));
        g.fillRect (px - 1.5f, (float) lane.getY(), 3.0f, (float) lane.getHeight());
        g.setColour (Theme::playhead);
        g.drawVerticalLine ((int) px, (float) lane.getY(), (float) lane.getBottom());

        // Indicación al arrastrar archivos de audio encima
        if (dropHighlight)
        {
            g.setColour (Theme::accent.withAlpha (0.15f));
            g.fillRect (lane);
            g.setColour (Theme::accent);
            g.drawRect (lane, 2);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (tr8 ("Soltar para importar aquí"), lane, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().withWidth (Timeline::headerWidth).reduced (10, 7);
        area.removeFromLeft (4);
        auto meterRow = area.removeFromBottom (14);
        peakBounds = meterRow.removeFromRight (46);
        meterRow.removeFromRight (6);
        meterBounds = meterRow.withSizeKeepingCentre (meterRow.getWidth(), 6);
        area.removeFromBottom (3);

        auto row1 = area.removeFromTop (24);
        area.removeFromTop (5);
        auto row2 = area.removeFromTop (24);

        deleteButton.setBounds (row1.removeFromRight (22).withSizeKeepingCentre (22, 22));
        row1.removeFromRight (8);
        monitorButton.setBounds (row1.removeFromRight (32));
        row1.removeFromRight (3);
        soloButton.setBounds (row1.removeFromRight (26));
        row1.removeFromRight (3);
        muteButton.setBounds (row1.removeFromRight (26));
        row1.removeFromRight (3);
        armButton.setBounds (row1.removeFromRight (26));
        row1.removeFromRight (8);

        // Nombre a la izquierda; botones de plugins y efectos entre el nombre y los botones de pista
        nameLabel.setBounds (row1.removeFromLeft (juce::jmax (60, row1.getWidth() - 112)));
        row1.removeFromLeft (4);
        const int chipW = (row1.getWidth() - 4) / 2;
        pluginChip.setBounds (row1.removeFromLeft (chipW));
        row1.removeFromLeft (4);
        effectChip.setBounds (row1);

        inputBox.setBounds (row2.removeFromLeft (130));
        editDrumsButton.setBounds (inputBox.getBounds());
        row2.removeFromLeft (8);
        panSlider.setBounds (row2.removeFromRight (28));
        row2.removeFromRight (4);
        volumeSlider.setBounds (row2);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto lane = getLaneBounds();

        if (peakBounds.contains (e.getPosition()))   // clic en el pico: reiniciar
        {
            track.inputHold = 0.0f;
            repaint (peakBounds);
            return;
        }

        if (! lane.contains (e.getPosition()))
        {
            host.selectRegion (track, -1);   // clic en la cabecera: seleccionar la pista
            return;
        }

        const double sr = engine.getSampleRate();
        if (sr <= 0)
            return;

        if (autoView >= 0)
        {
            automationMouseDown (e, lane, sr);
            return;
        }

        if (track.isDrum())
        {
            drumMouseDown (e, lane);
            return;
        }

        const auto sample = (juce::int64) (timeline.xToSeconds ((float) e.x, lane.getX()) * sr);
        const int index = regionIndexAt (sample);

        if (index < 0 && e.mods.isPopupMenu())
        {
            showLaneMenu();
            return;
        }
        draggingRegion = false;

        if (index >= 0 && e.mods.isPopupMenu())
        {
            showRegionMenu (track.regions[(size_t) index].id, sample);
            return;
        }

        if (index >= 0)
        {
            auto id = track.regions[(size_t) index].id;
            dragStartSample = track.regions[(size_t) index].start;
            duplicatedOnDrag = false;
            if (e.mods.isAltDown() && ! engine.isRecording())
            {
                // Alt + arrastrar: se arrastra una copia (se registra al soltar)
                id = host.duplicateRegion (track, id, dragStartSample, false);
                duplicatedOnDrag = true;
            }
            host.selectRegion (track, id);
            draggingRegion = true;
            dragStartX = e.x;
            if (e.getNumberOfClicks() > 1 && ! duplicatedOnDrag)
                host.openAudioEditor();
        }
        else
        {
            host.selectRegion (track, -1);
            if (! engine.isRecording())
                engine.setPosition (juce::jmax ((juce::int64) 0, sample));
        }
        repaintLane();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingPoint >= 0)
        {
            automationDrag (e);
            return;
        }
        if (paintingDrums)
        {
            paintDrumBar (barAt (e.x));
            return;
        }
        if (! draggingRegion || engine.isRecording() || e.getDistanceFromDragStartX() == 0)
            return;
        const double sr = engine.getSampleRate();
        const int index = regionIndexById (selectedRegion);
        if (sr <= 0 || index < 0)
            return;

        const auto delta = (juce::int64) ((e.x - dragStartX) / timeline.pixelsPerSecond * sr);
        {
            const juce::ScopedLock sl (engine.getLock());
            track.regions[(size_t) index].start = juce::jmax ((juce::int64) 0, dragStartSample + delta);
        }
        repaintLane();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (draggingPoint >= 0)
            commitAutomationEdit (tr8 ("Automatización"));
        draggingPoint = -1;

        if (paintingDrums)
        {
            paintingDrums = false;
            DrumGrid::commitUndo (track, engine, host, drumBefore, tr8 ("Colocar patrones de batería"));
            if (onDrumsChanged) onDrumsChanged();
        }

        if (draggingRegion)
        {
            const int index = regionIndexById (selectedRegion);
            Track* t = &track;
            if (duplicatedOnDrag && index >= 0)
            {
                // Una sola acción: "duplicar" ya en su posición final
                const auto copy = track.regions[(size_t) index];
                auto& eng = engine;
                host.recordUndo (tr8 ("Duplicar región"),
                                 [&eng, t, copy] { eng.takeRegion (*t, copy.id); },
                                 [&eng, t, copy] { eng.putRegion (*t, copy, -1); });
                host.regionsChanged();
            }
            else if (index >= 0 && e.getDistanceFromDragStartX() != 0)
            {
                const auto id = selectedRegion, from = dragStartSample, to = track.regions[(size_t) index].start;
                if (from != to)
                {
                    auto& eng = engine;
                    host.recordUndo (tr8 ("Mover región"),
                                     [&eng, t, id, from] { eng.setRegionStart (*t, id, from); },
                                     [&eng, t, id, to]   { eng.setRegionStart (*t, id, to); });
                }
                host.regionsChanged();
            }
        }
        draggingRegion = false;
        duplicatedOnDrag = false;
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        // En Mac la tecla "borrar" es backspace
        if (track.drum != nullptr && selectedDrumBar >= 0 && selectedDrumBar < Drums::maxBars
             && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey))
        {
            auto& d = *track.drum;
            if (d.arrangement[(size_t) selectedDrumBar] >= 0)
            {
                const auto before = d;
                {
                    const juce::ScopedLock sl (engine.getLock());
                    d.arrangement[(size_t) selectedDrumBar] = -1;
                }
                DrumGrid::commitUndo (track, engine, host, before, tr8 ("Quitar patrón de batería"));
                repaintLane();
            }
            return true;
        }
        const int index = regionIndexById (selectedRegion);
        if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
             && index >= 0 && ! engine.isRecording())
        {
            const auto id = selectedRegion;
            selectedRegion = -1;
            host.deleteRegion (track, id);
            return true;
        }
        return false;
    }

public:
    std::function<void()> onDrumsChanged;   // para refrescar el editor de batería abierto

private:
    // ---------------- Batería en la pista ----------------
    int barAt (int x) const
    {
        const auto lane = getLaneBounds();
        return (int) std::floor (juce::jmax (0.0, timeline.xToSeconds ((float) x, lane.getX())) / timeline.barSeconds());
    }

    void paintDrumBar (int bar)
    {
        if (bar < 0 || bar >= Drums::maxBars || track.drum->arrangement[(size_t) bar] == paintPattern)
            return;
        {
            const juce::ScopedLock sl (engine.getLock());
            track.drum->arrangement[(size_t) bar] = (int8_t) paintPattern;
        }
        repaintLane();
    }

    void drumMouseDown (const juce::MouseEvent& e, juce::Rectangle<int>)
    {
        host.selectRegion (track, -1);
        const int bar = barAt (e.x);
        if (bar < 0 || bar >= Drums::maxBars)
            return;
        auto& d = *track.drum;

        if (e.getNumberOfClicks() > 1)
        {
            host.openDrumEditor (track);
            return;
        }

        if (e.mods.isPopupMenu())
        {
            showDrumMenu (bar);
            return;
        }

        drumBefore = d;
        selectedDrumBar = bar;
        grabKeyboardFocus();
        const int existing = d.arrangement[(size_t) bar];

        if (e.mods.isAltDown())
            paintPattern = -1;                   // Alt + clic (o arrastrar): borra
        else if (existing >= 0)
        {
            // Clic en un bloque: lo selecciona y pasa ese patrón al editor (no lo quita)
            paintPattern = existing;
            if (d.currentPattern != existing)
            {
                d.currentPattern = existing;
                if (onDrumsChanged) onDrumsChanged();
            }
        }
        else
            paintPattern = d.currentPattern;     // clic en un hueco: pone el patrón actual

        paintingDrums = true;
        paintDrumBar (bar);
        repaintLane();
    }

    void showDrumMenu (int bar)
    {
        juce::PopupMenu m;
        m.addSectionHeader (tr8 ("Compás ") + juce::String (bar + 1));
        for (int p = 0; p < Drums::numPatterns; ++p)
            m.addItem (p + 1, tr8 ("Patrón ") + DrumColours::letter (p), true, track.drum->arrangement[(size_t) bar] == p);
        m.addItem (10, tr8 ("Vacío"), true, track.drum->arrangement[(size_t) bar] < 0);
        m.addSeparator();
        m.addItem (20, tr8 ("Rellenar 8 compases desde aquí (A A A C B B B C)"));
        m.addItem (21, tr8 ("Editar patrones..."));
        juce::Component::SafePointer<TrackComponent> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self, bar] (int r)
        {
            if (self == nullptr || r == 0) return;
            if (r == 21) { self->host.openDrumEditor (self->track); return; }
            auto& d = *self->track.drum;
            const auto before = d;
            {
                const juce::ScopedLock sl (self->engine.getLock());
                if (r >= 1 && r <= Drums::numPatterns) d.arrangement[(size_t) bar] = (int8_t) (r - 1);
                if (r == 10) d.arrangement[(size_t) bar] = -1;
                if (r == 20)
                {
                    static const int8_t song[] = { 0, 0, 0, 2, 1, 1, 1, 2 };
                    for (int i = 0; i < 8 && bar + i < Drums::maxBars; ++i)
                        d.arrangement[(size_t) (bar + i)] = song[i];
                }
            }
            DrumGrid::commitUndo (self->track, self->engine, self->host, before, tr8 ("Colocar patrones de batería"));
            self->repaintLane();
        });
    }

    void drawDrumLane (juce::Graphics& g, juce::Rectangle<int> lane, double sr)
    {
        juce::ignoreUnused (sr);
        const auto& d = *track.drum;
        const double barSec = timeline.barSeconds();
        const int steps = juce::jlimit (1, Drums::maxSteps, timeline.sigNum * 16 / juce::jmax (1, timeline.sigDen));
        const int first = juce::jmax (0, (int) std::floor (timeline.viewStart / barSec));
        const int last = juce::jmin (Drums::maxBars - 1, (int) std::ceil ((timeline.viewStart + lane.getWidth() / timeline.pixelsPerSecond) / barSec));

        for (int b = first; b <= last; ++b)
        {
            const int p = d.arrangement[(size_t) b];
            if (p < 0)
                continue;
            const float x0 = timeline.secondsToX (b * barSec, lane.getX()) + 1.0f;
            const float x1 = timeline.secondsToX ((b + 1) * barSec, lane.getX()) - 1.0f;
            auto rect = juce::Rectangle<float> (x0, (float) lane.getY() + 3.0f, juce::jmax (2.0f, x1 - x0), (float) lane.getHeight() - 7.0f);
            const auto c = DrumColours::forPattern (p);
            g.setColour (c.darker (0.6f).withAlpha (0.85f));
            g.fillRoundedRectangle (rect, 3.0f);
            g.setColour (c);
            g.fillRoundedRectangle (rect.withHeight (13.0f), 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            if (rect.getWidth() > 12.0f)
                g.drawText (DrumColours::letter (p), rect.withHeight (13.0f).reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
            if (b == selectedDrumBar)
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (rect.reduced (0.5f), 3.0f, 1.5f);
            }

            // Miniatura de los golpes: bombo, caja y platos
            auto body = rect.withTrimmedTop (16.0f).reduced (3.0f, 2.0f);
            const float cw = body.getWidth() / (float) steps;
            if (cw >= 2.0f)
            {
                static const int rows[3][3] = { { Drums::kick, -1, -1 }, { Drums::snare, -1, -1 },
                                                { Drums::hatClosed, Drums::hatOpen, Drums::ride } };
                const float rh = body.getHeight() / 3.0f;
                for (int row = 0; row < 3; ++row)
                    for (int s = 0; s < steps; ++s)
                    {
                        uint8_t v = 0;
                        for (int k = 0; k < 3; ++k)
                            if (rows[row][k] >= 0)
                                v = juce::jmax (v, d.patterns[(size_t) p].steps[(size_t) rows[row][k]][(size_t) s]);
                        if (v == 0) continue;
                        g.setColour (c.brighter (0.4f).withAlpha (v == 1 ? 0.45f : v == 2 ? 0.75f : 1.0f));
                        const float dot = juce::jmin (cw * 0.7f, rh * 0.7f);
                        g.fillEllipse (body.getX() + s * cw + (cw - dot) * 0.5f, body.getY() + row * rh + (rh - dot) * 0.5f, dot, dot);
                    }
            }
        }
    }

    // ---------------- Automatización ----------------
    juce::NormalisableRange<float> volumeRange() const
    {
        juce::NormalisableRange<float> r (-60.0f, 6.0f);
        r.setSkewForCentre (-12.0f);   // igual que el fader
        return r;
    }

    float valueToY (float v, juce::Rectangle<int> lane) const
    {
        const float norm = autoView == Track::autoVolume ? volumeRange().convertTo0to1 (v) : (v + 1.0f) * 0.5f;
        return (float) lane.getBottom() - 6.0f - norm * ((float) lane.getHeight() - 12.0f);
    }

    float yToValue (float y, juce::Rectangle<int> lane) const
    {
        const float norm = juce::jlimit (0.0f, 1.0f, ((float) lane.getBottom() - 6.0f - y) / ((float) lane.getHeight() - 12.0f));
        return autoView == Track::autoVolume ? volumeRange().convertFrom0to1 (norm) : norm * 2.0f - 1.0f;
    }

    juce::String formatValue (float v) const
    {
        if (autoView == Track::autoVolume)
            return v <= -59.9f ? "-inf dB" : juce::String (v, 1) + " dB";
        if (std::abs (v) < 0.02f) return "Centro";
        return juce::String (std::abs (v) * 100.0f, 0) + (v < 0 ? " % izq." : " % der.");
    }

    void drawAutomation (juce::Graphics& g, juce::Rectangle<int> lane, double sr)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));   // regiones atenuadas
        g.fillRect (lane);

        const auto colour = autoView == Track::autoVolume ? juce::Colour (0xff7ee08a) : juce::Colour (0xff6cc4ff);
        const auto& pts = track.automation[autoView];
        const bool on = track.automationOn[autoView].load();

        if (autoView == Track::autoPan)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawHorizontalLine ((int) valueToY (0.0f, lane), (float) lane.getX(), (float) lane.getRight());
        }

        if (pts.empty())
        {
            // Sin puntos: línea discontinua en el valor actual
            const float y = valueToY (autoView == Track::autoVolume ? track.volumeDb.load() : track.pan.load(), lane);
            const float dashes[] = { 6.0f, 4.0f };
            g.setColour (colour.withAlpha (0.5f));
            g.drawDashedLine ({ (float) lane.getX(), y, (float) lane.getRight(), y }, dashes, 2, 1.5f);
        }
        else
        {
            juce::Path p;
            auto xOf = [&] (juce::int64 pos) { return timeline.secondsToX ((double) pos / sr, lane.getX()); };
            p.startNewSubPath ((float) lane.getX(), valueToY (pts.front().value, lane));
            for (auto& pt : pts)
                p.lineTo (xOf (pt.pos), valueToY (pt.value, lane));
            p.lineTo ((float) lane.getRight(), valueToY (pts.back().value, lane));
            g.setColour (colour.withAlpha (on ? 0.95f : 0.35f));
            g.strokePath (p, juce::PathStrokeType (2.0f));
            for (size_t i = 0; i < pts.size(); ++i)
            {
                const auto c = juce::Point<float> (xOf (pts[i].pos), valueToY (pts[i].value, lane));
                g.setColour ((int) i == draggingPoint ? juce::Colours::white : colour);
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c));
                if ((int) i == draggingPoint)
                {
                    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                    g.drawText (formatValue (pts[i].value), juce::Rectangle<float> (c.x + 8.0f, c.y - 18.0f, 110.0f, 14.0f),
                                juce::Justification::centredLeft);
                }
            }
        }

        // Etiqueta
        auto label = juce::Rectangle<float> ((float) lane.getX() + 6.0f, (float) lane.getY() + 4.0f, 290.0f, 15.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (label, 3.0f);
        g.setColour (colour);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (juce::String (autoView == Track::autoVolume ? tr8 ("AUTOMATIZACIÓN · VOLUMEN") : tr8 ("AUTOMATIZACIÓN · PANORAMA"))
                      + (on ? juce::String() : " (desactivada)") + tr8 ("  ·  clic derecho: opciones"),
                    label.reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    int pointAt (juce::Point<int> p, juce::Rectangle<int> lane, double sr) const
    {
        const auto& pts = track.automation[autoView];
        for (int i = (int) pts.size() - 1; i >= 0; --i)
        {
            const auto c = juce::Point<float> (timeline.secondsToX ((double) pts[(size_t) i].pos / sr, lane.getX()),
                                               valueToY (pts[(size_t) i].value, lane));
            if (c.getDistanceFrom (p.toFloat()) <= 7.0f)
                return i;
        }
        return -1;
    }

    void automationMouseDown (const juce::MouseEvent& e, juce::Rectangle<int> lane, double sr)
    {
        const int hit = pointAt (e.getPosition(), lane, sr);
        auto& pts = track.automation[autoView];
        autoBefore = pts;   // para poder deshacer el cambio

        if (e.mods.isPopupMenu())
        {
            if (hit >= 0)
            {
                {
                    const juce::ScopedLock sl (engine.getLock());
                    pts.erase (pts.begin() + hit);
                }
                commitAutomationEdit ("Borrar punto");
                repaintLane();
            }
            else
            {
                showLaneMenu();
            }
            return;
        }

        if (hit >= 0 && e.getNumberOfClicks() > 1)   // doble clic en un punto: borrarlo
        {
            {
                const juce::ScopedLock sl (engine.getLock());
                pts.erase (pts.begin() + hit);
            }
            commitAutomationEdit ("Borrar punto");
            repaintLane();
            return;
        }

        if (hit >= 0)
        {
            draggingPoint = hit;
        }
        else
        {
            // Punto nuevo donde se hace clic
            const auto pos = (juce::int64) std::llround (juce::jmax (0.0, timeline.xToSeconds ((float) e.x, lane.getX())) * sr);
            const AudioPointHolder holder { pos, yToValue ((float) e.y, lane) };
            const juce::ScopedLock sl (engine.getLock());
            auto it = std::lower_bound (pts.begin(), pts.end(), pos, [] (const Track::AutoPoint& a, juce::int64 v) { return a.pos < v; });
            draggingPoint = (int) (pts.insert (it, { holder.pos, holder.value }) - pts.begin());
        }
        repaintLane();
    }

    struct AudioPointHolder { juce::int64 pos; float value; };

    // Registra en el historial el cambio de automatización desde "autoBefore"
    void commitAutomationEdit (const juce::String& name)
    {
        if (autoView < 0)
            return;
        const auto before = autoBefore, after = track.automation[autoView];
        if (before.size() == after.size() && std::equal (before.begin(), before.end(), after.begin(),
                [] (const Track::AutoPoint& a, const Track::AutoPoint& b) { return a.pos == b.pos && a.value == b.value; }))
            return;
        Track* t = &track;
        auto& eng = engine;
        const int param = autoView;
        auto set = [&eng, t, param] (const std::vector<Track::AutoPoint>& v)
        {
            const juce::ScopedLock sl (eng.getLock());
            t->automation[param] = v;
        };
        host.recordUndo (name, [set, before] { set (before); }, [set, after] { set (after); });
    }

    void automationDrag (const juce::MouseEvent& e)
    {
        const double sr = engine.getSampleRate();
        auto& pts = track.automation[autoView];
        if (sr <= 0 || draggingPoint >= (int) pts.size())
            return;
        const auto lane = getLaneBounds();
        auto pos = (juce::int64) std::llround (juce::jmax (0.0, timeline.xToSeconds ((float) e.x, lane.getX())) * sr);
        // No puede adelantar ni quedarse detrás de sus vecinos
        if (draggingPoint > 0)                        pos = juce::jmax (pos, pts[(size_t) draggingPoint - 1].pos + 1);
        if (draggingPoint + 1 < (int) pts.size())     pos = juce::jmin (pos, pts[(size_t) draggingPoint + 1].pos - 1);
        {
            const juce::ScopedLock sl (engine.getLock());
            pts[(size_t) draggingPoint].pos = pos;
            pts[(size_t) draggingPoint].value = yToValue ((float) e.y, lane);
        }
        repaintLane();
    }

    // Clic derecho en una zona vacía del carril
    void showLaneMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader (tr8 ("Automatización"));
        m.addItem (1, "Mostrar volumen", true, autoView == Track::autoVolume);
        m.addItem (2, "Mostrar panorama", true, autoView == Track::autoPan);
        m.addItem (3, tr8 ("Ocultar automatización"), autoView >= 0);
        if (autoView >= 0)
        {
            m.addSeparator();
            m.addItem (4, tr8 ("Activa (la curva manda sobre el fader)"), true, track.automationOn[autoView].load());
            m.addItem (5, tr8 ("Borrar todos los puntos"), ! track.automation[autoView].empty());
        }
        juce::Component::SafePointer<TrackComponent> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self] (int r)
        {
            if (self == nullptr || r == 0) return;
            auto& t = self->track;
            if (r == 1) self->autoView = Track::autoVolume;
            if (r == 2) self->autoView = Track::autoPan;
            if (r == 3) self->autoView = -1;
            if (r == 4) t.automationOn[self->autoView] = ! t.automationOn[self->autoView].load();
            if (r == 5)
            {
                self->autoBefore = t.automation[self->autoView];
                {
                    const juce::ScopedLock sl (self->engine.getLock());
                    t.automation[self->autoView].clear();
                }
                self->commitAutomationEdit (tr8 ("Borrar automatización"));
            }
            self->repaint();
        });
    }

    // Menú contextual de una región (clic derecho)
    void showRegionMenu (juce::int64 regionId, juce::int64 clickPosition)
    {
        host.selectRegion (track, regionId);
        juce::PopupMenu m;
        m.addItem (1, "Abrir en el editor");
        m.addItem (2, "Transponer...", ! engine.isRecording());
        m.addItem (4, "Cambiar velocidad...", ! engine.isRecording());

        const int index = regionIndexById (regionId);
        if (index >= 0 && track.regions[(size_t) index].takes.size() > 1)
        {
            const auto& reg = track.regions[(size_t) index];
            juce::PopupMenu takesMenu;
            for (int k = 0; k < (int) reg.takes.size(); ++k)
                takesMenu.addItem (100 + k, "Toma " + juce::String (k + 1), true, k == reg.activeTake);
            m.addSeparator();
            m.addSubMenu ("Tomas", takesMenu);
        }
        m.addSeparator();
        m.addItem (5, tr8 ("Cortar aquí"), ! engine.isRecording());
        m.addItem (6, tr8 ("Duplicar"), ! engine.isRecording());
        m.addItem (3, tr8 ("Eliminar región"), ! engine.isRecording());

        juce::Component::SafePointer<TrackComponent> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self, regionId, clickPosition] (int result)
        {
            if (self == nullptr || result == 0)
                return;
            if (result >= 100)
            {
                const int idx = self->regionIndexById (regionId);
                const int before = idx >= 0 ? self->track.regions[(size_t) idx].activeTake : 0, after = result - 100;
                if (self->engine.setActiveTake (self->track, regionId, after))
                {
                    Track* t = &self->track;
                    auto& eng = self->engine;
                    self->host.recordUndo ("Cambiar de toma",
                                           [&eng, t, regionId, before] { eng.setActiveTake (*t, regionId, before); },
                                           [&eng, t, regionId, after]  { eng.setActiveTake (*t, regionId, after); });
                    self->repaintLane();
                    self->host.regionsChanged();
                }
                return;
            }
            if (result == 5)
            {
                self->host.splitRegion (self->track, regionId, clickPosition);
                return;
            }
            if (result == 6)
            {
                const int idx = self->regionIndexById (regionId);
                if (idx >= 0)
                    self->host.duplicateRegion (self->track, regionId, self->track.regions[(size_t) idx].getEnd(), true);
                return;
            }
            if (result == 1)
                self->host.openAudioEditor();
            else if (result == 2)
                self->host.transposeRegion (self->track, regionId);
            else if (result == 4)
                self->host.changeRegionSpeed (self->track, regionId);
            else if (result == 3)
            {
                self->selectedRegion = -1;
                self->host.deleteRegion (self->track, regionId);
            }
        });
    }

    int regionIndexAt (juce::int64 sample) const
    {
        for (int i = (int) track.regions.size() - 1; i >= 0; --i)
        {
            const auto& r = track.regions[(size_t) i];
            if (sample >= r.start && sample < r.getEnd())
                return i;
        }
        return -1;
    }

    int regionIndexById (juce::int64 id) const
    {
        for (int i = 0; i < (int) track.regions.size(); ++i)
            if (track.regions[(size_t) i].id == id)
                return i;
        return -1;
    }

    void drawRegion (juce::Graphics& g, const Region& r, bool isSelected, juce::Rectangle<int> lane, double sr)
    {
        const float x0 = timeline.secondsToX ((double) r.start / sr, lane.getX());
        const float x1 = timeline.secondsToX ((double) r.getEnd() / sr, lane.getX());
        if (x1 < (float) lane.getX() || x0 > (float) lane.getRight())
            return;

        juce::Rectangle<float> rect (x0, (float) lane.getY() + 3.0f, juce::jmax (2.0f, x1 - x0), (float) lane.getHeight() - 7.0f);
        const float headerH = 13.0f;
        auto body = rect.withTrimmedTop (headerH);

        g.setColour (track.colour.darker (0.55f).withAlpha (0.85f));
        g.fillRoundedRectangle (rect, 3.0f);

        g.setGradientFill (juce::ColourGradient (track.colour.brighter (0.15f), 0, rect.getY(),
                                                 track.colour.darker (0.1f), 0, rect.getY() + headerH, false));
        g.fillRoundedRectangle (rect.withHeight (headerH + 3.0f), 3.0f);
        g.setColour (track.colour.darker (0.55f).withAlpha (0.85f));
        g.fillRect (rect.withTop (rect.getY() + headerH).withHeight (3.0f));

        // Forma de onda
        const auto& peaks = r.clip->peaksMix;
        const int numPeaks = (int) peaks.size();
        const float mid = body.getCentreY();
        const float halfH = body.getHeight() * 0.46f;
        const double samplesPerPixel = sr / timeline.pixelsPerSecond;
        const int px0 = juce::jmax ((int) x0, lane.getX());
        const int px1 = juce::jmin ((int) x1, lane.getRight());

        g.setColour (track.colour.brighter (0.6f).withAlpha (0.9f));
        for (int px = px0; px < px1; ++px)
        {
            const double s0 = timeline.xToSeconds ((float) px, lane.getX()) * sr - (double) r.start;
            int i0 = (int) (s0 / Clip::samplesPerPeak);
            int i1 = juce::jmax (i0 + 1, (int) ((s0 + samplesPerPixel) / Clip::samplesPerPeak));
            i0 = juce::jlimit (0, numPeaks, i0);
            i1 = juce::jlimit (0, numPeaks, i1);

            float pk = 0.0f;
            for (int k = i0; k < i1; ++k)
                pk = juce::jmax (pk, peaks[(size_t) k]);

            g.drawVerticalLine (px, mid - pk * halfH, mid + pk * halfH + 1.0f);
        }

        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        const juce::String takeInfo = r.takes.size() > 1 ? tr8 ("  ·  toma ") + juce::String (r.activeTake + 1) + "/" + juce::String ((int) r.takes.size())
                                                         : juce::String();
        g.drawText (r.clip->name + (r.clip->getNumChannels() > 1 ? tr8 (" · estéreo") : juce::String()) + takeInfo, rect.withHeight (headerH).reduced (5.0f, 0.0f).toNearestInt(),
                    juce::Justification::centredLeft, true);

        g.setColour (isSelected ? juce::Colours::white : Theme::border);
        g.drawRoundedRectangle (rect, 3.0f, isSelected ? 1.8f : 1.0f);
    }

    void setupToggle (juce::Button& b, juce::Colour onColour, std::function<void (bool)> setter)
    {
        b.setClickingTogglesState (true);
        b.setWantsKeyboardFocus (false);
        b.setColour (juce::TextButton::buttonOnColourId, onColour);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.onClick = [&b, setter] { setter (b.getToggleState()); };
        addAndMakeVisible (b);
    }

    void timerCallback() override
    {
        // Mantiene la cabecera sincronizada con el mezclador
        if (! volumeSlider.isMouseButtonDown() && std::abs (volumeSlider.getValue() - track.volumeDb.load()) > 0.05)
            volumeSlider.setValue (track.volumeDb.load(), juce::dontSendNotification);
        if (! panSlider.isMouseButtonDown() && std::abs (panSlider.getValue() - track.pan.load()) > 0.005)
            panSlider.setValue (track.pan.load(), juce::dontSendNotification);
        muteButton.setToggleState (track.mute.load(), juce::dontSendNotification);
        soloButton.setToggleState (track.solo.load(), juce::dontSendNotification);
        armButton.setToggleState (track.armed.load(), juce::dontSendNotification);
        monitorButton.setToggleState (track.monitor.load(), juce::dontSendNotification);
        if (nameLabel.getText() != track.name && ! nameLabel.isBeingEdited())
            nameLabel.setText (track.name, juce::dontSendNotification);

        pluginChip.refresh();
        effectChip.refresh();

        const float p = track.peak.exchange (0.0f);
        level = juce::jmax (p, level * 0.85f);
        repaint (meterBounds);

        if (p > track.inputHold)
        {
            track.inputHold = p;
            repaint (peakBounds);
        }
    }

    Track& track;
    AudioEngine& engine;
    Timeline& timeline;
    MixerHost& host;
    std::function<void (TrackComponent&)> onDelete;

    InsertChip pluginChip, effectChip;
    juce::Label nameLabel;
    juce::ComboBox inputBox;
    IconButton armButton { "armar", IconButton::Icon::recordSmall, Theme::recordRed };
    juce::TextButton muteButton { "M" }, soloButton { "S" }, monitorButton { "IN" };
    IconButton deleteButton { "borrar", IconButton::Icon::close };
    juce::Slider volumeSlider, panSlider;
    juce::Rectangle<int> meterBounds, peakBounds;
    float level = 0.0f;

    int autoView = -1, draggingPoint = -1;
    juce::TextButton editDrumsButton { tr8 ("Editar batería") };
    Drums::DrumData drumBefore;
    bool paintingDrums = false;
    int paintPattern = 0;
    int selectedDrumBar = -1;
    std::vector<Track::AutoPoint> autoBefore;
    bool duplicatedOnDrag = false;
    bool trackSelected = false;
    juce::int64 selectedRegion = -1;
    bool draggingRegion = false;
    bool dropHighlight = false;
    int dragStartX = 0;
    juce::int64 dragStartSample = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackComponent)
};
