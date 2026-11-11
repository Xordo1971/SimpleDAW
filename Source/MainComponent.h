#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "AppPaths.h"
#include "Dialogs.h"
#include "AudioEngine.h"
#include "Effects.h"
#include "Theme.h"
#include "Timeline.h"
#include "TrackComponent.h"
#include "MixerComponent.h"
#include "PluginWindows.h"
#include "AudioEditor.h"
#include "ExportDialog.h"
#include "Tuner.h"
#include "AudioSettingsPanel.h"
#include "Undo.h"
#include "PresetChain.h"
#include "ResourceMeter.h"
#include "Updater.h"
#include "Splash.h"
#include <map>

// Barra para cambiar la altura del editor arrastrando
class DragBar : public juce::Component
{
public:
    std::function<void (int deltaY, bool start)> onDrag;
    DragBar() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::border);
        g.setColour (Theme::textDim.withAlpha (0.5f));
        const float cx = getWidth() * 0.5f;
        g.fillRoundedRectangle (cx - 18.0f, getHeight() * 0.5f - 1.0f, 36.0f, 2.0f, 1.0f);
    }
    void mouseDown (const juce::MouseEvent&) override             { if (onDrag) onDrag (0, true); }
    void mouseDrag (const juce::MouseEvent& e) override           { if (onDrag) onDrag (e.getDistanceFromDragStartY(), false); }
};

class MainComponent : public juce::Component,
                      public MixerHost,
                      public juce::FileDragAndDropTarget,
                      public juce::MenuBarModel,
                      private juce::ChangeListener,
                      private juce::Timer,
                      private juce::ScrollBar::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override { return handleKey (key); }
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }
    void parentHierarchyChanged() override             { updateWindowTitle(); }

    // MixerHost
    void showInsertMenu (Track&, Section, int slot, juce::Component& target) override;
    void showInsertOptions (Track&, Section, int slot, juce::Component& target) override;
    void openInsertEditor (Track&, Section, int slot) override;
    void showInsertPanel (Track&, juce::Component& target) override;
    void selectRegion (Track&, juce::int64 regionId) override;
    void openAudioEditor() override;
    void transposeRegion (Track&, juce::int64 regionId) override;
    void changeRegionSpeed (Track&, juce::int64 regionId) override;
    void regionsChanged() override;
    void importFiles (const juce::StringArray& files, Track* target, juce::int64 position) override;

    // Deshacer global y operaciones de región
    void recordUndo (const juce::String& name, std::function<void()> undoFn, std::function<void()> redoFn) override;
    void undo() override;
    void redo() override;
    bool canUndo() const override { return undoManager.canUndo(); }
    bool canRedo() const override { return undoManager.canRedo(); }
    void deleteRegion (Track&, juce::int64 regionId) override;
    void splitRegion (Track&, juce::int64 regionId, juce::int64 position) override;
    juce::int64 duplicateRegion (Track&, juce::int64 regionId, juce::int64 newStart, bool recordInHistory) override;

    // Menús (barra de menús de macOS / menú de la ventana en Windows)
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int itemId, int) override;

    // Cerrar la aplicación (pregunta si hay cambios sin guardar)
    void requestQuit();

    // Al arrancar: si quedó una sesión sin cerrar bien, ofrecer recuperarla
    void checkForRecovery();
    void checkForUpdates (bool askedByUser);   // GitHub: ¿hay una versión nueva?
    void showWhatsNewIfUpdated();              // tras actualizar: novedades de la versión nueva (CHANGELOG.md)

    // Diagnóstico: con la variable de entorno SIMPLEDAW_CAPTURA=carpeta guarda imágenes de la ventana
    // (barra normal y con nombres desplegados) y se cierra. Sirve para revisar el diseño sin pantalla.
    void saveDiagnosticCaptures (const juce::File& folder);

    // Soltar archivos fuera de una pista: se crean pistas nuevas
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    bool handleKey (const juce::KeyPress&) override;

private:
    // Pistas
    Track* addTrack (const juce::String& name = {}, bool drum = false);
    void chooseFilesToImport();
    void doImport (const juce::StringArray& files, Track* target, juce::int64 position, bool forceMono);
    void toggleEditor();

    // Proyectos
    void newProject();
    void openProject();
    void loadProject (const juce::File&, bool isRecovery = false);
    void saveProject (std::function<void (bool)> done = {});
    void saveProjectAs (std::function<void (bool)> done = {});
    bool writeProject (const juce::File& projectFile);
    void clearProject();
    bool hasUnsavedChanges();
    void markSaved();
    void askToSaveThen (std::function<void()> action);
    void updateWindowTitle();
    std::unique_ptr<juce::XmlElement> createProjectXml (const juce::File* audioFolder, juce::StringArray& errors, bool forRecovery = false);

    // Autoguardado y recuperación
    void autosaveTick();
    void clearRecovery();
    juce::String lastRecoverySnapshot;
    juce::TimedCallback autosaveTimer { [this] { autosaveTick(); } };
    void insertsToXml (Track&, juce::XmlElement& trackXml);
    void insertsFromXml (Track&, const juce::XmlElement& trackXml, juce::StringArray& warnings);
    static void trackParamsToXml (const Track&, juce::XmlElement&);
    static void trackParamsFromXml (Track&, const juce::XmlElement&);
    static void preferStereoLayout (juce::AudioPluginInstance&);

    // Exportar
    void exportMix();
    void startExport (const juce::File& target, ExportSettings settings);
    void setEditorVisible (bool);
    TrackComponent* componentFor (const Track*) const;
    void removeTrack (TrackComponent&);
    void layoutTracks();
    void updateMixerTracks();
    void updateMeters();
    bool trackExists (const Track*) const;
    juce::StringArray getActiveInputNames() const;

    // Transporte
    void togglePlay();
    void toggleRecord();
    void stopTransport();
    void goToStart();
    void zoom (double factor);

    // Línea de tiempo
    void updateScrollBar();
    void repaintTimeline();
    double getVisibleSeconds() const;

    // Mezclador, efectos y plugins
    void toggleMixer();
    void animateMixer();
    void toggleTuner();
    void toggleMetronome();
    void toggleCycle();

    // Deshacer: pistas que se quitan sin destruirse, y vigilancia de cambios de mezcla / proyecto
    struct TrackHolder { std::unique_ptr<Track> track; int uiIndex = -1, engineIndex = -1; };
    std::shared_ptr<TrackHolder> detachTrackFromProject (Track*);
    Track* attachTrackToProject (TrackHolder&);
    void addTrackWithUndo (bool drum = false);
    void showAddTrackMenu();
    void openDrumEditor (Track&) override;
    static void drumToXml (const Drums::DrumData&, juce::XmlElement&);
    static void drumFromXml (Drums::DrumData&, const juce::XmlElement&);
    std::unique_ptr<DrumEditorWindow> drumEditorWindow;
    void watchForChanges();
    void syncChangeWatcher();
    void clearHistory();
    void splitAtPlayhead();
    void duplicateSelectedRegion();

    struct MixState
    {
        float vol, pan; std::array<float, Track::maxBuses> sends; bool mute, solo;
        bool operator== (const MixState& o) const { return vol == o.vol && pan == o.pan && sends == o.sends && mute == o.mute && solo == o.solo; }
    };
    struct ProjectState
    {
        double bpm; int num, den; juce::int64 loopStart, loopEnd; bool loopOn, punchOn; std::vector<Timeline::Marker> markers;
        bool operator== (const ProjectState& o) const
        {
            return bpm == o.bpm && num == o.num && den == o.den && loopStart == o.loopStart && loopEnd == o.loopEnd
                && loopOn == o.loopOn && punchOn == o.punchOn && markers.size() == o.markers.size()
                && std::equal (markers.begin(), markers.end(), o.markers.begin(),
                               [] (const Timeline::Marker& a, const Timeline::Marker& b) { return a.pos == b.pos && a.name == b.name; });
        }
    };
    static MixState readMix (const Track&);
    static void writeMix (Track&, const MixState&);
    ProjectState readProjectState() const;
    void writeProjectState (const ProjectState&);
    std::map<const Track*, MixState> committedMix;
    ProjectState committedProject {};
    int groupingUndo = 0;   // > 0: las acciones se agrupan en un solo paso
    void setupDefaultBuses();
    juce::Array<std::pair<int, Track*>> activeBuses() const;
    void removeAllBuses();
    int createBus (const juce::String& name, const juce::String& effectName);
    static juce::Colour busColour (int slot);
    void showAddBusMenu (juce::Component& target) override;
    void showBusMenu (Track& bus, juce::Component& target) override;
    void toggleAutomationView();
    void addMarkerAtPlayhead();
    void jumpToMarker (int direction);
    void togglePunch();
    void showCycleMenu();
    void ensureLoopRange();
    void loadAppSettings();
    void saveAppSettings();
    void offerUpdate (const Updater::Release&);
    juce::String skippedVersion;   // "Saltar esta versión"
    juce::String lastRunVersion;   // versión con la que se abrió la última vez (para enseñar las novedades)
    std::unique_ptr<SplashWindow> aboutWindow;   // "Ver > Acerca de SimpleDAW"
    void showAbout();
    bool updateWaiting = false;    // ya descargada: se instala al cerrar
    void showMetronomeOptions();
    void setTempo (double bpm);
    void setTimeSignature (int num, int den);
    juce::String positionText() const;
    void showPluginManager();
    void loadExternalPlugin (Track&, int slot, const juce::PluginDescription&);
    void closeEditorFor (juce::AudioProcessor&);
    int applyAmpPreset (Track&, juce::AudioProcessor& amp, const juce::XmlElement& preset);
    void closeEditorsForTrack (Track&);
    void removeInsert (Track&, Section, int slot);
    void recordInsertAdded (Track&, Section, int slot);

    // Audio
    void showAudioSettings();
    void refreshDeviceInfo();
    void saveAudioSettings();

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    // El orden importa: se destruyen en orden inverso
    SimpleDAWLookAndFeel lookAndFeel;
    AudioEngine engine;
    juce::AudioDeviceManager deviceManager;
    ResourceMeter resourceMeter { deviceManager };   // CPU y RAM, arriba a la derecha
    juce::AudioPluginFormatManager pluginFormats;
    juce::KnownPluginList knownPlugins;
    juce::TooltipWindow tooltips { this };
    Timeline timeline;

    IconButton addTrackButton { "pista",     IconButton::Icon::plus };
    IconButton audioButton    { "audio",     IconButton::Icon::settings };
    IconButton mixerButton    { "mezclador", IconButton::Icon::mixer };
    IconButton pluginsButton  { "plugins",   IconButton::Icon::plugin };
    IconButton importButton   { "importar",  IconButton::Icon::import };
    IconButton editorButton   { "editor",    IconButton::Icon::wave };
    IconButton tunerButton    { "afinador",  IconButton::Icon::tuner,  Theme::soloYellow };
    IconButton metronomeButton{ "metronomo", IconButton::Icon::metronome, Theme::soloYellow };
    IconButton cycleButton    { "ciclo",     IconButton::Icon::cycle, juce::Colour (0xffffc801) };
    IconButton toStartButton  { "inicio",    IconButton::Icon::toStart };
    IconButton playButton     { "play",      IconButton::Icon::play,   Theme::playGreen };
    IconButton stopButton     { "stop",      IconButton::Icon::stop };
    IconButton recordButton   { "rec",       IconButton::Icon::record, Theme::recordRed };
    IconButton zoomOutButton  { "alejar",    IconButton::Icon::zoomOut };
    IconButton zoomInButton   { "acercar",   IconButton::Icon::zoomIn };

    // Zoom de las pistas con reguladores: a lo ancho (tiempo) y a lo alto (altura de las pistas)
    juce::Slider hZoomSlider, vZoomSlider;
    int trackHeight = 84;
    void setTrackHeight (int h);
    void syncZoomSliders();

    // Franja plegable bajo la barra superior: muestra u oculta el nombre de cada botón
    struct LabelsToggle : public juce::Button
    {
        LabelsToggle() : juce::Button ("nombres") {}
        bool expanded = false;
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (Theme::panel.darker (down ? 0.3f : 0.15f));
            g.fillRect (r);
            g.setColour (Theme::border);
            g.drawHorizontalLine (getHeight() - 1, 0.0f, r.getRight());
            // Asa central con una flecha (abajo: desplegar; arriba: plegar)
            auto handle = r.withSizeKeepingCentre (46.0f, r.getHeight());
            g.setColour ((over ? Theme::text : Theme::textDim).withAlpha (over ? 0.9f : 0.6f));
            juce::Path p;
            const float cx = handle.getCentreX(), cy = handle.getCentreY(), w = 5.0f, h = 2.5f;
            if (expanded) { p.startNewSubPath (cx - w, cy + h); p.lineTo (cx, cy - h); p.lineTo (cx + w, cy + h); }
            else          { p.startNewSubPath (cx - w, cy - h); p.lineTo (cx, cy + h); p.lineTo (cx + w, cy - h); }
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    };
    LabelsToggle labelsToggle;
    bool showButtonLabels = false;
    int toolbarTotalHeight() const;
    juce::Rectangle<int> vZoomLabelArea;
    LcdDisplay lcd;
    juce::Label deviceInfo;

    Ruler ruler { engine, timeline };
    juce::Viewport viewport;
    juce::Component trackContainer;
    juce::ScrollBar hScroll { false };
    juce::OwnedArray<TrackComponent> trackComponents;

    std::unique_ptr<ChannelStrip> masterStrip;   // canal master fijo a la izquierda
    std::unique_ptr<MixerComponent> mixer;
    bool mixerOpen = false;          // el mezclador se despliega junto al master
    float mixerReveal = 0.0f;        // 0 = oculto, 1 = desplegado (animado)
    juce::TimedCallback mixerAnimation { [this] { animateMixer(); } };

    std::unique_ptr<TunerWindow> tunerWindow;
    juce::OwnedArray<PluginEditorWindow> editorWindows;

    std::unique_ptr<AudioEditor> audioEditor;
    DragBar editorDragBar;
    bool editorVisible = false;
    int editorHeight = 280, editorHeightAtDragStart = 280;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::File projectFile;            // vacío = proyecto sin guardar
    juce::String savedSnapshot;        // estado al guardar, para saber si hay cambios
    juce::Image logo;
    juce::Rectangle<int> logoArea;

    Track* selectedTrack = nullptr;
    juce::int64 selectedRegion = -1;

    int trackCounter = 0;
    juce::int64 lastDrawnPosition = -1;

    juce::UndoManager undoManager { 300000, 50 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
