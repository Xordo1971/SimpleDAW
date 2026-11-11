#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Drums.h"
#include <atomic>
#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <memory>
#include <vector>

// Audio en memoria (mono o estéreo) + picos precalculados para dibujar la forma de onda.
// Un clip nunca se modifica mientras suena: el editor crea uno nuevo y lo sustituye.
struct Clip
{
    static constexpr int samplesPerPeak = 256;

    juce::AudioBuffer<float> audio;
    std::vector<float> peaks[2];   // pico por bloque y canal
    std::vector<float> peaksMix;   // máximo de ambos canales (para la vista de pistas)
    juce::String name;
    juce::File file;      // archivo de origen (grabación o importación)
    juce::File savedAs;   // copia guardada en la carpeta del proyecto (no se hereda al editar)
    juce::File recoveryAs;  // copia en la carpeta de recuperación (autoguardado)

    int getNumChannels() const     { return audio.getNumChannels(); }
    juce::int64 getLength() const  { return audio.getNumSamples(); }

    void computePeaks()
    {
        const int len = audio.getNumSamples();
        const int numPeaks = (len + samplesPerPeak - 1) / samplesPerPeak;
        peaksMix.assign ((size_t) numPeaks, 0.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            peaks[ch].clear();
            if (ch >= audio.getNumChannels())
                continue;

            peaks[ch].resize ((size_t) numPeaks);
            const float* data = audio.getReadPointer (ch);
            for (int i = 0; i < numPeaks; ++i)
            {
                const int st = i * samplesPerPeak;
                const int n = juce::jmin (samplesPerPeak, len - st);
                auto r = juce::FloatVectorOperations::findMinAndMax (data + st, n);
                const float p = juce::jmax (std::abs (r.getStart()), std::abs (r.getEnd()));
                peaks[ch][(size_t) i] = p;
                peaksMix[(size_t) i] = juce::jmax (peaksMix[(size_t) i], p);
            }
        }
    }

    // Copia vacía con el mismo nombre y archivo de origen
    std::shared_ptr<Clip> cloneEmpty (int channels, int length) const
    {
        auto c = std::make_shared<Clip>();
        c->audio.setSize (channels, juce::jmax (0, length));
        c->name = name;
        c->file = file;
        return c;
    }
};

// Una región: un clip colocado en la línea de tiempo (posición en muestras)
struct Region
{
    std::shared_ptr<Clip> clip;   // la toma activa (la que suena)
    juce::int64 start = 0;
    juce::int64 id = 0;           // identificador estable (el índice cambia al borrar o mover)

    // Grabación en ciclo: todas las tomas de la región (vacío si solo hay una)
    std::vector<std::shared_ptr<Clip>> takes;
    int activeTake = 0;

    juce::int64 getLength() const { return clip->getLength(); }
    juce::int64 getEnd() const    { return start + getLength(); }

    static juce::int64 newId()
    {
        static std::atomic<juce::int64> counter { 0 };
        return ++counter;
    }
};

// Un efecto o plugin insertado en un canal
struct Insert
{
    std::unique_ptr<juce::AudioProcessor> processor;
    std::atomic<bool> bypass { false };
    bool isExternalPlugin = false;
};

// Dos secciones de inserciones por canal: primero los plugins (p. ej. un simulador de
// amplificador) y después los efectos integrados (delay, reverb...)
enum class Section { plugins = 0, effects = 1 };

struct Track
{
    static constexpr int maxInsertsPerSection = 4;

    ~Track() { delete recorder; }

    juce::String name;
    juce::Colour colour;
    bool isMaster = false;
    bool isBus = false;    // bus auxiliar (reverb / delay compartidos)

    // Pista de batería: patrones y kit sintetizado (nullptr en pistas de audio)
    std::shared_ptr<Drums::DrumData> drum;
    std::unique_ptr<Drums::Kit> kit;
    std::atomic<int> drumPreview { -1 };   // la interfaz pide oír un instrumento (al editar)
    bool isDrum() const { return drum != nullptr; }

    // Envíos post-fader a los buses auxiliares (ganancia lineal 0..1), uno por hueco de bus
    static constexpr int maxBuses = 8;
    std::atomic<float> send[maxBuses] {};

    // Automatización: puntos (posición en muestras, valor) de volumen (dB) y panorama (-1..1)
    enum AutoParam { autoVolume = 0, autoPan = 1 };
    struct AutoPoint { juce::int64 pos; float value; };
    std::vector<AutoPoint> automation[2];          // ordenados por posición; se modifican con el lock del motor
    std::atomic<bool> automationOn[2] { { true }, { true } };

    bool hasAutomation (int p) const { return automationOn[p].load() && ! automation[p].empty(); }

    float automationValueAt (int p, juce::int64 pos) const
    {
        const auto& pts = automation[p];
        if (pos <= pts.front().pos) return pts.front().value;
        if (pos >= pts.back().pos)  return pts.back().value;
        auto it = std::upper_bound (pts.begin(), pts.end(), pos, [] (juce::int64 v, const AutoPoint& a) { return v < a.pos; });
        const auto& b = *it;
        const auto& a = *(it - 1);
        const double t = (double) (pos - a.pos) / (double) juce::jmax ((juce::int64) 1, b.pos - a.pos);
        return (float) (a.value + (b.value - a.value) * t);
    }

    float lastGainL = -1.0f, lastGainR = -1.0f;   // solo hilo de audio: rampa de ganancia sin "zipper"

    std::atomic<int>   inputChannel { 0 };   // índice dentro de las entradas activas; -1 = sin entrada
    std::atomic<float> volumeDb { 0.0f };
    std::atomic<float> pan  { 0.0f };
    std::atomic<bool>  mute { false }, solo { false }, monitor { false }, armed { false };
    std::atomic<float> peak { 0.0f };                       // pico de entrada
    std::atomic<float> outPeakL { 0.0f }, outPeakR { 0.0f }; // pico de salida (tras fader)
    float meterL = 0.0f, meterR = 0.0f;   // lectura para la interfaz (la actualiza la ventana principal)
    float inputHold = 0.0f, outputHold = 0.0f;   // picos máximos mostrados en dB (solo interfaz; clic para reiniciar)

    // Grabación en curso: forma de onda en directo y pico de la toma
    std::vector<float> livePeaks;                // un pico cada Clip::samplesPerPeak muestras (reservado antes de grabar)
    std::atomic<int> livePeakCount { 0 };
    std::atomic<float> recordPeak { 0.0f };
    float liveAccum = 0.0f;                      // solo hilo de audio
    int liveAccumCount = 0;

    // Solo los modifica el hilo de la interfaz, siempre con el lock del motor tomado.
    // El hilo de audio solo los lee (con try-lock).
    std::vector<Region> regions;
    std::vector<std::unique_ptr<Insert>> inserts[2];
    juce::AudioFormatWriter::ThreadedWriter* recorder = nullptr;
    juce::int64 recordStart = 0;
    juce::int64 recordEnd = std::numeric_limits<juce::int64>::max();   // punch out
    juce::File recordFile;

    juce::AudioBuffer<float> work;   // buffer de trabajo del canal (lo reserva el motor)

    std::vector<std::unique_ptr<Insert>>& getInserts (Section s) { return inserts[(int) s]; }
};

class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    enum State { stopped = 0, playing, recording };
    static constexpr int workChannels = 8;      // margen para plugins con entradas extra (sidechain)
    static constexpr int maxBlockSize = 8192;

    AudioEngine()
    {
        formatManager.registerBasicFormats();
        writerThread.startThread();
        master.name = "Master";
        master.isMaster = true;
        master.colour = juce::Colour (0xffb0b4bb);
        master.work.setSize (workChannels, maxBlockSize);

    }

    // ---------- Buses auxiliares (hasta 8, cada uno en su hueco fijo) ----------
    Track* getBus (int slot)             { return slot >= 0 && slot < Track::maxBuses ? busSlots[(size_t) slot].get() : nullptr; }
    const Track* getBus (int slot) const { return slot >= 0 && slot < Track::maxBuses ? busSlots[(size_t) slot].get() : nullptr; }

    int getBusSlot (const Track* t) const
    {
        for (int i = 0; i < Track::maxBuses; ++i)
            if (busSlots[(size_t) i].get() == t && t != nullptr)
                return i;
        return -1;
    }

    // Crea un bus en el primer hueco libre (los envíos de las pistas a ese hueco empiezan en 0)
    int addBus (const juce::String& name, juce::Colour colour)
    {
        for (int i = 0; i < Track::maxBuses; ++i)
        {
            if (busSlots[(size_t) i] != nullptr)
                continue;
            auto bus = std::make_unique<Track>();
            bus->name = name;
            bus->isBus = true;
            bus->colour = colour;
            bus->work.setSize (workChannels, maxBlockSize);
            const juce::ScopedLock sl (lock);
            for (auto* t : tracks)
                t->send[i] = 0.0f;
            busSlots[(size_t) i] = std::move (bus);
            return i;
        }
        return -1;
    }

    // Crear un bus en un hueco concreto (al abrir un proyecto)
    Track* createBusAt (int slot, const juce::String& name, juce::Colour colour)
    {
        if (slot < 0 || slot >= Track::maxBuses)
            return nullptr;
        auto bus = std::make_unique<Track>();
        bus->name = name;
        bus->isBus = true;
        bus->colour = colour;
        bus->work.setSize (workChannels, maxBlockSize);
        auto* raw = bus.get();
        const juce::ScopedLock sl (lock);
        busSlots[(size_t) slot] = std::move (bus);
        return raw;
    }

    // Quitar / volver a poner un bus sin destruirlo (para deshacer)
    std::unique_ptr<Track> detachBus (int slot)
    {
        if (slot < 0 || slot >= Track::maxBuses)
            return nullptr;
        const juce::ScopedLock sl (lock);
        return std::move (busSlots[(size_t) slot]);
    }

    void attachBus (int slot, std::unique_ptr<Track> bus)
    {
        if (slot < 0 || slot >= Track::maxBuses || bus == nullptr)
            return;
        for (auto& list : bus->inserts)
            for (auto& ins : list)
                prepareProcessor (*ins->processor);
        bus->lastGainL = bus->lastGainR = -1.0f;
        const juce::ScopedLock sl (lock);
        busSlots[(size_t) slot] = std::move (bus);
    }

    ~AudioEngine() override
    {
        tracks.clear();
        for (auto& b : busSlots)
            b = nullptr;
        writerThread.stopThread (2000);
    }

    // ---------- Pistas ----------
    Track* addTrack (const juce::String& name, juce::Colour colour)
    {
        auto t = std::make_unique<Track>();
        t->name = name;
        t->colour = colour;
        t->work.setSize (workChannels, maxBlockSize);
        auto* raw = t.get();
        const juce::ScopedLock sl (lock);
        tracks.add (t.release());
        return raw;
    }

    // Quitar una pista sin destruirla (para poder deshacer); devuelve la pista y su posición
    std::pair<std::unique_ptr<Track>, int> detachTrack (Track* t)
    {
        const juce::ScopedLock sl (lock);
        const int idx = tracks.indexOf (t);
        if (idx < 0)
            return { nullptr, -1 };
        return { std::unique_ptr<Track> (tracks.removeAndReturn (idx)), idx };
    }

    Track* attachTrack (std::unique_ptr<Track> t, int index)
    {
        auto* raw = t.get();
        raw->lastGainL = raw->lastGainR = -1.0f;
        for (auto& list : raw->inserts)
            for (auto& ins : list)
                prepareProcessor (*ins->processor);
        const juce::ScopedLock sl (lock);
        tracks.insert (index, t.release());
        return raw;
    }

    // Igual con los efectos y plugins de un canal
    std::unique_ptr<Insert> detachInsert (Track& t, Section s, int index)
    {
        const juce::ScopedLock sl (lock);
        auto& list = t.getInserts (s);
        if (index < 0 || index >= (int) list.size())
            return nullptr;
        auto ins = std::move (list[(size_t) index]);
        list.erase (list.begin() + index);
        return ins;
    }

    void attachInsert (Track& t, Section s, int index, std::unique_ptr<Insert> ins)
    {
        if (ins == nullptr)
            return;
        prepareProcessor (*ins->processor);
        const juce::ScopedLock sl (lock);
        auto& list = t.getInserts (s);
        index = juce::jlimit (0, (int) list.size(), index);
        list.insert (list.begin() + index, std::move (ins));
    }

    // Regiones (para deshacer): quitar por identificador / volver a poner en su sitio
    std::pair<Region, int> takeRegion (Track& t, juce::int64 regionId)
    {
        const juce::ScopedLock sl (lock);
        for (int i = 0; i < (int) t.regions.size(); ++i)
            if (t.regions[(size_t) i].id == regionId)
            {
                Region r = t.regions[(size_t) i];
                t.regions.erase (t.regions.begin() + i);
                return { r, i };
            }
        return { Region(), -1 };
    }

    void putRegion (Track& t, const Region& r, int index)
    {
        const juce::ScopedLock sl (lock);
        index = index < 0 ? (int) t.regions.size() : juce::jlimit (0, (int) t.regions.size(), index);
        t.regions.insert (t.regions.begin() + index, r);
    }

    void setRegionStart (Track& t, juce::int64 regionId, juce::int64 start)
    {
        const juce::ScopedLock sl (lock);
        for (auto& r : t.regions)
            if (r.id == regionId)
                r.start = start;
    }

    // Solo esta pista suena (para exportar pistas por separado); nullptr = normal
    std::atomic<Track*> exclusiveTrack { nullptr };

    Track* addDrumTrack (const juce::String& name, juce::Colour colour)
    {
        auto t = std::make_unique<Track>();
        t->name = name;
        t->colour = colour;
        t->work.setSize (workChannels, maxBlockSize);
        t->drum = std::make_shared<Drums::DrumData>();
        t->kit = std::make_unique<Drums::Kit>();
        t->kit->prepare (sampleRate.load() > 0 ? sampleRate.load() : 48000.0);
        t->inputChannel = -1;
        auto* raw = t.get();
        const juce::ScopedLock sl (lock);
        tracks.add (t.release());
        return raw;
    }

    void removeTrack (Track* t)
    {
        std::unique_ptr<Track> doomed;
        {
            const juce::ScopedLock sl (lock);
            const int idx = tracks.indexOf (t);
            if (idx < 0) return;
            doomed.reset (tracks.removeAndReturn (idx));
        }
        // Se destruye fuera del lock (los plugins pueden tardar en cerrarse)
    }

    Track& getMaster()                   { return master; }
    const Track& getMaster() const       { return master; }
    juce::CriticalSection& getLock()     { return lock; }

    // ---------- Inserciones (efectos y plugins) ----------
    // Prepara el procesador y lo coloca en el canal. Devuelve false si la sección está llena.
    bool addInsert (Track& t, Section s, std::unique_ptr<juce::AudioProcessor> p, bool external, int slot = -1)
    {
        if (p == nullptr)
            return false;

        prepareProcessor (*p);

        auto ins = std::make_unique<Insert>();
        ins->processor = std::move (p);
        ins->isExternalPlugin = external;

        const juce::ScopedLock sl (lock);
        auto& list = t.getInserts (s);
        if ((int) list.size() >= Track::maxInsertsPerSection)
            return false;
        if (slot < 0 || slot > (int) list.size())
            slot = (int) list.size();
        list.insert (list.begin() + slot, std::move (ins));
        return true;
    }

    // Quita una inserción. El llamador debe cerrar antes su ventana de edición.
    void removeInsert (Track& t, Section s, int index)
    {
        std::unique_ptr<Insert> doomed;
        {
            const juce::ScopedLock sl (lock);
            auto& list = t.getInserts (s);
            if (index < 0 || index >= (int) list.size()) return;
            doomed = std::move (list[(size_t) index]);
            list.erase (list.begin() + index);
        }
        if (doomed != nullptr)
            doomed->processor->releaseResources();
    }

    // ---------- Tempo y compás ----------
    std::atomic<double> bpm { 120.0 };
    std::atomic<int> sigNum { 4 }, sigDen { 4 };    // 3/4, 4/4, 6/8...
    std::atomic<bool> metronomeOn { false };
    std::atomic<bool> countInOn { true };           // un compás de precuenta al grabar
    std::atomic<float> metronomeVolume { 0.7f };

    // Duración de una parte (la figura del denominador) y de un compás, en muestras
    double samplesPerBeat() const
    {
        const double sr = sampleRate.load() > 0 ? sampleRate.load() : 48000.0;
        return sr * 60.0 / juce::jlimit (20.0, 400.0, bpm.load()) * 4.0 / juce::jmax (1, sigDen.load());
    }
    double samplesPerBar() const { return samplesPerBeat() * juce::jmax (1, sigNum.load()); }

    bool isInCountIn() const { return state.load() == recording && position.load() < recordStartPosition.load(); }

    // ---------- Ciclo y pinchado (punch in/out) ----------
    // El rango del ciclo sirve también de rango de pinchado
    std::atomic<bool> loopOn { false }, punchOn { false };
    std::atomic<juce::int64> loopStart { 0 }, loopEnd { 0 };
    bool hasValidLoop() const { return loopEnd.load() - loopStart.load() > 1000; }

    enum class RecordMode { normal = 0, cycle, punch };
    RecordMode getRecordMode() const { return (RecordMode) recordMode.load(); }

    // ---------- Compensación de latencia ----------
    // Retardo total (ida y vuelta) que se resta a lo grabado = latencia del driver + ajuste manual
    std::atomic<int> manualLatencySamples { 0 };
    int getReportedLatency() const { return reportedLatency.load(); }
    int getTotalLatency() const    { return juce::jmax (0, reportedLatency.load() + manualLatencySamples.load()); }

    // Medición por bucle (cable de una salida a una entrada): emite un clic y mide cuándo vuelve
    void startLatencyMeasurement (int inputChannel)
    {
        measureBuffer.assign ((size_t) (juce::jmax (8000.0, sampleRate.load()) * 1.0), 0.0f);
        measureInput = inputChannel;
        measurePos = 0;
        measureState = 1;   // 1 = midiendo, 2 = terminado
    }
    bool isMeasuringLatency() const { return measureState.load() == 1; }

    // Devuelve el retardo en muestras, o -1 si no llegó el clic (sin cable o volumen muy bajo)
    int getMeasuredLatency()
    {
        if (measureState.load() != 2)
            return -1;
        measureState = 0;
        int best = -1;
        float peak = 0.0f;
        for (size_t i = 0; i < measureBuffer.size(); ++i)
            if (std::abs (measureBuffer[i]) > peak) { peak = std::abs (measureBuffer[i]); best = (int) i; }
        if (peak < 0.02f)
            return -1;
        // Primera muestra que supera la mitad del pico (el inicio del clic, no su máximo)
        for (int i = 0; i <= best; ++i)
            if (std::abs (measureBuffer[(size_t) i]) > peak * 0.5f)
                return i;
        return best;
    }

    // ¿Se perdieron muestras al escribir en disco durante la última toma?
    bool takeLostSamples() const { return droppedSamples.load(); }

    // ---------- Tomas ----------
    bool setActiveTake (Track& t, juce::int64 regionId, int take)
    {
        const juce::ScopedLock sl (lock);
        for (auto& r : t.regions)
            if (r.id == regionId && take >= 0 && take < (int) r.takes.size())
            {
                r.activeTake = take;
                r.clip = r.takes[(size_t) take];
                return true;
            }
        return false;
    }

    // ---------- Afinador ----------
    std::atomic<int> tunerInput { -1 };             // canal de entrada que se analiza (-1: apagado)
    std::atomic<bool> muteForTuner { false };       // silenciar la salida mientras se afina

    // Lee (desde la interfaz) las muestras acumuladas para el afinador
    int readTunerSamples (float* dest, int maxSamples)
    {
        const auto scope = tunerFifo.read (juce::jmin (maxSamples, tunerFifo.getNumReady()));
        if (scope.blockSize1 > 0) std::copy_n (tunerData.data() + scope.startIndex1, scope.blockSize1, dest);
        if (scope.blockSize2 > 0) std::copy_n (tunerData.data() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);
        return scope.blockSize1 + scope.blockSize2;
    }

    // ---------- Transporte ----------
    int  getState() const        { return state.load(); }
    bool isRecording() const     { return state.load() == recording; }
    double getSampleRate() const { return sampleRate.load(); }
    int getBlockSize() const     { return blockSize.load(); }

    juce::int64 getPosition() const  { return position.load(); }
    void setPosition (juce::int64 p) { position = juce::jmax ((juce::int64) 0, p); }

    double getPositionSeconds() const
    {
        const double sr = getSampleRate();
        return sr > 0 ? (double) getPosition() / sr : 0.0;
    }

    void play()
    {
        if (state.load() == stopped)
            state = playing;
    }

    int startRecording (const juce::File& folder)
    {
        if (state.load() == recording)
            return 0;

        const double sr = sampleRate.load();
        if (sr <= 0)
            return 0;

        folder.createDirectory();

        struct Pending
        {
            Track* track;
            std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
            juce::File file;
        };
        std::vector<Pending> pending;
        juce::WavAudioFormat wav;
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
        const auto options = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (1).withBitsPerSample (24);

        for (auto* t : tracks)
        {
            if (! t->armed.load() || t->inputChannel.load() < 0)
                continue;

            auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (t->name) + "_" + stamp, ".wav", false);
            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
            if (stream == nullptr)
                continue;

            // WAV mono, 24 bits, a la frecuencia del dispositivo (graba la señal limpia, sin efectos)
            // Búfer amplio (~5 s) por si el disco se atasca un momento
            if (auto writer = wav.createWriterFor (stream, options))
                pending.push_back ({ t, std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 1 << 18), file });
        }

        if (pending.empty())
            return 0;

        // Forma de onda en directo: hasta una hora por toma
        const auto capacity = (size_t) (sr * 3600.0 / Clip::samplesPerPeak);
        for (auto& p : pending)
        {
            p.track->livePeaks.assign (capacity, 0.0f);
            p.track->livePeakCount = 0;
            p.track->recordPeak = 0.0f;
            p.track->liveAccum = 0.0f;
            p.track->liveAccumCount = 0;
        }

        // Ventana de grabación y punto de arranque del transporte según el modo
        const auto bar = (juce::int64) std::llround (samplesPerBar());
        const bool loopOk = hasValidLoop();
        const auto mode = punchOn.load() && loopOk ? RecordMode::punch
                        : loopOn.load() && loopOk  ? RecordMode::cycle
                                                   : RecordMode::normal;
        auto transportStart = position.load();
        juce::int64 windowStart = transportStart, windowEnd = std::numeric_limits<juce::int64>::max();

        if (mode == RecordMode::cycle)
        {
            transportStart = windowStart = loopStart.load();
            windowEnd = std::numeric_limits<juce::int64>::max();   // graba todas las vueltas seguidas
        }
        else if (mode == RecordMode::punch)
        {
            windowStart = loopStart.load();
            windowEnd = loopEnd.load();
            if (transportStart >= windowStart)                     // arrancar un compás antes del punch in
                transportStart = juce::jmax ((juce::int64) 0, windowStart - bar);
        }

        droppedSamples = false;
        const juce::ScopedLock sl (lock);
        for (auto& p : pending)
        {
            p.track->recorder    = p.writer.release();
            p.track->recordStart = windowStart;
            p.track->recordEnd   = windowEnd;
            p.track->recordFile  = p.file;
        }
        recordMode = (int) mode;
        recordStartPosition = transportStart;
        // Precuenta: el transporte arranca un compás antes
        position = transportStart - (countInOn.load() ? bar : 0);
        state = recording;
        return (int) pending.size();
    }

    void stop()
    {
        if (position.load() < 0)
            position = recordStartPosition.load();   // parado durante la precuenta
        std::vector<std::pair<Track*, std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>>> finished;
        {
            const juce::ScopedLock sl (lock);
            state = stopped;
            for (auto* t : tracks)
            {
                if (t->recorder != nullptr)
                {
                    finished.emplace_back (t, std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> (t->recorder));
                    t->recorder = nullptr;
                }
            }
        }

        lastTakePeaks.clear();
        for (auto& f : finished)
            lastTakePeaks.push_back ({ f.first, f.first->recordPeak.load() });

        for (auto& f : finished)
        {
            f.second.reset();
            juce::String error;
            if (auto clip = loadAudioFile (f.first->recordFile, false, error))
                addRecordedTake (*f.first, clip);
            f.first->livePeakCount = 0;
            std::vector<float>().swap (f.first->livePeaks);   // el hilo de audio ya no la usa
        }
    }

    // ---------- Regiones y archivos de audio ----------
    juce::int64 addRegion (Track& t, std::shared_ptr<Clip> clip, juce::int64 start)
    {
        Region r;
        r.clip = std::move (clip);
        r.start = juce::jmax ((juce::int64) 0, start);
        r.id = Region::newId();
        const juce::ScopedLock sl (lock);
        t.regions.push_back (r);
        return r.id;
    }

    // Sustituye el audio de una región (edición). El clip antiguo se libera fuera del lock.
    bool replaceClip (Track& t, juce::int64 regionId, std::shared_ptr<Clip> newClip)
    {
        std::shared_ptr<Clip> old;
        {
            const juce::ScopedLock sl (lock);
            for (auto& r : t.regions)
            {
                if (r.id == regionId)
                {
                    old = std::move (r.clip);
                    r.clip = newClip;
                    if (r.activeTake >= 0 && r.activeTake < (int) r.takes.size())
                        r.takes[(size_t) r.activeTake] = std::move (newClip);
                    return true;
                }
            }
        }
        return false;
    }

    bool canRead (const juce::File& f)            { return formatManager.findFormatForFileExtension (f.getFileExtension()) != nullptr; }
    juce::String getSupportedWildcards()          { return formatManager.getWildcardForAllFormats(); }

    int getFileChannels (const juce::File& f)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
        return reader != nullptr ? (int) reader->numChannels : 0;
    }

    // Carga un archivo (wav, aiff, mp3, flac...) a la frecuencia del dispositivo.
    // forceMono: mezcla los canales en uno. Si no, conserva mono o estéreo (máx. 2 canales).
    std::shared_ptr<Clip> loadAudioFile (const juce::File& file, bool forceMono, juce::String& error)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr)
        {
            error = "Formato no reconocido o archivo dañado.";
            return nullptr;
        }

        const double fileRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
        const double targetRate = sampleRate.load() > 0 ? sampleRate.load() : fileRate;
        const juce::int64 len = reader->lengthInSamples;

        if (len <= 0)                         { error = "El archivo está vacío."; return nullptr; }
        if (len > (juce::int64) (fileRate * 60.0 * 60.0)) { error = "El archivo dura más de una hora."; return nullptr; }

        const int fileChannels = juce::jlimit (1, 2, (int) reader->numChannels);
        juce::AudioBuffer<float> source (fileChannels, (int) len);
        reader->read (&source, 0, (int) len, 0, true, fileChannels > 1);

        const int outChannels = forceMono ? 1 : fileChannels;
        if (forceMono && fileChannels > 1)
        {
            source.addFrom (0, 0, source, 1, 0, (int) len);
            source.applyGain (0, 0, (int) len, 0.5f);
        }

        auto clip = std::make_shared<Clip>();
        clip->name = file.getFileNameWithoutExtension();
        clip->file = file;

        if (std::abs (fileRate - targetRate) < 0.5)
        {
            clip->audio.setSize (outChannels, (int) len);
            for (int ch = 0; ch < outChannels; ++ch)
                clip->audio.copyFrom (ch, 0, source, ch, 0, (int) len);
        }
        else
        {
            // Conversión de frecuencia de muestreo (p. ej. un MP3 a 44,1 kHz en una sesión a 48 kHz)
            const double ratio = fileRate / targetRate;
            const int outLen = (int) std::floor ((double) len / ratio);
            clip->audio.setSize (outChannels, outLen);
            for (int ch = 0; ch < outChannels; ++ch)
            {
                juce::WindowedSincInterpolator interp;
                interp.process (ratio, source.getReadPointer (ch), clip->audio.getWritePointer (ch), outLen,
                                (int) len, 0);
            }
        }

        clip->computePeaks();
        return clip;
    }

    // Coloca lo grabado: quita la latencia y, si fue en ciclo, lo reparte en tomas
    void addRecordedTake (Track& t, std::shared_ptr<Clip> clip)
    {
        const int latency = getTotalLatency();
        const int len = (int) clip->getLength() - latency;
        if (len <= 0)
            return;

        // Lo grabado llega "tarde" (ida y vuelta por la interfaz): se descartan las primeras muestras
        auto trimmed = clip->cloneEmpty (clip->getNumChannels(), len);
        for (int ch = 0; ch < clip->getNumChannels(); ++ch)
            trimmed->audio.copyFrom (ch, 0, clip->audio, ch, latency, len);

        const auto mode = getRecordMode();
        const auto loopLen = loopEnd.load() - loopStart.load();

        if (mode != RecordMode::cycle || loopLen <= 0 || len <= loopLen + loopLen / 4)
        {
            if (mode == RecordMode::cycle && len > loopLen)   // poco más de una vuelta: solo la primera
                trimmed = sliceClip (*trimmed, 0, (int) loopLen, trimmed->name);
            trimmed->computePeaks();
            addRegion (t, trimmed, t.recordStart);
            return;
        }

        // Una toma por vuelta (la última solo si llegó a un cuarto del ciclo)
        Region r;
        r.start = t.recordStart;
        r.id = Region::newId();
        for (juce::int64 from = 0, n = 1; from < len; from += loopLen, ++n)
        {
            const int count = (int) juce::jmin (loopLen, (juce::int64) len - from);
            if (n > 1 && count < loopLen / 4)
                break;
            auto take = sliceClip (*trimmed, (int) from, count, trimmed->name + " (toma " + juce::String (n) + ")");
            take->computePeaks();
            r.takes.push_back (take);
        }
        r.activeTake = (int) r.takes.size() - 1;   // la última vuelta suele ser la buena
        r.clip = r.takes.back();

        const juce::ScopedLock sl (lock);
        t.regions.push_back (r);
    }

    static std::shared_ptr<Clip> sliceClip (const Clip& c, int from, int count, const juce::String& name)
    {
        auto out = c.cloneEmpty (c.getNumChannels(), count);
        for (int ch = 0; ch < c.getNumChannels(); ++ch)
            out->audio.copyFrom (ch, 0, c.audio, ch, from, count);
        out->name = name;
        return out;
    }

    // Pico de cada pista en la última toma (para avisar si ha saturado)
    const std::vector<std::pair<Track*, float>>& getLastTakePeaks() const { return lastTakePeaks; }

    double getContentEndSeconds() const
    {
        auto end = position.load();
        for (auto* t : tracks)
            for (auto& r : t->regions)
                end = juce::jmax (end, r.getEnd());
        const double sr = getSampleRate();
        return sr > 0 ? (double) end / sr : 0.0;
    }

    // ---------- Hilo de audio ----------
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                           float* const* out, int numOut,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr)
                juce::FloatVectorOperations::clear (out[ch], numSamples);

        // Nunca bloqueamos el hilo de audio: si la interfaz está tocando algo, saltamos este bloque
        const juce::ScopedTryLock sl (lock);
        if (! sl.isLocked() || numSamples > maxBlockSize || rendering.load())
            return;

        if (measureState.load() == 1)
        {
            runLatencyMeasurement (in, numIn, out, numOut, numSamples);
            return;
        }

        processAudio (in, numIn, out, numOut, numSamples);

        // Última protección para oídos y altavoces: nada de valores inválidos ni por encima de 0 dBFS
        for (int ch = 0; ch < numOut; ++ch)
            if (auto* o = out[ch])
                for (int i = 0; i < numSamples; ++i)
                    o[i] = std::isfinite (o[i]) ? juce::jlimit (-1.0f, 1.0f, o[i]) : 0.0f;
    }

    // Si un efecto se desboca (valores inválidos o > +40 dB) se silencia, se desactiva y se avisa.
    // La interfaz lo recoge con takeRunawayInsert().
    bool takeRunawayInsert (Track*& track, juce::AudioProcessor*& processor)
    {
        track = runawayTrack.exchange (nullptr);
        processor = runawayProcessor.exchange (nullptr);
        return track != nullptr && processor != nullptr;
    }

    void runLatencyMeasurement (const float* const* in, int numIn, float* const* out, int numOut, int numSamples)
    {
        // Clic en las salidas al empezar; se recoge la entrada elegida durante un segundo
        if (measurePos == 0)
            for (int ch = 0; ch < juce::jmin (2, numOut); ++ch)
                if (out[ch] != nullptr)
                    for (int i = 0; i < juce::jmin (16, numSamples); ++i)
                        out[ch][i] = 0.8f;

        const float* src = measureInput >= 0 && measureInput < numIn && in != nullptr ? in[measureInput] : nullptr;
        for (int i = 0; i < numSamples && measurePos < (int) measureBuffer.size(); ++i, ++measurePos)
            measureBuffer[(size_t) measurePos] = src != nullptr ? src[i] : 0.0f;

        if (measurePos >= (int) measureBuffer.size())
            measureState = 2;
    }

    // ---------- Exportación (render offline) ----------
    // El llamador debe haber quitado antes el motor del dispositivo de audio.
    juce::int64 getContentEndSamples() const
    {
        juce::int64 end = 0;
        for (auto* t : tracks)
        {
            for (auto& r : t->regions)
                end = juce::jmax (end, r.getEnd());
            if (t->isDrum())
                end = juce::jmax (end, (juce::int64) std::llround ((t->drum->lastBar() + 1) * samplesPerBar()));
        }
        return end;
    }

    double getMaxTailSeconds() const
    {
        double tail = 0.0;
        auto check = [&tail] (const Track& t)
        {
            for (auto& list : t.inserts)
                for (auto& ins : list)
                    if (! ins->bypass.load())
                        tail = juce::jmax (tail, ins->processor->getTailLengthSeconds());
        };
        for (auto* t : tracks) check (*t);
        check (master);
        for (auto& b : busSlots) if (b != nullptr) check (*b);
        return juce::jlimit (1.0, 8.0, tail);
    }

    // Devuelve false si se canceló (progress devolvió false) o hubo un error de escritura
    bool renderOffline (juce::AudioFormatWriter& writer, juce::int64 length, const std::function<bool (double)>& progress,
                        float& peak)
    {
        peak = 0.0f;
        constexpr int block = 512;
        rendering = true;
        const auto savedPos = position.load();
        stop();

        {
            const juce::ScopedLock sl (lock);
            blockSize = block;
            auto prepareTrack = [this] (Track& t)
            {
                for (auto& list : t.inserts)
                    for (auto& ins : list)
                    {
                        ins->processor->releaseResources();
                        prepareProcessor (*ins->processor);   // reinicia colas de delay, reverb...
                    }
                if (t.kit != nullptr)
                    t.kit->prepare (sampleRate.load() > 0 ? sampleRate.load() : 48000.0);
            };
            for (auto* t : tracks) prepareTrack (*t);
            for (auto& b : busSlots) if (b != nullptr) prepareTrack (*b);
            prepareTrack (master);
        }

        position = 0;
        state = playing;

        juce::AudioBuffer<float> out (2, block);
        bool ok = true;
        for (juce::int64 done = 0; done < length && ok; done += block)
        {
            const int n = (int) juce::jmin ((juce::int64) block, length - done);
            out.clear();
            {
                const juce::ScopedLock sl (lock);   // aquí sí esperamos: no se puede saltar ningún bloque
                processAudio (nullptr, 0, out.getArrayOfWritePointers(), 2, n);
            }
            peak = juce::jmax (peak, out.getMagnitude (0, n));
            ok = writer.writeFromAudioSampleBuffer (out, 0, n) && progress ((double) (done + n) / (double) length);
        }

        state = stopped;
        position = savedPos;
        rendering = false;
        return ok;
    }

private:
    // Divide el bloque en segmentos para saltar al inicio del ciclo exactamente en su final
    void processAudio (const float* const* in, int numIn, float* const* out, int numOut, int numSamples)
    {
        constexpr int maxChannels = 64;
        const float* inSeg[maxChannels] {};
        float* outSeg[maxChannels] {};
        numIn = juce::jmin (numIn, maxChannels);
        numOut = juce::jmin (numOut, maxChannels);

        int done = 0;
        while (done < numSamples)
        {
            const int st = state.load();
            const auto pos = position.load();
            const auto ls = loopStart.load(), le = loopEnd.load();
            const bool looping = st != stopped && loopOn.load() && le - ls > 1000 && pos < le && pos >= ls - 1
                                  && ! (st == recording && getRecordMode() == RecordMode::punch)
                                  && ! rendering.load();   // al exportar, siempre de corrido

            int seg = numSamples - done;
            if (looping)
                seg = (int) juce::jmin ((juce::int64) seg, le - pos);

            for (int ch = 0; ch < numIn; ++ch)  inSeg[ch]  = in != nullptr && in[ch] != nullptr ? in[ch] + done : nullptr;
            for (int ch = 0; ch < numOut; ++ch) outSeg[ch] = out[ch] != nullptr ? out[ch] + done : nullptr;

            processRange (in != nullptr ? inSeg : nullptr, numIn, outSeg, numOut, seg, st, pos);

            done += seg;
            if (st != stopped)
            {
                auto next = pos + seg;
                if (looping && next >= le)
                    next = ls;
                position.store (next);
            }
        }
    }

    void processRange (const float* const* in, int numIn, float* const* out, int numOut, int numSamples,
                       int st, juce::int64 pos)
    {
        juce::ScopedNoDenormals noDenormals;

        bool anySolo = false;
        for (auto* t : tracks)
            if (t->solo.load()) { anySolo = true; break; }
        Track* const only = exclusiveTrack.load();

        const int tunerCh = tunerInput.load();
        if (tunerCh >= 0 && tunerCh < numIn && in != nullptr && in[tunerCh] != nullptr)
        {
            const auto scope = tunerFifo.write (juce::jmin (numSamples, tunerFifo.getFreeSpace()));
            if (scope.blockSize1 > 0) std::copy_n (in[tunerCh], scope.blockSize1, tunerData.data() + scope.startIndex1);
            if (scope.blockSize2 > 0) std::copy_n (in[tunerCh] + scope.blockSize1, scope.blockSize2, tunerData.data() + scope.startIndex2);
        }

        auto& mix = master.work;
        mix.clear (0, numSamples);
        mix.clear (1, numSamples);
        for (auto& b : busSlots)
        {
            if (b == nullptr) continue;
            b->work.clear (0, numSamples);
            b->work.clear (1, numSamples);
        }

        for (auto* t : tracks)
        {
            auto& w = t->work;
            w.clear (0, numSamples);
            w.clear (1, numSamples);
            bool hasSignal = false;

            // Entrada: medidor, grabación y monitorización
            const int inCh = t->inputChannel.load();
            const float* src = (inCh >= 0 && inCh < numIn) ? in[inCh] : nullptr;

            if (src != nullptr)
            {
                auto range = juce::FloatVectorOperations::findMinAndMax (src, numSamples);
                const float p = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
                updatePeak (t->peak, p);

                if (st == recording && t->recorder != nullptr)
                {
                    const auto skip = juce::jlimit ((juce::int64) 0, (juce::int64) numSamples, t->recordStart - pos);
                    const auto stopAt = juce::jlimit ((juce::int64) 0, (juce::int64) numSamples, t->recordEnd - pos);
                    if (skip < stopAt)
                    {
                        const float* from = src + skip;
                        const int n = (int) (stopAt - skip);
                        if (! t->recorder->write (&from, n))
                            droppedSamples = true;   // el disco no da abasto: se avisará al parar

                        float takePeak = 0.0f;
                        int count = t->livePeakCount.load (std::memory_order_relaxed);
                        for (int i = 0; i < n; ++i)
                        {
                            const float v = std::abs (from[i]);
                            takePeak = juce::jmax (takePeak, v);
                            t->liveAccum = juce::jmax (t->liveAccum, v);
                            if (++t->liveAccumCount == Clip::samplesPerPeak)
                            {
                                if (count < (int) t->livePeaks.size())
                                    t->livePeaks[(size_t) count++] = t->liveAccum;
                                t->liveAccum = 0.0f;
                                t->liveAccumCount = 0;
                            }
                        }
                        t->livePeakCount.store (count, std::memory_order_release);
                        updatePeak (t->recordPeak, takePeak);
                    }
                }

                if (t->monitor.load())
                {
                    w.copyFrom (0, 0, src, numSamples);
                    w.copyFrom (1, 0, src, numSamples);
                    hasSignal = true;
                }
            }

            // Reproducción de las regiones. Si se solapan, suena la más reciente (así funciona
            // el pinchado: la toma nueva sustituye a la antigua solo en ese tramo)
            if (st != stopped && ! t->regions.empty())
            {
                std::fill_n (covered.begin(), numSamples, (uint8_t) 0);
                for (auto it = t->regions.rbegin(); it != t->regions.rend(); ++it)
                {
                    const auto& r = *it;
                    const juce::int64 from = juce::jmax (pos, r.start);
                    const juce::int64 to   = juce::jmin (pos + (juce::int64) numSamples, r.getEnd());
                    if (to <= from)
                        continue;

                    const auto& audio = r.clip->audio;
                    const bool stereo = audio.getNumChannels() > 1;
                    int i = (int) (from - pos);
                    const int end = (int) (to - pos);
                    while (i < end)
                    {
                        while (i < end && covered[(size_t) i] != 0) ++i;   // tramo ya cubierto por otra región
                        int j = i;
                        while (j < end && covered[(size_t) j] == 0) ++j;
                        if (j > i)
                        {
                            const int srcPos = (int) (pos + i - r.start);
                            const float* left  = audio.getReadPointer (0, srcPos);
                            const float* right = stereo ? audio.getReadPointer (1, srcPos) : left;
                            w.addFrom (0, i, left, j - i);
                            w.addFrom (1, i, right, j - i);
                            std::fill (covered.begin() + i, covered.begin() + j, (uint8_t) 1);
                            hasSignal = true;
                        }
                        i = j;
                    }
                }
            }
            juce::ignoreUnused (hasSignal);

            if (t->isDrum())
                renderDrums (*t, numSamples, st, pos);

            processInserts (*t, numSamples);

            // Automatización (en reproducción): mueve el fader y el panorama
            if (st != stopped)
            {
                const auto mid = pos + numSamples / 2;
                if (t->hasAutomation (Track::autoVolume)) t->volumeDb = t->automationValueAt (Track::autoVolume, mid);
                if (t->hasAutomation (Track::autoPan))    t->pan = t->automationValueAt (Track::autoPan, mid);
            }

            const bool audible = only != nullptr ? t == only
                                                 : ! t->mute.load() && (! anySolo || t->solo.load());
            applyFaderAndMeter (*t, numSamples, audible);

            if (audible)
            {
                mix.addFrom (0, 0, w, 0, 0, numSamples);
                mix.addFrom (1, 0, w, 1, 0, numSamples);

                // Envíos post-fader a los buses
                for (int b = 0; b < Track::maxBuses; ++b)
                {
                    auto* bus = busSlots[(size_t) b].get();
                    const float amount = t->send[b].load();
                    if (bus != nullptr && amount > 0.0001f)
                    {
                        bus->work.addFrom (0, 0, w, 0, 0, numSamples, amount);
                        bus->work.addFrom (1, 0, w, 1, 0, numSamples, amount);
                    }
                }
            }
        }

        // Buses auxiliares (reverb y delay compartidos): sus efectos y su fader, y al master
        for (auto& slot : busSlots)
        {
            if (slot == nullptr) continue;
            auto& b = *slot;
            processInserts (b, numSamples);
            const bool audible = ! b.mute.load();
            applyFaderAndMeter (b, numSamples, audible);
            if (audible)
            {
                mix.addFrom (0, 0, b.work, 0, 0, numSamples);
                mix.addFrom (1, 0, b.work, 1, 0, numSamples);
            }
        }

        // Canal master
        processInserts (master, numSamples);
        applyFaderAndMeter (master, numSamples, ! master.mute.load());

        if (! master.mute.load())
        {
            if (numOut > 0 && out[0] != nullptr) juce::FloatVectorOperations::copy (out[0], mix.getReadPointer (0), numSamples);
            if (numOut > 1 && out[1] != nullptr) juce::FloatVectorOperations::copy (out[1], mix.getReadPointer (1), numSamples);
        }
        else
        {
            for (int ch = 0; ch < numOut; ++ch)
                if (out[ch] != nullptr)
                    juce::FloatVectorOperations::clear (out[ch], numSamples);
        }

        if (st != stopped && ! rendering.load())
        {
            const bool countIn = st == recording && pos < recordStartPosition.load();
            if (metronomeOn.load() || countIn)
                addMetronome (out, numOut, numSamples, pos);
        }

        if (muteForTuner.load() && ! rendering.load())   // al exportar nunca se silencia
            for (int ch = 0; ch < numOut; ++ch)
                if (out[ch] != nullptr)
                    juce::FloatVectorOperations::clear (out[ch], numSamples);
    }

    // Batería: dispara los pasos que caen en este tramo y suma el kit a la pista
    void renderDrums (Track& t, int numSamples, int st, juce::int64 pos)
    {
        auto& d = *t.drum;
        if (st != stopped)
        {
            const double sr = sampleRate.load() > 0 ? sampleRate.load() : 48000.0;
            const double sixteenth = sr * 60.0 / juce::jlimit (20.0, 400.0, bpm.load()) / 4.0;
            const int stepsPerBar = juce::jlimit (1, Drums::maxSteps, (int) std::lround (samplesPerBar() / sixteenth));
            const double swingOffset = juce::jlimit (0.0f, 1.0f, d.swing) * sixteenth / 3.0;

            for (auto g = (juce::int64) std::floor ((double) pos / sixteenth) - 1; (double) g * sixteenth < (double) (pos + numSamples); ++g)
            {
                if (g < 0)
                    continue;
                const auto bar = g / stepsPerBar;
                const int step = (int) (g % stepsPerBar);
                const double when = (double) g * sixteenth + ((step & 1) != 0 ? swingOffset : 0.0);
                const auto offset = (juce::int64) std::floor (when) - pos;
                if (offset < 0 || offset >= numSamples || bar >= Drums::maxBars)
                    continue;
                const int pattern = d.arrangement[(size_t) bar];
                if (pattern < 0 || pattern >= Drums::numPatterns)
                    continue;
                for (int inst = 0; inst < Drums::numInstruments; ++inst)
                {
                    const auto v = d.patterns[(size_t) pattern].steps[(size_t) inst][(size_t) step];
                    if (v == 0)
                        continue;
                    float vel = Drums::velocityOf (v);
                    if (d.humanize)
                    {
                        humanizeSeed = humanizeSeed * 1664525u + 1013904223u;
                        vel *= 0.9f + 0.1f * (float) (humanizeSeed >> 8) / (float) (1u << 24);
                    }
                    t.kit->trigger (inst, vel, (int) offset);
                }
            }
        }

        const int preview = t.drumPreview.exchange (-1);
        if (preview >= 0 && preview < Drums::numInstruments)
            t.kit->trigger (preview, 0.85f, 0);

        t.kit->render (t.work.getWritePointer (0), t.work.getWritePointer (1), numSamples, d.levels);
    }

    // Clic del metrónomo: tono corto con caída rápida; más agudo y fuerte en la primera parte del compás
    void addMetronome (float* const* out, int numOut, int numSamples, juce::int64 pos)
    {
        const double spb = samplesPerBeat();
        const int num = juce::jmax (1, sigNum.load());
        const double sr = sampleRate.load() > 0 ? sampleRate.load() : 48000.0;
        const float volume = metronomeVolume.load();

        for (int i = 0; i < numSamples; ++i)
        {
            const double p = (double) (pos + i);
            const double beat = std::floor (p / spb);
            if (p - beat * spb < 1.0)   // empieza una parte en esta muestra
            {
                const auto beatIndex = (juce::int64) beat;
                const bool accent = ((beatIndex % num) + num) % num == 0;
                clickFreq = accent ? 1760.0 : 1175.0;
                clickGain = volume * (accent ? 0.9f : 0.55f);
                clickPhase = 0.0;
                clickPos = 0;
                clickLength = (int) (sr * 0.045);
            }

            if (clickPos < clickLength)
            {
                const float tt = (float) clickPos / (float) sr;
                const float env = std::exp (-tt * 90.0f) * juce::jmin (1.0f, clickPos / (float) (sr * 0.0008));
                const float v = clickGain * env * (float) std::sin (clickPhase);
                clickPhase += juce::MathConstants<double>::twoPi * clickFreq / sr;
                ++clickPos;
                for (int ch = 0; ch < juce::jmin (2, numOut); ++ch)
                    if (out[ch] != nullptr)
                        out[ch][i] += v;
            }
        }
    }

public:
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        sampleRate = device->getCurrentSampleRate();
        reportedLatency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
        blockSize  = juce::jmin (maxBlockSize, device->getCurrentBufferSizeSamples());

        // Vuelve a preparar todos los efectos y plugins con la nueva frecuencia/buffer
        const juce::ScopedLock sl (lock);
        auto prepareTrack = [this] (Track& t)
        {
            for (auto& list : t.inserts)
                for (auto& ins : list)
                    prepareProcessor (*ins->processor);
            if (t.kit != nullptr)
                t.kit->prepare (sampleRate.load());
        };
        for (auto* t : tracks) prepareTrack (*t);
        for (auto& b : busSlots) if (b != nullptr) prepareTrack (*b);
        prepareTrack (master);
    }

    void audioDeviceStopped() override {}

private:
    static void updatePeak (std::atomic<float>& target, float p)
    {
        float prev = target.load();
        while (p > prev && ! target.compare_exchange_weak (prev, p)) {}
    }

    void prepareProcessor (juce::AudioProcessor& p)
    {
        const double sr = sampleRate.load() > 0 ? sampleRate.load() : 44100.0;
        const int bs = blockSize.load() > 0 ? blockSize.load() : 512;
        p.setRateAndBufferSizeDetails (sr, bs);
        p.prepareToPlay (sr, bs);
    }

    static bool isSane (const juce::AudioBuffer<float>& b, int numSamples)
    {
        for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
        {
            const auto* d = b.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                if (! (std::abs (d[i]) < 100.0f))   // también atrapa NaN e infinito
                    return false;
        }
        return true;
    }

    std::atomic<Track*> runawayTrack { nullptr };
    std::atomic<juce::AudioProcessor*> runawayProcessor { nullptr };

    void processInserts (Track& t, int numSamples)
    {
        auto& w = t.work;
        for (auto& list : t.inserts)
        {
            for (auto& ins : list)
            {
                if (ins->bypass.load())
                    continue;

                auto& p = *ins->processor;
                const int ins_ = p.getTotalNumInputChannels();
                const int outs = p.getTotalNumOutputChannels();
                const int nch  = juce::jlimit (1, workChannels, juce::jmax (2, juce::jmax (ins_, outs)));

                for (int ch = 2; ch < nch; ++ch)       // entradas extra (sidechain) en silencio
                    w.clear (ch, 0, numSamples);

                juce::AudioBuffer<float> view (w.getArrayOfWritePointers(), nch, numSamples);
                midiScratch.clear();
                p.processBlock (view, midiScratch);

                if (! isSane (w, numSamples))
                {
                    w.clear (0, numSamples);
                    ins->bypass = true;
                    runawayTrack = &t;
                    runawayProcessor = &p;
                    continue;
                }

                if (outs == 1)                          // plugin mono: duplicamos a la derecha
                    w.copyFrom (1, 0, w, 0, 0, numSamples);
            }
        }
    }

    void applyFaderAndMeter (Track& t, int numSamples, bool audible)
    {
        auto& w = t.work;
        const float g = juce::Decibels::decibelsToGain (t.volumeDb.load(), -60.0f);
        const float angle = (t.pan.load() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        // Ley de potencia constante normalizada para que en el centro la ganancia sea 1
        const float gl = juce::jmin (g * std::cos (angle) * juce::MathConstants<float>::sqrt2, g);
        const float gr = juce::jmin (g * std::sin (angle) * juce::MathConstants<float>::sqrt2, g);
        if (t.lastGainL < 0.0f) { t.lastGainL = gl; t.lastGainR = gr; }
        w.applyGainRamp (0, 0, numSamples, t.lastGainL, gl);
        w.applyGainRamp (1, 0, numSamples, t.lastGainR, gr);
        t.lastGainL = gl;
        t.lastGainR = gr;

        if (audible)
        {
            updatePeak (t.outPeakL, w.getMagnitude (0, 0, numSamples));
            updatePeak (t.outPeakR, w.getMagnitude (1, 0, numSamples));
        }
    }

    juce::TimeSliceThread writerThread { "Escritura de audio" };
    juce::AudioFormatManager formatManager;
    juce::CriticalSection lock;
    juce::OwnedArray<Track> tracks;
    Track master;
    std::array<std::unique_ptr<Track>, Track::maxBuses> busSlots;
    juce::MidiBuffer midiScratch;

    std::atomic<int> state { stopped };
    std::atomic<juce::int64> position { 0 };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> blockSize { 0 };
    std::atomic<bool> rendering { false };
    std::atomic<juce::int64> recordStartPosition { 0 };
    std::atomic<int> recordMode { 0 }, reportedLatency { 0 };
    std::atomic<bool> droppedSamples { false };
    std::array<uint8_t, maxBlockSize> covered {};   // tramos ya sonando (solapes de regiones)

    // Medición de latencia
    std::atomic<int> measureState { 0 };
    std::vector<float> measureBuffer;
    int measureInput = 0, measurePos = 0;
    std::vector<std::pair<Track*, float>> lastTakePeaks;
    uint32_t humanizeSeed = 12345u;

    // Estado del clic del metrónomo (solo hilo de audio)
    double clickPhase = 0.0, clickFreq = 1000.0;
    float clickGain = 0.0f;
    int clickPos = 1 << 30, clickLength = 0;

    // Muestras para el afinador (hilo de audio -> interfaz)
    juce::AbstractFifo tunerFifo { 32768 };
    std::vector<float> tunerData = std::vector<float> (32768);
};
