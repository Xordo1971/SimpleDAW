#include "settings.h"
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_dsp/juce_dsp.h>
#include "Effects.h"
#include <cstdio>
#include <complex>
static int failures = 0;
#define CHECK(c, ...) do { if (c) printf ("  OK   "); else { printf ("  FALLO "); ++failures; } printf (__VA_ARGS__); printf ("\n"); } while (0)

// Amplitud de una frecuencia concreta (DFT de un solo punto)
static double amplitudeAt (const float* x, int n, double f, double sr)
{
    std::complex<double> acc = 0;
    for (int i = 0; i < n; ++i) acc += (double) x[i] * std::exp (std::complex<double> (0, -2 * M_PI * f * i / sr));
    return 2.0 * std::abs (acc) / n;
}

int main()
{
    const double sr = 48000; const int block = 256, total = 48000;
    std::vector<std::pair<const char*, float>> extra;
    auto run = [&] (int channel, float gainValue, int cab, float inputLevel, std::vector<float>& out)
    {
        AmpEffect amp;
        auto set = [&] (const char* id, float plain) { for (auto* p : amp.getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p)) if (r->paramID == id) r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
        set ("channel", (float) channel); set ("gain", gainValue); set ("cabinet", (float) cab); set ("gate", -90.0f);
        for (auto& e : extra) set (e.first, e.second);
        amp.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi; out.clear();
        for (int pos = 0; pos < total; pos += block)
        {
            for (int i = 0; i < block; ++i) { const float v = inputLevel * (float) std::sin (2 * M_PI * 196.0 * (pos + i) / sr); buf.setSample (0, i, v); buf.setSample (1, i, v); }
            amp.processBlock (buf, midi);
            for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
        }
    };
    auto thd = [&] (const std::vector<float>& y) {
        const float* x = y.data() + 24000; const int n = 19200;   // 0.4 s estables (múltiplo exacto del periodo de 196 Hz no hace falta)
        const double f1 = amplitudeAt (x, n, 196.0, sr); double h = 0;
        for (int k = 2; k <= 8; ++k) { const double a = amplitudeAt (x, n, 196.0 * k, sr); h += a * a; }
        return std::sqrt (h) / f1; };
    auto peakOf = [] (const std::vector<float>& y) { float m = 0; bool bad = false; for (float v : y) { if (! std::isfinite (v)) bad = true; m = std::max (m, std::abs (v)); } return bad ? -1.0f : m; };

    printf ("Ampli: tocando suave (nivel 0.03), cada canal distorsiona más que el anterior\n");
    std::vector<float> clean, crunch, lead;
    run (0, 5, 0, 0.03f, clean); run (1, 5, 0, 0.03f, crunch); run (2, 5, 0, 0.03f, lead);
    const double tc = thd (clean), tr = thd (crunch), tl = thd (lead);
    printf ("     THD: limpio %.1f %%, crunch %.1f %%, lead %.1f %%\n", tc * 100, tr * 100, tl * 100);
    CHECK (tc < tr && tr < tl, "más distorsión de limpio a crunch y a lead");
    CHECK (tc < 0.05, "el limpio está limpio tocando suave");
    run (0, 5, 0, 0.3f, clean); run (1, 5, 0, 0.3f, crunch); run (2, 5, 0, 0.3f, lead);
    for (auto* y : { &clean, &crunch, &lead })
        CHECK (peakOf (*y) > 0.05f && peakOf (*y) < 1.2f, "nivel de salida sensato y sin valores raros (pico %.2f)", peakOf (*y));

    printf ("Ampli: la ganancia aumenta la distorsión en crunch\n");
    std::vector<float> lo, hi; run (1, 1, 0, 0.3f, lo); run (1, 9, 0, 0.3f, hi);
    CHECK (thd (lo) < thd (hi), "ganancia 1: %.1f %%  <  ganancia 9: %.1f %%", thd (lo) * 100, thd (hi) * 100);

    printf ("Ampli: la pantalla quita los agudos ásperos\n");
    std::vector<float> noCab, cab4; run (1, 8, 0, 0.3f, noCab); run (1, 8, 3, 0.3f, cab4);
    auto band = [&] (const std::vector<float>& y, int k0, int k1) { double e = 0; for (int k = k0; k <= k1; ++k) { const double a = amplitudeAt (y.data() + 24000, 19200, 196.0 * k, sr); e += a * a; } return std::sqrt (e); };
    const double hfNo = band (noCab, 26, 45), hf4 = band (cab4, 26, 45), lowNo = band (noCab, 1, 3), low4 = band (cab4, 1, 3);
    CHECK (hf4 / low4 < 0.25 * hfNo / lowNo, "agudos 5-9 kHz respecto al tono: sin pantalla %.3f, con 4x12 %.3f", hfNo / lowNo, hf4 / low4);

    printf ("Ampli: silencio con la puerta de ruido\n");
    {
        AmpEffect amp; amp.prepareToPlay (sr, block); juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi; float mx = 0;
        for (int pos = 0; pos < 48000; pos += block) { for (int i = 0; i < block; ++i) { const float v = 0.0001f * (float) ((i * 7919) % 200 - 100) / 100.0f; buf.setSample (0, i, v); buf.setSample (1, i, v); }
            amp.processBlock (buf, midi); if (pos > 24000) mx = std::max (mx, buf.getMagnitude (0, 0, block)); }
        CHECK (mx < 0.001f, "ruido de fondo (-80 dB) silenciado por la puerta (salida %.5f)", mx);
    }
    printf ("Ampli: controles del frontal\n");
    {
        std::vector<float> a, b;
        extra = { { "power", 0.0f } }; run (2, 9, 3, 0.3f, a); extra.clear();
        float maxDiff = 0; for (int i = 0; i < (int) a.size(); ++i) maxDiff = std::max (maxDiff, std::abs (a[(size_t) i] - 0.3f * (float) std::sin (2 * M_PI * 196.0 * i / sr)));
        CHECK (maxDiff < 1e-5f, "apagado: la señal pasa tal cual (dif. %.6f)", maxDiff);
        run (1, 4, 0, 0.1f, a); extra = { { "sat", 1.0f } }; run (1, 4, 0, 0.1f, b); extra.clear();
        CHECK (thd (b) > thd (a), "SAT: más saturación (%.1f %% -> %.1f %%)", thd (a) * 100, thd (b) * 100);
        extra = { { "cleanVol", 1.0f } }; run (0, 5, 0, 0.3f, a); extra = { { "cleanVol", 10.0f } }; run (0, 5, 0, 0.3f, b); extra.clear();
        CHECK (thd (b) > thd (a), "CLEAN VOLUME mueve el canal limpio (%.1f %% -> %.1f %%)", thd (a) * 100, thd (b) * 100);
        extra = { { "gain", 10.0f } }; run (0, 5, 0, 0.3f, b); extra.clear(); run (0, 5, 0, 0.3f, a);
        CHECK (std::abs (thd (a) - thd (b)) < 1e-6, "GAIN no afecta al canal limpio");
        auto lowE = [&] (const std::vector<float>& y) { return amplitudeAt (y.data() + 24000, 19200, 196.0, sr); };
        run (1, 3, 0, 0.05f, a); extra = { { "fat", 1.0f } }; run (1, 3, 0, 0.05f, b); extra.clear();
        CHECK (lowE (b) > lowE (a), "FAT: más graves (%.3f -> %.3f)", lowE (a), lowE (b));
        auto hiE = [&] (const std::vector<float>& y) { double e = 0; for (int k = 12; k <= 20; ++k) { const double v = amplitudeAt (y.data() + 24000, 19200, 196.0 * k, sr); e += v * v; } return std::sqrt (e); };
        run (0, 5, 0, 0.3f, a); extra = { { "bright", 1.0f } }; run (0, 5, 0, 0.3f, b); extra.clear();
        CHECK (hiE (b) > hiE (a), "BRIGHT: limpio más brillante");
        for (auto* p : { "fat", "c45", "sat", "voice", "bright" })
        { extra = { { p, 1.0f } }; run (2, 10, 0, 0.5f, b); extra.clear(); CHECK (peakOf (b) > 0.05f && peakOf (b) < 1.5f, "%s activado: nivel sensato (pico %.2f)", p, peakOf (b)); }
    }
    printf ("%s (%d fallos)\n", failures ? "HAY FALLOS" : "TODO BIEN", failures);
}
