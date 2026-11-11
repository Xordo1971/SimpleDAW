#include "enginetest_common.h"
#include "PresetChain.h"
struct Dev32 : FakeDevice { int getCurrentBufferSizeSamples() override { return BS; } int getDefaultBufferSize() override { return BS; } int BS = 32; };
int main()
{
    juce::Random r (5);
    for (int bs : { 32, 64, 128 })
    for (int p = 0; p < Presets::factoryXml().size(); ++p)
    {
        AudioEngine engine; Dev32 dev; dev.BS = bs; engine.audioDeviceAboutToStart (&dev);
        auto* t = engine.addTrack ("G", {});
        t->inputChannel = 0; t->monitor = true;
        engine.addInsert (*t, Section::effects, BuiltInEffects::createByName ("Ampli"), false);
        auto& amp = *t->getInserts (Section::effects)[0]->processor;
        Presets::applyChain (engine, *t, amp, *Presets::factory (p), nullptr);
        juce::AudioBuffer<float> in (2, bs), out (2, bs); juce::int64 clk = 0; float pk = 0; bool bad = false;
        for (int b = 0; b < 48000 * 2 / bs; ++b)
        {
            for (int i = 0; i < bs; ++i) { in.setSample (0, i, 0.003f * (float) std::sin (2 * M_PI * 50 * (clk + i) / 48000.0) + 0.0005f * (r.nextFloat() - 0.5f)); in.setSample (1, i, 0); }
            clk += bs;
            juce::AudioIODeviceCallbackContext ctx;
            engine.audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, bs, ctx);
            for (int i = 0; i < bs; ++i) { float v = out.getSample (0, i); if (! std::isfinite (v)) bad = true; else pk = std::max (pk, std::abs (v)); }
        }
        printf ("bs %3d %-34s pico %g %s\n", bs, Presets::factory (p)->getStringAttribute ("name").toRawUTF8(), pk, bad ? "NaN" : "");
        engine.audioDeviceStopped();
    }
}
