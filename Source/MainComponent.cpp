#include "Text.h"
#include "MainComponent.h"
#include "Splash.h"

namespace
{
    constexpr int rowHeight = 84;
    constexpr int toolbarHeight = 64;
    constexpr int statusHeight = 24;
}

MainComponent::MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    auto setupButton = [this] (juce::Button& b, std::function<void()> action, const juce::String& tip)
    {
        b.setWantsKeyboardFocus (false);
        b.onClick = std::move (action);
        b.setTooltip (tip);
        addAndMakeVisible (b);
    };

    setupButton (addTrackButton, [this] { showAddTrackMenu(); },  tr8 ("Añadir una pista de audio o de batería"));
    setupButton (audioButton,    [this] { showAudioSettings(); }, "Dispositivos de entrada y salida");
    setupButton (mixerButton,    [this] { toggleMixer(); },       "Mostrar u ocultar el mezclador (X)");
    setupButton (pluginsButton,  [this] { showPluginManager(); }, "Buscar y gestionar plugins (VST3 / AU)");
    setupButton (importButton,   [this] { chooseFilesToImport(); }, tr8 ("Importar audio (wav, aiff, mp3...) a la pista seleccionada. También puedes arrastrar archivos a una pista"));
    setupButton (editorButton,   [this] { toggleEditor(); },       tr8 ("Mostrar u ocultar el editor de audio (E)"));
    setupButton (tunerButton,    [this] { toggleTuner(); },        tr8 ("Afinador de guitarra y bajo (T)"));
    addAndMakeVisible (resourceMeter);
    setupButton (metronomeButton,[this] { toggleMetronome(); },    tr8 ("Metrónomo (K). Clic derecho: volumen y precuenta"));
    metronomeButton.onRightClick = [this] { showMetronomeOptions(); };
    setupButton (cycleButton,    [this] { toggleCycle(); },        tr8 ("Ciclo (C): repite el rango marcado en la regla. Grabando en ciclo, cada vuelta es una toma. Clic derecho: pinchado"));
    cycleButton.onRightClick = [this] { showCycleMenu(); };
    ruler.onLoopChanged = [this] { repaintTimeline(); };
    ruler.onMarkersChanged = [this] { repaintTimeline(); };
    setupButton (toStartButton,  [this] { goToStart(); },         "Ir al inicio (Inicio)");
    setupButton (playButton,     [this] { togglePlay(); },        "Reproducir / parar (Espacio)");
    setupButton (recordButton,   [this] { toggleRecord(); },      "Grabar en las pistas armadas (R)");
    setupButton (stopButton,     [this]
                 {
                     if (engine.getState() == AudioEngine::stopped) goToStart();
                     else stopTransport();
                 },                                               "Parar (pulsa otra vez para volver al inicio)");
    setupButton (zoomOutButton,  [this] { zoom (1.0 / 1.5); },    "Alejar");
    setupButton (zoomInButton,   [this] { zoom (1.5); },          "Acercar");

    addTrackButton.setIconLabel ("Pista");
    audioButton.setIconLabel ("Audio");
    mixerButton.setIconLabel ("Mezclador");
    tunerButton.setIconLabel ("Afinador");
    audioButton.setIconLabel ({});     // solo icono (el texto está en la ayuda)
    pluginsButton.setIconLabel ({});
    importButton.setIconLabel ({});
    editorButton.setIconLabel ("Editor");

    // Editor de audio (panel inferior, oculto al principio)
    audioEditor = std::make_unique<AudioEditor> (*this, engine);
    audioEditor->onClose = [this] { setEditorVisible (false); };
    audioEditor->onScaleTempo = [this] (double factor) { setTempo (std::round (timeline.bpm * factor * 10.0) / 10.0); };
    addChildComponent (*audioEditor);
    addChildComponent (editorDragBar);
    editorDragBar.onDrag = [this] (int dy, bool start)
    {
        if (start) { editorHeightAtDragStart = editorHeight; return; }
        editorHeight = juce::jlimit (150, juce::jmax (150, getHeight() - 300), editorHeightAtDragStart - dy);
        resized();
    };
    recordButton.fillWhenOn = true;

    lcd.onTempoChanged = [this] (double v) { setTempo (v); };
    lcd.onSignatureChanged = [this] (int n, int d) { setTimeSignature (n, d); };
    addAndMakeVisible (lcd);

    deviceInfo.setJustificationType (juce::Justification::centredLeft);
    deviceInfo.setFont (juce::FontOptions (12.0f));
    deviceInfo.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (deviceInfo);

    addAndMakeVisible (ruler);

    viewport.setViewedComponent (&trackContainer, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    hScroll.setAutoHide (false);
    hScroll.addListener (this);
    addAndMakeVisible (hScroll);

    // Plugins externos: formatos (VST3, y AU en Mac) y la lista de plugins ya encontrados
    juce::addDefaultFormatsToManager (pluginFormats);
    if (auto xml = juce::XmlDocument::parse (AppPaths::pluginList()))
        knownPlugins.recreateFromXml (*xml);
    knownPlugins.addChangeListener (this);

    // Canal master siempre visible a la izquierda
    masterStrip = std::make_unique<ChannelStrip> (*this, engine.getMaster());
    addAndMakeVisible (*masterStrip);

    // Mezclador: se despliega junto al master (oculto al principio)
    mixer = std::make_unique<MixerComponent> (*this, engine);
    addChildComponent (*mixer);

    // Afinador (ventana flotante)
    tunerWindow = std::make_unique<TunerWindow> (engine, [this] { return getActiveInputNames(); });

    std::unique_ptr<juce::XmlElement> saved;
    auto settings = AppPaths::audioSettings();
    if (settings.existsAsFile())
        saved = juce::XmlDocument::parse (settings);

    auto error = deviceManager.initialise (16, 2, saved.get(), true);
    loadAppSettings();   // después de abrir el dispositivo: la latencia manual depende de la frecuencia
    setupDefaultBuses();
    deviceManager.addAudioCallback (&engine);
    deviceManager.addChangeListener (this);

    refreshDeviceInfo();
    if (error.isNotEmpty())
        deviceInfo.setText ("Error de audio: " + error, juce::dontSendNotification);

    logo = Brand::loadImage ("logoapp_png");

    addTrack();
    setSize (1440, 820);
    markSaved();
    syncChangeWatcher();
    startTimerHz (30);
    autosaveTimer.startTimer (120 * 1000);   // autoguardado cada 2 minutos
}

MainComponent::~MainComponent()
{
    stopTimer();
    autosaveTimer.stopTimer();
    drumEditorWindow = nullptr;
    audioEditor = nullptr;
    editorWindows.clear();            // primero las ventanas de los plugins
    tunerWindow = nullptr;
    mixerAnimation.stopTimer();
    mixer = nullptr;
    masterStrip = nullptr;
    knownPlugins.removeChangeListener (this);
    hScroll.removeListener (this);
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
    engine.stop();
    trackComponents.clear();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

// ===================== Archivos =====================

void MainComponent::saveAudioSettings()
{
    if (auto xml = deviceManager.createStateXml())
        xml->writeTo (AppPaths::audioSettings());
}

// ===================== Pistas =====================

juce::StringArray MainComponent::getActiveInputNames() const
{
    juce::StringArray result;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        auto names  = dev->getInputChannelNames();
        auto active = dev->getActiveInputChannels();
        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                result.add (names[i]);
    }
    return result;
}

Track* MainComponent::addTrack (const juce::String& name, bool drum)
{
    static const juce::Colour palette[] = {
        juce::Colour (0xffe07a5f), juce::Colour (0xff81b29a), juce::Colour (0xfff2cc8f),
        juce::Colour (0xff6c8ebf), juce::Colour (0xffb784a7), juce::Colour (0xff4fb3bf)
    };
    const int n = (int) (sizeof (palette) / sizeof (palette[0]));

    ++trackCounter;
    auto* track = drum ? engine.addDrumTrack (name.isNotEmpty() ? name : tr8 ("Batería"), juce::Colour (0xffc9a227))
                       : engine.addTrack (name.isNotEmpty() ? name : "Pista " + juce::String (trackCounter), palette[(trackCounter - 1) % n]);

    auto* tc = trackComponents.add (new TrackComponent (*track, engine, timeline, *this,
                                                        [this] (TrackComponent& c) { removeTrack (c); }));
    tc->onDrumsChanged = [this] { if (drumEditorWindow != nullptr) drumEditorWindow->refresh(); };
    tc->setInputChannels (getActiveInputNames());
    trackContainer.addAndMakeVisible (tc);
    resized();
    updateMixerTracks();
    return track;
}

void MainComponent::removeTrack (TrackComponent& c)
{
    if (engine.isRecording())
        return;

    juce::Component::SafePointer<MainComponent> self (this);
    juce::Component::SafePointer<TrackComponent> safe (&c);

    juce::MessageManager::callAsync ([self, safe]
    {
        if (self == nullptr || safe == nullptr)
            return;

        auto* track = &safe->getTrack();
        auto holder = self->detachTrackFromProject (track);
        if (holder == nullptr)
            return;
        self->recordUndo (tr8 ("Borrar pista \"") + holder->track->name + "\"",
            [self, holder] { if (self != nullptr) self->attachTrackToProject (*holder); },
            [self, holder, track]
            {
                // Rehacer: volver a quitarla (es el mismo objeto que se restauró al deshacer)
                if (self == nullptr) return;
                if (auto h = self->detachTrackFromProject (track))
                    *holder = std::move (*h);
            });
    });
}

void MainComponent::layoutTracks()
{
    const int width = viewport.getMaximumVisibleWidth();
    trackContainer.setSize (width, rowHeight * trackComponents.size());

    for (int i = 0; i < trackComponents.size(); ++i)
        trackComponents[i]->setBounds (0, i * rowHeight, width, rowHeight);
}

// ===================== Transporte =====================

void MainComponent::togglePlay()
{
    if (engine.getState() == AudioEngine::stopped)
        engine.play();
    else
        stopTransport();
}

void MainComponent::toggleRecord()
{
    if (engine.isRecording())
    {
        stopTransport();
        return;
    }

    if (engine.startRecording (AppPaths::recordingsFolder()) == 0)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Nada que grabar",
            tr8 ("Arma al menos una pista (botón R) que tenga una entrada seleccionada."));
}

void MainComponent::stopTransport()
{
    const bool wasRecording = engine.isRecording();

    // Regiones que había antes de parar (para saber cuáles son nuevas)
    std::map<Track*, size_t> before;
    if (wasRecording)
        for (auto* tc : trackComponents)
            before[&tc->getTrack()] = tc->getTrack().regions.size();

    engine.stop();

    if (wasRecording)
    {
        std::vector<std::pair<Track*, Region>> added;
        for (auto& [t, count] : before)
            for (size_t i = count; i < t->regions.size(); ++i)
                added.push_back ({ t, t->regions[i] });
        if (! added.empty())
            recordUndo (tr8 ("Grabación"),
                [this, added] { for (auto& [t, r] : added) engine.takeRegion (*t, r.id); },
                [this, added] { for (auto& [t, r] : added) engine.putRegion (*t, r, -1); });
    }

    if (wasRecording && engine.takeLostSamples())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr8 ("Se han perdido muestras"),
            tr8 ("El disco no ha podido guardar la grabación a tiempo y la toma puede tener cortes. "
                 "Cierra otros programas que usen mucho el disco, o graba en un disco más rápido."));

    // Aviso si alguna toma ha saturado la entrada
    if (wasRecording)
    {
        juce::StringArray clipped;
        for (auto& [t, peak] : engine.getLastTakePeaks())
            if (PeakReadout::isClip (peak) && componentFor (t) != nullptr)
                clipped.add ("\"" + t->name + "\"");
        if (! clipped.isEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr8 ("La grabación ha saturado"),
                tr8 ("La toma de ") + clipped.joinIntoString (", ")
                + tr8 (" ha llegado a 0 dBFS (CLIP), así que puede sonar distorsionada.\n\n"
                       "Baja la ganancia de entrada en tu interfaz de audio y vuelve a grabar. "
                       "Lo ideal es que los picos queden entre -12 y -6 dB."));
    }
    updateScrollBar();
    repaintTimeline();
}

void MainComponent::goToStart()
{
    if (engine.isRecording())
        return;
    engine.setPosition (0);
    timeline.viewStart = 0.0;
    updateScrollBar();
    repaintTimeline();
}

void MainComponent::zoom (double factor)
{
    timeline.pixelsPerSecond = juce::jlimit (5.0, 2000.0, timeline.pixelsPerSecond * factor);
    updateScrollBar();
    repaintTimeline();
}

bool MainComponent::handleKey (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (key == juce::KeyPress::homeKey || key == juce::KeyPress::returnKey) { goToStart(); return true; }
    const auto c = key.getTextCharacter();
    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();
    if (mods.isCommandDown())
    {
        const auto k = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) (c != 0 ? c : code));
        if (k == 'z' && mods.isShiftDown()) { redo(); return true; }
        if (k == 'z') { undo(); return true; }
        if (k == 'y') { redo(); return true; }
        if (k == 'd') { duplicateSelectedRegion(); return true; }
        if (k == 'i') { chooseFilesToImport(); return true; }
        if (k == 'n') { newProject(); return true; }
        if (k == 'o') { openProject(); return true; }
        if (k == 's' && mods.isShiftDown()) { saveProjectAs(); return true; }
        if (k == 's') { saveProject(); return true; }
        if (k == 'e' && mods.isShiftDown()) { exportMix(); return true; }
        return false;
    }
    if (c == 'e' || c == 'E') { toggleEditor(); return true; }
    if (c == 'r' || c == 'R') { toggleRecord(); return true; }
    if (c == 'x' || c == 'X') { toggleMixer(); return true; }
    if (c == 'k' || c == 'K') { toggleMetronome(); return true; }
    if (c == 'c' || c == 'C') { toggleCycle(); return true; }
    if (c == 'p' || c == 'P') { togglePunch(); return true; }
    if (c == 'm' || c == 'M') { addMarkerAtPlayhead(); return true; }
    if (c == 'a' || c == 'A') { toggleAutomationView(); return true; }
    if (c == 's' || c == 'S') { splitAtPlayhead(); return true; }
    if (c == ',')             { jumpToMarker (-1); return true; }
    if (c == '.')             { jumpToMarker (1); return true; }
    if (c == 't' || c == 'T') { toggleTuner(); return true; }
    if (c == '+')             { zoom (1.5); return true; }
    if (c == '-')             { zoom (1.0 / 1.5); return true; }
    return false;
}

// ===================== Línea de tiempo =====================

double MainComponent::getVisibleSeconds() const
{
    return juce::jmax (1.0, ruler.getWidth() / timeline.pixelsPerSecond);
}

void MainComponent::updateScrollBar()
{
    const double visible = getVisibleSeconds();
    const double total = juce::jmax (engine.getContentEndSeconds() + visible * 0.5, timeline.viewStart + visible);
    hScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
    hScroll.setCurrentRange (timeline.viewStart, visible, juce::dontSendNotification);
}

void MainComponent::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    timeline.viewStart = newRangeStart;
    repaintTimeline();
}

void MainComponent::repaintTimeline()
{
    ruler.repaint();
    for (auto* tc : trackComponents)
        tc->repaintLane();
}

void MainComponent::timerCallback()
{
    // ¿Algún efecto se ha desbocado? El motor ya lo ha silenciado y desactivado: avisamos
    {
        Track* t = nullptr;
        juce::AudioProcessor* p = nullptr;
        if (engine.takeRunawayInsert (t, p))
        {
            juce::String fx = "un efecto", where = "un canal";
            if (trackExists (t) || t == &engine.getMaster())
            {
                where = t == &engine.getMaster() ? juce::String ("Master") : t->name;
                for (auto& list : t->inserts)
                    for (auto& ins : list)
                        if (ins->processor.get() == p)
                            fx = juce::String (tr8 ("«")) + p->getName() + tr8 ("»");
            }
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr8 ("Efecto desactivado"),
                tr8 ("El efecto ") + fx + tr8 (" de ") + where
                + tr8 (" ha generado una señal descontrolada. Se ha silenciado y desactivado para proteger tus oídos y altavoces.\n\n"
                       "Puedes volver a activarlo en su ranura. Si vuelve a pasar, cuéntaselo a Claude con este mensaje."));
            repaint();
        }
    }

    const int st = engine.getState();
    const auto pos = engine.getPosition();

    playButton.setToggleState (st != AudioEngine::stopped, juce::dontSendNotification);
    mixerButton.setToggleState (mixerOpen, juce::dontSendNotification);
    metronomeButton.setToggleState (engine.metronomeOn.load(), juce::dontSendNotification);
    cycleButton.setToggleState (engine.loopOn.load() || engine.punchOn.load(), juce::dontSendNotification);
    tunerButton.setToggleState (tunerWindow != nullptr && tunerWindow->isVisible(), juce::dontSendNotification);
    recordButton.setToggleState (st == AudioEngine::recording, juce::dontSendNotification);

    updateMeters();
    watchForChanges();
    editorButton.setToggleState (editorVisible, juce::dontSendNotification);
    if (editorVisible && audioEditor != nullptr)
        audioEditor->tick();

    const double sr = engine.getSampleRate();
    const double seconds = engine.getPositionSeconds();
    juce::ignoreUnused (sr);
    lcd.setValues (positionText(), formatTime (seconds), timeline.bpm, timeline.sigNum, timeline.sigDen, st, engine.isInCountIn());

    if (st == AudioEngine::stopped && pos == lastDrawnPosition)
        return;

    lastDrawnPosition = pos;

    if (st != AudioEngine::stopped)
    {
        const double visible = getVisibleSeconds();
        if (seconds > timeline.viewStart + visible * 0.95 || seconds < timeline.viewStart)
            timeline.viewStart = juce::jmax (0.0, seconds - visible * 0.05);
    }

    updateScrollBar();
    repaintTimeline();
}

// ===================== Audio =====================

void MainComponent::showAudioSettings()
{
    if (engine.getState() != AudioEngine::stopped)
        stopTransport();

    auto* panel = new AudioSettingsPanel (deviceManager, engine, [this] { return getActiveInputNames(); },
                                          [this] { saveAppSettings(); });

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel);
    o.dialogTitle = tr8 ("Configuración de audio");
    o.dialogBackgroundColour = juce::Colour (0xff24272e);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &knownPlugins)
    {
        if (auto xml = knownPlugins.createXml())
            xml->writeTo (AppPaths::pluginList());
        return;
    }

    refreshDeviceInfo();
    auto names = getActiveInputNames();
    for (auto* tc : trackComponents)
        tc->setInputChannels (names);
    if (tunerWindow != nullptr)
        tunerWindow->getTuner().refreshInputs();
    saveAudioSettings();
}

void MainComponent::refreshDeviceInfo()
{
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const double sr = dev->getCurrentSampleRate();
        const int buffer = dev->getCurrentBufferSizeSamples();
        const double ms = sr > 0 ? 1000.0 * buffer / sr : 0.0;

        deviceInfo.setText (dev->getTypeName() + "  |  " + dev->getName()
                              + "  |  " + juce::String (sr, 0) + " Hz"
                              + "  |  buffer " + juce::String (buffer) + " (" + juce::String (ms, 1) + " ms)",
                            juce::dontSendNotification);
    }
    else
    {
        deviceInfo.setText ("Sin dispositivo de audio. Pulsa \"Audio\" para elegir uno.", juce::dontSendNotification);
    }
}

// ===================== Dibujo y disposición =====================

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Barra de control superior
    auto top = getLocalBounds().removeFromTop (toolbarHeight).toFloat();
    g.setGradientFill (juce::ColourGradient (Theme::panelLight.brighter (0.05f), 0, 0,
                                             Theme::panel.darker (0.1f), 0, top.getBottom(), false));
    g.fillRect (top);
    g.setColour (Theme::border);
    g.drawHorizontalLine ((int) top.getBottom() - 1, 0.0f, top.getRight());

    // Logotipo (o el nombre, si no se ha incluido la imagen)
    if (logo.isValid())
    {
        g.drawImageWithin (logo, logoArea.getX(), logoArea.getY(), logoArea.getWidth(), logoArea.getHeight(),
                           juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    }
    else
    {
        auto la = logoArea.toFloat();
        const juce::Font font (juce::FontOptions (17.0f, juce::Font::bold));
        const float w1 = juce::GlyphArrangement::getStringWidth (font, "Simple");
        const float w2 = juce::GlyphArrangement::getStringWidth (font, "DAW");
        const float x = la.getCentreX() - (w1 + w2) * 0.5f;
        g.setFont (font);
        g.setColour (Theme::text);
        g.drawText ("Simple", juce::Rectangle<float> (x, la.getY(), w1 + 2.0f, la.getHeight()), juce::Justification::centredLeft);
        g.setColour (Theme::accent);
        g.drawText ("DAW", juce::Rectangle<float> (x + w1, la.getY(), w2 + 4.0f, la.getHeight()), juce::Justification::centredLeft);
    }

    // Esquina sobre las cabeceras de pista, a la izquierda de la regla
    auto corner = juce::Rectangle<int> (viewport.getX(), ruler.getY(), Timeline::headerWidth, ruler.getHeight()).toFloat();
    g.setGradientFill (juce::ColourGradient (Theme::panelLight, 0, corner.getY(), Theme::panel, 0, corner.getBottom(), false));
    g.fillRect (corner);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("PISTAS (" + juce::String (trackComponents.size()) + ")", corner.reduced (12.0f, 0.0f).toNearestInt(),
                juce::Justification::centredLeft);
    g.setColour (Theme::border);
    g.drawRect (corner.getUnion (ruler.getBounds().toFloat()), 1.0f);

    // Fondo de la zona de pistas vacía
    auto tracksArea = viewport.getBounds().toFloat();
    g.setColour (Theme::panelDark);
    g.fillRect (tracksArea.withWidth ((float) Timeline::headerWidth));
    g.setColour (Theme::lane.darker (0.15f));
    g.fillRect (tracksArea.withTrimmedLeft ((float) Timeline::headerWidth));

    // Separaciones entre el master, el mezclador y las pistas
    g.setColour (Theme::border);
    g.fillRect (masterStrip->getRight(), masterStrip->getY(), 3, masterStrip->getHeight());
    if (mixer != nullptr && mixer->isVisible())
        g.fillRect (mixer->getRight(), mixer->getY(), 3, mixer->getHeight());

    // Barra de estado inferior
    auto status = getLocalBounds().removeFromBottom (statusHeight).toFloat();
    g.setColour (Theme::panelDark);
    g.fillRect (status);
    g.setColour (Theme::border);
    g.drawHorizontalLine ((int) status.getY(), 0.0f, status.getRight());
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop (toolbarHeight).reduced (12, 10);
    auto status = area.removeFromBottom (statusHeight);
    deviceInfo.setBounds (status.reduced (10, 0));

    // Logotipo encima del canal master
    logoArea = toolbar.removeFromLeft (ChannelStrip::stripWidth - 10);
    toolbar.removeFromLeft (14);

    // Izquierda: pistas, mezclador, audio y plugins
    addTrackButton.setBounds (toolbar.removeFromLeft (84));
    toolbar.removeFromLeft (6);
    mixerButton.setBounds (toolbar.removeFromLeft (122));
    toolbar.removeFromLeft (6);
    audioButton.setBounds (toolbar.removeFromLeft (40));
    toolbar.removeFromLeft (6);
    pluginsButton.setBounds (toolbar.removeFromLeft (40));

    // Derecha: afinador, editor e importar
    tunerButton.setBounds (toolbar.removeFromRight (112));
    toolbar.removeFromRight (6);
    editorButton.setBounds (toolbar.removeFromRight (92));
    toolbar.removeFromRight (6);
    importButton.setBounds (toolbar.removeFromRight (40));
    toolbar.removeFromRight (10);
    resourceMeter.setBounds (toolbar.removeFromRight (150).withSizeKeepingCentre (150, 34));

    // Centro: transporte, metrónomo y pantalla
    const int bw = 44, gap = 4;
    const int lcdW = juce::jlimit (300, 390, toolbar.getWidth() - (6 * bw + 5 * gap + 12) - 20);
    const int groupW = 6 * bw + 5 * gap + 12 + lcdW;
    auto group = toolbar.withSizeKeepingCentre (juce::jmin (groupW, toolbar.getWidth()), toolbar.getHeight());
    toStartButton.setBounds   (group.removeFromLeft (bw)); group.removeFromLeft (gap);
    stopButton.setBounds      (group.removeFromLeft (bw)); group.removeFromLeft (gap);
    playButton.setBounds      (group.removeFromLeft (bw)); group.removeFromLeft (gap);
    recordButton.setBounds    (group.removeFromLeft (bw)); group.removeFromLeft (gap);
    cycleButton.setBounds     (group.removeFromLeft (bw)); group.removeFromLeft (gap);
    metronomeButton.setBounds (group.removeFromLeft (bw)); group.removeFromLeft (12);
    lcd.setBounds (group);

    masterStrip->setBounds (area.removeFromLeft (ChannelStrip::stripWidth));
    area.removeFromLeft (3);

    // Mezclador desplegable junto al master (deja siempre sitio para las pistas)
    if (mixerReveal > 0.0f && mixer != nullptr)
    {
        const int full = juce::jmin (mixer->getPreferredWidth(), juce::jmax (ChannelStrip::stripWidth, area.getWidth() - 480));
        const float e = mixerReveal < 0.5f ? 2.0f * mixerReveal * mixerReveal
                                           : 1.0f - std::pow (-2.0f * mixerReveal + 2.0f, 2.0f) * 0.5f;
        const int w = juce::jmax (1, juce::roundToInt (full * e));
        mixer->setVisible (true);
        mixer->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (3);
    }
    else if (mixer != nullptr)
    {
        mixer->setVisible (false);
    }

    if (editorVisible)
    {
        editorHeight = juce::jlimit (150, juce::jmax (150, area.getHeight() - 160), editorHeight);
        audioEditor->setBounds (area.removeFromBottom (editorHeight));
        editorDragBar.setBounds (area.removeFromBottom (5));
    }

    auto rulerRow  = area.removeFromTop (Ruler::bandHeight + Ruler::markerRowHeight + 24);
    auto scrollRow = area.removeFromBottom (18);

    viewport.setBounds (area);
    layoutTracks();

    const int laneX = viewport.getX() + Timeline::headerWidth;
    const int laneW = juce::jmax (10, viewport.getMaximumVisibleWidth() - Timeline::headerWidth);
    ruler.setBounds (laneX, rulerRow.getY(), laneW, rulerRow.getHeight());
    hScroll.setBounds (laneX, scrollRow.getY() + 3, juce::jmax (10, laneW - 64), 12);
    zoomOutButton.setBounds (laneX + laneW - 58, scrollRow.getY() + 1, 26, 16);
    zoomInButton.setBounds  (laneX + laneW - 28, scrollRow.getY() + 1, 26, 16);

    updateScrollBar();
    repaint();
}

// ===================== Mezclador, efectos y plugins =====================

void MainComponent::updateMixerTracks()
{
    juce::Array<Track*> list;
    for (auto* tc : trackComponents)
        list.add (&tc->getTrack());
    if (mixer != nullptr)
        mixer->setTracks (list, activeBuses());
    resized();
}

bool MainComponent::trackExists (const Track* t) const
{
    if (t == &engine.getMaster())
        return true;
    if (engine.getBusSlot (t) >= 0)
        return true;
    for (auto* tc : trackComponents)
        if (&tc->getTrack() == t)
            return true;
    return false;
}

void MainComponent::toggleMixer()
{
    mixerOpen = ! mixerOpen;
    mixerAnimation.startTimerHz (60);
}

void MainComponent::animateMixer()
{
    // Despliegue/repliegue en ~0,22 s
    const float step = 1.0f / (0.22f * 60.0f);
    mixerReveal = juce::jlimit (0.0f, 1.0f, mixerReveal + (mixerOpen ? step : -step));
    if ((mixerOpen && mixerReveal >= 1.0f) || (! mixerOpen && mixerReveal <= 0.0f))
        mixerAnimation.stopTimer();
    resized();
}

void MainComponent::showPluginManager()
{
    auto* list = new juce::PluginListComponent (pluginFormats, knownPlugins, AppPaths::deadMansPedal(), nullptr);
    list->setOptionsButtonText ("Opciones / buscar...");
    list->setSize (720, 480);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list);
   #if JUCE_MAC
    o.dialogTitle = "Plugins (VST3 / AU)";
   #else
    o.dialogTitle = "Plugins (VST3)";
   #endif
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();

    if (knownPlugins.getNumTypes() == 0)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Buscar plugins",
            tr8 ("Pulsa \"Opciones / buscar...\" y elige \"Buscar nuevos plugins\" para cada formato. "
                 "SimpleDAW encontrará los plugins instalados (AmpliTube, Guitar Rig, etc.).\n\n"
                 "La búsqueda puede tardar unos minutos la primera vez."));
}

void MainComponent::showInsertMenu (Track& track, Section section, int slot, juce::Component& target)
{
    juce::PopupMenu menu;
    const auto types = knownPlugins.getTypes();
    static constexpr int scanId = 1, firstEffectId = 100;

    if (section == Section::effects)
    {
        menu.addSectionHeader ("Efectos de SimpleDAW");
        const auto names = BuiltInEffects::names();
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (firstEffectId + i, names[i]);
    }
    else
    {
        menu.addSectionHeader ("Plugins");
        if (types.isEmpty())
            menu.addItem (-1, tr8 ("No hay plugins todavía"), false);
        else
            juce::KnownPluginList::addToMenu (menu, types, juce::KnownPluginList::sortByManufacturer);
        menu.addSeparator();
        menu.addItem (scanId, "Buscar plugins...");
    }

    juce::Component::SafePointer<MainComponent> self (this);
    Track* trackPtr = &track;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
        [self, trackPtr, section, slot, types] (int result)
        {
            if (self == nullptr || result == 0 || ! self->trackExists (trackPtr))
                return;

            if (result == scanId)
            {
                self->showPluginManager();
                return;
            }

            if (section == Section::effects && result >= firstEffectId)
            {
                if (self->engine.addInsert (*trackPtr, Section::effects, BuiltInEffects::create (result - firstEffectId), false, slot))
                {
                    self->recordInsertAdded (*trackPtr, Section::effects, slot);
                    self->openInsertEditor (*trackPtr, Section::effects, slot);
                }
                return;
            }

            const int index = juce::KnownPluginList::getIndexChosenByMenu (types, result);
            if (index >= 0)
                self->loadExternalPlugin (*trackPtr, slot, types[index]);
        });
}

void MainComponent::loadExternalPlugin (Track& track, int slot, const juce::PluginDescription& desc)
{
    juce::Component::SafePointer<MainComponent> self (this);
    Track* trackPtr = &track;

    const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
    const int bs = engine.getBlockSize() > 0 ? engine.getBlockSize() : 512;

    pluginFormats.createPluginInstanceAsync (desc, sr, bs,
        [self, trackPtr, slot, desc] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (self == nullptr || ! self->trackExists (trackPtr))
                return;

            if (instance == nullptr)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "No se pudo cargar el plugin",
                                                        desc.name + ":\n" + error);
                return;
            }

            preferStereoLayout (*instance);

            if (self->engine.addInsert (*trackPtr, Section::plugins, std::move (instance), true, slot))
            {
                self->recordInsertAdded (*trackPtr, Section::plugins, slot);
                self->openInsertEditor (*trackPtr, Section::plugins, slot);
            }
        });
}

void MainComponent::showInsertOptions (Track& track, Section section, int slot, juce::Component& target)
{
    auto& list = track.getInserts (section);
    if (slot < 0 || slot >= (int) list.size())
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader (list[(size_t) slot]->processor->getName());
    menu.addItem (1, "Abrir");
    menu.addItem (2, "Bypass", true, list[(size_t) slot]->bypass.load());
    menu.addSeparator();
    menu.addItem (3, "Quitar");

    juce::Component::SafePointer<MainComponent> self (this);
    Track* trackPtr = &track;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
        [self, trackPtr, section, slot] (int result)
        {
            if (self == nullptr || result == 0 || ! self->trackExists (trackPtr))
                return;

            auto& inserts = trackPtr->getInserts (section);
            if (slot >= (int) inserts.size())
                return;

            if (result == 1) self->openInsertEditor (*trackPtr, section, slot);
            if (result == 2)
            {
                auto* ins = inserts[(size_t) slot].get();
                const bool before = ins->bypass.load();
                ins->bypass = ! before;
                self->recordUndo ("Bypass", [ins, before] { ins->bypass = before; }, [ins, before] { ins->bypass = ! before; });
            }
            if (result == 3) self->removeInsert (*trackPtr, section, slot);
        });
}

void MainComponent::openInsertEditor (Track& track, Section section, int slot)
{
    auto& list = track.getInserts (section);
    if (slot < 0 || slot >= (int) list.size())
        return;

    auto& processor = *list[(size_t) slot]->processor;

    for (auto* w : editorWindows)
    {
        if (&w->getProcessor() == &processor)
        {
            w->setVisible (true);
            w->toFront (true);
            return;
        }
    }

    juce::Component::SafePointer<MainComponent> self (this);
    editorWindows.add (new PluginEditorWindow (processor, track.name + tr8 (" · ") + processor.getName(),
        [self] (PluginEditorWindow* w)
        {
            // Se borra de forma asíncrona: estamos dentro de un evento de la propia ventana
            juce::MessageManager::callAsync ([self, w]
            {
                if (self != nullptr)
                    self->editorWindows.removeObject (w);
            });
        }));

    // El frontal del ampli: sus presets pueden traer también reverb, delay... para esta pista
    if (auto* ampEditor = dynamic_cast<AmpEditor*> (editorWindows.getLast()->getContentComponent()))
    {
        Track* t = &track;
        juce::AudioProcessor* amp = &processor;
        ampEditor->applyPresetHook = [self, t, amp] (const juce::XmlElement& preset)
        {
            return self != nullptr && self->trackExists (t) ? self->applyAmpPreset (*t, *amp, preset) : -1;
        };
        ampEditor->capturePresetHook = [self, t, amp] (bool includeEffects, const juce::String& name)
        {
            return Presets::capture (self != nullptr && self->trackExists (t) ? t : nullptr, *amp, includeEffects, name);
        };
        ampEditor->hasOtherEffects = [self, t, amp]
        {
            if (self == nullptr || ! self->trackExists (t)) return false;
            const auto where = Presets::locate (*t, *amp);
            if (where.first < 0) return false;
            for (auto& ins : t->getInserts ((Section) where.first))
                if (! ins->isExternalPlugin && ins->processor.get() != amp) return true;
            return false;
        };
    }
}

int MainComponent::applyAmpPreset (Track& track, juce::AudioProcessor& amp, const juce::XmlElement& preset)
{
    juce::String beforeName;
    if (auto* a = dynamic_cast<AmpEffect*> (&amp)) beforeName = a->presetName;
    auto before = std::shared_ptr<juce::XmlElement> (Presets::capture (&track, amp, true, beforeName).release());
    auto after = std::make_shared<juce::XmlElement> (preset);

    auto apply = [this] (Track& t, juce::AudioProcessor& a, const juce::XmlElement& x)
    {
        const int skipped = Presets::applyChain (engine, t, a, x, [this] (juce::AudioProcessor& p) { closeEditorFor (p); });
        updateMixerTracks();
        repaint();
        return skipped;
    };
    const int skipped = apply (track, amp, *after);

    Track* t = &track;
    juce::AudioProcessor* a = &amp;
    juce::Component::SafePointer<MainComponent> self (this);
    recordUndo ("Preset " + preset.getStringAttribute ("name"),
        [self, t, a, before, apply] { if (self != nullptr && self->trackExists (t)) apply (*t, *a, *before); },
        [self, t, a, after, apply]  { if (self != nullptr && self->trackExists (t)) apply (*t, *a, *after); });
    return skipped;
}

void MainComponent::closeEditorFor (juce::AudioProcessor& processor)
{
    for (int i = editorWindows.size(); --i >= 0;)
        if (&editorWindows[i]->getProcessor() == &processor)
            editorWindows.remove (i);
}

void MainComponent::closeEditorsForTrack (Track& track)
{
    for (auto& list : track.inserts)
        for (auto& ins : list)
            closeEditorFor (*ins->processor);
}

void MainComponent::removeInsert (Track& track, Section section, int slot)
{
    auto& list = track.getInserts (section);
    if (slot < 0 || slot >= (int) list.size())
        return;

    closeEditorFor (*list[(size_t) slot]->processor);   // el editor siempre antes que el procesador
    // No se destruye: queda en el historial por si se deshace
    auto holder = std::make_shared<std::unique_ptr<Insert>> (engine.detachInsert (track, section, slot));
    const auto name = (*holder)->processor->getName();
    Track* t = &track;
    juce::Component::SafePointer<MainComponent> self (this);
    recordUndo ("Quitar " + name,
        [self, t, section, slot, holder] { if (self != nullptr) self->engine.attachInsert (*t, section, slot, std::move (*holder)); },
        [self, t, section, slot, holder]
        {
            if (self == nullptr) return;
            auto& l = t->getInserts (section);
            if (slot < (int) l.size()) self->closeEditorFor (*l[(size_t) slot]->processor);
            *holder = self->engine.detachInsert (*t, section, slot);
        });
}

void MainComponent::recordInsertAdded (Track& track, Section section, int slot)
{
    auto& list = track.getInserts (section);
    slot = juce::jlimit (0, (int) list.size() - 1, slot);
    const auto name = list[(size_t) slot]->processor->getName();
    auto holder = std::make_shared<std::unique_ptr<Insert>>();
    Track* t = &track;
    juce::Component::SafePointer<MainComponent> self (this);
    recordUndo (tr8 ("Añadir ") + name,
        [self, t, section, slot, holder]
        {
            if (self == nullptr) return;
            auto& l = t->getInserts (section);
            if (slot < (int) l.size()) self->closeEditorFor (*l[(size_t) slot]->processor);
            *holder = self->engine.detachInsert (*t, section, slot);
        },
        [self, t, section, slot, holder] { if (self != nullptr) self->engine.attachInsert (*t, section, slot, std::move (*holder)); });
}

void MainComponent::updateMeters()
{
    // Un único lector de los picos del motor; el master se muestra en dos sitios a la vez
    auto read = [] (Track& t)
    {
        t.meterL = t.outPeakL.exchange (0.0f);
        t.meterR = t.outPeakR.exchange (0.0f);
        t.outputHold = juce::jmax (t.outputHold, t.meterL, t.meterR);
    };
    for (auto* tc : trackComponents)
        read (tc->getTrack());
    read (engine.getMaster());
    for (auto& b : activeBuses())
        read (*b.second);

    masterStrip->refresh();
    if (mixerReveal > 0.0f)
        mixer->refresh();
}

void MainComponent::showInsertPanel (Track& track, juce::Component& target)
{
    auto panel = std::make_unique<InsertPanel> (*this, track);
    juce::CallOutBox::launchAsynchronously (std::move (panel), target.getScreenBounds(), nullptr);
}

// ===================== Selección, editor e importación =====================

TrackComponent* MainComponent::componentFor (const Track* t) const
{
    for (auto* tc : trackComponents)
        if (&tc->getTrack() == t)
            return tc;
    return nullptr;
}

void MainComponent::selectRegion (Track& track, juce::int64 regionId)
{
    // Si solo se selecciona la pista, el editor muestra la región bajo el cursor (o la primera)
    juce::int64 shown = regionId;
    if (shown < 0 && ! track.regions.empty())
    {
        shown = track.regions.front().id;
        const auto pos = engine.getPosition();
        for (auto& r : track.regions)
            if (pos >= r.start && pos < r.getEnd()) { shown = r.id; break; }
    }

    selectedTrack = &track;
    selectedRegion = regionId;

    for (auto* tc : trackComponents)
        tc->setSelection (&tc->getTrack() == &track, &tc->getTrack() == &track ? regionId : -1);

    if (audioEditor != nullptr)
        audioEditor->setRegion (&track, shown);
}

void MainComponent::openAudioEditor()
{
    setEditorVisible (true);
}

void MainComponent::changeRegionSpeed (Track& track, juce::int64 regionId)
{
    if (regionId >= 0)
        selectRegion (track, regionId);
    setEditorVisible (true);
    audioEditor->showSpeedDialog();
}

void MainComponent::transposeRegion (Track& track, juce::int64 regionId)
{
    if (regionId >= 0)
        selectRegion (track, regionId);
    setEditorVisible (true);
    audioEditor->showTransposeDialog();
}

void MainComponent::toggleEditor()
{
    setEditorVisible (! editorVisible);
}

void MainComponent::setEditorVisible (bool show)
{
    editorVisible = show;
    audioEditor->setVisible (show);
    editorDragBar.setVisible (show);
    resized();
    if (show)
        audioEditor->grabKeyboardFocus();
    else
        grabKeyboardFocus();
}

void MainComponent::regionsChanged()
{
    if (audioEditor != nullptr)
        audioEditor->refresh();
    updateScrollBar();
    repaintTimeline();
}

void MainComponent::chooseFilesToImport()
{
    fileChooser = std::make_unique<juce::FileChooser> (tr8 ("Importar audio"),
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       engine.getSupportedWildcards());

    juce::Component::SafePointer<MainComponent> self (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::canSelectMultipleItems,
        [self] (const juce::FileChooser& fc)
        {
            if (self == nullptr)
                return;
            juce::StringArray paths;
            for (auto& f : fc.getResults())
                paths.add (f.getFullPathName());
            if (! paths.isEmpty())
                self->importFiles (paths, self->selectedTrack, self->engine.getPosition());
        });
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (engine.canRead (juce::File (f)))
            return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    importFiles (files, nullptr, 0);   // fuera de una pista: pistas nuevas desde el principio
}

void MainComponent::importFiles (const juce::StringArray& files, Track* target, juce::int64 position)
{
    if (engine.isRecording())
        return;

    juce::StringArray valid;
    bool anyStereo = false;
    for (auto& path : files)
    {
        const juce::File f (path);
        if (! engine.canRead (f))
            continue;
        valid.add (path);
        anyStereo = anyStereo || engine.getFileChannels (f) > 1;
    }

    if (valid.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Importar audio",
                                                tr8 ("Ese formato no se puede importar. Usa wav, aiff, mp3, flac u ogg."));
        return;
    }

    if (! anyStereo)
    {
        doImport (valid, target, position, true);
        return;
    }

    // Archivo estéreo: preguntar cómo importarlo
    auto* aw = new juce::AlertWindow (tr8 ("Importar audio"),
                                      valid.size() == 1 ? tr8 ("\"") + juce::File (valid[0]).getFileName() + tr8 ("\" es estéreo. ¿Cómo quieres importarlo?")
                                                        : tr8 ("Algunos archivos son estéreo. ¿Cómo quieres importarlos?"),
                                      juce::MessageBoxIconType::QuestionIcon);
    aw->addButton (tr8 ("Estéreo"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton (tr8 ("Mono (mezclar canales)"), 2);
    aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> self (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([self, valid, target, position] (int result)
    {
        if (self != nullptr && result != 0)
            self->doImport (valid, target, position, result == 2);
    }), true);
}

void MainComponent::doImport (const juce::StringArray& files, Track* target, juce::int64 position, bool forceMono)
{
    undoManager.beginNewTransaction (tr8 ("Importar audio"));
    ++groupingUndo;
    const juce::ScopeGuard endGroup { [this] { --groupingUndo; } };
    // La pista de destino podría haberse borrado mientras el diálogo estaba abierto
    if (target != nullptr && componentFor (target) == nullptr)
        target = nullptr;

    juce::MouseCursor::showWaitCursor();
    juce::StringArray errors;
    Track* lastTrack = nullptr;
    juce::int64 lastRegion = -1;

    for (int i = 0; i < files.size(); ++i)
    {
        const juce::File f (files[i]);
        juce::String error;
        auto clip = engine.loadAudioFile (f, forceMono, error);
        if (clip == nullptr)
        {
            errors.add (f.getFileName() + ": " + error);
            continue;
        }

        // El primer archivo va a la pista indicada; los demás, a pistas nuevas (stems, backing tracks...)
        Track* t = (i == 0 && target != nullptr) ? target : nullptr;
        if (t == nullptr)
        {
            addTrackWithUndo();
            t = &trackComponents.getLast()->getTrack();
            t->name = f.getFileNameWithoutExtension();
        }
        lastRegion = engine.addRegion (*t, clip, position);
        {
            const auto taken = engine.takeRegion (*t, lastRegion);   // copia para poder rehacer
            const Region region = taken.first;
            engine.putRegion (*t, region, taken.second);
            recordUndo ("Importar", [this, t, id = lastRegion] { engine.takeRegion (*t, id); },
                                    [this, t, region] { engine.putRegion (*t, region, -1); });
        }
        lastTrack = t;
    }
    juce::MouseCursor::hideWaitCursor();

    if (lastTrack != nullptr)
        selectRegion (*lastTrack, lastRegion);
    regionsChanged();

    if (! errors.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr8 ("No se pudo importar"),
                                                errors.joinIntoString ("\n"));
}

// ===================== Proyectos =====================
//
// Un proyecto es una carpeta:   Mi canción/
//                                 Mi canción.sdaw   (XML: pistas, regiones, mezcla, efectos, plugins)
//                                 Audio/            (el audio de cada región, en WAV de 32 bits)

namespace
{
    const juce::String projectExtension = ".sdaw";

    juce::File defaultProjectsFolder()
    {
        auto f = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SimpleDAW").getChildFile ("Proyectos");
        f.createDirectory();
        return f;
    }
}

void MainComponent::trackParamsToXml (const Track& t, juce::XmlElement& x)
{
    x.setAttribute ("name", t.name);
    x.setAttribute ("colour", t.colour.toString());
    x.setAttribute ("input", t.inputChannel.load());
    x.setAttribute ("volume", (double) t.volumeDb.load());
    x.setAttribute ("pan", (double) t.pan.load());
    x.setAttribute ("mute", t.mute.load());
    x.setAttribute ("solo", t.solo.load());
    x.setAttribute ("monitor", t.monitor.load());
    x.setAttribute ("armed", t.armed.load());
    for (int b = 0; b < Track::maxBuses; ++b)
        if (t.send[b].load() > 0.0f)
            x.setAttribute ("send" + juce::String (b), (double) t.send[b].load());

    if (t.isDrum())
    {
        x.setAttribute ("kind", "drum");
        drumToXml (*t.drum, *x.createNewChildElement ("DRUM"));
    }

    for (int p = 0; p < 2; ++p)
    {
        if (t.automation[p].empty())
            continue;
        auto* ax = x.createNewChildElement ("AUTOMATION");
        ax->setAttribute ("param", p == Track::autoVolume ? "volume" : "pan");
        ax->setAttribute ("on", t.automationOn[p].load());
        for (auto& pt : t.automation[p])
        {
            auto* px = ax->createNewChildElement ("P");
            px->setAttribute ("pos", juce::String (pt.pos));
            px->setAttribute ("v", (double) pt.value);
        }
    }
}

void MainComponent::trackParamsFromXml (Track& t, const juce::XmlElement& x)
{
    if (! t.isMaster)
    {
        t.name = x.getStringAttribute ("name", t.name);
        if (x.hasAttribute ("colour"))
            t.colour = juce::Colour::fromString (x.getStringAttribute ("colour"));
        t.inputChannel = x.getIntAttribute ("input", 0);
        t.solo    = x.getBoolAttribute ("solo");
        t.monitor = x.getBoolAttribute ("monitor");
        t.armed   = x.getBoolAttribute ("armed");
    }
    t.volumeDb = (float) x.getDoubleAttribute ("volume", 0.0);
    t.pan      = (float) x.getDoubleAttribute ("pan", 0.0);
    t.mute     = x.getBoolAttribute ("mute");
    for (int b = 0; b < Track::maxBuses; ++b)
        t.send[b] = (float) x.getDoubleAttribute ("send" + juce::String (b), 0.0);

    for (auto& list : t.automation) list.clear();
    for (auto* ax : x.getChildWithTagNameIterator ("AUTOMATION"))
    {
        const int p = ax->getStringAttribute ("param") == "pan" ? Track::autoPan : Track::autoVolume;
        t.automationOn[p] = ax->getBoolAttribute ("on", true);
        for (auto* px : ax->getChildWithTagNameIterator ("P"))
            t.automation[p].push_back ({ px->getStringAttribute ("pos").getLargeIntValue(), (float) px->getDoubleAttribute ("v") });
        std::sort (t.automation[p].begin(), t.automation[p].end(), [] (auto& a, auto& b) { return a.pos < b.pos; });
    }
}

void MainComponent::preferStereoLayout (juce::AudioPluginInstance& instance)
{
    // Preferimos estéreo de entrada y salida; si no lo admite, mono -> estéreo
    auto layout = instance.getBusesLayout();
    if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0)  = juce::AudioChannelSet::stereo();
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! instance.setBusesLayout (layout) && ! layout.inputBuses.isEmpty())
    {
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        instance.setBusesLayout (layout);
    }
}

void MainComponent::insertsToXml (Track& t, juce::XmlElement& parent)
{
    for (int sec = 0; sec < 2; ++sec)
    {
        for (auto& ins : t.inserts[sec])
        {
            auto* ix = parent.createNewChildElement ("INSERT");
            ix->setAttribute ("section", sec == (int) Section::plugins ? "plugins" : "effects");
            ix->setAttribute ("bypass", ins->bypass.load());

            juce::MemoryBlock state;
            ins->processor->getStateInformation (state);
            ix->setAttribute ("state", state.toBase64Encoding());

            auto* plugin = dynamic_cast<juce::AudioPluginInstance*> (ins->processor.get());
            if (ins->isExternalPlugin && plugin != nullptr)
            {
                ix->setAttribute ("kind", "plugin");
                ix->addChildElement (plugin->getPluginDescription().createXml().release());
            }
            else
            {
                ix->setAttribute ("kind", "builtin");
                ix->setAttribute ("name", ins->processor->getName());
            }
        }
    }
}

void MainComponent::insertsFromXml (Track& t, const juce::XmlElement& parent, juce::StringArray& warnings)
{
    const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
    const int bs = engine.getBlockSize() > 0 ? engine.getBlockSize() : 512;

    for (auto* ix : parent.getChildWithTagNameIterator ("INSERT"))
    {
        const auto section = ix->getStringAttribute ("section") == "plugins" ? Section::plugins : Section::effects;
        std::unique_ptr<juce::AudioProcessor> processor;
        bool external = false;

        if (ix->getStringAttribute ("kind") == "plugin")
        {
            juce::PluginDescription desc;
            auto* px = ix->getChildByName ("PLUGIN");
            if (px == nullptr || ! desc.loadFromXml (*px))
                continue;

            juce::String error;
            if (auto instance = pluginFormats.createPluginInstance (desc, sr, bs, error))
            {
                preferStereoLayout (*instance);
                processor = std::move (instance);
                external = true;
            }
            else
            {
                warnings.add (tr8 ("Plugin no disponible: ") + desc.name + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));
                continue;
            }
        }
        else
        {
            processor = BuiltInEffects::createByName (ix->getStringAttribute ("name"));
            if (processor == nullptr)
                continue;
        }

        juce::MemoryBlock state;
        if (state.fromBase64Encoding (ix->getStringAttribute ("state")) && state.getSize() > 0)
            processor->setStateInformation (state.getData(), (int) state.getSize());

        if (engine.addInsert (t, section, std::move (processor), external))
            t.getInserts (section).back()->bypass = ix->getBoolAttribute ("bypass");
    }
}

std::unique_ptr<juce::XmlElement> MainComponent::createProjectXml (const juce::File* audioFolder, juce::StringArray& errors, bool forRecovery)
{
    // audioFolder == nullptr: solo una "foto" del estado para detectar cambios (no escribe nada)
    auto root = std::make_unique<juce::XmlElement> ("SIMPLEDAW_PROJECT");
    root->setAttribute ("version", 1);
    const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
    root->setAttribute ("sampleRate", sr);
    root->setAttribute ("bpm", timeline.bpm);
    root->setAttribute ("sigNum", timeline.sigNum);
    root->setAttribute ("sigDen", timeline.sigDen);
    root->setAttribute ("metronome", engine.metronomeOn.load());
    root->setAttribute ("countIn", engine.countInOn.load());
    root->setAttribute ("metronomeVolume", (double) engine.metronomeVolume.load());
    root->setAttribute ("loopStart", juce::String (engine.loopStart.load()));
    root->setAttribute ("loopEnd", juce::String (engine.loopEnd.load()));
    root->setAttribute ("loopOn", engine.loopOn.load());
    root->setAttribute ("punchOn", engine.punchOn.load());
    for (auto& mk : timeline.markers)
    {
        auto* mx = root->createNewChildElement ("MARKER");
        mx->setAttribute ("pos", juce::String (mk.pos));
        mx->setAttribute ("name", mk.name);
    }

    auto* mx = root->createNewChildElement ("MASTER");
    trackParamsToXml (engine.getMaster(), *mx);
    insertsToXml (engine.getMaster(), *mx);

    root->setAttribute ("busesSaved", true);   // aunque no haya ninguno (se han quitado todos)
    for (auto& [slot, bus] : activeBuses())
    {
        auto* bx = root->createNewChildElement ("BUS");
        bx->setAttribute ("index", slot);
        trackParamsToXml (*bus, *bx);
        insertsToXml (*bus, *bx);
    }

    for (auto* tc : trackComponents)
    {
        auto& t = tc->getTrack();
        auto* tx = root->createNewChildElement ("TRACK");
        trackParamsToXml (t, *tx);
        insertsToXml (t, *tx);

        for (auto& r : t.regions)
        {
            auto* rx = tx->createNewChildElement ("REGION");
            rx->setAttribute ("start", juce::String (r.start));
            rx->setAttribute ("name", r.clip->name);

            // Cada clip se escribe una sola vez; si ya está en la carpeta del proyecto, se reutiliza
            auto writeClip = [&] (Clip& clip) -> juce::String
            {
                if (audioFolder == nullptr)
                    return juce::String::toHexString ((juce::pointer_sized_int) &clip);   // solo para detectar cambios

                // Recuperación: se apunta a los archivos que ya existen; solo se escribe lo que no está en disco
                if (forRecovery)
                {
                    if (clip.savedAs.existsAsFile())    return "abs:" + clip.savedAs.getFullPathName();
                    if (clip.recoveryAs.existsAsFile()) return "abs:" + clip.recoveryAs.getFullPathName();
                }

                juce::File f = clip.savedAs;
                if (! (f.existsAsFile() && f.isAChildOf (*audioFolder)))
                {
                    f = audioFolder->getNonexistentChildFile (juce::File::createLegalFileName (clip.name.isNotEmpty() ? clip.name : "audio"),
                                                              ".wav", false);
                    bool written = false;
                    std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
                    if (stream != nullptr)
                    {
                        juce::WavAudioFormat wav;
                        const auto options = juce::AudioFormatWriterOptions().withSampleRate (sr)
                                                                             .withNumChannels (clip.getNumChannels())
                                                                             .withBitsPerSample (32)
                                                                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
                        if (auto writer = wav.createWriterFor (stream, options))
                            written = writer->writeFromAudioSampleBuffer (clip.audio, 0, clip.audio.getNumSamples());
                    }
                    if (! written)
                    {
                        errors.add (tr8 ("No se pudo guardar el audio de \"") + clip.name + "\"");
                        return {};
                    }
                    if (forRecovery)
                    {
                        clip.recoveryAs = f;
                        return "abs:" + f.getFullPathName();
                    }
                    clip.savedAs = f;
                }
                return f.getFileName();   // siempre dentro de la carpeta Audio
            };

            if (r.takes.size() > 1)
            {
                rx->setAttribute ("activeTake", r.activeTake);
                for (auto& take : r.takes)
                    rx->createNewChildElement ("TAKE")->setAttribute ("file", writeClip (*take));
                rx->setAttribute ("name", r.clip->name);
            }
            else
            {
                rx->setAttribute ("file", writeClip (*r.clip));
            }
        }
    }
    return root;
}

bool MainComponent::writeProject (const juce::File& file)
{
    if (engine.isRecording())
        return false;

    auto audioFolder = file.getParentDirectory().getChildFile ("Audio");
    audioFolder.createDirectory();

    juce::MouseCursor::showWaitCursor();
    juce::StringArray errors;
    auto xml = createProjectXml (&audioFolder, errors);

    // Escritura segura: primero a un temporal, luego se sustituye el archivo
    juce::TemporaryFile temp (file);
    if (! xml->writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
        errors.add (tr8 ("No se pudo escribir el archivo del proyecto."));
    juce::MouseCursor::hideWaitCursor();

    if (! errors.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr8 ("Error al guardar"),
                                                errors.joinIntoString ("\n"));
        return false;
    }

    projectFile = file;
    markSaved();
    updateWindowTitle();
    clearRecovery();
    return true;
}

void MainComponent::saveProject (std::function<void (bool)> done)
{
    if (projectFile == juce::File())
    {
        saveProjectAs (std::move (done));
        return;
    }
    const bool ok = writeProject (projectFile);
    if (done) done (ok);
}

void MainComponent::saveProjectAs (std::function<void (bool)> done)
{
    if (engine.isRecording())
        return;

    const auto start = projectFile != juce::File() ? projectFile : defaultProjectsFolder().getChildFile (tr8 ("Mi canción") + projectExtension);
    fileChooser = std::make_unique<juce::FileChooser> (tr8 ("Guardar proyecto"), start, "*" + projectExtension);

    juce::Component::SafePointer<MainComponent> self (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
        [self, done] (const juce::FileChooser& fc)
        {
            if (self == nullptr)
                return;

            auto f = fc.getResult();
            if (f == juce::File())
            {
                if (done) done (false);
                return;
            }

            f = f.withFileExtension (projectExtension);

            // Cada proyecto en su propia carpeta (con el audio dentro), salvo que ya lo esté
            const auto name = f.getFileNameWithoutExtension();
            if (f.getParentDirectory().getFileName() != name)
            {
                auto folder = f.getParentDirectory().getChildFile (name);
                folder.createDirectory();
                f = folder.getChildFile (name + projectExtension);
            }

            const bool ok = self->writeProject (f);
            if (done) done (ok);
        });
}

void MainComponent::clearProject()
{
    engine.stop();
    undoManager.clearUndoHistory();   // libera pistas y plugins "aparcados" antes de vaciar el proyecto
    drumEditorWindow = nullptr;
    editorWindows.clear();   // primero las ventanas de plugins

    selectedTrack = nullptr;
    selectedRegion = -1;

    juce::Array<Track*> tracks;
    for (auto* tc : trackComponents)
        tracks.add (&tc->getTrack());

    for (auto* t : tracks)
        audioEditor->forgetTrack (t);

    if (mixer != nullptr)
        mixer->setTracks ({});       // el mezclador deja de apuntar a las pistas...
    trackComponents.clear();         // ...y la vista de pistas también...
    for (auto* t : tracks)
        engine.removeTrack (t);      // ...antes de destruirlas

    auto& master = engine.getMaster();
    for (int sec = 0; sec < 2; ++sec)
        while (! master.inserts[sec].empty())
            engine.removeInsert (master, (Section) sec, (int) master.inserts[sec].size() - 1);
    master.volumeDb = 0.0f;
    master.pan = 0.0f;
    master.mute = false;
    setupDefaultBuses();

    trackCounter = 0;
    setTempo (120.0);
    setTimeSignature (4, 4);
    engine.metronomeOn = false;
    engine.loopOn = false;
    engine.punchOn = false;
    engine.loopStart = 0;
    engine.loopEnd = 0;
    timeline.markers.clear();
    engine.setPosition (0);
    timeline.viewStart = 0.0;
    resized();
}

void MainComponent::loadProject (const juce::File& file, bool isRecovery)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("SIMPLEDAW_PROJECT"))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Abrir proyecto",
                                                tr8 ("Ese archivo no es un proyecto de SimpleDAW o está dañado."));
        return;
    }

    clearProject();
    juce::MouseCursor::showWaitCursor();

    // Si el proyecto se grabó a otra frecuencia, las posiciones se adaptan (el audio ya se convierte al cargarlo)
    const double projectRate = xml->getDoubleAttribute ("sampleRate", 48000.0);
    const double rate = engine.getSampleRate() > 0 ? engine.getSampleRate() : projectRate;
    const double scale = rate / juce::jmax (1.0, projectRate);

    setTempo (xml->getDoubleAttribute ("bpm", 120.0));
    setTimeSignature (xml->getIntAttribute ("sigNum", 4), xml->getIntAttribute ("sigDen", 4));
    engine.metronomeOn = xml->getBoolAttribute ("metronome", false);
    engine.countInOn = xml->getBoolAttribute ("countIn", true);
    engine.metronomeVolume = (float) xml->getDoubleAttribute ("metronomeVolume", 0.7);
    engine.loopStart = (juce::int64) std::llround ((double) xml->getStringAttribute ("loopStart", "0").getLargeIntValue() * (engine.getSampleRate() > 0 ? engine.getSampleRate() / juce::jmax (1.0, xml->getDoubleAttribute ("sampleRate", 48000.0)) : 1.0));
    engine.loopEnd = (juce::int64) std::llround ((double) xml->getStringAttribute ("loopEnd", "0").getLargeIntValue() * (engine.getSampleRate() > 0 ? engine.getSampleRate() / juce::jmax (1.0, xml->getDoubleAttribute ("sampleRate", 48000.0)) : 1.0));
    engine.loopOn = xml->getBoolAttribute ("loopOn", false);
    engine.punchOn = xml->getBoolAttribute ("punchOn", false);
    timeline.markers.clear();
    for (auto* mx : xml->getChildWithTagNameIterator ("MARKER"))
        timeline.markers.push_back ({ (juce::int64) std::llround ((double) mx->getStringAttribute ("pos").getLargeIntValue() * scale),
                                      mx->getStringAttribute ("name") });
    timeline.sortMarkers();

    juce::StringArray warnings;
    if (auto* mx = xml->getChildByName ("MASTER"))
    {
        trackParamsFromXml (engine.getMaster(), *mx);
        insertsFromXml (engine.getMaster(), *mx, warnings);
    }

    // Buses del proyecto (si los guardó; los proyectos antiguos se quedan con los de por defecto)
    if (xml->getBoolAttribute ("busesSaved") || xml->getChildByName ("BUS") != nullptr)
    {
        removeAllBuses();
        for (auto* bx : xml->getChildWithTagNameIterator ("BUS"))
        {
            const int slot = bx->getIntAttribute ("index", -1);
            auto* bus = engine.createBusAt (slot, bx->getStringAttribute ("name", "Bus"), busColour (slot));
            if (bus == nullptr)
                continue;
            trackParamsFromXml (*bus, *bx);
            insertsFromXml (*bus, *bx, warnings);
        }
    }

    const auto audioFolder = file.getParentDirectory().getChildFile ("Audio");
    for (auto* tx : xml->getChildWithTagNameIterator ("TRACK"))
    {
        auto* t = addTrack (tx->getStringAttribute ("name"), tx->getStringAttribute ("kind") == "drum");
        trackParamsFromXml (*t, *tx);
        if (t->isDrum())
        {
            t->inputChannel = -1;
            t->armed = false;
            if (auto* dx = tx->getChildByName ("DRUM"))
                drumFromXml (*t->drum, *dx);
        }
        for (auto& list : t->automation)
            for (auto& pt : list)
                pt.pos = (juce::int64) std::llround ((double) pt.pos * scale);
        if (auto* tc = componentFor (t))
            tc->setInputChannels (getActiveInputNames());
        insertsFromXml (*t, *tx, warnings);

        for (auto* rx : tx->getChildWithTagNameIterator ("REGION"))
        {
            auto load = [&] (const juce::String& fileName) -> std::shared_ptr<Clip>
            {
                const auto f = fileName.startsWith ("abs:") ? juce::File (fileName.substring (4)) : audioFolder.getChildFile (fileName);
                juce::String error;
                auto clip = engine.loadAudioFile (f, false, error);
                if (clip == nullptr)
                    warnings.add (f.getFileName() + ": " + error);
                else
                    clip->savedAs = f;
                return clip;
            };

            const auto start = (juce::int64) std::llround ((double) rx->getStringAttribute ("start").getLargeIntValue() * scale);
            if (rx->getChildByName ("TAKE") != nullptr)
            {
                // Región con varias tomas (grabación en ciclo)
                std::vector<std::shared_ptr<Clip>> takes;
                for (auto* tk : rx->getChildWithTagNameIterator ("TAKE"))
                    if (auto clip = load (tk->getStringAttribute ("file")))
                        takes.push_back (clip);
                if (takes.empty())
                    continue;
                const auto id = engine.addRegion (*t, takes.front(), start);
                for (auto& reg : t->regions)
                    if (reg.id == id)
                    {
                        const juce::ScopedLock sl (engine.getLock());
                        reg.takes = takes;
                        reg.activeTake = juce::jlimit (0, (int) takes.size() - 1, rx->getIntAttribute ("activeTake", 0));
                        reg.clip = takes[(size_t) reg.activeTake];
                    }
            }
            else if (auto clip = load (rx->getStringAttribute ("file")))
            {
                clip->name = rx->getStringAttribute ("name", clip->name);
                engine.addRegion (*t, clip, start);
            }
        }
    }

    if (trackComponents.isEmpty())
        addTrack();

    juce::MouseCursor::hideWaitCursor();

    projectFile = file;
    updateMixerTracks();
    regionsChanged();
    resized();
    markSaved();
    clearHistory();

    if (isRecovery)
    {
        // Se recupera "sin guardar": guardar irá al proyecto original (si lo había)
        const juce::File original (xml->getStringAttribute ("originalProject"));
        projectFile = original.getParentDirectory().isDirectory() && xml->getStringAttribute ("originalProject").isNotEmpty() ? original : juce::File();
        savedSnapshot = {};
    }
    updateWindowTitle();

    if (! warnings.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr8 ("Proyecto abierto con avisos"),
                                                warnings.joinIntoString ("\n"));
}

void MainComponent::openProject()
{
    askToSaveThen ([this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Abrir proyecto", defaultProjectsFolder(), "*" + projectExtension);
        juce::Component::SafePointer<MainComponent> self (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [self] (const juce::FileChooser& fc)
            {
                if (self != nullptr && fc.getResult().existsAsFile())
                {
                    self->clearRecovery();
                    self->loadProject (fc.getResult());
                }
            });
    });
}

void MainComponent::newProject()
{
    askToSaveThen ([this]
    {
        clearRecovery();
        clearProject();
        addTrack();
        projectFile = juce::File();
        regionsChanged();
        markSaved();
        clearHistory();
        updateWindowTitle();
    });
}

bool MainComponent::hasUnsavedChanges()
{
    juce::StringArray ignored;
    return createProjectXml (nullptr, ignored)->toString() != savedSnapshot;
}

void MainComponent::markSaved()
{
    juce::StringArray ignored;
    savedSnapshot = createProjectXml (nullptr, ignored)->toString();
}

void MainComponent::askToSaveThen (std::function<void()> action)
{
    if (engine.isRecording())
        return;

    if (! hasUnsavedChanges())
    {
        action();
        return;
    }

    const auto name = projectFile != juce::File() ? projectFile.getFileNameWithoutExtension() : tr8 ("Sin título");
    auto* aw = new juce::AlertWindow (tr8 ("¿Guardar los cambios?"),
                                      tr8 ("El proyecto \"") + name + tr8 ("\" tiene cambios sin guardar."),
                                      juce::MessageBoxIconType::QuestionIcon);
    aw->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("No guardar", 2);
    aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> self (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([self, action] (int result)
    {
        if (self == nullptr || result == 0)
            return;
        if (result == 2)
            action();
        else
            self->saveProject ([action] (bool ok) { if (ok) action(); });
    }), true);
}

void MainComponent::requestQuit()
{
    juce::Component::SafePointer<MainComponent> self (this);
    askToSaveThen ([self]
    {
        if (self != nullptr)
            self->clearRecovery();   // salida normal: no hay nada que recuperar
        juce::JUCEApplicationBase::quit();
    });
}

void MainComponent::updateWindowTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName ("SimpleDAW  " + tr8 ("—") + "  "
                         + (projectFile != juce::File() ? projectFile.getFileNameWithoutExtension() : tr8 ("Sin título")));
}

// ===================== Exportar la mezcla =====================

namespace
{
    // Render en segundo plano con barra de progreso (se destruye sola al terminar).
    // Cada trabajo: (pista que suena sola o nullptr = mezcla completa, archivo de destino)
    class ExportJob : public juce::ThreadWithProgressWindow
    {
    public:
        struct Item { Track* only; juce::File file; };
        using Done = std::function<void (bool ok, bool cancelled, const juce::String& error, juce::StringArray clipped, float peak)>;

        ExportJob (AudioEngine& e, std::vector<Item> list, ExportSettings s, juce::int64 len, Done d)
            : juce::ThreadWithProgressWindow (tr8 ("Exportando..."), true, true),
              engine (e), items (std::move (list)), settings (s), length (len), done (std::move (d)) {}

        void run() override
        {
            const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
            for (size_t i = 0; i < items.size(); ++i)
            {
                auto& item = items[i];
                if (items.size() > 1)
                    setStatusMessage (item.file.getFileName());
                item.file.deleteFile();
                auto format = settings.createFormat();
                std::unique_ptr<juce::OutputStream> stream (item.file.createOutputStream().release());
                if (stream == nullptr)
                {
                    error = tr8 ("No se puede escribir en esa carpeta.");
                    return;
                }
                auto writer = format->createWriterFor (stream, settings.writerOptions (sr));
                if (writer == nullptr)
                {
                    error = tr8 ("Ese formato no admite estas opciones.");
                    return;
                }

                engine.exclusiveTrack = item.only;
                float itemPeak = 0.0f;
                const bool okItem = engine.renderOffline (*writer, length, [this, i] (double p)
                {
                    setProgress ((i + p) / (double) items.size());
                    return ! threadShouldExit();
                }, itemPeak);
                engine.exclusiveTrack = nullptr;
                written.add (item.file);

                if (! okItem)
                    return;
                peak = juce::jmax (peak, itemPeak);
                if (itemPeak > 1.0f)
                    clipped.add (item.only != nullptr ? item.only->name : juce::String ("Mezcla"));
            }
            ok = true;
        }

        void threadComplete (bool userPressedCancel) override
        {
            if (userPressedCancel || ! ok)
                for (auto& f : written)
                    f.deleteFile();
            done (ok && ! userPressedCancel, userPressedCancel, error, clipped, peak);
            delete this;
        }

    private:
        AudioEngine& engine;
        std::vector<Item> items;
        ExportSettings settings;
        juce::int64 length;
        Done done;
        bool ok = false;
        float peak = 0.0f;
        juce::String error;
        juce::StringArray clipped;
        juce::Array<juce::File> written;
    };
}

void MainComponent::exportMix()
{
    if (engine.isRecording())
        return;

    if (engine.getContentEndSamples() == 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Exportar mezcla",
                                                tr8 ("No hay nada que exportar: graba o importa audio primero."));
        return;
    }

    juce::Component::SafePointer<MainComponent> self (this);
    auto* dialog = new ExportDialog ([self] (ExportSettings settings)
    {
        if (self == nullptr)
            return;

        const auto base = self->projectFile != juce::File() ? self->projectFile.getSiblingFile (self->projectFile.getFileNameWithoutExtension())
                                                            : defaultProjectsFolder().getChildFile (tr8 ("Mezcla"));

        if (settings.stems)
        {
            // Stems: se elige una carpeta y se crea un archivo por pista
            self->fileChooser = std::make_unique<juce::FileChooser> (tr8 ("Carpeta para las pistas"), base.getParentDirectory(), "*");
            self->fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [self, settings] (const juce::FileChooser& fc)
                {
                    if (self != nullptr && fc.getResult().isDirectory())
                        self->startExport (fc.getResult(), settings);
                });
            return;
        }
        self->fileChooser = std::make_unique<juce::FileChooser> ("Exportar mezcla", base.withFileExtension (settings.extension()),
                                                                 "*" + settings.extension());
        self->fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                          | juce::FileBrowserComponent::warnAboutOverwriting,
            [self, settings] (const juce::FileChooser& fc)
            {
                if (self != nullptr && fc.getResult() != juce::File())
                    self->startExport (fc.getResult().withFileExtension (settings.extension()), settings);
            });
    });

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (dialog);
    o.dialogTitle = "Exportar mezcla";
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::startExport (const juce::File& target, ExportSettings settings)
{
    stopTransport();
    const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
    const auto length = engine.getContentEndSamples() + (juce::int64) (engine.getMaxTailSeconds() * sr);

    std::vector<ExportJob::Item> items;
    if (settings.stems)
    {
        int n = 1;
        for (auto* tc : trackComponents)
        {
            auto& t = tc->getTrack();
            if (t.regions.empty() && ! (t.isDrum() && t.drum->lastBar() >= 0))
                continue;   // pistas vacías, no
            const auto name = juce::String (n++).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (t.name);
            items.push_back ({ &t, target.getChildFile (name + settings.extension()) });
        }
    }
    else
    {
        items.push_back ({ nullptr, target });
    }
    if (items.empty())
        return;

    // Mientras se exporta, el motor no atiende a la tarjeta de sonido
    deviceManager.removeAudioCallback (&engine);

    juce::Component::SafePointer<MainComponent> self (this);
    const bool stems = settings.stems;
    auto* job = new ExportJob (engine, items, settings, length,
        [self, target, stems] (bool ok, bool cancelled, const juce::String& error, juce::StringArray clipped, float peak)
        {
            if (self == nullptr)
                return;
            self->deviceManager.addAudioCallback (&self->engine);
            if (cancelled)
                return;

            if (! ok)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr8 ("No se pudo exportar"),
                                                        error.isNotEmpty() ? error : tr8 ("Error al escribir el archivo."));
                return;
            }

            juce::String msg = (stems ? tr8 ("Pistas exportadas en:\n") : tr8 ("Mezcla exportada en:\n")) + target.getFullPathName();
            if (! clipped.isEmpty())
                msg << tr8 ("\n\nAviso: satura (pico de +") << juce::String (juce::Decibels::gainToDecibels (peak), 1)
                    << " dB) en: " << clipped.joinIntoString (", ")
                    << tr8 (". Baja el volumen (o el Master) y vuelve a exportar.");
            juce::AlertWindow::showMessageBoxAsync (clipped.isEmpty() ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                    "Exportar", msg);
            target.revealToUser();
        });
    job->launchThread();
}

// ===================== Menús =====================

namespace MenuIds
{
    enum
    {
        newProject = 1, open, save, saveAs, exportMix, quit, checkUpdates,
        undo = 60, redo, split, duplicate,
        newTrack = 10, importAudio, transpose, speed, newDrumTrack,
        mixer = 20, editor, plugins, audioSettings,
        play = 30, record, toStart, metronome, countIn, tuner, cycle, punch, addMarker, prevMarker, nextMarker
    };
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "Archivo", tr8 ("Edición"), "Pista", "Transporte", "Ver" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    auto add = [&m] (int id, const juce::String& text, juce::KeyPress key = {}, bool ticked = false)
    {
        juce::PopupMenu::Item item (text);
        item.itemID = id;
        item.isTicked = ticked;
        if (key.isValid())
            item.shortcutKeyDescription = key.getTextDescriptionWithIcons();
        m.addItem (item);
    };

    if (index == 0)
    {
        add (MenuIds::newProject, "Nuevo proyecto",       juce::KeyPress ('n', cmd, 0));
        add (MenuIds::open,       "Abrir proyecto...",    juce::KeyPress ('o', cmd, 0));
        m.addSeparator();
        add (MenuIds::save,       "Guardar",              juce::KeyPress ('s', cmd, 0));
        add (MenuIds::saveAs,     "Guardar como...",      juce::KeyPress ('s', cmd | shift, 0));
        m.addSeparator();
        add (MenuIds::exportMix,  "Exportar mezcla...",   juce::KeyPress ('e', cmd | shift, 0));
        m.addSeparator();
        add (MenuIds::checkUpdates, "Buscar actualizaciones...");
       #if ! JUCE_MAC
        m.addSeparator();
        add (MenuIds::quit,       "Salir");
       #endif
    }
    else if (index == 1)
    {
        add (MenuIds::undo, undoManager.canUndo() ? "Deshacer: " + undoManager.getUndoDescription() : juce::String ("Deshacer"),
             juce::KeyPress ('z', cmd, 0));
        add (MenuIds::redo, undoManager.canRedo() ? "Rehacer: " + undoManager.getRedoDescription() : juce::String ("Rehacer"),
             juce::KeyPress ('z', cmd | shift, 0));
        m.addSeparator();
        add (MenuIds::split,     tr8 ("Cortar la región en el cursor"), juce::KeyPress ('s', 0, 0));
        add (MenuIds::duplicate, tr8 ("Duplicar la región"),            juce::KeyPress ('d', cmd, 0));
    }
    else if (index == 2)
    {
        add (MenuIds::newTrack,    "Nueva pista de audio");
        add (MenuIds::newDrumTrack, tr8 ("Nueva pista de batería"));
        add (MenuIds::importAudio, "Importar audio...", juce::KeyPress ('i', cmd, 0));
        m.addSeparator();
        add (MenuIds::transpose,   tr8 ("Transponer región..."));
        add (MenuIds::speed,       tr8 ("Cambiar velocidad de la región..."));
    }
    else if (index == 3)
    {
        add (MenuIds::play,      "Reproducir / Parar", juce::KeyPress (juce::KeyPress::spaceKey));
        add (MenuIds::record,    "Grabar",             juce::KeyPress ('r', 0, 0));
        add (MenuIds::toStart,   "Ir al inicio",       juce::KeyPress (juce::KeyPress::returnKey));
        m.addSeparator();
        add (MenuIds::addMarker,  tr8 ("Añadir marcador"),   juce::KeyPress ('m', 0, 0));
        add (MenuIds::prevMarker, "Marcador anterior",       juce::KeyPress (',', 0, 0), false);
        add (MenuIds::nextMarker, "Marcador siguiente",      juce::KeyPress ('.', 0, 0), false);
        m.addSeparator();
        add (MenuIds::cycle,     "Ciclo",              juce::KeyPress ('c', 0, 0), engine.loopOn.load());
        add (MenuIds::punch,     tr8 ("Pinchado automático (punch in/out)"), juce::KeyPress ('p', 0, 0), engine.punchOn.load());
        m.addSeparator();
        add (MenuIds::metronome, tr8 ("Metrónomo"),     juce::KeyPress ('k', 0, 0), engine.metronomeOn.load());
        add (MenuIds::countIn,   tr8 ("Precuenta de un compás al grabar"), {}, engine.countInOn.load());
        m.addSeparator();
        add (MenuIds::tuner,     "Afinador",           juce::KeyPress ('t', 0, 0), tunerWindow != nullptr && tunerWindow->isVisible());
    }
    else if (index == 4)
    {
        add (MenuIds::mixer,  "Mezclador",       juce::KeyPress ('x', 0, 0), mixerOpen);
        add (MenuIds::editor, "Editor de audio", juce::KeyPress ('e', 0, 0), editorVisible);
        m.addSeparator();
        add (MenuIds::plugins,       "Plugins...");
        add (MenuIds::audioSettings, "Ajustes de audio...");
    }
    return m;
}

void MainComponent::menuItemSelected (int itemId, int)
{
    switch (itemId)
    {
        case MenuIds::newProject:    newProject(); break;
        case MenuIds::checkUpdates:  checkForUpdates (true); break;
        case MenuIds::undo:          undo(); break;
        case MenuIds::redo:          redo(); break;
        case MenuIds::split:         splitAtPlayhead(); break;
        case MenuIds::duplicate:     duplicateSelectedRegion(); break;
        case MenuIds::open:          openProject(); break;
        case MenuIds::save:          saveProject(); break;
        case MenuIds::saveAs:        saveProjectAs(); break;
        case MenuIds::exportMix:     exportMix(); break;
        case MenuIds::quit:          requestQuit(); break;
        case MenuIds::newTrack:      addTrackWithUndo(); break;
        case MenuIds::newDrumTrack:  addTrackWithUndo (true); break;
        case MenuIds::importAudio:   chooseFilesToImport(); break;
        case MenuIds::speed:
            if (selectedTrack != nullptr)
                changeRegionSpeed (*selectedTrack, selectedRegion);
            break;
        case MenuIds::transpose:
            if (selectedTrack != nullptr)
                transposeRegion (*selectedTrack, selectedRegion);
            break;
        case MenuIds::mixer:         toggleMixer(); break;
        case MenuIds::editor:        toggleEditor(); break;
        case MenuIds::plugins:       showPluginManager(); break;
        case MenuIds::audioSettings: showAudioSettings(); break;
        case MenuIds::play:          togglePlay(); break;
        case MenuIds::record:        toggleRecord(); break;
        case MenuIds::toStart:       goToStart(); break;
        case MenuIds::metronome:     toggleMetronome(); break;
        case MenuIds::countIn:       engine.countInOn = ! engine.countInOn.load(); break;
        case MenuIds::tuner:         toggleTuner(); break;
        case MenuIds::cycle:         toggleCycle(); break;
        case MenuIds::addMarker:     addMarkerAtPlayhead(); break;
        case MenuIds::prevMarker:    jumpToMarker (-1); break;
        case MenuIds::nextMarker:    jumpToMarker (1); break;
        case MenuIds::punch:         togglePunch(); break;
        default: break;
    }
}

// ===================== Tempo, compás, metrónomo y afinador =====================

void MainComponent::setTempo (double bpm)
{
    bpm = juce::jlimit (20.0, 300.0, bpm);
    engine.bpm = bpm;
    timeline.bpm = bpm;
    repaintTimeline();
}

void MainComponent::setTimeSignature (int num, int den)
{
    num = juce::jlimit (1, 16, num);
    den = (den == 2 || den == 4 || den == 8 || den == 16) ? den : 4;
    engine.sigNum = num;
    engine.sigDen = den;
    timeline.sigNum = num;
    timeline.sigDen = den;
    repaintTimeline();
}

juce::String MainComponent::positionText() const
{
    // compás . parte . semicorchea (como en los DAW habituales)
    const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
    const double sec = (double) engine.getPosition() / sr;
    const double beat = timeline.beatSeconds(), bar = timeline.barSeconds();
    const double barIndex = std::floor (sec / bar);
    const double inBar = sec - barIndex * bar;
    const int beatIndex = juce::jlimit (0, timeline.sigNum - 1, (int) std::floor (inBar / beat));
    const int sixteenth = juce::jlimit (0, 3, (int) std::floor ((inBar - beatIndex * beat) / (beat / 4.0)));
    return juce::String::formatted ("%3d . %d . %d", (int) barIndex + 1, beatIndex + 1, sixteenth + 1);
}

void MainComponent::toggleMetronome()
{
    engine.metronomeOn = ! engine.metronomeOn.load();
}

void MainComponent::showMetronomeOptions()
{
    // Pequeño panel con el volumen del clic y la precuenta
    struct Options : public juce::Component
    {
        explicit Options (AudioEngine& e) : engine (e)
        {
            volume.setRange (0.0, 1.0, 0.01);
            volume.setValue (engine.metronomeVolume.load(), juce::dontSendNotification);
            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.onValueChange = [this] { engine.metronomeVolume = (float) volume.getValue(); };
            addAndMakeVisible (volume);

            countIn.setButtonText (tr8 ("Precuenta de un compás al grabar"));
            countIn.setToggleState (engine.countInOn.load(), juce::dontSendNotification);
            countIn.onClick = [this] { engine.countInOn = countIn.getToggleState(); };
            addAndMakeVisible (countIn);

            label.setText (tr8 ("Volumen del metrónomo"), juce::dontSendNotification);
            label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            label.setColour (juce::Label::textColourId, Theme::textDim);
            addAndMakeVisible (label);
            setSize (250, 86);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6);
            label.setBounds (r.removeFromTop (18));
            volume.setBounds (r.removeFromTop (26));
            r.removeFromTop (6);
            countIn.setBounds (r.removeFromTop (24));
        }

        AudioEngine& engine;
        juce::Slider volume;
        juce::ToggleButton countIn;
        juce::Label label;
    };

    juce::CallOutBox::launchAsynchronously (std::make_unique<Options> (engine), metronomeButton.getScreenBounds(), nullptr);
}

void MainComponent::toggleTuner()
{
    if (tunerWindow == nullptr)
        return;
    if (tunerWindow->isVisible())
        tunerWindow->closeButtonPressed();
    else
        tunerWindow->show (selectedTrack != nullptr ? juce::jmax (0, selectedTrack->inputChannel.load()) : 0);
}

// ===================== Ciclo, pinchado y ajustes de la aplicación =====================

void MainComponent::ensureLoopRange()
{
    // Si no hay rango marcado: 4 compases desde el compás donde está el cursor
    if (engine.hasValidLoop())
        return;
    const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
    const double bar = timeline.barSeconds() * sr;
    const double startBar = std::floor ((double) juce::jmax ((juce::int64) 0, engine.getPosition()) / bar);
    engine.loopStart = (juce::int64) std::llround (startBar * bar);
    engine.loopEnd = (juce::int64) std::llround ((startBar + 4.0) * bar);
}

void MainComponent::toggleCycle()
{
    if (engine.isRecording())
        return;
    ensureLoopRange();
    engine.loopOn = ! engine.loopOn.load();
    if (engine.loopOn.load())
        engine.punchOn = false;
    repaintTimeline();
}

void MainComponent::togglePunch()
{
    if (engine.isRecording())
        return;
    ensureLoopRange();
    engine.punchOn = ! engine.punchOn.load();
    if (engine.punchOn.load())
        engine.loopOn = false;
    repaintTimeline();
}

void MainComponent::showCycleMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Ciclo (C)", true, engine.loopOn.load());
    m.addItem (2, tr8 ("Pinchado automático (P)"), true, engine.punchOn.load());
    m.addSeparator();
    m.addItem (3, tr8 ("Quitar el rango"), engine.hasValidLoop());
    juce::Component::SafePointer<MainComponent> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&cycleButton), [self] (int r)
    {
        if (self == nullptr || r == 0) return;
        if (r == 1) self->toggleCycle();
        if (r == 2) self->togglePunch();
        if (r == 3)
        {
            self->engine.loopOn = false; self->engine.punchOn = false;
            self->engine.loopStart = 0; self->engine.loopEnd = 0;
            self->repaintTimeline();
        }
    });
}

void MainComponent::loadAppSettings()
{
    if (auto xml = juce::XmlDocument::parse (AppPaths::appSettings()))
    {
        // La latencia manual se guarda en ms para que valga a cualquier frecuencia de muestreo
        const double ms = xml->getDoubleAttribute ("manualLatencyMs", 0.0);
        const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
        engine.manualLatencySamples = (int) std::lround (ms * sr / 1000.0);
        skippedVersion = xml->getStringAttribute ("skipVersion");
    }
}

void MainComponent::saveAppSettings()
{
    juce::XmlElement xml ("SIMPLEDAW_SETTINGS");
    const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
    xml.setAttribute ("manualLatencyMs", engine.manualLatencySamples.load() * 1000.0 / sr);
    if (skippedVersion.isNotEmpty())
        xml.setAttribute ("skipVersion", skippedVersion);
    xml.writeTo (AppPaths::appSettings());
}

// ===================== Marcadores =====================

void MainComponent::addMarkerAtPlayhead()
{
    ruler.addMarker (juce::jmax ((juce::int64) 0, engine.getPosition()));
}

void MainComponent::jumpToMarker (int direction)
{
    if (engine.isRecording() || timeline.markers.empty())
        return;
    const auto pos = engine.getPosition();
    const double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
    const auto tolerance = (juce::int64) (sr * 0.05);   // pulsar "anterior" justo en un marcador va al de antes
    juce::int64 target = direction < 0 ? 0 : -1;
    for (auto& mk : timeline.markers)
    {
        if (direction > 0 && mk.pos > pos + tolerance) { target = mk.pos; break; }
        if (direction < 0 && mk.pos < pos - tolerance) target = mk.pos;
    }
    if (target >= 0)
    {
        engine.setPosition (target);
        lastDrawnPosition = -1;
        repaintTimeline();
    }
}

// ===================== Buses y automatización =====================

void MainComponent::setupDefaultBuses()
{
    // Por defecto: un bus de reverb y otro de delay (se pueden quitar y añadir otros)
    removeAllBuses();
    createBus ("Reverb", "Reverb");
    createBus ("Delay", "Delay");
}

juce::Array<std::pair<int, Track*>> MainComponent::activeBuses() const
{
    juce::Array<std::pair<int, Track*>> list;
    for (int i = 0; i < Track::maxBuses; ++i)
        if (auto* b = const_cast<AudioEngine&> (engine).getBus (i))
            list.add ({ i, b });
    return list;
}

juce::Colour MainComponent::busColour (int slot)
{
    static const juce::uint32 c[] = { 0xff7fb8d8, 0xffd8a97f, 0xffa58fd8, 0xff8fd8a5, 0xffd88fb8, 0xffd8d07f, 0xff7fd8d0, 0xffb8b8b8 };
    return juce::Colour (c[juce::jlimit (0, 7, slot)]);
}

void MainComponent::removeAllBuses()
{
    for (int i = 0; i < Track::maxBuses; ++i)
        if (auto* bus = engine.getBus (i))
        {
            closeEditorsForTrack (*bus);
            engine.detachBus (i);   // se destruye aquí
        }
}

int MainComponent::createBus (const juce::String& name, const juce::String& effectName)
{
    // Busca el hueco que va a usar para darle su color
    int slot = -1;
    for (int i = 0; i < Track::maxBuses && slot < 0; ++i)
        if (engine.getBus (i) == nullptr) slot = i;
    if (slot < 0)
        return -1;
    slot = engine.addBus (name, busColour (slot));
    auto* bus = engine.getBus (slot);
    if (bus != nullptr && effectName.isNotEmpty())
    {
        if (auto fx = BuiltInEffects::createByName (effectName))
        {
            // En un bus, el efecto va al 100 %: la cantidad la decide cada envío
            for (auto* p : fx->getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (r->paramID == "mix")
                        r->setValueNotifyingHost (1.0f);
            engine.addInsert (*bus, Section::effects, std::move (fx), false);
        }
    }
    return slot;
}

void MainComponent::showAddBusMenu (juce::Component& target)
{
    juce::PopupMenu m;
    m.addSectionHeader (tr8 ("Nuevo bus con..."));
    const auto names = BuiltInEffects::names();
    for (int i = 0; i < names.size(); ++i)
        m.addItem (i + 1, names[i]);
    m.addSeparator();
    m.addItem (100, tr8 ("Bus vacío (para cargarle plugins o varios efectos)"));

    juce::Component::SafePointer<MainComponent> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [self, names] (int r)
    {
        if (self == nullptr || r == 0)
            return;
        const auto effect = r == 100 ? juce::String() : names[r - 1];
        const int slot = self->createBus (effect.isNotEmpty() ? effect : juce::String ("Bus"), effect);
        if (slot < 0)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Buses", tr8 ("Como máximo puede haber 8 buses."));
            return;
        }
        auto holder = std::make_shared<std::unique_ptr<Track>>();
        self->recordUndo (tr8 ("Añadir bus"),
            [self, slot, holder] { if (self == nullptr) return; self->closeEditorsForTrack (*self->engine.getBus (slot)); *holder = self->engine.detachBus (slot); },
            [self, slot, holder] { if (self != nullptr) self->engine.attachBus (slot, std::move (*holder)); });
        self->updateMixerTracks();
        if (! self->mixerOpen)
            self->toggleMixer();
    });
}

void MainComponent::showBusMenu (Track& bus, juce::Component& target)
{
    const int slot = engine.getBusSlot (&bus);
    if (slot < 0)
        return;
    juce::PopupMenu m;
    m.addSectionHeader (bus.name);
    m.addItem (1, "Renombrar...");
    m.addItem (2, "Quitar el bus");

    juce::Component::SafePointer<MainComponent> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [self, slot] (int r)
    {
        if (self == nullptr || r == 0 || self->engine.getBus (slot) == nullptr)
            return;
        auto* bus = self->engine.getBus (slot);

        if (r == 1)
        {
            auto* aw = new juce::AlertWindow ("Nombre del bus", {}, juce::MessageBoxIconType::NoIcon);
            aw->addTextEditor ("name", bus->name);
            aw->addButton ("Aceptar", 1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([self, slot, aw] (int res)
            {
                if (self == nullptr || res != 1) return;
                auto* b = self->engine.getBus (slot);
                const auto name = aw->getTextEditorContents ("name").trim();
                if (b == nullptr || name.isEmpty()) return;
                const auto before = b->name;
                b->name = name;
                self->recordUndo ("Renombrar bus",
                    [self, slot, before] { if (self != nullptr) if (auto* x = self->engine.getBus (slot)) x->name = before; },
                    [self, slot, name]   { if (self != nullptr) if (auto* x = self->engine.getBus (slot)) x->name = name; });
                self->updateMixerTracks();
            }), true);
            return;
        }

        // Quitar: el bus queda en el historial, junto con lo que le enviaba cada pista
        std::vector<std::pair<Track*, float>> sends;
        for (auto* tc : self->trackComponents)
            sends.push_back ({ &tc->getTrack(), tc->getTrack().send[slot].load() });
        self->closeEditorsForTrack (*bus);
        auto holder = std::make_shared<std::unique_ptr<Track>> (self->engine.detachBus (slot));
        self->recordUndo (tr8 ("Quitar bus"),
            [self, slot, holder, sends]
            {
                if (self == nullptr) return;
                self->engine.attachBus (slot, std::move (*holder));
                for (auto& [t, v] : sends) t->send[slot] = v;
            },
            [self, slot, holder]
            {
                if (self == nullptr || self->engine.getBus (slot) == nullptr) return;
                self->closeEditorsForTrack (*self->engine.getBus (slot));
                *holder = self->engine.detachBus (slot);
            });
        self->updateMixerTracks();
    });
}

void MainComponent::toggleAutomationView()
{
    // Pista seleccionada: oculta -> volumen -> panorama -> oculta
    if (selectedTrack == nullptr)
        return;
    if (auto* tc = componentFor (selectedTrack))
    {
        const int v = tc->getAutomationView();
        tc->showAutomation (v < 0 ? Track::autoVolume : v == Track::autoVolume ? Track::autoPan : -1);
    }
}

// ===================== Autoguardado y recuperación =====================

void MainComponent::autosaveTick()
{
    if (engine.isRecording() || juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0)
        return;
    if (! hasUnsavedChanges())
        return;

    juce::StringArray ignored;
    const auto snapshot = createProjectXml (nullptr, ignored)->toString();
    if (snapshot == lastRecoverySnapshot)
        return;   // nada nuevo desde el último autoguardado

    const auto file = AppPaths::recoveryFile();
    auto audioFolder = file.getParentDirectory().getChildFile ("Audio");
    audioFolder.createDirectory();

    juce::StringArray errors;
    auto xml = createProjectXml (&audioFolder, errors, true);
    xml->setAttribute ("originalProject", projectFile.getFullPathName());
    if (errors.isEmpty() && xml->writeTo (file))
        lastRecoverySnapshot = snapshot;
}

void MainComponent::clearRecovery()
{
    const auto file = AppPaths::recoveryFile();
    file.getParentDirectory().deleteRecursively();
    lastRecoverySnapshot = {};
    for (auto* tc : trackComponents)
        for (auto& r : tc->getTrack().regions)
        {
            r.clip->recoveryAs = juce::File();
            for (auto& take : r.takes)
                take->recoveryAs = juce::File();
        }
}

void MainComponent::checkForRecovery()
{
    const auto file = AppPaths::recoveryFile();
    if (! file.existsAsFile())
        return;

    auto* aw = new juce::AlertWindow (tr8 ("Recuperar la sesión"),
        tr8 ("SimpleDAW no se cerró correctamente la última vez. ¿Quieres recuperar la sesión autoguardada "
             "(de ") + file.getLastModificationTime().formatted ("%d/%m %H:%M") + ")?",
        juce::MessageBoxIconType::QuestionIcon);
    aw->addButton ("Recuperar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Descartar", 0);
    juce::Component::SafePointer<MainComponent> self (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([self, file] (int result)
    {
        if (self == nullptr)
            return;
        if (result == 1)
            self->loadProject (file, true);
        else
            self->clearRecovery();
    }), true);
}

// ===================== Actualizaciones (GitHub) =====================

void MainComponent::checkForUpdates (bool askedByUser)
{
    if (Updater::repository().isEmpty())
    {
        if (askedByUser)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Actualizaciones",
                tr8 ("Esta copia de SimpleDAW no está enlazada con un repositorio de GitHub.\n\n"
                     "Las actualizaciones automáticas funcionan en las versiones descargadas de la página "
                     "\"Releases\" del repositorio (o compiladas desde una copia clonada de él)."));
        return;
    }
    if (updateWaiting)
    {
        if (askedByUser)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Actualizaciones",
                tr8 ("La actualización ya está descargada: se instalará al cerrar SimpleDAW."));
        return;
    }

    juce::Component::SafePointer<MainComponent> self (this);
    juce::Thread::launch ([self, askedByUser]
    {
        Updater::Release release;
        juce::String error;
        const bool ok = Updater::fetchLatest (release, error);
        juce::MessageManager::callAsync ([self, askedByUser, ok, release, error]
        {
            if (self == nullptr)
                return;
            if (! ok)
            {
                if (askedByUser)
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Actualizaciones",
                                                            tr8 ("No se ha podido comprobar si hay versiones nuevas.\n") + error);
                return;
            }
            const bool newer = Updater::isNewer (release.version, Updater::currentVersion());
            if (! newer || (! askedByUser && release.version == self->skippedVersion))
            {
                if (askedByUser)
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Actualizaciones",
                        tr8 ("Tienes la última versión (") + Updater::currentVersion() + ").");
                return;
            }
            self->offerUpdate (release);
        });
    });
}

void MainComponent::offerUpdate (const Updater::Release& release)
{
    auto notes = release.notes.trim();
    if (notes.length() > 700)
        notes = notes.substring (0, 700) + "...";

    juce::String text = tr8 ("Tienes la ") + Updater::currentVersion() + tr8 (". ¿Quieres descargar la nueva e instalarla?\n"
                        "Se instalará sola al cerrar SimpleDAW (antes te pregunta si quieres guardar).");
    if (notes.isNotEmpty())
        text << "\n\n" << tr8 ("Novedades:\n") << notes;
    if (release.assetUrl.isEmpty())
        text << "\n\n" << tr8 ("(Todavía no hay archivo para este sistema en esa versión: se abrirá la página.)");

    auto* aw = new juce::AlertWindow (tr8 ("Nueva versión: SimpleDAW ") + release.version, text, juce::MessageBoxIconType::InfoIcon);
    aw->addButton ("Actualizar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton (tr8 ("Más tarde"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->addButton (tr8 ("Saltar esta versión"), 2);

    juce::Component::SafePointer<MainComponent> self (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([self, release] (int r)
    {
        if (self == nullptr || r == 0)
            return;
        if (r == 2)
        {
            self->skippedVersion = release.version;
            self->saveAppSettings();
            return;
        }
        if (release.assetUrl.isEmpty())
        {
            juce::URL (release.pageUrl).launchInDefaultBrowser();
            return;
        }
        (new Updater::DownloadJob (release, [self] (juce::File package)
        {
            if (self == nullptr)
                return;
            if (! Updater::scheduleInstall (package))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr8 ("Actualización"),
                    tr8 ("No se ha podido preparar la instalación automática. Se abre la carpeta con la descarga para instalarla a mano."));
                package.revealToUser();
                return;
            }
            self->updateWaiting = true;
            auto* done = new juce::AlertWindow (tr8 ("Actualización lista"),
                tr8 ("Se instalará al cerrar SimpleDAW y se volverá a abrir sola.\n¿Cerrar ahora?"), juce::MessageBoxIconType::InfoIcon);
            done->addButton ("Cerrar y actualizar", 1, juce::KeyPress (juce::KeyPress::returnKey));
            done->addButton (tr8 ("Más tarde"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            done->enterModalState (true, juce::ModalCallbackFunction::create ([self] (int res)
            {
                if (self != nullptr && res == 1)
                    self->requestQuit();
            }), true);
        }))->launchThread();
    }), true);
}

// ===================== Deshacer global =====================

void MainComponent::recordUndo (const juce::String& name, std::function<void()> undoFn, std::function<void()> redoFn)
{
    // Tras deshacer/rehacer, repintar todo lo que pueda haber cambiado
    juce::Component::SafePointer<MainComponent> self (this);
    auto refresh = [self]
    {
        if (self == nullptr) return;
        self->regionsChanged();
        self->updateMixerTracks();
        self->syncChangeWatcher();
        if (self->drumEditorWindow != nullptr)
            self->drumEditorWindow->refresh();
    };
    if (groupingUndo == 0)
        undoManager.beginNewTransaction (name);
    undoManager.perform (new LambdaUndoAction ([undoFn, refresh] { undoFn(); refresh(); },
                                               [redoFn, refresh] { redoFn(); refresh(); }), name);
    syncChangeWatcher();
}

void MainComponent::undo()
{
    if (engine.isRecording())
        return;
    undoManager.undo();
    syncChangeWatcher();
    regionsChanged();
}

void MainComponent::redo()
{
    if (engine.isRecording())
        return;
    undoManager.redo();
    syncChangeWatcher();
    regionsChanged();
}

void MainComponent::clearHistory()
{
    undoManager.clearUndoHistory();
    syncChangeWatcher();
}

std::shared_ptr<MainComponent::TrackHolder> MainComponent::detachTrackFromProject (Track* track)
{
    auto* tc = componentFor (track);
    if (tc == nullptr)
        return nullptr;

    closeEditorsForTrack (*track);
    if (audioEditor != nullptr)
        audioEditor->forgetTrack (track);
    if (drumEditorWindow != nullptr && drumEditorWindow->getTrack() == track)
        drumEditorWindow = nullptr;
    if (selectedTrack == track)
    {
        selectedTrack = nullptr;
        selectedRegion = -1;
    }

    auto holder = std::make_shared<TrackHolder>();
    holder->uiIndex = trackComponents.indexOf (tc);
    trackComponents.removeObject (tc);
    updateMixerTracks();                 // el mezclador deja de apuntar a la pista...
    auto [ptr, index] = engine.detachTrack (track);   // ...antes de sacarla del motor
    holder->track = std::move (ptr);
    holder->engineIndex = index;
    resized();
    return holder;
}

Track* MainComponent::attachTrackToProject (TrackHolder& holder)
{
    if (holder.track == nullptr)
        return nullptr;
    auto* track = engine.attachTrack (std::move (holder.track), juce::jmax (0, holder.engineIndex));
    auto* tc = trackComponents.insert (juce::jlimit (0, trackComponents.size(), holder.uiIndex),
                                       new TrackComponent (*track, engine, timeline, *this,
                                                           [this] (TrackComponent& c) { removeTrack (c); }));
    tc->onDrumsChanged = [this] { if (drumEditorWindow != nullptr) drumEditorWindow->refresh(); };
    tc->setInputChannels (getActiveInputNames());
    trackContainer.addAndMakeVisible (tc);
    updateMixerTracks();
    resized();
    return track;
}

void MainComponent::addTrackWithUndo (bool drum)
{
    if (engine.isRecording())
        return;
    auto* track = addTrack ({}, drum);
    if (drum)
    {
        // Batería nueva: estilo según el compás y 8 compases (A A A C B B B C) desde el compás actual
        const int num = engine.sigNum.load(), den = engine.sigDen.load();
        const int style = num == 3 && den == 4 ? 7 : (den == 8 && num % 3 == 0) ? 8 : 0;
        Drums::applyStyle (*track->drum, style, juce::jlimit (1, Drums::maxSteps, num * 16 / juce::jmax (1, den)));
        static const int8_t song[] = { 0, 0, 0, 2, 1, 1, 1, 2 };
        const int startBar = juce::jmax (0, (int) std::floor ((double) juce::jmax ((juce::int64) 0, engine.getPosition()) / engine.samplesPerBar()));
        for (int i = 0; i < 8 && startBar + i < Drums::maxBars; ++i)
            track->drum->arrangement[(size_t) (startBar + i)] = song[i];
        regionsChanged();
    }
    auto holder = std::make_shared<TrackHolder>();
    juce::Component::SafePointer<MainComponent> self (this);
    recordUndo (drum ? tr8 ("Nueva pista de batería") : juce::String ("Nueva pista"),
        [self, track, holder]
        {
            if (self == nullptr) return;
            auto h = self->detachTrackFromProject (track);
            if (h != nullptr) *holder = std::move (*h);
        },
        [self, holder] { if (self != nullptr) self->attachTrackToProject (*holder); });
}

// --- Vigilancia: faders, panoramas, mute/solo, envíos, tempo, compás, ciclo y marcadores ---

MainComponent::MixState MainComponent::readMix (const Track& t)
{
    std::array<float, Track::maxBuses> sends {};
    for (int b = 0; b < Track::maxBuses; ++b) sends[(size_t) b] = t.send[b].load();
    return { t.volumeDb.load(), t.pan.load(), sends, t.mute.load(), t.solo.load() };
}

void MainComponent::writeMix (Track& t, const MixState& m)
{
    t.volumeDb = m.vol; t.pan = m.pan; t.mute = m.mute; t.solo = m.solo;
    for (int b = 0; b < Track::maxBuses; ++b) t.send[b] = m.sends[(size_t) b];
}

MainComponent::ProjectState MainComponent::readProjectState() const
{
    return { timeline.bpm, timeline.sigNum, timeline.sigDen, engine.loopStart.load(), engine.loopEnd.load(),
             engine.loopOn.load(), engine.punchOn.load(), timeline.markers };
}

void MainComponent::writeProjectState (const ProjectState& p)
{
    setTempo (p.bpm);
    setTimeSignature (p.num, p.den);
    engine.loopStart = p.loopStart;
    engine.loopEnd = p.loopEnd;
    engine.loopOn = p.loopOn;
    engine.punchOn = p.punchOn;
    timeline.markers = p.markers;
    repaintTimeline();
}

void MainComponent::syncChangeWatcher()
{
    committedMix.clear();
    for (auto* tc : trackComponents)
        committedMix[&tc->getTrack()] = readMix (tc->getTrack());
    committedMix[&engine.getMaster()] = readMix (engine.getMaster());
    for (auto& b : activeBuses())
        committedMix[b.second] = readMix (*b.second);
    committedProject = readProjectState();
}

void MainComponent::watchForChanges()
{
    // Se registra al soltar el ratón (un arrastre de fader = un solo paso)
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown() || engine.isRecording())
        return;

    juce::Array<Track*> all;
    for (auto* tc : trackComponents) all.add (&tc->getTrack());
    all.add (&engine.getMaster());
    for (auto& b : activeBuses()) all.add (b.second);

    for (auto* t : all)
    {
        auto now = readMix (*t);
        auto it = committedMix.find (t);
        if (it == committedMix.end()) { committedMix[t] = now; continue; }

        // Lo que mueve la automatización no es un cambio del usuario
        if (t->hasAutomation (Track::autoVolume)) it->second.vol = now.vol;
        if (t->hasAutomation (Track::autoPan))    it->second.pan = now.pan;

        if (! (it->second == now))
        {
            const auto before = it->second;
            it->second = now;
            juce::Component::SafePointer<MainComponent> self (this);
            recordUndo ("Mezcla (" + t->name + ")",
                        [t, before] { writeMix (*t, before); },
                        [t, now]    { writeMix (*t, now); });
        }
    }

    auto project = readProjectState();
    if (! (project == committedProject))
    {
        const auto before = committedProject;
        committedProject = project;
        recordUndo (tr8 ("Tempo, compás, ciclo o marcadores"),
                    [this, before]  { writeProjectState (before); },
                    [this, project] { writeProjectState (project); });
    }
}

// --- Regiones ---

void MainComponent::deleteRegion (Track& track, juce::int64 regionId)
{
    if (engine.isRecording())
        return;
    const auto taken = engine.takeRegion (track, regionId);
    const Region region = taken.first;
    const int index = taken.second;
    if (index < 0)
        return;
    Track* t = &track;
    recordUndo (tr8 ("Eliminar región"),
                [this, t, region, index] { engine.putRegion (*t, region, index); },
                [this, t, id = region.id] { engine.takeRegion (*t, id); });
    if (selectedRegion == regionId)
        selectedRegion = -1;
    regionsChanged();
}

void MainComponent::splitRegion (Track& track, juce::int64 regionId, juce::int64 position)
{
    if (engine.isRecording())
        return;
    int index = -1;
    for (int i = 0; i < (int) track.regions.size(); ++i)
        if (track.regions[(size_t) i].id == regionId) index = i;
    if (index < 0)
        return;

    const Region original = track.regions[(size_t) index];
    const auto offset = position - original.start;
    if (offset < 64 || offset > original.getLength() - 64)
        return;   // el corte tiene que caer dentro de la región

    // Las dos mitades (también de cada toma, si las hay)
    auto slice = [] (const std::shared_ptr<Clip>& c, juce::int64 from, juce::int64 to) -> std::shared_ptr<Clip>
    {
        to = juce::jmin (to, c->getLength());
        if (to - from < 1)
            return nullptr;
        auto out = AudioEngine::sliceClip (*c, (int) from, (int) (to - from), c->name);
        out->computePeaks();
        return out;
    };

    Region left = original, right = original;
    right.id = Region::newId();
    right.start = original.start + offset;
    left.clip = slice (original.clip, 0, offset);
    right.clip = slice (original.clip, offset, original.getLength());
    left.takes.clear(); right.takes.clear();
    for (auto& take : original.takes)
    {
        if (auto l = slice (take, 0, offset)) left.takes.push_back (l);
        if (auto r = slice (take, offset, take->getLength())) right.takes.push_back (r);
    }
    left.activeTake = juce::jlimit (0, juce::jmax (0, (int) left.takes.size() - 1), original.activeTake);
    right.activeTake = juce::jlimit (0, juce::jmax (0, (int) right.takes.size() - 1), original.activeTake);
    if (left.takes.size() < 2)  left.takes.clear();
    if (right.takes.size() < 2) right.takes.clear();

    Track* t = &track;
    auto apply = [this, t, index] (const Region& a, const Region* b)
    {
        engine.takeRegion (*t, a.id);
        engine.putRegion (*t, a, index);
        if (b != nullptr)
            engine.putRegion (*t, *b, index + 1);
    };
    apply (left, &right);
    recordUndo (tr8 ("Cortar región"),
                [this, t, apply, original, rightId = right.id] { engine.takeRegion (*t, rightId); apply (original, nullptr); },
                [apply, left, right] { apply (left, &right); });
    selectRegion (track, right.id);
    regionsChanged();
}

juce::int64 MainComponent::duplicateRegion (Track& track, juce::int64 regionId, juce::int64 newStart, bool recordInHistory)
{
    for (auto& r : track.regions)
    {
        if (r.id != regionId)
            continue;
        Region copy = r;   // el audio se comparte: al editar una copia se crea un clip nuevo
        copy.id = Region::newId();
        copy.start = juce::jmax ((juce::int64) 0, newStart);
        engine.putRegion (track, copy, -1);
        if (recordInHistory)
        {
            Track* t = &track;
            recordUndo (tr8 ("Duplicar región"),
                        [this, t, id = copy.id] { engine.takeRegion (*t, id); },
                        [this, t, copy] { engine.putRegion (*t, copy, -1); });
            selectRegion (track, copy.id);
        }
        regionsChanged();
        return copy.id;
    }
    return -1;
}

void MainComponent::splitAtPlayhead()
{
    if (selectedTrack == nullptr)
        return;
    const auto pos = engine.getPosition();
    // La región seleccionada, o la que esté bajo el cursor en la pista seleccionada
    for (auto& r : selectedTrack->regions)
        if ((selectedRegion < 0 || r.id == selectedRegion) && pos > r.start && pos < r.getEnd())
        {
            splitRegion (*selectedTrack, r.id, pos);
            return;
        }
}

void MainComponent::duplicateSelectedRegion()
{
    if (selectedTrack == nullptr || selectedRegion < 0)
        return;
    for (auto& r : selectedTrack->regions)
        if (r.id == selectedRegion)
        {
            duplicateRegion (*selectedTrack, r.id, r.getEnd(), true);
            return;
        }
}

// ===================== Batería =====================

void MainComponent::showAddTrackMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Pista de audio");
    m.addItem (2, tr8 ("Pista de batería"));
    juce::Component::SafePointer<MainComponent> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackButton), [self] (int r)
    {
        if (self != nullptr && r > 0)
            self->addTrackWithUndo (r == 2);
    });
}

void MainComponent::openDrumEditor (Track& track)
{
    if (! track.isDrum())
        return;
    if (drumEditorWindow == nullptr || drumEditorWindow->getTrack() != &track)
        drumEditorWindow = std::make_unique<DrumEditorWindow> (track, engine, *this);
    drumEditorWindow->setVisible (true);
    drumEditorWindow->toFront (true);
    drumEditorWindow->refresh();
}

void MainComponent::drumToXml (const Drums::DrumData& d, juce::XmlElement& x)
{
    x.setAttribute ("swing", (double) d.swing);
    x.setAttribute ("humanize", d.humanize);
    juce::StringArray lv;
    for (auto l : d.levels) lv.add (juce::String (l, 3));
    x.setAttribute ("levels", lv.joinIntoString (","));

    for (int p = 0; p < Drums::numPatterns; ++p)
    {
        auto* px = x.createNewChildElement ("PATTERN");
        px->setAttribute ("index", p);
        for (int i = 0; i < Drums::numInstruments; ++i)
        {
            juce::String row;
            for (auto v : d.patterns[(size_t) p].steps[(size_t) i]) row << (int) v;
            px->setAttribute ("i" + juce::String (i), row);
        }
    }

    juce::String song;
    for (int b = 0; b <= d.lastBar(); ++b)
        song << (d.arrangement[(size_t) b] < 0 ? juce::String ("-") : DrumColours::letter (d.arrangement[(size_t) b]));
    x.setAttribute ("song", song);
}

void MainComponent::drumFromXml (Drums::DrumData& d, const juce::XmlElement& x)
{
    d = Drums::DrumData();
    d.swing = (float) x.getDoubleAttribute ("swing", 0.0);
    d.humanize = x.getBoolAttribute ("humanize", true);
    auto lv = juce::StringArray::fromTokens (x.getStringAttribute ("levels"), ",", {});
    for (int i = 0; i < juce::jmin (lv.size(), (int) Drums::numInstruments); ++i)
        d.levels[(size_t) i] = lv[i].getFloatValue();

    for (auto* px : x.getChildWithTagNameIterator ("PATTERN"))
    {
        const int p = px->getIntAttribute ("index", -1);
        if (p < 0 || p >= Drums::numPatterns) continue;
        for (int i = 0; i < Drums::numInstruments; ++i)
        {
            const auto row = px->getStringAttribute ("i" + juce::String (i));
            for (int s = 0; s < juce::jmin (row.length(), Drums::maxSteps); ++s)
                d.patterns[(size_t) p].steps[(size_t) i][(size_t) s] = (uint8_t) juce::jlimit (0, 3, (int) row[s] - (int) '0');
        }
    }

    const auto song = x.getStringAttribute ("song");
    for (int b = 0; b < juce::jmin (song.length(), Drums::maxBars); ++b)
        d.arrangement[(size_t) b] = song[b] == '-' ? (int8_t) -1 : (int8_t) juce::jlimit (0, Drums::numPatterns - 1, (int) (song[b] - 'A'));
}
