#pragma once
#include "Text.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioEngine.h"
#include "Theme.h"

// Lo que el mezclador necesita de la ventana principal
struct MixerHost
{
    virtual ~MixerHost() = default;
    virtual void showInsertMenu (Track&, Section, int slot, juce::Component& target) = 0;
    virtual void showInsertOptions (Track&, Section, int slot, juce::Component& target) = 0;
    virtual void openInsertEditor (Track&, Section, int slot) = 0;
    virtual void showInsertPanel (Track&, juce::Component& target) = 0;

    // Regiones, selección e importación (pistas y editor)
    virtual void selectRegion (Track&, juce::int64 regionId) = 0;   // -1: solo la pista
    virtual void openAudioEditor() = 0;
    virtual void openDrumEditor (Track&) = 0;
    virtual void showAddBusMenu (juce::Component& target) = 0;           // "+ Bus"
    virtual void showBusMenu (Track& bus, juce::Component& target) = 0; // renombrar / quitar
    virtual void transposeRegion (Track&, juce::int64 regionId) = 0;
    virtual void changeRegionSpeed (Track&, juce::int64 regionId) = 0;
    virtual void regionsChanged() = 0;
    virtual void importFiles (const juce::StringArray& files, Track* target, juce::int64 position) = 0;

    // Deshacer global: el cambio ya está hecho; se registra cómo deshacerlo y rehacerlo
    virtual void recordUndo (const juce::String& name, std::function<void()> undoFn, std::function<void()> redoFn) = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;

    // Regiones: borrar, cortar y duplicar (con deshacer)
    virtual void deleteRegion (Track&, juce::int64 regionId) = 0;
    virtual void splitRegion (Track&, juce::int64 regionId, juce::int64 position) = 0;
    virtual juce::int64 duplicateRegion (Track&, juce::int64 regionId, juce::int64 newStart, bool recordInHistory) = 0;
    virtual bool handleKey (const juce::KeyPress&) = 0;   // atajos de transporte desde la ventana del mezclador
};

// Medidor vertical estéreo (post-fader)
class StereoMeter : public juce::Component
{
public:
    void setLevels (float l, float r)
    {
        levelL = juce::jmax (l, levelL * 0.86f);
        levelR = juce::jmax (r, levelR * 0.86f);
        holdL = juce::jmax (levelL, holdL * 0.995f);
        holdR = juce::jmax (levelR, holdR * 0.995f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float w = (r.getWidth() - 2.0f) * 0.5f;
        drawBar (g, r.removeFromLeft (w), levelL, holdL);
        r.removeFromLeft (2.0f);
        drawBar (g, r, levelR, holdR);
    }

    static float toNorm (float gain)
    {
        const float db = juce::Decibels::gainToDecibels (gain, -60.0f);
        return juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 6.0f, 0.0f, 1.0f));
    }

private:
    void drawBar (juce::Graphics& g, juce::Rectangle<float> r, float level, float hold)
    {
        Theme::fillInset (g, r, juce::Colour (0xff111214), 2.0f);
        auto inner = r.reduced (1.0f);
        const float n = toNorm (level);
        if (n > 0.0f)
        {
            juce::ColourGradient grad (Theme::playGreen, 0, inner.getBottom(), Theme::recordRed, 0, inner.getY(), false);
            grad.addColour (0.80, Theme::soloYellow);
            g.setGradientFill (grad);
            g.fillRect (inner.withTop (inner.getBottom() - inner.getHeight() * n));
        }
        const float hn = toNorm (hold);
        if (hn > 0.01f)
        {
            g.setColour (hn > toNorm (1.0f) ? Theme::recordRed : Theme::text.withAlpha (0.8f));
            g.fillRect (inner.getX(), inner.getBottom() - inner.getHeight() * hn, inner.getWidth(), 1.5f);
        }
    }

    float levelL = 0, levelR = 0, holdL = 0, holdR = 0;
};

// Una ranura de inserción (efecto o plugin)
class InsertSlot : public juce::Component, public juce::SettableTooltipClient
{
public:
    InsertSlot (MixerHost& h, Track& t, Section s, int i) : host (h), track (t), section (s), index (i)
    {
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        auto& list = track.getInserts (section);
        const bool used = index < (int) list.size();
        const bool over = isMouseOver();

        if (! used)
        {
            Theme::fillInset (g, r, Theme::panelDark, 3.0f);
            if (over && index == (int) list.size())
            {
                g.setColour (Theme::textDim);
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText ("+", r, juce::Justification::centred);
            }
            return;
        }

        auto& ins = *list[(size_t) index];
        const bool bypassed = ins.bypass.load();
        const auto base = section == Section::plugins ? juce::Colour (0xff3d5a80) : juce::Colour (0xff4b6b55);
        Theme::fillRaised (g, r, bypassed ? Theme::panelLight : (over ? base.brighter (0.15f) : base), 3.0f);

        g.setColour (bypassed ? Theme::textDim : Theme::text);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawFittedText (ins.processor->getName(), r.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition()))
            return;

        auto& list = track.getInserts (section);
        const bool used = index < (int) list.size();

        if (! used)
            host.showInsertMenu (track, section, juce::jmin (index, (int) list.size()), *this);
        else if (e.mods.isPopupMenu())
            host.showInsertOptions (track, section, index, *this);
        else
            host.openInsertEditor (track, section, index);
    }

    void mouseEnter (const juce::MouseEvent&) override
    {
        auto& list = track.getInserts (section);
        setTooltip (index < (int) list.size() ? "Clic: abrir.  Clic derecho: bypass / quitar"
                                              : (section == Section::plugins ? tr8 ("Añadir plugin") : tr8 ("Añadir efecto")));
    }

private:
    MixerHost& host;
    Track& track;
    Section section;
    int index;
};

// Botón compacto para la cabecera de pista: muestra cuántos plugins/efectos hay y abre el panel
class InsertChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    InsertChip (MixerHost& h, Track& t, Section s) : host (h), track (t), section (s)
    {
        setRepaintsOnMouseActivity (true);
        setTooltip (section == Section::plugins ? "Plugins de la pista" : "Efectos de la pista");
    }

    void refresh()
    {
        int active = 0, total = (int) track.getInserts (section).size();
        for (auto& ins : track.getInserts (section))
            if (! ins->bypass.load()) ++active;
        if (active != lastActive || total != lastTotal) { lastActive = active; lastTotal = total; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const auto on = section == Section::plugins ? juce::Colour (0xff3d5a80) : juce::Colour (0xff4b6b55);
        auto base = lastActive > 0 ? on : Theme::panelLight;
        if (isMouseButtonDown()) base = base.darker (0.2f);
        else if (isMouseOver())  base = base.brighter (0.1f);
        Theme::fillRaised (g, r, base, 4.0f);

        g.setColour (lastActive > 0 ? Theme::text : Theme::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        const juce::String label = section == Section::plugins ? "PLUG" : "FX";
        g.drawText (lastTotal > 0 ? label + " " + juce::String (lastTotal) : label, r, juce::Justification::centred);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()))
            host.showInsertPanel (track, *this);
    }

private:
    MixerHost& host;
    Track& track;
    Section section;
    int lastActive = -1, lastTotal = -1;
};

// Panel desplegable (desde la cabecera de pista) con las ranuras de plugins y efectos
class InsertPanel : public juce::Component, private juce::Timer
{
public:
    InsertPanel (MixerHost& h, Track& t)
    {
        for (int i = 0; i < Track::maxInsertsPerSection; ++i)
        {
            addAndMakeVisible (pluginSlots.add (new InsertSlot (h, t, Section::plugins, i)));
            addAndMakeVisible (effectSlots.add (new InsertSlot (h, t, Section::effects, i)));
        }
        setSize (190, 2 * (16 + Track::maxInsertsPerSection * 24) + 14);
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText ("PLUGINS", pluginsLabel, juce::Justification::centredLeft);
        g.drawText ("EFECTOS", effectsLabel, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        pluginsLabel = r.removeFromTop (16);
        for (auto* s : pluginSlots) { s->setBounds (r.removeFromTop (22)); r.removeFromTop (2); }
        r.removeFromTop (6);
        effectsLabel = r.removeFromTop (16);
        for (auto* s : effectSlots) { s->setBounds (r.removeFromTop (22)); r.removeFromTop (2); }
    }

private:
    void timerCallback() override
    {
        for (auto* s : pluginSlots) s->repaint();
        for (auto* s : effectSlots) s->repaint();
    }

    juce::OwnedArray<InsertSlot> pluginSlots, effectSlots;
    juce::Rectangle<int> pluginsLabel, effectsLabel;
};

// Canal del mezclador: plugins, efectos, panorama, fader con medidor, mute/solo y nombre
class ChannelStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int stripWidth = 104;

    // buses: (hueco, bus) de los buses activos, para mostrar un envío a cada uno
    ChannelStrip (MixerHost& h, Track& t, const juce::Array<std::pair<int, Track*>>& buses = {}) : host (h), track (t)
    {
        if (hasSends())
        {
            for (auto& [slot, bus] : buses)
            {
                auto* k = sendKnobs.add (new juce::Slider());
                k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                k->setRange (0.0, 1.0, 0.01);
                k->setDoubleClickReturnValue (true, 0.0);
                k->setValue (track.send[slot].load(), juce::dontSendNotification);
                k->getProperties().set ("arcFromStart", true);
                k->setColour (juce::Slider::rotarySliderFillColourId, bus->colour);
                k->setTooltip (tr8 ("Envío al bus \"") + bus->name + tr8 ("\" (doble clic: 0)"));
                k->setWantsKeyboardFocus (false);
                const int sl = slot;
                k->onValueChange = [this, sl, k] { track.send[sl] = (float) k->getValue(); };
                addAndMakeVisible (k);
                sendSlots.add (slot);
                sendNames.add (shortBusName (bus->name));
            }
        }
        for (int i = 0; i < Track::maxInsertsPerSection; ++i)
        {
            addAndMakeVisible (pluginSlots.add (new InsertSlot (host, track, Section::plugins, i)));
            addAndMakeVisible (effectSlots.add (new InsertSlot (host, track, Section::effects, i)));
        }

        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setTooltip ("Panorama (doble clic: centro)");
        pan.setValue (track.pan.load(), juce::dontSendNotification);
        pan.onValueChange = [this] { track.pan = (float) pan.getValue(); };
        pan.setWantsKeyboardFocus (false);
        addAndMakeVisible (pan);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setValue (track.volumeDb.load(), juce::dontSendNotification);
        fader.onValueChange = [this] { track.volumeDb = (float) fader.getValue(); };
        fader.setWantsKeyboardFocus (false);
        addAndMakeVisible (fader);
        addAndMakeVisible (meter);

        dbLabel.setJustificationType (juce::Justification::centred);
        dbLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        dbLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff15171a));
        addAndMakeVisible (dbLabel);

        if (! track.isMaster && ! track.isBus)
        {
            setupToggle (muteButton, Theme::muteBlue,   [this] (bool on) { track.mute = on; });
            setupToggle (soloButton, Theme::soloYellow, [this] (bool on) { track.solo = on; });
        }
        else
        {
            setupToggle (muteButton, Theme::muteBlue, [this] (bool on) { track.mute = on; });
        }
    }

    Track& getTrack() { return track; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (track.isBus && nameArea.contains (e.getPosition()))
        {
            host.showBusMenu (track, *this);   // renombrar o quitar el bus
            return;
        }
        if (peakBox.contains (e.getPosition()))
        {
            track.outputHold = 0.0f;
            lastPeakText.clear();
            repaint (peakBox);
        }
    }

    void refresh()
    {
        meter.setLevels (track.meterL, track.meterR);

        if (! fader.isMouseButtonDown() && std::abs (fader.getValue() - track.volumeDb.load()) > 0.05)
            fader.setValue (track.volumeDb.load(), juce::dontSendNotification);
        if (! pan.isMouseButtonDown() && std::abs (pan.getValue() - track.pan.load()) > 0.005)
            pan.setValue (track.pan.load(), juce::dontSendNotification);
        muteButton.setToggleState (track.mute.load(), juce::dontSendNotification);
        soloButton.setToggleState (track.solo.load(), juce::dontSendNotification);
        for (int b = 0; b < sendKnobs.size(); ++b)
        {
            const int slot = sendSlots[b];
            if (! sendKnobs[b]->isMouseButtonDown() && std::abs (sendKnobs[b]->getValue() - track.send[slot].load()) > 0.005)
                sendKnobs[b]->setValue (track.send[slot].load(), juce::dontSendNotification);
        }

        const float db = (float) fader.getValue();
        dbLabel.setText (db <= -59.9f ? "-inf" : juce::String (db, 1), juce::dontSendNotification);

        const auto peakText = PeakReadout::text (track.outputHold, false);
        if (peakText != lastPeakText) { lastPeakText = peakText; repaint (peakBox); }

        for (auto* s : pluginSlots) s->repaint();
        for (auto* s : effectSlots) s->repaint();

        if (track.name != lastName) { lastName = track.name; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (Theme::panelLight, 0, 0, Theme::panel.darker (0.15f), 0, r.getBottom(), false));
        g.fillRect (r);
        g.setColour (Theme::border);
        g.drawVerticalLine (getWidth() - 1, 0.0f, r.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawVerticalLine (0, 0.0f, r.getBottom());

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText ("PLUGINS", pluginsLabel, juce::Justification::centredLeft);
        g.drawText ("EFECTOS", effectsLabel, juce::Justification::centredLeft);
        if (hasSends() && ! sendKnobs.isEmpty())
        {
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (tr8 ("ENVÍOS"), sendArea.withHeight (14), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            for (int b = 0; b < sendKnobs.size(); ++b)
            {
                auto k = sendKnobs[b]->getBounds();
                g.setColour (Theme::text.withAlpha (0.85f));
                g.drawFittedText (sendNames[b], k.getX() - 3, k.getY() - 12, k.getWidth() + 6, 11,
                                  juce::Justification::centred, 1, 0.7f);
            }
        }
        if (track.isBus)
        {
            g.setColour (track.colour.withAlpha (0.10f));
            g.fillRect (getLocalBounds());
        }

        // Escala de dB junto al fader
        g.setFont (juce::FontOptions (9.0f));
        for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -40.0f, -60.0f })
        {
            const float y = (float) fader.getY() + (float) fader.getPositionOfValue (db);
            g.setColour (Theme::textDim.withAlpha (db == 0.0f ? 1.0f : 0.7f));
            g.drawText (db == -60.0f ? "-inf" : juce::String ((int) db), scaleArea.getX(), (int) y - 6, scaleArea.getWidth(), 12,
                        juce::Justification::centredRight);
        }

        PeakReadout::draw (g, peakBox.toFloat(), track.outputHold, false);

        // Nombre con el color de la pista
        auto nameR = nameArea.toFloat().reduced (3.0f, 2.0f);
        Theme::fillRaised (g, nameR, track.colour.darker (0.2f), 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawFittedText (track.name, nameR.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 6);
        nameArea = getLocalBounds().removeFromBottom (30);
        r.removeFromBottom (30);

        pluginsLabel = r.removeFromTop (14);
        for (auto* s : pluginSlots) { s->setBounds (r.removeFromTop (20)); r.removeFromTop (2); }
        r.removeFromTop (4);
        effectsLabel = r.removeFromTop (14);
        for (auto* s : effectSlots) { s->setBounds (r.removeFromTop (20)); r.removeFromTop (2); }
        r.removeFromTop (6);

        if (hasSends() && ! sendKnobs.isEmpty())
        {
            // Envíos: potenciómetros de 3 en 3, con el nombre corto del bus encima
            const int n = sendKnobs.size(), perRow = juce::jmin (3, n), rowsCount = (n + 2) / 3;
            sendArea = r.removeFromTop (14 + rowsCount * 42);
            auto rows = sendArea.withTrimmedTop (14);
            for (int row = 0; row < rowsCount; ++row)
            {
                auto line = rows.removeFromTop (42).withTrimmedTop (12);
                const int inRow = juce::jmin (perRow, n - row * 3);
                const int cellW = line.getWidth() / perRow;
                auto cells = line.withSizeKeepingCentre (cellW * inRow, line.getHeight());
                for (int i = 0; i < inRow; ++i)
                    sendKnobs[row * 3 + i]->setBounds (cells.removeFromLeft (cellW).withSizeKeepingCentre (30, 30));
            }
            r.removeFromTop (4);
        }

        pan.setBounds (r.removeFromTop (38).withSizeKeepingCentre (38, 38));
        r.removeFromTop (4);
        peakBox = r.removeFromTop (17).reduced (8, 0);   // pico máximo de salida (clic: reiniciar)
        r.removeFromTop (4);

        auto buttons = r.removeFromBottom (24);
        if (track.isMaster || track.isBus)
            muteButton.setBounds (buttons.withSizeKeepingCentre (30, 24));
        else
        {
            muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
            soloButton.setBounds (buttons.reduced (2, 0));
        }
        r.removeFromBottom (4);
        dbLabel.setBounds (r.removeFromBottom (18).reduced (10, 0));
        r.removeFromBottom (4);

        scaleArea = r.removeFromLeft (22);
        meter.setBounds (r.removeFromRight (14));
        r.removeFromRight (4);
        fader.setBounds (r);
    }

private:
    void setupToggle (juce::TextButton& b, juce::Colour onColour, std::function<void (bool)> setter)
    {
        b.setClickingTogglesState (true);
        b.setWantsKeyboardFocus (false);
        b.setColour (juce::TextButton::buttonOnColourId, onColour);
        b.onClick = [&b, setter] { setter (b.getToggleState()); };
        addAndMakeVisible (b);
    }

    MixerHost& host;
    Track& track;
    juce::OwnedArray<InsertSlot> pluginSlots, effectSlots;
    juce::Slider pan, fader;
    StereoMeter meter;
    juce::Label dbLabel;
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    bool hasSends() const { return ! track.isMaster && ! track.isBus; }

    static juce::String shortBusName (const juce::String& name)
    {
        static const char* known[][2] = { { "Reverb", "REV" }, { "Delay", "DLY" }, { "Chorus", "CHO" }, { "Flanger", "FLG" },
                                          { "Phaser", "PHS" }, { "Pitch", "PIT" }, { "Ecualizador", "EQ" }, { "Compresor", "COMP" },
                                          { "Overdrive", "OD" }, { "Ampli", "AMP" } };
        for (auto& k : known)
            if (name.equalsIgnoreCase (k[0]))
                return k[1];
        return name.substring (0, 5).toUpperCase();
    }

    juce::OwnedArray<juce::Slider> sendKnobs;
    juce::Array<int> sendSlots;
    juce::StringArray sendNames;
    juce::Rectangle<int> pluginsLabel, effectsLabel, scaleArea, nameArea, peakBox, sendArea;
    juce::String lastName, lastPeakText;
};

// El mezclador completo: todas las pistas con desplazamiento + master fijo a la derecha
class MixerComponent : public juce::Component
{
public:
    MixerComponent (MixerHost& h, AudioEngine&) : host (h)
    {
        setWantsKeyboardFocus (true);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);
        setOpaque (true);

        addBusColumn.onClick = [this] { host.showAddBusMenu (addBusColumn); };
        content.addAndMakeVisible (addBusColumn);
    }

    // Ancho que ocuparía con todos los canales visibles
    int getPreferredWidth() const { return juce::jmax (1, strips.size()) * ChannelStrip::stripWidth + addBusWidth; }

    void refresh()
    {
        for (auto* s : strips) s->refresh();
    }

    void setTracks (const juce::Array<Track*>& tracks, const juce::Array<std::pair<int, Track*>>& buses = {})
    {
        strips.clear();
        for (auto* t : tracks)
            content.addAndMakeVisible (strips.add (new ChannelStrip (host, *t, buses)));
        for (auto& b : buses)   // los buses, a la derecha de las pistas
            content.addAndMakeVisible (strips.add (new ChannelStrip (host, *b.second)));
        addBusColumn.setVisible (buses.size() < Track::maxBuses);
        resized();
    }

    bool keyPressed (const juce::KeyPress& key) override { return host.handleKey (key); }
    void mouseDown (const juce::MouseEvent&) override    { grabKeyboardFocus(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::background);
        if (strips.isEmpty())
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText ("Sin pistas", getLocalBounds().reduced (8), juce::Justification::centred, 2);
        }
    }

    void resized() override
    {
        // Los canales mantienen su ancho: al desplegarse el panel van apareciendo de izquierda a derecha
        viewport.setBounds (getLocalBounds());
        const int h = viewport.getMaximumVisibleHeight();
        content.setSize (juce::jmax (getWidth(), strips.size() * ChannelStrip::stripWidth + addBusWidth), h);
        for (int i = 0; i < strips.size(); ++i)
            strips[i]->setBounds (i * ChannelStrip::stripWidth, 0, ChannelStrip::stripWidth, h);
        addBusColumn.setBounds (strips.size() * ChannelStrip::stripWidth, 0, addBusWidth, h);
    }

private:
    // Columna estrecha "+ Bus" al final del mezclador
    struct AddBusColumn : public juce::Button
    {
        AddBusColumn() : juce::Button ("+ Bus") { setTooltip (tr8 ("Añadir un bus de efecto (reverb, delay, chorus...) al que enviar las pistas")); }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
            g.setColour ((down ? Theme::panelLight.darker (0.2f) : over ? Theme::panelLight : Theme::panel).withAlpha (0.9f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (Theme::border);
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
            g.setColour (over ? Theme::text : Theme::textDim);
            g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.drawText ("+", r.withHeight (40.0f), juce::Justification::centred);
            juce::Graphics::ScopedSaveState st (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText ("BUS", juce::Rectangle<float> (120.0f, 20.0f).withCentre (r.getCentre()), juce::Justification::centred);
        }
    };

    static constexpr int addBusWidth = 40;
    AddBusColumn addBusColumn;
    MixerHost& host;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<ChannelStrip> strips;
};
