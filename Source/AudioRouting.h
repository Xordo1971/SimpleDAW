#pragma once
// Entrada y salida en dispositivos distintos (p. ej. grabar con el micro de los AirPods y escuchar por los
// altavoces del Mac), aunque vayan a frecuencias de muestreo diferentes.
//
// La salida es el dispositivo principal (AudioDeviceManager de la ventana) y su reloj manda: el motor corre en
// su hilo. La entrada se abre aparte en otro AudioDeviceManager; InputBridge copia lo que llega a una cola sin
// bloqueos y DeviceRouter, en el hilo de la salida, lo convierte a la frecuencia de la salida (interpolación
// sinc) corrigiendo la deriva entre los dos relojes (cada aparato tiene el suyo) con un leve ajuste de la
// relación de conversión según lo llena que esté la cola. Así lo hace también un dispositivo agregado de macOS.
#include <juce_audio_devices/juce_audio_devices.h>
#include "AudioEngine.h"

class InputBridge : public juce::AudioIODeviceCallback
{
public:
    static constexpr int maxChannels = 16;
    static constexpr int capacity = 1 << 17;   // ~2,7 s a 48 kHz

    InputBridge() : fifo (capacity), store (maxChannels, capacity) {}

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        rate = d->getCurrentSampleRate();
        blockSize = d->getCurrentBufferSizeSamples();
        deviceLatency = d->getInputLatencyInSamples();
        channels = juce::jlimit (0, maxChannels, d->getActiveInputChannels().countNumberOfSetBits());
        overflow = true;   // que la salida vacíe la cola y vuelva a cebarla
        running = true;
    }

    void audioDeviceStopped() override { running = false; }

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const*, int, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        const int nch = juce::jmin (numIn, channels.load());
        if (fifo.getFreeSpace() < numSamples)
        {
            overflow = true;   // la salida no consume (exportando, parada...): se descarta
            return;
        }
        const auto scope = fifo.write (numSamples);
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            const float* src = ch < nch ? in[ch] : nullptr;
            auto copy = [&] (int start, int count, int offset)
            {
                if (count <= 0) return;
                if (src != nullptr) store.copyFrom (ch, start, src + offset, count);
                else                store.clear (ch, start, count);
            };
            copy (scope.startIndex1, scope.blockSize1, 0);
            copy (scope.startIndex2, scope.blockSize2, scope.blockSize1);
        }
    }

    juce::AbstractFifo fifo;
    juce::AudioBuffer<float> store;
    std::atomic<double> rate { 0.0 };
    std::atomic<int> blockSize { 0 }, deviceLatency { 0 }, channels { 0 };
    std::atomic<bool> running { false }, overflow { false };
};

class DeviceRouter : public juce::AudioIODeviceCallback
{
public:
    DeviceRouter (AudioEngine& e, InputBridge& b) : engine (e), bridge (b) {}

    // true: la entrada viene de InputBridge (otro dispositivo); false: la del propio dispositivo de salida
    void setUseBridge (bool on) { useBridge = on; primed = false; }
    bool isUsingBridge() const  { return useBridge.load(); }

    // Datos para enseñar en Preferencias
    double getInputRate() const   { return bridge.rate.load(); }
    double getOutputRate() const  { return outRate.load(); }
    int getBridgeLatency() const  { return engine.extraInputLatency.load(); }
    bool isConverting() const     { return useBridge.load() && std::abs (bridge.rate.load() - outRate.load()) > 0.5; }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        outRate = d->getCurrentSampleRate();
        outBlock = d->getCurrentBufferSizeSamples();
        acc.setSize (InputBridge::maxChannels, accSize, false, true, true);
        tmp.setSize (InputBridge::maxChannels, juce::jmax (8192, outBlock.load()), false, true, true);
        primed = false;
        engine.audioDeviceAboutToStart (d);
    }

    void audioDeviceStopped() override { engine.audioDeviceStopped(); }

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext& ctx) override
    {
        if (! useBridge.load())
        {
            engine.extraInputLatency = 0;
            engine.audioDeviceIOCallbackWithContext (in, numIn, out, numOut, numSamples, ctx);
            return;
        }

        const int nch = juce::jlimit (0, InputBridge::maxChannels, bridge.channels.load());
        if (numSamples > tmp.getNumSamples())
            return;
        convert (nch, numSamples);

        const float* ptrs[InputBridge::maxChannels] {};
        for (int ch = 0; ch < nch; ++ch)
            ptrs[ch] = tmp.getReadPointer (ch);
        engine.audioDeviceIOCallbackWithContext (ptrs, nch, out, numOut, numSamples, ctx);
    }

private:
    static constexpr int accSize = 1 << 15;

    // Saca de la cola lo que haya y produce numSamples a la frecuencia de la salida en tmp
    void convert (int nch, int numSamples)
    {
        const double inRate = bridge.rate.load(), oRate = outRate.load();
        if (! bridge.running.load() || inRate <= 0 || oRate <= 0 || nch == 0)
        {
            tmp.clear();
            primed = false;
            return;
        }

        if (bridge.overflow.exchange (false))
        {
            bridge.fifo.finishedRead (bridge.fifo.getNumReady());
            readPos = writePos = 0;
            primed = false;
        }

        // Pasar de la cola al acumulador
        if (readPos > accSize / 2)
        {
            const int keep = writePos - readPos;
            for (int ch = 0; ch < nch; ++ch)
                std::memmove (acc.getWritePointer (ch), acc.getReadPointer (ch, readPos), sizeof (float) * (size_t) keep);
            readPos = 0;
            writePos = keep;
        }
        const int ready = juce::jmin (bridge.fifo.getNumReady(), accSize - writePos);
        if (ready > 0)
        {
            const auto scope = bridge.fifo.read (ready);
            for (int ch = 0; ch < nch; ++ch)
            {
                if (scope.blockSize1 > 0) acc.copyFrom (ch, writePos, bridge.store, ch, scope.startIndex1, scope.blockSize1);
                if (scope.blockSize2 > 0) acc.copyFrom (ch, writePos + scope.blockSize1, bridge.store, ch, scope.startIndex2, scope.blockSize2);
            }
            writePos += ready;
        }

        const double nominal = inRate / oRate;   // muestras de entrada por cada una de salida
        // Colchón: lo justo para que ni los bloques de la entrada ni los de la salida lo vacíen
        const double target = (double) bridge.blockSize.load() * 1.5 + (double) outBlock.load() * nominal * 1.5 + 64.0;
        const int fill = writePos - readPos;

        engine.extraInputLatency = (int) std::lround ((target + bridge.deviceLatency.load()) / nominal);

        if (! primed)
        {
            if (fill < (int) target)
            {
                tmp.clear();
                return;   // aún cebando
            }
            // Empezar justo en el colchón: se descarta lo que sobre
            readPos = writePos - (int) target;
            smoothedFill = target;
            for (auto& r : resamplers) r.reset();
            primed = true;
        }

        // Corrección de la deriva: si la cola crece, se consume un pelín más deprisa (y al revés). El llenado
        // se promedia durante unos segundos (sube y baja a cada bloque) para que el tono no oscile
        const double blocksPerSecond = oRate / (double) juce::jmax (1, numSamples);
        const double alpha = 1.0 - std::exp (-1.0 / (3.0 * blocksPerSecond));   // ~3 s
        smoothedFill += alpha * ((double) (writePos - readPos) - smoothedFill);
        const double error = (smoothedFill - target) / target;
        const double ratio = nominal * (1.0 + juce::jlimit (-0.005, 0.005, error * 0.002));

        const int needed = (int) std::ceil ((double) numSamples * ratio) + 8;
        if (writePos - readPos < needed)
        {
            tmp.clear();
            primed = false;   // se ha vaciado: se vuelve a cebar
            return;
        }

        int used = 0;
        for (int ch = 0; ch < nch; ++ch)
            used = resamplers[(size_t) ch].process (ratio, acc.getReadPointer (ch, readPos), tmp.getWritePointer (ch), numSamples);
        readPos += used;
    }

    AudioEngine& engine;
    InputBridge& bridge;
    std::atomic<bool> useBridge { false }, primed { false };
    std::atomic<double> outRate { 0.0 };
    std::atomic<int> outBlock { 512 };
    juce::AudioBuffer<float> acc, tmp;
    int readPos = 0, writePos = 0;
    double smoothedFill = 0.0;
    std::array<juce::WindowedSincInterpolator, InputBridge::maxChannels> resamplers;
};
