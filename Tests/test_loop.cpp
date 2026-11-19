#include "enginetest_common.h"

// Bucle de regiones (como en Logic): la región se alarga repitiendo su audio
static std::shared_ptr<Clip> rampClip (int len)
{
    auto c = std::make_shared<Clip>(); c->audio.setSize (1, len);
    for (int i = 0; i < len; ++i) c->audio.setSample (0, i, 0.1f + 0.8f * (float) i / (float) len);
    c->computePeaks(); return c;
}

int main()
{
    printf ("Bucle de regiones\n");
    const int len = 12000;   // 0,25 s
    {
        Rig rig; auto* t = rig.engine.addTrack ("Loop", {});
        const auto id = rig.engine.addRegion (*t, rampClip (len), 0);
        rig.engine.setRegionLoop (*t, id, (juce::int64) len * 4);
        const auto& r = t->regions.front();
        CHECK (r.isLooped() && r.getLength() == (juce::int64) len * 4, "la región dura 4 veces el audio (%lld)", (long long) r.getLength());
        CHECK (r.sourceLength() == len, "el audio original no cambia (%lld)", (long long) r.sourceLength());

        rig.engine.play(); rig.runSeconds (1.3); rig.engine.stop();
        bool same = true;
        for (int k = 1; k < 4; ++k)
            for (int i = 0; i < len; i += 997)
                if (std::abs (rig.output[(size_t) (k * len + i)] - rig.output[(size_t) i]) > 1.0e-6f) same = false;
        CHECK (same, "cada vuelta suena igual que la primera");
        CHECK (std::abs (rig.output[(size_t) (3 * len + 100)] - (0.1f + 0.8f * 100.0f / len)) < 1.0e-4f, "la cuarta vuelta empieza de nuevo (%.4f)", rig.output[(size_t) (3 * len + 100)]);
        CHECK (std::abs (rig.output[(size_t) (4 * len + 500)]) < 1.0e-6f, "después del bucle, silencio (%.4f)", rig.output[(size_t) (4 * len + 500)]);
    }
    {
        Rig rig; auto* t = rig.engine.addTrack ("Parcial", {});
        const auto id = rig.engine.addRegion (*t, rampClip (len), 0);
        rig.engine.setRegionLoop (*t, id, (juce::int64) (len * 2.5));
        rig.engine.play(); rig.runSeconds (1.0); rig.engine.stop();
        CHECK (std::abs (rig.output[(size_t) (2 * len + len / 4)] - rig.output[(size_t) (len / 4)]) < 1.0e-6f, "media vuelta al final: suena el principio del audio");
        CHECK (std::abs (rig.output[(size_t) (len * 5 / 2 + 300)]) < 1.0e-6f, "y se corta a las 2,5 vueltas");

        rig.engine.setRegionLoop (*t, id, len / 2);
        CHECK (! t->regions.front().isLooped() && t->regions.front().getLength() == len, "acortar por debajo del audio: sin bucle");
    }
    {
        Rig rig; auto* t = rig.engine.addTrack ("Editar", {});
        const auto id = rig.engine.addRegion (*t, rampClip (len), 0);
        rig.engine.setRegionLoop (*t, id, (juce::int64) len * 3);
        rig.engine.replaceClip (*t, id, rampClip (len / 2));   // editar el audio (más corto)
        CHECK (t->regions.front().getLength() == (juce::int64) (len / 2) * 3, "al editar el audio se mantienen las 3 vueltas (%lld)", (long long) t->regions.front().getLength());

        auto tiled = AudioEngine::tileClip (*t->regions.front().clip, t->regions.front().getLength());
        CHECK (tiled->getLength() == (juce::int64) (len / 2) * 3 && std::abs (tiled->audio.getSample (0, len / 2 + 10) - tiled->audio.getSample (0, 10)) < 1.0e-7f,
               "el audio repetido (para cortar) copia cada vuelta");
    }

    printf (failures == 0 ? "TODO BIEN (0 fallos)\n" : "%d FALLOS\n", failures);
    return failures == 0 ? 0 : 1;
}
