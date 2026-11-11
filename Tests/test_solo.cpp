#include "enginetest_common.h"
#include "PresetChain.h"
// Pasa una señal por la cadena del preset (ampli + efectos) y mide el nivel de salida
static double runChain (int presetIndex, std::function<float(int)> input, int n, double& peakOut, int fromSample)
{
    Rig rig; auto* t = rig.engine.addTrack ("G", {});
    rig.engine.addInsert (*t, Section::effects, BuiltInEffects::createByName ("Ampli"), false);
    auto& amp = *t->getInserts (Section::effects)[0]->processor;
    Presets::applyChain (rig.engine, *t, amp, *Presets::factory (presetIndex), nullptr);
    for (auto& ins : t->getInserts (Section::effects)) ins->processor->prepareToPlay (48000, 256);
    juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m; double e = 0; long cnt = 0; peakOut = 0;
    for (int pos = 0; pos < n; pos += 256)
    {
        for (int i = 0; i < 256; ++i) { float v = input (pos + i); b.setSample (0, i, v); b.setSample (1, i, v); }
        for (auto& ins : t->getInserts (Section::effects)) ins->processor->processBlock (b, m);
        if (pos >= fromSample) for (int i = 0; i < 256; ++i) { float v = b.getSample (0, i); e += v * v; ++cnt; peakOut = std::max (peakOut, (double) std::abs (v)); }
    }
    return std::sqrt (e / std::max (1L, cnt));
}
int main()
{
    juce::Random r (1);
    for (int p = 0; p < Presets::factoryXml().size(); ++p)
    {
        double pk0, pk1, pk2, pk3;
        const double silence = runChain (p, [] (int) { return 0.0f; }, 96000, pk0, 48000);
        const double noise = runChain (p, [&] (int) { return 0.0001f * (r.nextFloat() * 2 - 1); }, 96000, pk1, 48000);        // -80 dB
        const double hum = runChain (p, [] (int i) { return 0.005f * (float) std::sin (2 * M_PI * 50 * i / 48000.0) + 0.002f * (float) std::sin (2 * M_PI * 150 * i / 48000.0); }, 96000, pk2, 48000);  // zumbido de pastilla ~-64 dB
        const double note = runChain (p, [] (int i) { return 0.25f * std::exp (-i / 30000.0f) * (float) std::sin (2 * M_PI * 330 * i / 48000.0); }, 96000, pk3, 0);
        printf ("%-34s silencio %.5f | ruido-80 %.4f | zumbido %.4f (pico %.3f) | nota pico %.3f\n", Presets::factory (p)->getStringAttribute ("name").toRawUTF8(), silence, noise, hum, pk2, pk3);
    }
}
