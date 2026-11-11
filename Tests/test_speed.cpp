#include "settings.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "pd.h"
#include "/home/claude/SimpleDAW/Source/Transpose.h"
#include <cstdio>
#include <chrono>
static int failures = 0;
#define CHECK(c, ...) do { if (c) printf ("  OK   "); else { printf ("  FALLO "); ++failures; } printf (__VA_ARGS__); printf ("\n"); } while (0)
int main()
{
    const double sr = 48000;
    auto note = [&] (double f, int len) { std::vector<float> x (len);
        for (int i = 0; i < len; ++i) { double t = i / sr; x[i] = (float) (0.4*std::sin(2*M_PI*f*t)+0.3*std::sin(2*M_PI*2*f*t+.4)+0.2*std::sin(2*M_PI*3*f*t+1.1)); }
        return x; };
    auto measure = [&] (const float* x) { std::vector<float> d (2048);
        for (int i = 0; i < 2048; ++i) d[i] = 0.5f * (x[2*i] + x[2*i+1]);
        return PitchDetector::detect (d.data(), 2048, sr / 2); };

    printf ("Transponer (sigue funcionando tras el cambio)\n");
    for (double st : { -5.0, 2.0, 12.0 })
    {
        const int len = (int) sr * 2; auto x = note (110.0, len); std::vector<float> y (len);
        PitchShifter::process (x.data(), len, y.data(), st);
        const double expected = 110.0 * std::pow (2.0, st / 12.0); const float got = measure (y.data() + 24000);
        CHECK (std::abs (1200 * std::log2 (got / expected)) < 1.0, "%+.0f st: %.2f Hz (esperado %.2f)", st, got, expected);
    }

    printf ("Cambiar velocidad (el tono no cambia)\n");
    for (double speed : { 0.5, 0.7, 0.85, 1.25 })
    {
        const int len = (int) sr * 2; auto x = note (110.0, len);
        // Un golpe a 1 s para comprobar que todo se estira proporcionalmente
        for (int i = 0; i < 600; ++i) x[48000 + i] += (float) (0.9 * std::exp (-i / 100.0));
        std::vector<float> y;
        auto t0 = std::chrono::steady_clock::now();
        PitchShifter::changeSpeed (x.data(), len, speed, y);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        const float got = measure (y.data() + 30000);
        const int expectedLen = (int) std::llround (len / speed);
        // localizar el golpe: máximo de |y| sobre el nivel de la nota
        int hit = 0; float mx = 0; for (int i = 0; i < (int) y.size(); ++i) if (std::abs (y[i]) > mx) { mx = std::abs (y[i]); hit = i; }
        CHECK ((int) y.size() == expectedLen && std::abs (1200 * std::log2 (got / 110.0)) < 1.0 && std::abs (hit - 48000 / speed) < 480,
               "%3.0f %%: dura %.2f s (esperado %.2f), tono %.2f Hz, golpe en %.3f s (esperado %.3f)  [%.0f ms]",
               speed * 100, y.size() / sr, expectedLen / sr, got, hit / sr, 1.0 / speed, ms);
    }
    printf ("Transponer no desplaza en el tiempo\n");
    for (double st : { -7.0, 3.0, 12.0 })
    {
        const int len = (int) sr * 2; std::vector<float> x (len, 0.0f), y (len);
        for (int i = 0; i < 2400; ++i) x[48000 + i] = (float) (std::sin (2*M_PI*440*i/sr) * std::exp (-i / 300.0));
        PitchShifter::process (x.data(), len, y.data(), st);
        int hit = 0; float mx = 0; for (int i = 0; i < len; ++i) if (std::abs (y[i]) > mx) { mx = std::abs (y[i]); hit = i; }
        CHECK (std::abs (hit - 48000) < 480, "%+.0f st: golpe en %.1f ms (esperado 1000.0)", st, hit / 48.0);
    }
    printf ("%s (%d fallos)\n", failures ? "HAY FALLOS" : "TODO BIEN", failures);
}
