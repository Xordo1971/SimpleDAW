#include "settings.h"
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_dsp/juce_dsp.h>
#include "Effects.h"
#include <cstdio>
#include <complex>
static int failures = 0;
#define CHECK(c, ...) do { if (c) printf ("  OK   "); else { printf ("  FALLO "); ++failures; } printf (__VA_ARGS__); printf ("\n"); } while (0)

// EQ de canal (ChannelEqEffect): plano = transparente; campana, cortes, apagado y estado guardado
static double amplitudeAt (const float* x, int n, double f, double sr)
{
    std::complex<double> acc = 0;
    for (int i = 0; i < n; ++i) acc += (double) x[i] * std::exp (std::complex<double> (0, -2 * M_PI * f * i / sr));
    return 2.0 * std::abs (acc) / n;
}

static void set (juce::AudioProcessor& p, const char* id, float plain)
{
    for (auto* q : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (q))
            if (r->paramID == id) r->setValueNotifyingHost (r->convertTo0to1 (plain));
}

// Ganancia en dB del EQ a una frecuencia (seno de 0,5 en estado estable)
static double gainDb (ChannelEqEffect& eq, double f)
{
    const double sr = 48000; const int block = 256, total = 24000;
    eq.prepareToPlay (sr, block);
    juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi; std::vector<float> out;
    for (int pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i) { const float v = 0.5f * (float) std::sin (2 * M_PI * f * (pos + i) / sr); buf.setSample (0, i, v); buf.setSample (1, i, v); }
        eq.processBlock (buf, midi);
        for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
    }
    const int n = 9600;
    return juce::Decibels::gainToDecibels (amplitudeAt (out.data() + total - n, n, f, sr) / 0.5, -120.0);
}

int main()
{
    printf ("EQ de canal\n");
    {
        ChannelEqEffect eq;
        double worst = 0;
        for (double f : { 40.0, 100.0, 440.0, 1000.0, 5000.0, 12000.0 })
            worst = std::max (worst, std::abs (gainDb (eq, f)));
        CHECK (worst < 0.1, "plano: no cambia el sonido (desviacion maxima %.3f dB)", worst);
        CHECK (! eq.isShaping(), "plano: no se marca como activo en el recuadro");
    }
    {
        ChannelEqEffect eq;
        set (eq, "b5f", 1000.0f); set (eq, "b5g", 6.0f); set (eq, "b5q", 1.0f);
        const double at = gainDb (eq, 1000.0), far = gainDb (eq, 100.0);
        CHECK (std::abs (at - 6.0) < 0.5, "campana +6 dB a 1 kHz: %.2f dB", at);
        CHECK (std::abs (far) < 0.8, "lejos de la campana casi nada: %.2f dB a 100 Hz", far);
        CHECK (eq.isShaping(), "con una banda tocada se marca como activo");
        CHECK (std::abs (eq.responseDb (1000.0f) - 6.0f) < 0.3f, "la curva dibujada coincide (%.2f dB)", eq.responseDb (1000.0f));

        juce::MemoryBlock state; eq.getStateInformation (state);
        ChannelEqEffect copy; copy.setStateInformation (state.getData(), (int) state.getSize());
        const double c = gainDb (copy, 1000.0);
        CHECK (std::abs (c - at) < 0.05, "el estado guardado se recupera igual (%.2f dB)", c);

        set (eq, "on", 0.0f);
        const double off = gainDb (eq, 1000.0);
        CHECK (std::abs (off) < 0.05, "EQ apagado: sonido sin tocar (%.3f dB)", off);
    }
    {
        ChannelEqEffect eq;
        set (eq, "b1on", 1.0f); set (eq, "b1f", 200.0f);
        const double low = gainDb (eq, 50.0), pass = gainDb (eq, 2000.0);
        CHECK (low < -30.0, "corte de graves a 200 Hz (24 dB/oct): 50 Hz baja %.1f dB", low);
        CHECK (std::abs (pass) < 0.5, "y deja pasar 2 kHz (%.2f dB)", pass);

        ChannelEqEffect eq2;
        set (eq2, "b8on", 1.0f); set (eq2, "b8f", 2000.0f);
        const double high = gainDb (eq2, 8000.0);
        CHECK (high < -30.0, "corte de agudos a 2 kHz: 8 kHz baja %.1f dB", high);
    }
    {
        ChannelEqEffect eq;
        set (eq, "gain", -6.0f);
        const double g = gainDb (eq, 440.0);
        CHECK (std::abs (g + 6.0) < 0.2, "ganancia de salida -6 dB: %.2f dB", g);
    }

    printf (failures == 0 ? "\nTodo bien\n" : "\n%d fallos\n", failures);
    return failures == 0 ? 0 : 1;
}
