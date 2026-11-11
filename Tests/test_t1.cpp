#include "enginetest_common.h"
// Señal de "guitarra": seno de 220 Hz con amplitud configurable
static float sine (juce::int64 n, float amp) { return amp * (float) std::sin (2 * M_PI * 220.0 * n / 48000.0); }
static float peakOf (const Clip& c, int from, int n) { return c.audio.getMagnitude (0, from, n); }

int main()
{
    printf ("1) Grabación normal\n");
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {}); t->inputChannel = 0; t->armed = true;
        rig.signal = [] (juce::int64 n) { return sine (n, 0.5f); };
        rig.engine.countInOn = false; rig.engine.setPosition (48000);
        rig.engine.startRecording (rig.folder); rig.runSeconds (2.0); rig.engine.stop();
        CHECK (t->regions.size() == 1 && t->regions[0].start == 48000, "región en 48000");
        CHECK (! t->regions.empty() && std::abs (t->regions[0].getLength() - 96000) <= 256, "dura 2 s");
        CHECK (! rig.engine.takeLostSamples(), "sin muestras perdidas");
    }

    printf ("2) Compensación de latencia (driver 100 + 156 muestras)\n");
    {
        Rig rig; rig.device.inLat = 100; rig.device.outLat = 156; rig.engine.audioDeviceAboutToStart (&rig.device);
        auto* t = rig.engine.addTrack ("G", {}); t->inputChannel = 0; t->armed = true;
        rig.engine.countInOn = false; rig.engine.setPosition (48000);
        rig.engine.startRecording (rig.folder);
        const auto c0 = rig.inputClock;
        // El golpe se toca en el tiempo musical 48000+24000, pero llega 256 muestras tarde
        rig.signal = [c0] (juce::int64 n) { return (n >= c0 + 24000 + 256 && n < c0 + 24000 + 256 + 64) ? 0.9f : 0.0f; };
        rig.runSeconds (1.0); rig.engine.stop();
        int at = -1;
        if (! t->regions.empty()) { auto& a = t->regions[0].clip->audio; for (int i = 0; i < a.getNumSamples(); ++i) if (a.getSample (0, i) > 0.5f) { at = i; break; } }
        CHECK (rig.engine.getTotalLatency() == 256, "latencia total = 256 (%d)", rig.engine.getTotalLatency());
        CHECK (at == 24000, "el golpe queda en su sitio: muestra %d (esperado 24000)", at);
        rig.engine.manualLatencySamples = 44;
        CHECK (rig.engine.getTotalLatency() == 300, "ajuste manual suma (+44 -> %d)", rig.engine.getTotalLatency());
    }

    printf ("3) Grabación en ciclo: 3,5 vueltas de 1 s -> 4 tomas\n");
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {}); t->inputChannel = 0; t->armed = true;
        rig.engine.countInOn = false;
        rig.engine.loopStart = 48000; rig.engine.loopEnd = 96000; rig.engine.loopOn = true;
        rig.engine.setPosition (0);
        rig.engine.startRecording (rig.folder);
        const auto c0 = rig.inputClock;
        // Cada vuelta con un volumen distinto: 0.1, 0.2, 0.3, 0.4
        rig.signal = [c0] (juce::int64 n) { const int pass = (int) ((n - c0) / 48000); return sine (n, 0.1f * (pass + 1)); };
        CHECK (rig.engine.getPosition() == 48000, "el transporte salta al inicio del ciclo");
        rig.runSeconds (3.5);
        CHECK (rig.engine.getPosition() >= 48000 && rig.engine.getPosition() < 96000, "la posición se mantiene dentro del ciclo (%lld)", (long long) rig.engine.getPosition());
        rig.engine.stop();
        CHECK (t->regions.size() == 1, "una sola región (%d)", (int) t->regions.size());
        if (! t->regions.empty())
        {
            auto& r = t->regions[0];
            CHECK (r.start == 48000, "región en el inicio del ciclo");
            CHECK (r.takes.size() == 4, "4 tomas (%d)", (int) r.takes.size());
            for (size_t k = 0; k < r.takes.size() && k < 3; ++k)
                CHECK (std::abs (peakOf (*r.takes[k], 1000, 40000) - 0.1f * (k + 1)) < 0.02f && r.takes[k]->getLength() == 48000,
                       "toma %d: volumen %.2f, %lld muestras", (int) k + 1, peakOf (*r.takes[k], 1000, 40000), (long long) r.takes[k]->getLength());
            CHECK (r.activeTake == 3 && r.clip == r.takes[3], "activa la última toma");
            rig.engine.setActiveTake (*t, r.id, 1);
            CHECK (r.clip == r.takes[1], "se puede elegir otra toma");
        }
    }

    printf ("4) Pinchado: sustituye solo el tramo 2 s - 3 s\n");
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {}); t->inputChannel = 0;
        auto base = std::make_shared<Clip>(); base->audio.setSize (1, 48000 * 4); 
        for (int i = 0; i < 48000 * 4; ++i) base->audio.setSample (0, i, 0.3f);
        base->computePeaks(); rig.engine.addRegion (*t, base, 0);
        t->armed = true; rig.engine.countInOn = false;
        rig.engine.loopStart = 96000; rig.engine.loopEnd = 144000; rig.engine.punchOn = true; rig.engine.loopOn = true;
        rig.engine.setPosition (0);
        rig.engine.startRecording (rig.folder);
        rig.signal = [] (juce::int64) { return 0.7f; };
        rig.runSeconds (4.0);
        rig.engine.stop();
        CHECK (t->regions.size() == 2, "se añade 1 región (hay %d)", (int) t->regions.size());
        if (t->regions.size() == 2)
        {
            auto& p = t->regions[1];
            CHECK (p.start == 96000 && std::abs (p.getLength() - 48000) <= 1, "toma pinchada: 96000, %lld muestras", (long long) p.getLength());
        }
        // Reproducir sin entrada: fuera del tramo 0.3, dentro 0.7 (no 1.0)
        rig.signal = [] (juce::int64) { return 0.0f; };
        rig.engine.punchOn = false; rig.engine.loopOn = false;
        rig.engine.setPosition (0); rig.output.clear(); rig.engine.play(); rig.runSeconds (4.0); rig.engine.stop();
        CHECK (std::abs (rig.output[48000] - 0.3f) < 0.01f && std::abs (rig.output[120000] - 0.7f) < 0.01f && std::abs (rig.output[170000] - 0.3f) < 0.01f,
               "suena 0.3 / 0.7 / 0.3 (%.2f / %.2f / %.2f)", rig.output[48000], rig.output[120000], rig.output[170000]);
    }

    printf ("5) Precuenta\n");
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {}); t->inputChannel = 0; t->armed = true;
        rig.signal = [] (juce::int64 n) { return sine (n, 0.5f); };
        rig.engine.countInOn = true; rig.engine.bpm = 120.0; rig.engine.setPosition (96000);
        rig.engine.startRecording (rig.folder);
        CHECK (rig.engine.getPosition() == 96000 - 96000, "arranca un compás antes (4/4 a 120 = 96000 muestras)");
        CHECK (rig.engine.isInCountIn(), "está en precuenta");
        rig.runSeconds (3.0); rig.engine.stop();
        CHECK (! t->regions.empty() && t->regions[0].start == 96000 && std::abs (t->regions[0].getLength() - 48000) <= 512,
               "la toma empieza al acabar la precuenta (%lld muestras)", t->regions.empty() ? 0LL : (long long) t->regions[0].getLength());
        bool clicks = false; for (int i = 0; i < 96000; ++i) if (std::abs (rig.output[i]) > 0.1f) { clicks = true; break; }
        CHECK (clicks, "suena el metrónomo en la precuenta");
    }

    printf ("6) Exportar con el ciclo activado no repite el tramo\n");
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {});
        auto ramp = std::make_shared<Clip>(); ramp->audio.setSize (1, 48000 * 3);
        for (int i = 0; i < 48000 * 3; ++i) ramp->audio.setSample (0, i, (float) i / (48000 * 3));
        ramp->computePeaks(); rig.engine.addRegion (*t, ramp, 0);
        rig.engine.loopStart = 0; rig.engine.loopEnd = 48000; rig.engine.loopOn = true;
        juce::MemoryOutputStream* mem = new juce::MemoryOutputStream();
        std::unique_ptr<juce::OutputStream> os (mem);
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32)
                                          .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        float peak = 0; rig.engine.renderOffline (*w, 48000 * 3, [] (double) { return true; }, peak);
        CHECK (peak > 0.95f, "llega al final de la rampa (pico %.2f)", peak);
    }

    printf ("%s (%d fallos)\n", failures ? "HAY FALLOS" : "TODO BIEN", failures);
    return failures;
}
