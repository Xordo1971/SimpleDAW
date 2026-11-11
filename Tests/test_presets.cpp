#include "enginetest_common.h"
#include "PresetChain.h"
static float plain (juce::AudioProcessor& p, const char* id)
{ for (auto* x : p.getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (x)) if (r->paramID == id) return r->convertFrom0to1 (r->getValue()); return -999; }
int main()
{
    printf ("1) Presets de fábrica: válidos y sin parámetros mal escritos\n");
    const auto all = Presets::factoryXml();
    for (int i = 0; i < all.size(); ++i)
    {
        auto x = Presets::factory (i);
        CHECK (x != nullptr && Presets::isValid (*x), "preset %d válido (%s)", i, x ? x->getStringAttribute ("name").toRawUTF8() : "?");
        if (! x) continue;
        for (auto* e : x->getChildWithTagNameIterator ("EFFECT"))
        {
            auto fx = BuiltInEffects::createByName (e->getStringAttribute ("type"));
            CHECK (fx != nullptr, "   efecto %s existe", e->getStringAttribute ("type").toRawUTF8());
            if (! fx) continue;
            for (auto* pe : e->getChildWithTagNameIterator ("PARAM"))
                if (plain (*fx, pe->getStringAttribute ("id").toRawUTF8()) == -999) CHECK (false, "   parámetro desconocido %s", pe->getStringAttribute ("id").toRawUTF8());
        }
    }

    printf ("2) Aplicar un preset con efectos a una pista\n");
    Rig rig; auto* t = rig.engine.addTrack ("G", {});
    rig.engine.addInsert (*t, Section::effects, BuiltInEffects::createByName ("Chorus"), false);
    rig.engine.addInsert (*t, Section::effects, BuiltInEffects::createByName ("Ampli"), false);
    auto& amp = *t->getInserts (Section::effects)[1]->processor;
    auto preset = juce::XmlDocument::parse (R"(<SIMPLEDAW_PRESET name="Prueba">
        <EFFECT type="Compresor"/>
        <EFFECT type="Ampli"><PARAM id="channel" value="Lead"/><PARAM id="gain" value="7.5"/><PARAM id="sat" value="on"/></EFFECT>
        <EFFECT type="Delay"><PARAM id="mix" value="0.2"/><PARAM id="time" value="420"/></EFFECT>
        <EFFECT type="Reverb"><PARAM id="mix" value="0.15"/></EFFECT></SIMPLEDAW_PRESET>)");
    std::vector<juce::AudioProcessor*> closed;
    int skipped = Presets::applyChain (rig.engine, *t, amp, *preset, [&] (juce::AudioProcessor& p) { closed.push_back (&p); });
    auto& l = t->getInserts (Section::effects);
    CHECK (skipped == 0 && l.size() == 4, "4 efectos, ninguno descartado (%d, %d)", (int) l.size(), skipped);
    CHECK (closed.size() == 1, "se cerró la ventana del chorus que se quitó");
    if (l.size() == 4)
        CHECK (l[0]->processor->getName() == "Compresor" && l[1]->processor.get() == &amp && l[2]->processor->getName() == "Delay" && l[3]->processor->getName() == "Reverb",
               "orden: compresor, ampli (el mismo), delay, reverb");
    CHECK (plain (amp, "channel") == 2 && std::abs (plain (amp, "gain") - 7.5f) < 0.01f && plain (amp, "sat") == 1, "ampli ajustado (lead, 7.5, sat)");
    CHECK (std::abs (plain (amp, "bass") - 5.0f) < 0.01f, "lo que no viene en el preset vuelve a su valor inicial");
    CHECK (std::abs (plain (*l[2]->processor, "time") - 420.0f) < 0.5f, "delay a 420 ms");
    CHECK (dynamic_cast<AmpEffect&> (amp).presetName == "Prueba", "nombre del preset guardado en el ampli");

    printf ("3) Guardar y volver a cargar (ida y vuelta)\n");
    auto saved = Presets::capture (t, amp, true, "Copia");
    juce::File f = juce::File::createTempFile (".sdpreset"); saved->writeTo (f);
    auto loaded = Presets::load (f);
    CHECK (loaded != nullptr, "el archivo guardado se puede leer");
    juce::XmlElement clean ("SIMPLEDAW_PRESET"); clean.createNewChildElement ("EFFECT")->setAttribute ("type", "Ampli");
    Presets::applyChain (rig.engine, *t, amp, clean, nullptr);
    CHECK (l.size() == 1 && plain (amp, "gain") == 5.0f, "preset vacío: solo el ampli, a valores iniciales");
    Presets::applyChain (rig.engine, *t, amp, *loaded, nullptr);
    CHECK (l.size() == 4 && plain (amp, "channel") == 2 && std::abs (plain (*l[3]->processor, "mix") - 0.15f) < 0.001f, "ida y vuelta: todo igual");
    f.deleteFile();

    printf ("4) El estado del proyecto guarda el nombre del preset\n");
    juce::MemoryBlock mb; amp.getStateInformation (mb);
    AmpEffect other; other.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (other.presetName == "Copia" || other.presetName == "Prueba", "nombre recuperado (%s)", other.presetName.toRawUTF8());
    CHECK (plain (other, "channel") == 2, "y sus ajustes");

    printf ("5) Ya no hay límite de huecos: solo se descarta lo desconocido\n");
    juce::String big = "<SIMPLEDAW_PRESET name=\"X\"><EFFECT type=\"Ampli\"/>";
    for (int i = 0; i < 5; ++i) big += "<EFFECT type=\"Reverb\"/>";
    big += "<EFFECT type=\"NoExiste\"/></SIMPLEDAW_PRESET>";
    skipped = Presets::applyChain (rig.engine, *t, amp, *juce::XmlDocument::parse (big), nullptr);
    CHECK (skipped == 1 && l.size() == 6, "1 descartado (el desconocido), 6 efectos: %d, %d", skipped, (int) l.size());
    printf ("%s (%d fallos)\n", failures ? "HAY FALLOS" : "TODO BIEN", failures);
    return failures;
}
