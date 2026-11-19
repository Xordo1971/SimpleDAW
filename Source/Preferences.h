#pragma once
// Archivo > Preferencias (al estilo de los Ajustes de Logic): pestañas General, Audio, Grabación y Visualización.
// Audio: sistema, salida, entrada (puede ser otro aparato: AudioRouting.h convierte su frecuencia), frecuencia de
// muestreo, tamaño del búfer, latencia resultante, prueba de sonido y retardo de grabación (con medición por cable).
#include <juce_audio_utils/juce_audio_utils.h>
#include "Text.h"
#include "Theme.h"
#include "Dialogs.h"
#include "AudioEngine.h"
#include "AudioRouting.h"

struct AppPrefs
{
    int startupAction = 0;        // 0 = proyecto vacío, 1 = abrir el último proyecto
    int autosaveMinutes = 2;      // 0 = desactivado
    bool checkUpdates = true;     // buscar actualizaciones al arrancar
    int recordBits = 24;          // 16, 24 o 32 (coma flotante)
    bool buttonLabels = false;    // nombres bajo los botones de la barra
};

// Lo que las Preferencias necesitan de la ventana principal
struct PreferencesHost
{
    virtual ~PreferencesHost() = default;
    virtual juce::AudioDeviceManager& getOutputDeviceManager() = 0;
    virtual AudioEngine& getEngine() = 0;
    virtual DeviceRouter& getRouter() = 0;
    virtual juce::String getInputDeviceName() = 0;       // el elegido ("" = sin entrada)
    virtual juce::String applyAudioSetup (const juce::String& type, const juce::String& output, const juce::String& input,
                                          double rate, int bufferSize, int outputPair) = 0;   // devuelve el error ("" = bien)
    virtual juce::StringArray getInputNames() = 0;      // entradas activas (para medir la latencia)
    virtual void latencyChanged() = 0;
    virtual AppPrefs getPrefs() = 0;
    virtual void setPrefs (const AppPrefs&) = 0;
    virtual int getAppearance() = 0;                     // 0 oscuro, 1 claro, 2.. temas (Grafito, Papel...)
    virtual void setAppearance (int) = 0;
};

class PreferencesComponent : public juce::Component, private juce::Timer
{
public:
    enum Page { general = 0, audio, recording, display, numPages };

    PreferencesComponent (PreferencesHost& h, int startPage) : host (h)
    {
        const char* names[] = { "General", "Audio", "Grabación", "Visualización" };
        for (int i = 0; i < numPages; ++i)
        {
            auto* t = tabs.add (new TabButton (tr8 (names[i]), i));
            t->onClick = [this, i] { showPage (i); };
            addAndMakeVisible (t);
        }

        buildGeneral();
        buildAudio();
        buildRecording();
        buildDisplay();

        setSize (680, 600);
        showPage (startPage);
        startTimerHz (4);
    }

    ~PreferencesComponent() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::panelDark);
        g.fillRect (getLocalBounds().removeFromTop (tabBarH));
        g.setColour (Theme::border);
        g.drawHorizontalLine (tabBarH, 0.0f, (float) getWidth());

        // Recuadro de la página, como en Logic
        g.setColour (Theme::panelDark.withAlpha (0.55f));
        g.fillRoundedRectangle (pageArea.toFloat(), 6.0f);
        g.setColour (Theme::border.withAlpha (0.6f));
        g.drawRoundedRectangle (pageArea.toFloat(), 6.0f, 1.0f);

        for (auto& s : sections[current])
        {
            g.setColour (Theme::strongText);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (s.title, s.bounds.withHeight (22), juce::Justification::centredLeft);
            if (s.line)
            {
                g.setColour (Theme::border.withAlpha (0.7f));
                g.drawHorizontalLine (s.bounds.getY() - 8, (float) s.bounds.getX(), (float) s.bounds.getRight());
            }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (tabBarH);
        const int w = 104;
        int x = (getWidth() - w * numPages) / 2;
        for (auto* t : tabs) { t->setBounds (x, bar.getY() + 6, w, bar.getHeight() - 12); x += w; }

        pageArea = r.reduced (20, 16);
        for (int p = 0; p < numPages; ++p)
            layoutPage (p);
    }

    void showPage (int p)
    {
        current = juce::jlimit (0, numPages - 1, p);
        for (int i = 0; i < tabs.size(); ++i)
            tabs[i]->setToggleState (i == current, juce::dontSendNotification);
        for (int i = 0; i < numPages; ++i)
            for (auto* c : pageComponents[i])
                c->setVisible (i == current);
        if (current == audio)
            refreshAudio();
        repaint();
    }

private:
    static constexpr int tabBarH = 70, rowH = 26, labelW = 230;

    // ---------- Pestañas con icono ----------
    struct TabButton : public juce::Button
    {
        TabButton (const juce::String& n, int i) : juce::Button (n), index (i) { setClickingTogglesState (false); }
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            if (getToggleState() || over)
            {
                g.setColour (getToggleState() ? Theme::panelLight : Theme::panelLight.withAlpha (0.5f));
                g.fillRoundedRectangle (r, 6.0f);
            }
            const auto c = getToggleState() ? Theme::accent : Theme::text;
            auto icon = r.removeFromTop (r.getHeight() * 0.62f).withSizeKeepingCentre (24.0f, 24.0f);
            g.setColour (c);
            drawIcon (g, icon);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (getName(), r, juce::Justification::centredTop);
        }
        void drawIcon (juce::Graphics& g, juce::Rectangle<float> r)
        {
            const auto ctr = r.getCentre();
            if (index == 0)   // engranaje
            {
                juce::Path p;
                for (int k = 0; k < 8; ++k)
                {
                    const float a = (float) k * juce::MathConstants<float>::twoPi / 8.0f;
                    p.addRectangle (-2.2f, -11.0f, 4.4f, 5.0f);
                    p.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi / 8.0f));
                    juce::ignoreUnused (a);
                }
                p.applyTransform (juce::AffineTransform::translation (ctr));
                g.fillPath (p);
                g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (ctr), 2.2f);
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (ctr));
            }
            else if (index == 1)   // onda
            {
                const float hs[] = { 4, 9, 16, 22, 14, 20, 10, 5 };
                for (int k = 0; k < 8; ++k)
                    g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, hs[k]).withCentre ({ r.getX() + 1.5f + (float) k * 3.0f, ctr.y }), 1.0f);
            }
            else if (index == 2)   // grabar
            {
                g.drawEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (ctr), 2.0f);
                g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (ctr));
            }
            else   // ojo
            {
                juce::Path eye;
                eye.startNewSubPath (ctr.x - 11.0f, ctr.y);
                eye.quadraticTo (ctr.x, ctr.y - 11.0f, ctr.x + 11.0f, ctr.y);
                eye.quadraticTo (ctr.x, ctr.y + 11.0f, ctr.x - 11.0f, ctr.y);
                g.strokePath (eye, juce::PathStrokeType (2.0f));
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (ctr));
            }
        }
        int index;
    };

    // ---------- Filas: etiqueta a la izquierda (alineada a la derecha) y control ----------
    struct Row { juce::Label* label = nullptr; juce::Component* control = nullptr; int height = rowH; int controlW = 0; };
    struct Section { juce::String title; bool line = false; juce::Rectangle<int> bounds; int beforeRow = 0; };

    juce::Label* makeLabel (int page, const juce::String& text)
    {
        auto* l = labels.add (new juce::Label ({}, text));
        l->setJustificationType (juce::Justification::centredRight);
        l->setColour (juce::Label::textColourId, Theme::text);
        l->setFont (juce::FontOptions (13.0f));
        addChildComponent (l);
        pageComponents[page].add (l);
        return l;
    }

    void addRow (int page, const juce::String& text, juce::Component& c, int controlW = 0, int height = rowH)
    {
        addChildComponent (c);
        pageComponents[page].add (&c);
        rows[page].push_back ({ text.isNotEmpty() ? makeLabel (page, text + ":") : nullptr, &c, height, controlW });
    }

    void addSection (int page, const juce::String& title, bool line)
    {
        sections[page].push_back ({ title, line, {}, (int) rows[page].size() });
    }

    void layoutPage (int page)
    {
        auto area = pageArea.reduced (18, 16);
        size_t sec = 0;
        for (size_t i = 0; i <= rows[page].size(); ++i)
        {
            while (sec < sections[page].size() && sections[page][sec].beforeRow == (int) i)
            {
                if (sections[page][sec].line) area.removeFromTop (14);
                sections[page][sec].bounds = area.removeFromTop (24).withHeight (24);
                area.removeFromTop (4);
                ++sec;
            }
            if (i == rows[page].size())
                break;
            auto& row = rows[page][i];
            auto line = area.removeFromTop (row.height);
            area.removeFromTop (6);
            if (row.label != nullptr)
            {
                row.label->setBounds (line.removeFromLeft (labelW));
                line.removeFromLeft (8);
            }
            else
                line.removeFromLeft (labelW + 8);
            row.control->setBounds (row.controlW > 0 ? line.removeFromLeft (juce::jmin (row.controlW, line.getWidth())) : line);
        }
    }

    static void styleNote (juce::Label& l)
    {
        l.setFont (juce::FontOptions (11.5f));
        l.setColour (juce::Label::textColourId, Theme::textDim);
        l.setJustificationType (juce::Justification::topLeft);
    }

    // ---------- General ----------
    void buildGeneral()
    {
        const auto p = host.getPrefs();
        addSection (general, "Proyectos", false);
        startup.addItem (tr8 ("Empezar con un proyecto vacío"), 1);
        startup.addItem (tr8 ("Abrir el último proyecto"), 2);
        startup.setSelectedId (p.startupAction + 1, juce::dontSendNotification);
        startup.onChange = [this] { auto q = host.getPrefs(); q.startupAction = startup.getSelectedId() - 1; host.setPrefs (q); };
        addRow (general, tr8 ("Al arrancar"), startup, 300);

        autosave.addItem ("Desactivada", 1);
        for (int m : { 1, 2, 5, 10 })
            autosave.addItem ("Cada " + juce::String (m) + (m == 1 ? " minuto" : " minutos"), m + 1);
        autosave.setSelectedId (p.autosaveMinutes + 1, juce::dontSendNotification);
        autosave.onChange = [this] { auto q = host.getPrefs(); q.autosaveMinutes = autosave.getSelectedId() - 1; host.setPrefs (q); };
        addRow (general, tr8 ("Copia de seguridad automática"), autosave, 300);

        styleNote (autosaveNote);
        autosaveNote.setText (tr8 ("Si SimpleDAW se cierra de golpe, al volver a abrirlo te ofrece recuperar la última copia."), juce::dontSendNotification);
        addRow (general, {}, autosaveNote, 0, 30);

        addSection (general, "Actualizaciones", true);
        updates.setButtonText (tr8 ("Buscar actualizaciones al arrancar"));
        updates.setToggleState (p.checkUpdates, juce::dontSendNotification);
        updates.onClick = [this] { auto q = host.getPrefs(); q.checkUpdates = updates.getToggleState(); host.setPrefs (q); };
        addRow (general, {}, updates);
    }

    // ---------- Audio ----------
    void buildAudio()
    {
        auto& dm = host.getOutputDeviceManager();
        addSection (audio, "Dispositivos", false);

        for (auto* t : dm.getAvailableDeviceTypes())
            typeBox.addItem (t->getTypeName(), typeBox.getNumItems() + 1);
        typeBox.onChange = [this] { fillDevices(); apply(); };
        if (typeBox.getNumItems() > 1)
            addRow (audio, "Sistema de audio", typeBox, 300);

        outBox.onChange = [this] { fillRatesAndBuffers(); apply(); };
        addRow (audio, "Dispositivo de salida", outBox, 300);
        pairBox.onChange = [this] { apply(); };
        addRow (audio, "Canales de salida", pairBox, 300);
        inBox.onChange = [this] { apply(); };
        addRow (audio, "Dispositivo de entrada", inBox, 300);
        rateBox.onChange = [this] { apply(); };
        addRow (audio, "Frecuencia de muestreo", rateBox, 300);
        bufferBox.onChange = [this] { apply(); };
        addRow (audio, tr8 ("Tamaño del búfer de E/S"), bufferBox, 300);

        latencyText.setFont (juce::FontOptions (13.0f));
        latencyText.setColour (juce::Label::textColourId, Theme::text);
        addRow (audio, "Latencia resultante", latencyText);
        styleNote (convertNote);
        addRow (audio, {}, convertNote, 0, 32);

        testButton.onClick = [this] { host.getOutputDeviceManager().playTestSound(); };
        testButton.setTooltip ("Suena un pitido corto por la salida elegida");
        addRow (audio, {}, testButton, 150);

        addSection (audio, tr8 ("Retardo de grabación"), true);
        manual.setSliderStyle (juce::Slider::LinearHorizontal);
        manual.setRange (-50.0, 100.0, 0.1);
        manual.setTextValueSuffix (" ms");
        manual.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        manual.setDoubleClickReturnValue (true, 0.0);
        manual.onValueChange = [this]
        {
            auto& e = host.getEngine();
            e.manualLatencySamples = (int) std::lround (manual.getValue() * rate() / 1000.0);
            host.latencyChanged();
            updateLatency();
        };
        addRow (audio, "Ajuste manual", manual, 360);
        measureButton.onClick = [this] { askToMeasure(); };
        auto* measureRow = new juce::Component();
        ownedRows.add (measureRow);
        measureRow->addAndMakeVisible (measureButton);
        measureRow->addAndMakeVisible (measureInput);
        measureRowComp = measureRow;
        addRow (audio, "Medir", *measureRow, 380);
        styleNote (latencyHelp);
        latencyHelp.setText (tr8 ("Lo grabado se adelanta solo lo que tarda el sonido en ir y volver por la interfaz. "
                                  "Si notas las tomas retrasadas, mídelo con un cable de una salida a una entrada o ajústalo a mano."),
                             juce::dontSendNotification);
        addRow (audio, {}, latencyHelp, 0, 34);

        manual.setValue (host.getEngine().manualLatencySamples.load() * 1000.0 / rate(), juce::dontSendNotification);
    }

    // ---------- Grabación ----------
    void buildRecording()
    {
        const auto p = host.getPrefs();
        addSection (recording, tr8 ("Grabación de audio"), false);
        formatLabel.setText ("WAVE (mono por cada entrada)", juce::dontSendNotification);
        formatLabel.setColour (juce::Label::textColourId, Theme::text);
        addRow (recording, "Tipo de archivo", formatLabel, 300);
        bitsBox.addItem ("16 bits", 16);
        bitsBox.addItem ("24 bits", 24);
        bitsBox.addItem ("32 bits (coma flotante)", 32);
        bitsBox.setSelectedId (p.recordBits, juce::dontSendNotification);
        bitsBox.onChange = [this] { auto q = host.getPrefs(); q.recordBits = bitsBox.getSelectedId(); host.setPrefs (q); };
        addRow (recording, "Profundidad de bits", bitsBox, 300);
        styleNote (bitsNote);
        bitsNote.setText (tr8 ("24 bits es lo habitual. 32 bits en coma flotante no satura nunca dentro del archivo, pero ocupa más."),
                          juce::dontSendNotification);
        addRow (recording, {}, bitsNote, 0, 30);

        addSection (recording, tr8 ("Metrónomo y precuenta"), true);
        auto& e = host.getEngine();
        countIn.setButtonText (tr8 ("Precuenta antes de grabar"));
        countIn.setToggleState (e.countInOn.load(), juce::dontSendNotification);
        countIn.onClick = [this] { host.getEngine().countInOn = countIn.getToggleState(); };
        addRow (recording, {}, countIn);
        for (int b = 1; b <= 4; ++b)
            bars.addItem (juce::String (b) + (b == 1 ? tr8 (" compás") : tr8 (" compases")), b);
        bars.setSelectedId (juce::jlimit (1, 4, e.countInBars.load()), juce::dontSendNotification);
        bars.onChange = [this] { host.getEngine().countInBars = bars.getSelectedId(); };
        addRow (recording, tr8 ("Duración de la precuenta"), bars, 200);
        metroVolume.setRange (0.0, 1.0, 0.01);
        metroVolume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        metroVolume.setValue (e.metronomeVolume.load(), juce::dontSendNotification);
        metroVolume.onValueChange = [this] { host.getEngine().metronomeVolume = (float) metroVolume.getValue(); };
        addRow (recording, tr8 ("Volumen del metrónomo"), metroVolume, 260);
        styleNote (metroNote);
        metroNote.setText (tr8 ("La precuenta y el metrónomo se guardan con cada proyecto."), juce::dontSendNotification);
        addRow (recording, {}, metroNote, 0, 22);
    }

    // ---------- Visualización ----------
    void buildDisplay()
    {
        addSection (display, "Aspecto", false);
        const char* looks[] = { "Predeterminado oscuro", "Predeterminado claro", "Grafito", "Papel", "Medianoche", "Analógico" };
        for (int i = 0; i < 6; ++i)
            appearance.addItem (tr8 (looks[i]), i + 1);
        appearance.setSelectedId (host.getAppearance() + 1, juce::dontSendNotification);
        appearance.onChange = [this] { host.setAppearance (appearance.getSelectedId() - 1); };
        addRow (display, "Tema", appearance, 300);

        labelsToggle.setButtonText ("Mostrar el nombre debajo de los botones de la barra");
        labelsToggle.setToggleState (host.getPrefs().buttonLabels, juce::dontSendNotification);
        labelsToggle.onClick = [this] { auto q = host.getPrefs(); q.buttonLabels = labelsToggle.getToggleState(); host.setPrefs (q); };
        addRow (display, {}, labelsToggle);
    }

    // ---------- Audio: listas y aplicar ----------
    double rate() const
    {
        auto& e = const_cast<PreferencesHost&> (host).getEngine();
        return e.getSampleRate() > 0 ? e.getSampleRate() : 48000.0;
    }

    juce::AudioIODeviceType* selectedType()
    {
        auto& types = host.getOutputDeviceManager().getAvailableDeviceTypes();
        const int i = typeBox.getSelectedId() - 1;
        return i >= 0 && i < types.size() ? types[i] : host.getOutputDeviceManager().getCurrentDeviceTypeObject();
    }

    // Lee el estado actual y rellena todo sin aplicar nada
    void refreshAudio()
    {
        juce::ScopedValueSetter<bool> svs (updating, true);
        auto& dm = host.getOutputDeviceManager();
        const auto typeName = dm.getCurrentAudioDeviceType();
        for (int i = 0; i < typeBox.getNumItems(); ++i)
            if (typeBox.getItemText (i) == typeName)
                typeBox.setSelectedItemIndex (i, juce::dontSendNotification);
        fillDevices();
        refreshMeasureInputs();
        updateLatency();
    }

    void fillDevices()
    {
        juce::ScopedValueSetter<bool> svs (updating, true);
        auto* type = selectedType();
        outBox.clear (juce::dontSendNotification);
        inBox.clear (juce::dontSendNotification);
        if (type == nullptr)
            return;
        type->scanForDevices();
        const auto outs = type->getDeviceNames (false), ins = type->getDeviceNames (true);
        for (int i = 0; i < outs.size(); ++i) outBox.addItem (outs[i], i + 1);
        inBox.addItem ("Sin entrada", 1000);
        for (int i = 0; i < ins.size(); ++i) inBox.addItem (ins[i], i + 1);

        const auto setup = host.getOutputDeviceManager().getAudioDeviceSetup();
        const int oi = outs.indexOf (setup.outputDeviceName);
        outBox.setSelectedId (oi >= 0 ? oi + 1 : (outs.isEmpty() ? 0 : 1), juce::dontSendNotification);
        const auto inName = host.getInputDeviceName();
        const int ii = ins.indexOf (inName);
        inBox.setSelectedId (inName.isEmpty() ? 1000 : (ii >= 0 ? ii + 1 : 1000), juce::dontSendNotification);
        fillRatesAndBuffers();
    }

    void fillRatesAndBuffers()
    {
        juce::ScopedValueSetter<bool> svs (updating, true);
        rateBox.clear (juce::dontSendNotification);
        bufferBox.clear (juce::dontSendNotification);
        pairBox.clear (juce::dontSendNotification);
        auto* type = selectedType();
        if (type == nullptr || outBox.getSelectedId() <= 0)
            return;
        const auto outName = outBox.getText();
        auto& dm = host.getOutputDeviceManager();
        auto* current = dm.getCurrentAudioDevice();
        std::unique_ptr<juce::AudioIODevice> temp;
        juce::AudioIODevice* dev = current != nullptr && current->getName() == outName ? current : nullptr;
        if (dev == nullptr)
        {
            temp.reset (type->createDevice (outName, {}));
            dev = temp.get();
        }
        if (dev == nullptr)
            return;

        const double curRate = current != nullptr && current == dev ? current->getCurrentSampleRate() : 48000.0;
        for (auto r : dev->getAvailableSampleRates())
            rateBox.addItem (juce::String (r, 0) + " Hz", (int) r);
        rateBox.setSelectedId ((int) curRate, juce::dontSendNotification);
        if (rateBox.getSelectedId() == 0 && rateBox.getNumItems() > 0)
            rateBox.setSelectedItemIndex (0, juce::dontSendNotification);

        const int curBuf = current != nullptr && current == dev ? current->getCurrentBufferSizeSamples() : dev->getDefaultBufferSize();
        for (auto b : dev->getAvailableBufferSizes())
            bufferBox.addItem (juce::String (b) + " samples", b);
        bufferBox.setSelectedId (curBuf, juce::dontSendNotification);
        if (bufferBox.getSelectedId() == 0 && bufferBox.getNumItems() > 0)
            bufferBox.setSelectedItemIndex (0, juce::dontSendNotification);

        const auto chans = dev->getOutputChannelNames();
        for (int i = 0; i + 1 < chans.size(); i += 2)
            pairBox.addItem (chans[i] + " + " + chans[i + 1], i / 2 + 1);
        if (chans.size() == 1)
            pairBox.addItem (chans[0], 1);
        int pair = 1;
        const auto setup = dm.getAudioDeviceSetup();
        if (current == dev && ! setup.useDefaultOutputChannels)
            pair = juce::jmax (0, setup.outputChannels.getHighestBit()) / 2 + 1;
        pairBox.setSelectedId (pair, juce::dontSendNotification);
        if (pairBox.getSelectedId() == 0 && pairBox.getNumItems() > 0)
            pairBox.setSelectedItemIndex (0, juce::dontSendNotification);
    }

    void apply()
    {
        if (updating)
            return;
        auto* type = selectedType();
        if (type == nullptr)
            return;
        const auto input = inBox.getSelectedId() == 1000 ? juce::String() : inBox.getText();
        const auto error = host.applyAudioSetup (type->getTypeName(), outBox.getText(), input,
                                                 (double) rateBox.getSelectedId(), bufferBox.getSelectedId(),
                                                 juce::jmax (0, pairBox.getSelectedId() - 1));
        refreshAudio();
        if (error.isNotEmpty())
            Dialogs::show (juce::MessageBoxIconType::WarningIcon, tr8 ("Audio"), error);
    }

    void updateLatency()
    {
        auto& dm = host.getOutputDeviceManager();
        auto& router = host.getRouter();
        auto* dev = dm.getCurrentAudioDevice();
        if (dev == nullptr)
        {
            latencyText.setText ("Sin dispositivo de salida", juce::dontSendNotification);
            convertNote.setText ({}, juce::dontSendNotification);
            return;
        }
        const double sr = dev->getCurrentSampleRate();
        const double ms = sr > 0 ? 1000.0 / sr : 0.0;
        const int outLat = dev->getOutputLatencyInSamples() + dev->getCurrentBufferSizeSamples();
        auto& e = host.getEngine();
        const int round = e.getReportedLatency() + dev->getCurrentBufferSizeSamples() * 2;
        latencyText.setText ("Ida y vuelta de " + juce::String (round * ms, 1) + " ms (" + juce::String (outLat * ms, 1) + " ms de salida)",
                             juce::dontSendNotification);

        juce::String note;
        if (router.isUsingBridge())
        {
            const double inRate = router.getInputRate();
            note = inRate <= 0 ? tr8 ("No se ha podido abrir la entrada elegida.")
                 : router.isConverting()
                     ? tr8 ("La entrada va a ") + juce::String (inRate, 0) + tr8 (" Hz y se convierte sola a ") + juce::String (sr, 0)
                       + tr8 (" Hz (añade ") + juce::String (router.getBridgeLatency() * ms, 1) + " ms)."
                     : tr8 ("Entrada y salida en aparatos distintos: se sincronizan solos (añade ")
                       + juce::String (router.getBridgeLatency() * ms, 1) + " ms).";
        }
        convertNote.setText (note, juce::dontSendNotification);
    }

    void refreshMeasureInputs()
    {
        const auto names = host.getInputNames();
        const int sel = juce::jmax (1, measureInput.getSelectedId());
        measureInput.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            measureInput.addItem (tr8 ("Vuelve por: ") + names[i], i + 1);
        measureInput.setSelectedId (juce::jmin (sel, juce::jmax (1, names.size())), juce::dontSendNotification);
        if (measureRowComp != nullptr)
        {
            auto r = measureRowComp->getLocalBounds();
            measureButton.setBounds (r.removeFromLeft (170));
            r.removeFromLeft (8);
            measureInput.setBounds (r);
        }
    }

    void askToMeasure()
    {
        auto& e = host.getEngine();
        if (e.getState() != AudioEngine::stopped || measureInput.getNumItems() == 0)
            return;
        auto* aw = new Dialogs::Window (tr8 ("Medir la latencia"),
            tr8 ("1. Conecta con un cable la salida 1 de la interfaz a la entrada elegida (y quita la guitarra).\n"
                 "2. Baja el volumen de los monitores y auriculares: sonará un clic fuerte.\n"
                 "3. Pulsa \"Medir\"."), juce::MessageBoxIconType::InfoIcon);
        aw->addButton ("Medir", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<PreferencesComponent> self (this);
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([self] (int r)
        {
            if (self != nullptr && r == 1)
            {
                self->host.getEngine().startLatencyMeasurement (self->measureInput.getSelectedId() - 1);
                self->measuring = true;
                self->measureButton.setEnabled (false);
            }
        }), true);
    }

    void timerCallback() override
    {
        if (current == audio)
        {
            updateLatency();
            if (measureInput.getNumItems() != host.getInputNames().size())
                refreshMeasureInputs();
        }
        if (measuring && ! host.getEngine().isMeasuringLatency())
        {
            measuring = false;
            measureButton.setEnabled (true);
            auto& e = host.getEngine();
            const int measured = e.getMeasuredLatency();
            if (measured < 0)
            {
                Dialogs::show (juce::MessageBoxIconType::WarningIcon, tr8 ("Medir la latencia"),
                    tr8 ("No se ha detectado el clic. Comprueba el cable, la entrada elegida y que la ganancia de esa entrada no esté al mínimo."));
                return;
            }
            const double ms = (measured - e.getReportedLatency()) * 1000.0 / rate();
            manual.setValue (juce::jlimit (-50.0, 100.0, std::round (ms * 10.0) / 10.0));
            Dialogs::show (juce::MessageBoxIconType::InfoIcon, tr8 ("Medir la latencia"),
                tr8 ("Latencia medida: ") + juce::String (measured * 1000.0 / rate(), 1) + tr8 (" ms. Ajuste aplicado."));
        }
    }

    PreferencesHost& host;
    juce::OwnedArray<TabButton> tabs;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::Component> ownedRows;
    std::array<juce::Array<juce::Component*>, numPages> pageComponents;
    std::array<std::vector<Row>, numPages> rows;
    std::array<std::vector<Section>, numPages> sections;
    juce::Rectangle<int> pageArea;
    int current = 0;
    bool updating = false, measuring = false;

    // General
    juce::ComboBox startup, autosave;
    juce::Label autosaveNote;
    juce::ToggleButton updates;
    // Audio
    juce::ComboBox typeBox, outBox, pairBox, inBox, rateBox, bufferBox, measureInput;
    juce::Label latencyText, convertNote, latencyHelp;
    juce::TextButton testButton { "Probar el sonido" }, measureButton { tr8 ("Medir con un cable...") };
    juce::Slider manual;
    juce::Component* measureRowComp = nullptr;
    // Grabación
    juce::Label formatLabel, bitsNote, metroNote;
    juce::ComboBox bitsBox, bars;
    juce::ToggleButton countIn;
    juce::Slider metroVolume;
    // Visualización
    juce::ComboBox appearance;
    juce::ToggleButton labelsToggle;
};

class PreferencesWindow : public juce::DocumentWindow
{
public:
    PreferencesWindow (PreferencesHost& host, int page, std::function<void()> onClose)
        : juce::DocumentWindow ("Preferencias", Theme::panel, juce::DocumentWindow::closeButton), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        content = new PreferencesComponent (host, page);
        setContentOwned (content, true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    void showPage (int p) { content->showPage (p); }
    void closeButtonPressed() override { if (closed) closed(); }

private:
    PreferencesComponent* content = nullptr;
    std::function<void()> closed;
};
