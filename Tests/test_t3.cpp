#include "enginetest_common.h"
static std::shared_ptr<Clip> constantClip (float v, int len)
{
    auto c = std::make_shared<Clip>(); c->audio.setSize (1, len);
    for (int i = 0; i < len; ++i) c->audio.setSample (0, i, v);
    c->computePeaks(); return c;
}
int main()
{
    printf ("1) Envíos a los buses\n");
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {}); rig.engine.addRegion (*t, constantClip (0.5f, 48000 * 2), 0);
        rig.engine.play(); rig.runSeconds (0.5); rig.engine.stop();
        CHECK (std::abs (rig.output[12000] - 0.5f) < 0.001f, "sin envío: 0.50 (%.3f)", rig.output[12000]);

        CHECK (rig.engine.getBus (0) == nullptr, "sin buses al empezar");
        CHECK (rig.engine.addBus ("B1", {}) == 0, "nuevo bus en el hueco 0");
        t->send[0] = 0.5f;   // bus vacío (sin efectos) a 0 dB: se suma la mitad
        rig.engine.setPosition (0); rig.output.clear(); rig.engine.play(); rig.runSeconds (0.5); rig.engine.stop();
        CHECK (std::abs (rig.output[12000] - 0.75f) < 0.001f, "envío 50%% al bus 1: 0.75 (%.3f)", rig.output[12000]);

        t->mute = true;
        rig.engine.setPosition (0); rig.output.clear(); rig.engine.play(); rig.runSeconds (0.5); rig.engine.stop();
        CHECK (std::abs (rig.output[12000]) < 0.001f, "pista silenciada: tampoco envía (%.3f)", rig.output[12000]);
        t->mute = false;

        rig.engine.getBus (0)->mute = true;
        rig.engine.setPosition (0); rig.output.clear(); rig.engine.play(); rig.runSeconds (0.5); rig.engine.stop();
        CHECK (std::abs (rig.output[12000] - 0.5f) < 0.001f, "bus silenciado: solo la pista (%.3f)", rig.output[12000]);
        rig.engine.getBus (0)->mute = false;

        // Quitar el bus (como en deshacer) y volver a ponerlo
        auto held = rig.engine.detachBus (0);
        rig.engine.setPosition (0); rig.output.clear(); rig.engine.play(); rig.runSeconds (0.5); rig.engine.stop();
        CHECK (std::abs (rig.output[12000] - 0.5f) < 0.001f, "bus quitado: el envío no suena (%.3f)", rig.output[12000]);
        rig.engine.attachBus (0, std::move (held));
        rig.engine.setPosition (0); rig.output.clear(); rig.engine.play(); rig.runSeconds (0.5); rig.engine.stop();
        CHECK (std::abs (rig.output[12000] - 0.75f) < 0.001f, "bus devuelto: vuelve a sonar (%.3f)", rig.output[12000]);

        // Varios buses a la vez, en huecos distintos
        CHECK (rig.engine.addBus ("B2", {}) == 1 && rig.engine.addBus ("B3", {}) == 2, "huecos 1 y 2");
        t->send[2] = 0.25f;
        rig.engine.setPosition (0); rig.output.clear(); rig.engine.play(); rig.runSeconds (0.5); rig.engine.stop();
        CHECK (std::abs (rig.output[12000] - 0.875f) < 0.001f, "dos envíos se suman (%.3f)", rig.output[12000]);
        rig.engine.detachBus (1);
        CHECK (rig.engine.addBus ("B4", {}) == 1, "reutiliza el hueco libre");
        CHECK (rig.engine.getBus (1)->name == "B4" && t->send[1].load() == 0.0f, "bus nuevo empieza sin envíos");
        for (int i = 3; i < Track::maxBuses; ++i) rig.engine.addBus ("X", {});
        CHECK (rig.engine.addBus ("lleno", {}) < 0, "máximo 8 buses");
    }

    printf ("2) Automatización de volumen\n");
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {}); rig.engine.addRegion (*t, constantClip (0.5f, 48000 * 3), 0);
        t->automation[Track::autoVolume] = { { 0, 0.0f }, { 96000, -20.0f } };   // de 0 dB a -20 dB en 2 s
        rig.engine.play(); rig.runSeconds (3.0); rig.engine.stop();
        const float atStart = rig.output[1000], atMid = rig.output[48000], atEnd = rig.output[120000];
        CHECK (std::abs (atStart - 0.5f * juce::Decibels::decibelsToGain (-20.0f * 1000 / 96000)) < 0.002f, "en la muestra 1000: -0,2 dB (%.3f)", atStart);
        CHECK (std::abs (atMid - 0.5f * juce::Decibels::decibelsToGain (-10.0f)) < 0.01f, "a mitad -10 dB (%.3f)", atMid);
        CHECK (std::abs (atEnd - 0.05f) < 0.002f, "al final -20 dB (%.3f)", atEnd);
        CHECK (std::abs (t->volumeDb.load() + 20.0f) < 0.1f, "el fader sigue a la automatización (%.1f dB)", t->volumeDb.load());
        // Sin saltos: la diferencia entre muestras seguidas es pequeña
        float maxStep = 0; for (int i = 1; i < 96000; ++i) maxStep = std::max (maxStep, std::abs (rig.output[i] - rig.output[i-1]));
        CHECK (maxStep < 0.001f, "sin escalones audibles (salto máximo %.5f)", maxStep);
        t->automationOn[Track::autoVolume] = false; t->volumeDb = 0.0f;
        rig.engine.setPosition (48000); rig.output.clear(); rig.engine.play(); rig.runSeconds (0.5); rig.engine.stop();
        CHECK (std::abs (rig.output[12000] - 0.5f) < 0.01f, "automatización desactivada: vuelve a 0 dB (%.3f)", rig.output[12000]);
    }

    printf ("3) Panorama automatizado\n");
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {}); rig.engine.addRegion (*t, constantClip (0.5f, 48000 * 2), 0);
        t->automation[Track::autoPan] = { { 0, -1.0f } };   // todo a la izquierda
        rig.engine.play(); rig.runSeconds (0.5); rig.engine.stop();
        CHECK (std::abs (rig.out.getSample (1, 100)) < 0.001f && std::abs (rig.output[12000] - 0.5f) < 0.01f, "izquierda: L=%.2f R=%.3f", rig.output[12000], rig.out.getSample (1, 100));
    }
    printf ("%s (%d fallos)\n", failures ? "HAY FALLOS" : "TODO BIEN", failures);
    return failures;
}
