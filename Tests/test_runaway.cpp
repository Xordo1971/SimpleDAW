#include "enginetest_common.h"
#include "Effects.h"
struct Boom : BuiltInEffect { Boom() : BuiltInEffect ("Boom") {} void prepare (const juce::dsp::ProcessSpec&) override {} void process (juce::AudioBuffer<float>& b) override { b.applyGain (1.0e6f); } };
int main()
{
    Rig rig; auto* t = rig.engine.addTrack ("G", {});
    t->inputChannel = 0; t->monitor = true;
    rig.signal = [] (juce::int64 n) { return 0.1f * (float) std::sin (n * 0.05); };
    rig.engine.addInsert (*t, Section::effects, std::make_unique<Boom>(), false);
    rig.runSeconds (0.5);
    float pk = 0; for (float v : rig.output) pk = std::max (pk, std::abs (v));
    CHECK (pk <= 1.0f, "la salida nunca pasa de 0 dBFS (%.3f)", pk);
    CHECK (t->getInserts (Section::effects)[0]->bypass.load(), "el efecto desbocado queda desactivado");
    Track* tt; juce::AudioProcessor* pp;
    CHECK (rig.engine.takeRunawayInsert (tt, pp) && tt == t && pp == t->getInserts (Section::effects)[0]->processor.get(), "se avisa de qué efecto y en qué pista");
    rig.output.clear(); rig.runSeconds (0.3);
    pk = 0; for (float v : rig.output) pk = std::max (pk, std::abs (v));
    CHECK (pk > 0.05f && pk < 0.2f, "la pista sigue sonando sin el efecto (%.3f)", pk);
    return failures;
}
