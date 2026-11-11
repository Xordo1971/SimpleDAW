#include "enginetest_common.h"
#include <juce_data_structures/juce_data_structures.h>
#include "Undo.h"
#include "Effects.h"
static std::shared_ptr<Clip> constantClip (float v, int len)
{ auto c = std::make_shared<Clip>(); c->audio.setSize (1, len); for (int i = 0; i < len; ++i) c->audio.setSample (0, i, v); c->computePeaks(); return c; }
static float play (Rig& rig, double seconds, int at) { rig.engine.setPosition (0); rig.output.clear(); rig.engine.play(); rig.runSeconds (seconds); rig.engine.stop(); return rig.output[(size_t) at]; }

int main()
{
    juce::UndoManager um (300000, 50);
    auto record = [&um] (const char* name, std::function<void()> u, std::function<void()> r)
    { um.beginNewTransaction (name); um.perform (new LambdaUndoAction (u, r), name); };

    printf ("1) Borrar región, deshacer y rehacer\n");
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {});
        const auto id = rig.engine.addRegion (*t, constantClip (0.4f, 48000), 0);
        auto taken = rig.engine.takeRegion (*t, id); const Region region = taken.first; const int index = taken.second;
        record ("borrar", [&] { rig.engine.putRegion (*t, region, index); }, [&] { rig.engine.takeRegion (*t, id); });
        CHECK (play (rig, 0.3, 5000) == 0.0f, "borrada: silencio");
        um.undo();   CHECK (std::abs (play (rig, 0.3, 5000) - 0.4f) < 0.001f, "deshacer: vuelve a sonar");
        um.redo();   CHECK (play (rig, 0.3, 5000) == 0.0f, "rehacer: silencio otra vez");
    }

    printf ("2) Quitar una pista con su efecto y recuperarla (con el audio en marcha)\n");
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {});
        rig.engine.addRegion (*t, constantClip (0.4f, 48000 * 4), 0);
        auto fx = BuiltInEffects::createByName ("Compresor");
        rig.engine.addInsert (*t, Section::effects, std::move (fx), false);
        t->volumeDb = -6.0f;
        const float before = play (rig, 0.5, 20000);
        auto detached = rig.engine.detachTrack (t);
        auto holder = std::make_shared<std::unique_ptr<Track>> (std::move (detached.first));
        const int idx = detached.second;
        CHECK (play (rig, 0.3, 5000) == 0.0f, "pista quitada: silencio");
        rig.engine.setPosition (0); rig.engine.play(); rig.runSeconds (0.1);
        auto* back = rig.engine.attachTrack (std::move (*holder), idx);   // mientras suena
        rig.runSeconds (0.1); rig.engine.stop();
        CHECK (back == t && back->getInserts (Section::effects).size() == 1 && back->volumeDb.load() == -6.0f,
               "vuelve la misma pista con su compresor y su volumen");
        CHECK (std::abs (play (rig, 0.5, 20000) - before) < 0.001f, "suena igual que antes (%.3f / %.3f)", play (rig, 0.5, 20000), before);
    }

    printf ("3) Quitar un efecto y volver a ponerlo\n");
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {});
        auto tone = std::make_shared<Clip>(); tone->audio.setSize (1, 48000);
        for (int i = 0; i < 48000; ++i) tone->audio.setSample (0, i, 0.4f * (float) std::sin (2 * M_PI * 196.0 * i / 48000.0));
        tone->computePeaks(); rig.engine.addRegion (*t, tone, 0);
        auto amp = BuiltInEffects::createByName ("Ampli");
        rig.engine.addInsert (*t, Section::effects, std::move (amp), false);
        auto rms = [&] { play (rig, 0.5, 0); double e = 0; for (int i = 12000; i < 22000; ++i) e += rig.output[(size_t) i] * rig.output[(size_t) i]; return std::sqrt (e / 10000); };
        auto shape = [&] { play (rig, 0.5, 0); return rig.output[15000]; };
        const double withAmp = rms(); const float s1 = shape();
        auto ins = rig.engine.detachInsert (*t, Section::effects, 0);
        const double direct = rms();
        CHECK (std::abs (direct - 0.4 / std::sqrt (2.0)) < 0.01, "sin el ampli: el tono limpio (RMS %.3f)", direct);
        rig.engine.attachInsert (*t, Section::effects, 0, std::move (ins));
        const double again = rms(); const float s2 = shape();
        CHECK (withAmp > 0.05 && std::abs (withAmp - direct) > 0.01 && std::abs (again - withAmp) < 0.005 && std::abs (s1 - s2) < 0.01f,
               "con el ampli otra vez suena igual que al principio (RMS %.3f / %.3f, distinto del limpio %.3f)", withAmp, again, direct);
    }

    printf ("4) Exportar una pista sola (stems)\n");
    {
        Rig rig; auto* a = rig.engine.addTrack ("A", {}); auto* b = rig.engine.addTrack ("B", {});
        rig.engine.addRegion (*a, constantClip (0.25f, 48000), 0); rig.engine.addRegion (*b, constantClip (0.5f, 48000), 0);
        rig.engine.exclusiveTrack = b;
        CHECK (std::abs (play (rig, 0.3, 5000) - 0.5f) < 0.001f, "solo suena la pista B (%.3f)", play (rig, 0.3, 5000));
        rig.engine.exclusiveTrack = nullptr;
        CHECK (std::abs (play (rig, 0.3, 5000) - 0.75f) < 0.001f, "sin exclusiva: suenan las dos (%.3f)", play (rig, 0.3, 5000));
    }
    printf ("%s (%d fallos)\n", failures ? "HAY FALLOS" : "TODO BIEN", failures);
    return failures;
}
