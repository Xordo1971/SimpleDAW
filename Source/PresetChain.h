#pragma once
// Presets del ampli aplicados a una pista: el ampli más los efectos integrados que lo acompañan
#include "AudioEngine.h"
#include "Effects.h"
#include "Presets.h"

namespace Presets
{
    // ---------- Cadena de la pista ----------
    // Dónde está el ampli dentro de la pista (sección e índice), o {-1,-1}
    inline std::pair<int, int> locate (Track& t, juce::AudioProcessor& amp)
    {
        for (int s = 0; s < 2; ++s)
        {
            auto& list = t.getInserts ((Section) s);
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(size_t) i]->processor.get() == &amp)
                    return { s, i };
        }
        return { -1, -1 };
    }

    // Preset con el ampli y (si includeEffects) los demás efectos integrados de su sección
    inline std::unique_ptr<juce::XmlElement> capture (Track* t, juce::AudioProcessor& amp, bool includeEffects, const juce::String& name)
    {
        auto x = std::make_unique<juce::XmlElement> (rootTag());
        x->setAttribute ("name", name);
        const auto where = t != nullptr ? locate (*t, amp) : std::pair<int, int> { -1, -1 };

        auto add = [&] (juce::AudioProcessor& p)
        {
            auto* e = x->createNewChildElement ("EFFECT");
            e->setAttribute ("type", p.getName());
            writeParams (p, *e);
        };

        if (! includeEffects || where.first < 0)
        {
            add (amp);
            return x;
        }
        for (auto& ins : t->getInserts ((Section) where.first))
            if (! ins->isExternalPlugin)
                add (*ins->processor);
        return x;
    }



    // Aplica el preset: ajusta el ampli y cambia los efectos integrados de su sección por los
    // del preset (los plugins externos no se tocan). beforeRemove: cerrar ventanas, etc.
    // Devuelve cuántos efectos no cupieron (máximo 4 por sección), o -1 si no se pudo.
    inline int applyChain (AudioEngine& engine, Track& t, juce::AudioProcessor& amp, const juce::XmlElement& preset,
                           const std::function<void (juce::AudioProcessor&)>& beforeRemove)
    {
        const auto* ampXml = ampElement (preset);
        auto where = locate (t, amp);
        if (ampXml == nullptr || where.first < 0)
            return -1;
        const auto section = (Section) where.first;

        // 1. Fuera los efectos integrados que acompañaban al ampli
        auto& list = t.getInserts (section);
        for (int i = (int) list.size(); --i >= 0;)
        {
            auto& ins = list[(size_t) i];
            if (ins->processor.get() == &amp || ins->isExternalPlugin)
                continue;
            if (beforeRemove) beforeRemove (*ins->processor);
            engine.removeInsert (t, section, i);
        }

        // 2. Ajustes del ampli
        readParams (amp, *ampXml);
        if (auto* a = dynamic_cast<AmpEffect*> (&amp))
            a->presetName = preset.getStringAttribute ("name");

        // 3. Efectos del preset, antes y después del ampli según su orden
        int skipped = 0;
        int pos = locate (t, amp).second;
        for (auto* e : preset.getChildWithTagNameIterator ("EFFECT"))
        {
            if (e == ampXml) { pos = locate (t, amp).second + 1; continue; }
            auto fx = BuiltInEffects::createByName (e->getStringAttribute ("type"));
            if (fx == nullptr || e->getStringAttribute ("type") == "Ampli") { ++skipped; continue; }
            readParams (*fx, *e);
            if (engine.addInsert (t, section, std::move (fx), false, pos))
                ++pos;
            else
                ++skipped;
        }
        return skipped;
    }

}
