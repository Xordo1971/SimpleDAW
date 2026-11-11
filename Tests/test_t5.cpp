#include "enginetest_common.h"
// Detecta los golpes: subidas bruscas de la envolvente
// Detector por energía: un golpe es un salto brusco del nivel respecto a lo que había justo antes
static std::vector<int> onsets (const std::vector<float>& y, float threshold)
{
    std::vector<int> r; int last = -100000;
    auto level = [&] (int from, int n) { float m = 0; for (int i = from; i < from + n && i < (int) y.size(); ++i) if (i >= 0) m = std::max (m, std::abs (y[(size_t) i])); return m; };
    for (int i = 0; i < (int) y.size(); ++i)
    {
        if (std::abs (y[(size_t) i]) <= threshold || i - last < 3000) continue;
        const float before = level (i - 1200, 1200), after = level (i, 600);
        if (after > threshold && after > 1.8f * before) { r.push_back (i); last = i; }
    }
    return r;
}
static void onlyInstrument (Drums::DrumData& d, int inst) { for (int i = 0; i < Drums::numInstruments; ++i) d.levels[(size_t) i] = i == inst ? 0.9f : 0.0f; }

int main()
{
    // A 120 BPM en 4/4: semicorchea = 6000 muestras, compás = 96000
    printf ("1) Estilo Rock, bombo: golpes en las semicorcheas 0, 8 y 10\n");
    {
        Rig rig; auto* t = rig.engine.addDrumTrack ("Bateria", {});
        auto& d = *t->drum; Drums::applyStyle (d, 0, 16); d.humanize = false; d.swing = 0;
        d.arrangement[0] = 0; d.arrangement[1] = 0;
        onlyInstrument (d, Drums::kick);
        rig.engine.play(); rig.runSeconds (4.0); rig.engine.stop();
        auto o = onsets (rig.output, 0.05f);
        printf ("     golpes en:"); for (int v : o) printf (" %d", v); printf ("\n");
        CHECK (o.size() == 6 && std::abs (o[1] - 48000) <= 3 && std::abs (o[3] - 96000) <= 3 && std::abs (o[4] - 144000) <= 3,
               "6 golpes; los de las semicorcheas 0 y 8 exactos");
        // La semicorchea 10 cae sobre la cola del golpe anterior: se comprueba sola
        Rig rig2; auto* t2 = rig2.engine.addDrumTrack ("B", {});
        auto& d2 = *t2->drum; d2.humanize = false; d2.patterns[0].steps[Drums::kick][10] = 3; d2.arrangement[0] = 0; onlyInstrument (d2, Drums::kick);
        rig2.engine.play(); rig2.runSeconds (1.5); rig2.engine.stop();
        int first = -1; for (int i = 0; i < (int) rig2.output.size(); ++i) if (std::abs (rig2.output[(size_t) i]) > 1e-4f) { first = i; break; }
        CHECK (first >= 60000 && first <= 60002, "semicorchea 10 exacta: primera muestra en %d (esperado 60000)", first);
        CHECK (rig.engine.getContentEndSamples() == 192000, "la canción dura 2 compases (%lld)", (long long) rig.engine.getContentEndSamples());
    }

    printf ("2) Swing: las semicorcheas impares se retrasan\n");
    {
        Rig rig; auto* t = rig.engine.addDrumTrack ("Bateria", {});
        auto& d = *t->drum; d.humanize = false; d.swing = 1.0f;     // tresillo
        d.patterns[0].steps[Drums::hatClosed][0] = 3; d.patterns[0].steps[Drums::hatClosed][1] = 3;
        d.arrangement[0] = 0; onlyInstrument (d, Drums::hatClosed);
        rig.engine.play(); rig.runSeconds (0.5); rig.engine.stop();
        auto o = onsets (rig.output, 0.03f);
        CHECK (o.size() >= 2 && std::abs (o[1] - 8000) <= 3, "segundo golpe en 6000 + 2000 = 8000 (%d)", o.size() >= 2 ? o[1] : -1);
    }

    printf ("3) Compases vacíos en silencio y patrón B distinto\n");
    {
        Rig rig; auto* t = rig.engine.addDrumTrack ("Bateria", {});
        auto& d = *t->drum; Drums::applyStyle (d, 0, 16); d.humanize = false;
        d.arrangement[0] = 0; d.arrangement[2] = 1;   // A, (nada), B
        rig.engine.play(); rig.runSeconds (6.0); rig.engine.stop();
        float bar2 = 0; for (int i = 96000 + 20000; i < 192000; ++i) bar2 = std::max (bar2, std::abs (rig.output[(size_t) i]));
        CHECK (bar2 < 0.001f, "compás 2 vacío: silencio (después de que se apaguen los platos)");
        float bar3 = 0; for (int i = 192000; i < 200000; ++i) bar3 = std::max (bar3, std::abs (rig.output[(size_t) i]));
        CHECK (bar3 > 0.2f, "compás 3 (B, con crash): suena (%.2f)", bar3);
    }

    printf ("4) Cada instrumento suena y no satura\n");
    for (int inst = 0; inst < Drums::numInstruments; ++inst)
    {
        Rig rig; auto* t = rig.engine.addDrumTrack ("Bateria", {});
        auto& d = *t->drum; d.humanize = false; d.patterns[0].steps[(size_t) inst][0] = 3; d.arrangement[0] = 0; onlyInstrument (d, inst);
        rig.engine.play(); rig.runSeconds (1.0); rig.engine.stop();
        float peak = 0; bool finite = true; for (float v : rig.output) { peak = std::max (peak, std::abs (v)); finite = finite && std::isfinite (v); }
        CHECK (finite && peak > 0.05f && peak < 1.0f, "%-16s pico %.2f", Drums::instrumentName (inst), peak);
    }

    printf ("5) Con ciclo: el patrón vuelve a empezar en cada vuelta\n");
    {
        Rig rig; auto* t = rig.engine.addDrumTrack ("Bateria", {});
        auto& d = *t->drum; d.humanize = false; d.patterns[0].steps[Drums::kick][0] = 3; d.arrangement[0] = 0; d.arrangement[1] = 0;
        onlyInstrument (d, Drums::kick);
        rig.engine.loopStart = 0; rig.engine.loopEnd = 96000; rig.engine.loopOn = true;
        rig.engine.play(); rig.runSeconds (6.0); rig.engine.stop();
        auto o = onsets (rig.output, 0.05f);
        CHECK (o.size() == 3 && std::abs (o[1] - 96000) <= 3 && std::abs (o[2] - 192000) <= 3, "bombo cada 96000 muestras (%d golpes)", (int) o.size());
    }

    printf ("6) Precuenta: la batería no suena antes del compás 1\n");
    {
        Rig rig; auto* t = rig.engine.addDrumTrack ("Bateria", {}); auto* g = rig.engine.addTrack ("G", {});
        g->inputChannel = 0; g->armed = true;
        auto& d = *t->drum; d.humanize = false; d.patterns[0].steps[Drums::kick][0] = 3; d.arrangement[0] = 0; onlyInstrument (d, Drums::kick);
        rig.engine.metronomeVolume = 0.0f; rig.engine.countInOn = true;
        rig.engine.startRecording (rig.folder); rig.runSeconds (2.5); rig.engine.stop();
        auto o = onsets (rig.output, 0.05f);
        CHECK (! o.empty() && std::abs (o[0] - 96000) <= 3, "primer bombo tras la precuenta de un compás (%d)", o.empty() ? -1 : o[0]);
    }
    printf ("%s (%d fallos)\n", failures ? "HAY FALLOS" : "TODO BIEN", failures);
    return failures;
}
