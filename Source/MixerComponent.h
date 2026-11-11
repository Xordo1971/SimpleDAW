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

// Aspecto de los canales del mezclador, al estilo de las consolas de Logic: gris medio, botones grises
// con relieve, huecos hundidos, cajas de valor oscuras, fader metálico y potenciómetro claro.
namespace StripLook
{
    const juce::Colour background     { 0xff4b4c4f };
    const juce::Colour backgroundDark { 0xff3f4043 };
    const juce::Colour slot           { 0xff3c3d40 };   // hueco vacío
    const juce::Colour face           { 0xff686a6e };   // botones y huecos ocupados
    const juce::Colour edge           { 0xff27282a };
    const juce::Colour text           { 0xffe9eaec };
    const juce::Colour textDim        { 0xffb3b5b9 };
    const juce::Colour valueBox       { 0xff2a2b2d };
    const juce::Colour muteOn         { 0xff47b4e6 };
    const juce::Colour pluginMark     { 0xff79aef5 };
    const juce::Colour effectMark     { 0xff86cf8f };

    // Botón o hueco ocupado: gris con un degradado suave y borde oscuro
    inline void drawFace (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, bool over, bool down)
    {
        auto c = down ? base.darker (0.25f) : over ? base.brighter (0.12f) : base;
        g.setGradientFill (juce::ColourGradient (c.brighter (0.14f), 0, r.getY(), c.darker (0.14f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 2.0f, r.getRight() - 2.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }

    // Hueco vacío: hundido, un poco más oscuro que el canal
    inline void drawSlot (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (slot);
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (edge.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 2.5f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) r.getBottom(), r.getX() + 2.0f, r.getRight() - 2.0f);
    }

    // Caja de valor (volumen, pico): fondo casi negro y texto claro
    inline void drawValueBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour textColour)
    {
        g.setColour (valueBox);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (text, r, juce::Justification::centred);
    }
}

// Dibujo de los controles dentro de un canal (fader, potenciómetros y botones M/S/R/I)
class StripLookAndFeel : public SimpleDAWLookAndFeel
{
public:
    StripLookAndFeel()
    {
        setColour (juce::TextButton::textColourOffId, StripLook::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        if (b.getToggleState())
            StripLook::drawFace (g, r, b.findColour (juce::TextButton::buttonOnColourId), over, down);
        else
            StripLook::drawFace (g, r, StripLook::face, over, down);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (13.0f, height * 0.6f), juce::Font::bold));
    }

    int getSliderThumbRadius (juce::Slider& s) override
    {
        return s.getSliderStyle() == juce::Slider::LinearVertical ? 19 : SimpleDAWLookAndFeel::getSliderThumbRadius (s);
    }

    // Fader: ranura fina oscura y tapa metálica alta con estrías
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            SimpleDAWLookAndFeel::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
            return;
        }
        const float cx = x + w * 0.5f;
        juce::Rectangle<float> groove (cx - 2.0f, (float) y + 4.0f, 4.0f, (float) h - 8.0f);
        g.setColour (juce::Colour (0xff1c1d1f));
        g.fillRoundedRectangle (groove, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine ((int) groove.getRight(), groove.getY(), groove.getBottom());

        juce::Rectangle<float> cap (cx - 12.0f, pos - 19.0f, 24.0f, 38.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);
        juce::ColourGradient metal (juce::Colour (0xffe2e3e5), 0, cap.getY(), juce::Colour (0xff8d8f93), 0, cap.getBottom(), false);
        metal.addColour (0.45, juce::Colour (0xffb9bbbe));
        metal.addColour (0.55, juce::Colour (0xff9fa1a5));
        g.setGradientFill (metal);
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (StripLook::edge);
        g.drawRoundedRectangle (cap.reduced (0.5f), 3.0f, 1.0f);
        for (float dy : { -9.0f, -6.0f, 6.0f, 9.0f })   // estrías
        {
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.drawHorizontalLine ((int) (pos + dy), cap.getX() + 4.0f, cap.getRight() - 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawHorizontalLine ((int) (pos + dy) + 1, cap.getX() + 4.0f, cap.getRight() - 4.0f);
        }
        g.setColour (juce::Colour (0xff2b2c2e));   // línea central (la posición del valor)
        g.fillRect (cap.getX() + 2.0f, pos - 1.0f, cap.getWidth() - 4.0f, 2.0f);
    }

    // Potenciómetro claro con línea indicadora; anillo con el valor (desde el centro, o desde el
    // principio y con el color del bus en los envíos)
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float prop,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const bool fromStart = (bool) slider.getProperties().getWithDefault ("arcFromStart", false);
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        auto knob = r.withSizeKeepingCentre (size, size);
        const auto c = knob.getCentre();
        const float radius = size * 0.5f - 1.0f;
        const float angle = startAngle + prop * (endAngle - startAngle);
        const float centreAngle = fromStart ? startAngle : (startAngle + endAngle) * 0.5f;

        juce::Path ring, value;
        ring.addCentredArc (c.x, c.y, radius, radius, 0, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff2a2b2d));
        g.strokePath (ring, juce::PathStrokeType (2.5f));
        if (std::abs (angle - centreAngle) > 0.01f)
        {
            value.addCentredArc (c.x, c.y, radius, radius, 0, juce::jmin (centreAngle, angle), juce::jmax (centreAngle, angle), true);
            g.setColour (fromStart ? slider.findColour (juce::Slider::rotarySliderFillColourId) : StripLook::muteOn);
            g.strokePath (value, juce::PathStrokeType (2.5f));
        }

        auto dome = knob.reduced (size * 0.16f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (dome.translated (0.0f, 1.5f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd0d1d4), dome.getX(), dome.getY(),
                                                 juce::Colour (0xff7c7e82), dome.getX(), dome.getBottom(), false));
        g.fillEllipse (dome);
        g.setColour (StripLook::edge);
        g.drawEllipse (dome, 1.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.0f, -dome.getHeight() * 0.5f + 1.5f, 2.0f, dome.getHeight() * 0.4f, 1.0f);
        g.setColour (juce::Colour (0xff222325));
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c));
    }

    static StripLookAndFeel& get()
    {
        static StripLookAndFeel instance;
        return instance;
    }
};

// Huecos que se muestran en una sección: como mínimo 4 y siempre uno libre al final
inline int insertSlotsFor (Track& t, Section s)
{
    return juce::jmax (Track::minInsertSlots, (int) t.getInserts (s).size() + 1);
}

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
            StripLook::drawSlot (g, r);
            if (over && index == (int) list.size())
            {
                g.setColour (StripLook::textDim);
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText ("+", r, juce::Justification::centred);
            }
            return;
        }

        // Ocupado: botón gris con una marca de color a la izquierda (azul: plugin, verde: efecto)
        auto& ins = *list[(size_t) index];
        const bool bypassed = ins.bypass.load();
        StripLook::drawFace (g, r, bypassed ? StripLook::slot.brighter (0.08f) : StripLook::face, over, false);
        const auto mark = section == Section::plugins ? StripLook::pluginMark : StripLook::effectMark;
        g.setColour (mark.withAlpha (bypassed ? 0.3f : 1.0f));
        g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 3.0f, r.getY() + 4.0f, 3.0f, r.getHeight() - 8.0f), 1.5f);

        g.setColour (bypassed ? StripLook::textDim.withAlpha (0.7f) : StripLook::text);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (ins.processor->getName(), r.reduced (9.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
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
    InsertPanel (MixerHost& h, Track& t) : host (h), track (t)
    {
        updateSlots();
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
    // Crea los huecos que hagan falta (al llenarse una sección aparece uno más) y ajusta el tamaño
    void updateSlots()
    {
        const int np = insertSlotsFor (track, Section::plugins), ne = insertSlotsFor (track, Section::effects);
        if (np == pluginSlots.size() && ne == effectSlots.size())
            return;
        pluginSlots.clear();
        effectSlots.clear();
        for (int i = 0; i < np; ++i) addAndMakeVisible (pluginSlots.add (new InsertSlot (host, track, Section::plugins, i)));
        for (int i = 0; i < ne; ++i) addAndMakeVisible (effectSlots.add (new InsertSlot (host, track, Section::effects, i)));
        setSize (190, 2 * 16 + (np + ne) * 24 + 14);   // el globo (CallOutBox) se recoloca solo
        resized();
    }

    void timerCallback() override
    {
        updateSlots();
        for (auto* s : pluginSlots) s->repaint();
        for (auto* s : effectSlots) s->repaint();
    }

    MixerHost& host;
    Track& track;
    juce::OwnedArray<InsertSlot> pluginSlots, effectSlots;
    juce::Rectangle<int> pluginsLabel, effectsLabel;
};

// Canal del mezclador al estilo de Logic, de arriba abajo: plugins, efectos, envíos, panorama,
// volumen y pico, fader con escala y medidor, grabar/monitor, mute/solo y nombre.
class ChannelStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int stripWidth = 104;

    // buses: (hueco, bus) de los buses activos, para mostrar un envío a cada uno
    ChannelStrip (MixerHost& h, Track& t, const juce::Array<std::pair<int, Track*>>& buses = {}) : host (h), track (t)
    {
        setLookAndFeel (&StripLookAndFeel::get());

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
                sendNames.add (bus->name);
                sendColours.add (bus->colour);
            }
        }
        setSlotCounts (insertSlotsFor (track, Section::plugins), insertSlotsFor (track, Section::effects));

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
        fader.setTooltip ("Volumen (doble clic: 0 dB)");
        fader.setValue (track.volumeDb.load(), juce::dontSendNotification);
        fader.onValueChange = [this] { track.volumeDb = (float) fader.getValue(); };
        fader.setWantsKeyboardFocus (false);
        addAndMakeVisible (fader);
        addAndMakeVisible (meter);

        setupToggle (muteButton, StripLook::muteOn, [this] (bool on) { track.mute = on; });
        muteButton.setTooltip ("Silenciar");
        if (! track.isMaster && ! track.isBus)
        {
            setupToggle (soloButton, Theme::soloYellow, [this] (bool on) { track.solo = on; });
            soloButton.setTooltip ("Solo");
            if (! track.isDrum())
            {
                setupToggle (armButton,     Theme::recordRed,     [this] (bool on) { track.armed = on; });
                setupToggle (monitorButton, Theme::monitorOrange, [this] (bool on) { track.monitor = on; });
                armButton.setTooltip ("Armar para grabar");
                monitorButton.setTooltip (tr8 ("Monitorizar la entrada: escuchar la entrada por la salida (cuidado con la realimentación)"));
            }
        }
    }

    ~ChannelStrip() override { setLookAndFeel (nullptr); }

    Track& getTrack() { return track; }

    // Huecos de plugins/efectos. Todos los canales usan los mismos para que los faders queden alineados
    // (MainComponent pone el máximo que necesite cualquier canal).
    int neededSlots (Section s) { return insertSlotsFor (track, s); }
    void setSlotCounts (int numPlugins, int numEffects)
    {
        if (numPlugins == pluginSlots.size() && numEffects == effectSlots.size())
            return;
        pluginSlots.clear();
        effectSlots.clear();
        for (int i = 0; i < numPlugins; ++i) addAndMakeVisible (pluginSlots.add (new InsertSlot (host, track, Section::plugins, i)));
        for (int i = 0; i < numEffects; ++i) addAndMakeVisible (effectSlots.add (new InsertSlot (host, track, Section::effects, i)));
        resized();
        repaint();
    }

    // Filas de envíos: todos los canales reservan las mismas para que los faders queden a la misma altura
    int numSends() const { return sendKnobs.size(); }
    void setSendRows (int rows)
    {
        if (rows == sendRows)
            return;
        sendRows = rows;
        resized();
        repaint();
    }

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
        armButton.setToggleState (track.armed.load(), juce::dontSendNotification);
        monitorButton.setToggleState (track.monitor.load(), juce::dontSendNotification);
        for (int b = 0; b < sendKnobs.size(); ++b)
        {
            const int slot = sendSlots[b];
            if (! sendKnobs[b]->isMouseButtonDown() && std::abs (sendKnobs[b]->getValue() - track.send[slot].load()) > 0.005)
                sendKnobs[b]->setValue (track.send[slot].load(), juce::dontSendNotification);
        }

        const float db = (float) fader.getValue();
        const auto volText = db <= -59.9f ? juce::String ("-inf") : juce::String (db, 1);
        if (volText != lastVolumeText) { lastVolumeText = volText; repaint (volumeBox); }

        const auto peakText = PeakReadout::text (track.outputHold, false);
        if (peakText != lastPeakText) { lastPeakText = peakText; repaint (peakBox); }

        for (auto* s : pluginSlots) s->repaint();
        for (auto* s : effectSlots) s->repaint();

        if (track.name != lastName) { lastName = track.name; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (StripLook::background.brighter (0.04f), 0, 0,
                                                 StripLook::background.darker (0.08f), 0, r.getBottom(), false));
        g.fillRect (r);
        g.setColour (StripLook::edge);
        g.drawVerticalLine (getWidth() - 1, 0.0f, r.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawVerticalLine (0, 0.0f, r.getBottom());

        // Títulos de sección
        g.setColour (StripLook::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("Plugins", pluginsLabel, juce::Justification::centred);
        g.drawText ("Efectos", effectsLabel, juce::Justification::centred);

        // Envíos: una fila por bus, con su potenciómetro y el nombre en un hueco con su color
        if (! sendKnobs.isEmpty())
        {
            g.setColour (StripLook::textDim);
            g.drawText (tr8 ("Envíos"), sendsLabel, juce::Justification::centred);
            for (int b = 0; b < sendKnobs.size(); ++b)
            {
                auto box = sendNameAreas[b].toFloat();
                StripLook::drawSlot (g, box);
                g.setColour (sendColours[b]);
                g.fillEllipse (box.getX() + 4.0f, box.getCentreY() - 3.0f, 6.0f, 6.0f);
                g.setColour (StripLook::text);
                g.setFont (juce::FontOptions (10.5f));
                g.drawFittedText (sendNames[b], box.withTrimmedLeft (13.0f).withTrimmedRight (2.0f).toNearestInt(),
                                  juce::Justification::centred, 1, 0.75f);
            }
        }

        // Volumen y pico, uno junto al otro
        const float db = (float) fader.getValue();
        StripLook::drawValueBox (g, volumeBox.toFloat(), db <= -59.9f ? juce::String ("-inf") : juce::String (db, 1), StripLook::text);
        PeakReadout::draw (g, peakBox.toFloat(), track.outputHold, false);

        // Escala de dB entre el fader y el medidor
        g.setFont (juce::FontOptions (8.5f));
        float lastY = -100.0f;
        for (float mark : { 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -15.0f, -18.0f, -24.0f, -30.0f, -40.0f, -50.0f, -60.0f })
        {
            const float y = (float) fader.getY() + (float) fader.getPositionOfValue (mark);
            if (y - lastY < 9.0f)
                continue;
            lastY = y;
            g.setColour (StripLook::textDim.withAlpha (mark == 0.0f ? 1.0f : 0.8f));
            g.drawText (mark > 0.0f ? "+" + juce::String ((int) mark) : juce::String ((int) std::abs (mark)),
                        scaleArea.getX(), (int) y - 5, scaleArea.getWidth() - 3, 10, juce::Justification::centredRight);
            g.drawHorizontalLine ((int) y, (float) scaleArea.getRight() - 2.0f, (float) scaleArea.getRight());
        }

        // Nombre abajo, sobre fondo oscuro, con una franja del color de la pista
        auto nameR = nameArea.toFloat();
        g.setColour (StripLook::backgroundDark);
        g.fillRect (nameR);
        g.setColour (track.colour);
        g.fillRect (nameR.removeFromTop (3.0f));
        g.setColour (StripLook::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawFittedText (track.name, nameR.reduced (5.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (7, 6);
        nameArea = getLocalBounds().removeFromBottom (28);
        r.removeFromBottom (28);

        pluginsLabel = r.removeFromTop (14);
        for (auto* s : pluginSlots) { s->setBounds (r.removeFromTop (20)); r.removeFromTop (2); }
        r.removeFromTop (4);
        effectsLabel = r.removeFromTop (14);
        for (auto* s : effectSlots) { s->setBounds (r.removeFromTop (20)); r.removeFromTop (2); }
        r.removeFromTop (4);

        sendNameAreas.clear();
        const int rows = juce::jmax (sendRows, sendKnobs.size());
        if (rows > 0)
        {
            sendsLabel = r.removeFromTop (14);
            for (int i = 0; i < rows; ++i)
            {
                auto row = r.removeFromTop (22);
                if (i < sendKnobs.size())
                {
                    sendKnobs[i]->setBounds (row.removeFromLeft (22));
                    row.removeFromLeft (3);
                    sendNameAreas.add (row.reduced (0, 1));
                }
                r.removeFromTop (2);
            }
            r.removeFromTop (2);
        }

        pan.setBounds (r.removeFromTop (40).withSizeKeepingCentre (40, 40));
        r.removeFromTop (5);
        auto values = r.removeFromTop (18);
        volumeBox = values.removeFromLeft (values.getWidth() / 2 - 2);
        values.removeFromLeft (4);
        peakBox = values;   // pico máximo de salida (clic: reiniciar)
        r.removeFromTop (6);

        // Abajo: M/S y, encima, R/I (en todos los canales se reserva el sitio para que los faders cuadren)
        auto msRow = r.removeFromBottom (22);
        r.removeFromBottom (3);
        auto riRow = r.removeFromBottom (20);
        r.removeFromBottom (6);
        const int half = msRow.getWidth() / 2 - 2;
        if (track.isMaster || track.isBus)
            muteButton.setBounds (msRow.withSizeKeepingCentre (half, msRow.getHeight()));
        else
        {
            muteButton.setBounds (msRow.removeFromLeft (half));
            soloButton.setBounds (msRow.removeFromRight (half));
            armButton.setBounds (riRow.removeFromLeft (half));
            monitorButton.setBounds (riRow.removeFromRight (half));
        }

        meter.setBounds (r.removeFromRight (12));
        r.removeFromRight (1);
        scaleArea = r.removeFromRight (22);
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

    bool hasSends() const { return ! track.isMaster && ! track.isBus; }

    MixerHost& host;
    Track& track;
    juce::OwnedArray<InsertSlot> pluginSlots, effectSlots;
    juce::Slider pan, fader;
    StereoMeter meter;
    juce::TextButton muteButton { "M" }, soloButton { "S" }, armButton { "R" }, monitorButton { "I" };

    juce::OwnedArray<juce::Slider> sendKnobs;
    juce::Array<int> sendSlots;
    juce::StringArray sendNames;
    juce::Array<juce::Colour> sendColours;
    juce::Array<juce::Rectangle<int>> sendNameAreas;
    juce::Rectangle<int> pluginsLabel, effectsLabel, sendsLabel, scaleArea, nameArea, volumeBox, peakBox;
    juce::String lastName, lastPeakText, lastVolumeText;
    int sendRows = 0;
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

    // Huecos de inserción comunes a todos los canales (el mayor que necesite alguno)
    void getNeededSlots (int& numPlugins, int& numEffects) const
    {
        for (auto* s : strips)
        {
            numPlugins = juce::jmax (numPlugins, s->neededSlots (Section::plugins));
            numEffects = juce::jmax (numEffects, s->neededSlots (Section::effects));
        }
    }
    void setSlotCounts (int numPlugins, int numEffects)
    {
        for (auto* s : strips) s->setSlotCounts (numPlugins, numEffects);
    }
    int getMaxSends() const
    {
        int n = 0;
        for (auto* s : strips) n = juce::jmax (n, s->numSends());
        return n;
    }
    void setSendRows (int rows)
    {
        for (auto* s : strips) s->setSendRows (rows);
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
        g.fillAll (StripLook::backgroundDark.darker (0.25f));
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
