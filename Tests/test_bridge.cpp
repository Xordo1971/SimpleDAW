#include "enginetest_common.h"
#include "AudioRouting.h"
#include <complex>

// Entrada en otro aparato (AudioRouting.h): la entrada va a otra frecuencia y con su propio reloj
// (un pelín más rápido o más lento); la salida tiene que recibirla convertida, sin cortes ni desbordes.
struct RateDevice : FakeDevice
{
    double rate; int block; int ins;
    RateDevice (double r, int b, int i) : rate (r), block (b), ins (i) {}
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBufferSizeSamples() override { return block; }
    juce::BigInteger getActiveInputChannels() const override { juce::BigInteger b; b.setRange (0, ins, true); return b; }
};

static double toneLevel (const float* x, int n, double f, double sr)
{
    std::complex<double> acc = 0;
    for (int i = 0; i < n; ++i) acc += (double) x[i] * std::exp (std::complex<double> (0, -2 * M_PI * f * i / sr));
    return 2.0 * std::abs (acc) / n;
}

static void runCase (double inRate, int inBlock, double outRate, int outBlock, double drift, const char* name)
{
    printf ("%s\n", name);
    AudioEngine engine;
    InputBridge bridge;
    DeviceRouter router (engine, bridge);
    RateDevice inDev (inRate, inBlock, 1), outDev (outRate, outBlock, 0);
    router.audioDeviceAboutToStart (&outDev);
    bridge.audioDeviceAboutToStart (&inDev);
    router.setUseBridge (true);

    auto* t = engine.addTrack ("Mic", {});
    t->inputChannel = 0;
    t->monitor = true;   // lo que entra sale por la salida

    const double f = 440.0, seconds = 20.0;
    const double realInRate = inRate * (1.0 + drift);   // reloj de la entrada algo distinto
    juce::AudioBuffer<float> inBuf (1, inBlock), outBuf (2, outBlock);
    std::vector<float> out;
    double inTime = 0.0, outTime = 0.0;
    juce::int64 inCount = 0;
    juce::AudioIODeviceCallbackContext ctx;
    int maxReady = 0;
    while (outTime < seconds)
    {
        // Se alternan los dos "relojes" según quién va por detrás
        if (inTime <= outTime)
        {
            for (int i = 0; i < inBlock; ++i)
                inBuf.setSample (0, i, 0.5f * (float) std::sin (2 * M_PI * f * (double) (inCount + i) / inRate));
            inCount += inBlock;
            bridge.audioDeviceIOCallbackWithContext (inBuf.getArrayOfReadPointers(), 1, nullptr, 0, inBlock, ctx);
            inTime += inBlock / realInRate;
        }
        else
        {
            router.audioDeviceIOCallbackWithContext (nullptr, 0, outBuf.getArrayOfWritePointers(), 2, outBlock, ctx);
            for (int i = 0; i < outBlock; ++i) out.push_back (outBuf.getSample (0, i));
            outTime += outBlock / outRate;
            maxReady = std::max (maxReady, bridge.fifo.getNumReady());
        }
    }

    // A partir del segundo 2 (ya cebado): tono a la frecuencia correcta y sin huecos. Con la deriva exagerada
    // de la prueba (300-400 ppm; un aparato real suele estar por debajo de 50) el tono puede moverse un
    // 0,1 % mientras se corrige (menos de 2 cents): por eso la tolerancia del nivel de la DFT de 1 s
    const int start = (int) (2.0 * outRate), n = (int) (outRate * 1.0);
    const double level = toneLevel (out.data() + start, n, f, outRate);
    CHECK (std::abs (level - 0.5) < 0.06, "llega el tono de 440 Hz a %.0f Hz (nivel %.3f)", outRate, level);
    const double levelLate = toneLevel (out.data() + (int) ((seconds - 2.0) * outRate), n, f, outRate);
    CHECK (std::abs (levelLate - 0.5) < 0.06, "sigue igual al final, pese a la deriva del reloj (nivel %.3f)", levelLate);

    int gaps = 0, run = 0;
    for (size_t i = (size_t) start; i < out.size(); ++i)
    {
        run = std::abs (out[i]) < 1.0e-6f ? run + 1 : 0;
        if (run == 64) ++gaps;   // 64 ceros seguidos: un corte (un seno de 440 Hz nunca los da)
    }
    CHECK (gaps == 0, "sin cortes en %.0f s (%d)", seconds - 2.0, gaps);
    CHECK (maxReady < 8192, "la cola no se dispara (máximo %d muestras pendientes)", maxReady);
    CHECK (engine.extraInputLatency.load() > 0 && engine.extraInputLatency.load() < (int) (outRate * 0.06),
           "latencia añadida razonable: %.1f ms", engine.extraInputLatency.load() * 1000.0 / outRate);
}

int main()
{
    runCase (16000.0, 256, 48000.0, 256, +0.0003, "AirPods (16 kHz) -> altavoces a 48 kHz, reloj de la entrada +0,03 %");
    runCase (24000.0, 480, 44100.0, 128, -0.0004, "Micro a 24 kHz -> salida a 44,1 kHz, reloj de la entrada -0,04 %");
    runCase (48000.0, 512, 48000.0, 64, +0.0002, "Mismo 48 kHz pero aparatos distintos (solo deriva)");
    printf (failures == 0 ? "TODO BIEN (0 fallos)\n" : "%d FALLOS\n", failures);
    return failures == 0 ? 0 : 1;
}
