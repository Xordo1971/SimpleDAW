#include "settings.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
// Sustituto mínimo de juce::Colour (el módulo gráfico no hace falta para probar el audio)
namespace juce { struct Colour { Colour (uint32 = 0) {} }; namespace Colours { inline const Colour red; } }
#include "AudioEngine.h"
#include <cstdio>

// Dispositivo falso: solo para que el motor conozca frecuencia y buffer
struct FakeDevice : juce::AudioIODevice
{
    FakeDevice() : juce::AudioIODevice ("fake", "fake") {}
    juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override  { return { "In1", "In2" }; }
    std::optional<juce::BigInteger> getDefaultOutputChannels() const override { return {}; }
    std::optional<juce::BigInteger> getDefaultInputChannels() const override  { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 256 }; }
    int getDefaultBufferSize() override { return 256; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 256; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return 3; }
    int getOutputLatencyInSamples() override { return outLat; }
    int getInputLatencyInSamples() override { return inLat; }
    int inLat = 0, outLat = 0;
};

struct Rig
{
    AudioEngine engine;
    FakeDevice device;
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sdaw_test");
    juce::AudioBuffer<float> in { 2, 256 }, out { 2, 256 };
    juce::int64 inputClock = 0;
    std::vector<float> output;                        // lo que sale por el canal izquierdo                       // "tiempo real" de la entrada
    std::function<float (juce::int64)> signal = [] (juce::int64) { return 0.0f; };

    Rig() { folder.deleteRecursively(); folder.createDirectory(); engine.audioDeviceAboutToStart (&device); }

    void run (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 256; ++i) { const float v = signal (inputClock + i); in.setSample (0, i, v); in.setSample (1, i, 0.0f); }
            inputClock += 256;
            juce::AudioIODeviceCallbackContext ctx;
            engine.audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, 256, ctx);
            if (output.size() < 48000 * 60) for (int i = 0; i < 256; ++i) output.push_back (out.getSample (0, i));
            if ((b & 3) == 0) juce::Thread::sleep (1);   // deja respirar al hilo que escribe en disco
        }
    }
    void runSeconds (double s) { run ((int) std::ceil (s * 48000.0 / 256.0)); }
};

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) printf ("  OK   "); else { printf ("  FALLO "); ++failures; } printf (__VA_ARGS__); printf ("\n"); } while (0)
