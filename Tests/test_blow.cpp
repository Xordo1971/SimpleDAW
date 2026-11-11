#include "enginetest_common.h"
#include "PresetChain.h"
int main()
{
    juce::Random r (3);
    for (int bs : { 32, 64 })
    for (int p = 0; p < Presets::factoryXml().size(); ++p)
    {
        Rig rig; auto* t = rig.engine.addTrack ("G", {});
        rig.engine.addInsert (*t, Section::effects, BuiltInEffects::createByName ("Ampli"), false);
        auto& amp = *t->getInserts (Section::effects)[0]->processor;
        Presets::applyChain (rig.engine, *t, amp, *Presets::factory (p), nullptr);
        auto& l = t->getInserts (Section::effects);
        juce::ignoreUnused (bs);
        juce::AudioBuffer<float> b (2, bs); juce::MidiBuffer m; float pk = 0; bool nan = false;
        std::vector<float> pkPer (l.size(), 0.0f);
        for (int pos = 0; pos < 48000 * 3; pos += bs)
        {
            for (int i = 0; i < bs; ++i) { float v = 0.02f * (float) std::sin (2 * M_PI * 50 * (pos + i) / 48000.0) + 0.001f * (r.nextFloat() - 0.5f); b.setSample (0, i, v); b.setSample (1, i, v); }
            for (size_t k = 0; k < l.size(); ++k) { l[k]->processor->processBlock (b, m); pkPer[k] = std::max (pkPer[k], b.getMagnitude (0, bs)); }
            for (int i = 0; i < bs; ++i) if (! std::isfinite (b.getSample (0, i))) nan = true;
            pk = std::max (pk, b.getMagnitude (0, bs));
        }
        if (pk > 2 || nan) { printf ("BLOQUE %d  %s: pico %g nan %d |", bs, Presets::factory (p)->getStringAttribute ("name").toRawUTF8(), pk, nan); for (size_t k = 0; k < l.size(); ++k) printf (" %s=%g", l[k]->processor->getName().toRawUTF8(), pkPer[k]); printf ("\n"); }
    }
    printf ("fin\n");
}
