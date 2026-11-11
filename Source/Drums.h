#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// ============================================================================
// Batería programable: datos de los patrones y kit sintetizado (sin muestras)
// ============================================================================

namespace Drums
{
    enum Instrument { kick = 0, snare, hatClosed, hatOpen, tomHigh, tomLow, crash, ride, numInstruments };

    inline const char* instrumentName (int i)
    {
        static const char* names[] = { "Bombo", "Caja", "Charles cerrado", "Charles abierto", "Tom alto", "Tom base", "Crash", "Ride" };
        return names[i];
    }

    constexpr int maxSteps = 32;      // semicorcheas por compás (hasta 8/4 o 16/8)
    constexpr int numPatterns = 4;    // A, B, C, D
    constexpr int maxBars = 512;

    // Valor de un paso: 0 = nada, 1 = suave, 2 = normal, 3 = acento
    inline float velocityOf (uint8_t v) { return v == 0 ? 0.0f : v == 1 ? 0.45f : v == 2 ? 0.75f : 1.0f; }

    struct Pattern
    {
        std::array<std::array<uint8_t, maxSteps>, numInstruments> steps {};
        void clear() { for (auto& row : steps) row.fill (0); }
    };

    // Todo lo que define una pista de batería (se copia entero para deshacer)
    struct DrumData
    {
        std::array<Pattern, numPatterns> patterns {};
        std::array<int8_t, maxBars> arrangement {};      // patrón de cada compás: -1 = silencio
        std::array<float, numInstruments> levels {};
        float swing = 0.0f;                              // 0..1 (1 = tresillo)
        bool humanize = true;
        int currentPattern = 0;                          // el que se edita (solo interfaz)

        DrumData()
        {
            arrangement.fill (-1);
            levels.fill (0.6f);   // deja margen para que el master no sature
        }

        int lastBar() const
        {
            for (int b = maxBars - 1; b >= 0; --b)
                if (arrangement[(size_t) b] >= 0)
                    return b;
            return -1;
        }
    };

    // ------------------------------------------------------------------ Estilos
    struct Style { const char* name; int beats, unit; float swing; };

    inline const std::array<Style, 9>& styles()
    {
        static const std::array<Style, 9> s {{
            { "Rock", 4, 4, 0.0f }, { "Pop", 4, 4, 0.0f }, { "Funk", 4, 4, 0.0f }, { "Balada", 4, 4, 0.0f },
            { "Shuffle", 4, 4, 0.75f }, { "Heavy", 4, 4, 0.0f }, { "Reggae", 4, 4, 0.0f },
            { "Vals (3/4)", 3, 4, 0.0f }, { "6/8", 6, 8, 0.0f } }};
        return s;
    }

    // Rellena A (ritmo), B (estribillo) y C (redoble) con el estilo elegido.
    // Notación: 'x' acento, 'o' normal, '-' suave, '.' nada; una letra por semicorchea.
    inline void applyStyle (DrumData& d, int styleIndex, int stepsPerBar)
    {
        struct Rows { const char* k; const char* s; const char* h; const char* oh; const char* th; const char* tl; const char* cr; const char* ri; };
        static const Rows A[] = {
            { "x.......x.o.....", "....x.......x...", "o.o.o.o.o.o.o.o.", "", "", "", "", "" },                       // Rock
            { "x......ox.......", "....x.......x...", "xoooxoooxoooxooo", "", "", "", "", "" },                       // Pop
            { "x..x..x...x..x..", "....x..-.-..x..-", "xoxoxoxoxoxoxoxo", "", "", "", "", "" },                       // Funk
            { "x.........x.....", "........x.......", "o.o.o.o.o.o.o.o.", "", "", "", "", "" },                       // Balada
            { "x.....o.x.....o.", "....x.......x...", "x.ox.ox.ox.ox.o.", "", "", "", "", "" },                       // Shuffle
            { "xoxoxoxoxoxoxoxo", "....x.......x...", "", "", "", "", "x...o...o...o...", "" },                       // Heavy
            { "........x.......", "........x.......", "..o...o...o...o.", "", "", "", "", "" },                       // Reggae
            { "x...........", "....o...o...", "o.o.o.o.o.o.", "", "", "", "", "" },                                   // Vals
            { "x.....o.....", "......x.....", "x.oo.ox.oo.o", "", "", "", "", "" } };                                 // 6/8
        static const Rows C[] = {   // redobles
            { "x.......x.......", "....x...xx.x....", "o.o.o.o.........", "", "............", "............xxoo", "", "" },
            { "x.......x.......", "....x.......xoxx", "xoooxoooxooo....", "", "", "", "", "" },
            { "x..x..x...x.....", "....x..-.-..xxxx", "xoxoxoxoxoxo....", "", "", "", "", "" },
            { "x.........x.....", "........x...oooo", "o.o.o.o.o.o.....", "", "", "", "", "" },
            { "x.....o.x.......", "....x.......xxox", "x.ox.ox.ox.o....", "", "", "", "", "" },
            { "xoxoxoxoxoxo....", "....x.......xxxx", "", "", "........xx..", "............xxxx", "x...............", "" },
            { "........x.......", "........x...oooo", "..o...o...o.....", "", "", "", "", "" },
            { "x...........", "....o...xxxx", "o.o.o.o.....", "", "", "", "", "" },
            { "x.....o.....", "......x.xxxx", "x.oo.o......", "", "", "", "", "" } };

        const int st = juce::jlimit (0, (int) styles().size() - 1, styleIndex);
        auto fill = [stepsPerBar] (Pattern& p, int inst, const char* row)
        {
            const int len = (int) std::strlen (row);
            if (len == 0) return;
            for (int i = 0; i < stepsPerBar && i < maxSteps; ++i)
            {
                const char c = row[i % len];   // si el compás es más largo que el estilo, se repite
                p.steps[(size_t) inst][(size_t) i] = c == 'x' ? 3 : c == 'o' ? 2 : c == '-' ? 1 : 0;
            }
        };
        auto load = [&fill] (Pattern& p, const Rows& r)
        {
            p.clear();
            fill (p, kick, r.k); fill (p, snare, r.s); fill (p, hatClosed, r.h); fill (p, hatOpen, r.oh);
            fill (p, tomHigh, r.th); fill (p, tomLow, r.tl); fill (p, crash, r.cr); fill (p, ride, r.ri);
        };

        load (d.patterns[0], A[st]);

        // B (estribillo): como A pero con ride (o charles abierto) y crash al empezar
        d.patterns[1] = d.patterns[0];
        auto& b = d.patterns[1];
        for (int i = 0; i < maxSteps; ++i)
        {
            if (b.steps[hatClosed][(size_t) i] != 0)
            {
                b.steps[ride][(size_t) i] = b.steps[hatClosed][(size_t) i];
                b.steps[hatClosed][(size_t) i] = 0;
            }
        }
        b.steps[crash][0] = 3;

        load (d.patterns[2], C[st]);
        d.swing = styles()[(size_t) st].swing;
    }

    // ------------------------------------------------------------------ Kit sintetizado
    class Kit
    {
    public:
        void prepare (double sampleRate) { sr = sampleRate; for (auto& v : voices) v.inst = -1; }

        void trigger (int inst, float velocity, int sampleOffset)
        {
            // El charles cerrado corta al abierto, como en una batería real
            if (inst == hatClosed)
                for (auto& v : voices)
                    if (v.inst == hatOpen) v.choke = true;

            Voice* slot = &voices[0];
            for (auto& v : voices)
            {
                if (v.inst < 0) { slot = &v; break; }
                if (v.age > slot->age) slot = &v;   // si no hay libres, la más antigua
            }
            *slot = Voice();
            slot->inst = inst;
            slot->vel = velocity;
            slot->delay = sampleOffset;
            slot->rng = 0x9E3779B9u ^ (uint32_t) (inst * 7919 + ++counter * 104729);
        }

        // Suma el kit en L/R (n muestras)
        void render (float* left, float* right, int n, const std::array<float, numInstruments>& levels)
        {
            static const float pan[] = { 0.0f, 0.05f, 0.3f, 0.3f, 0.25f, -0.25f, -0.35f, -0.2f };
            for (auto& v : voices)
            {
                if (v.inst < 0)
                    continue;
                const float level = levels[(size_t) v.inst] * v.vel;
                const float gl = std::sqrt (0.5f * (1.0f - pan[v.inst])), gr = std::sqrt (0.5f * (1.0f + pan[v.inst]));
                for (int i = 0; i < n; ++i)
                {
                    if (v.delay > 0) { --v.delay; continue; }
                    const float s = next (v) * level;
                    left[i] += s * gl * 1.4142f;
                    right[i] += s * gr * 1.4142f;
                    if (v.inst < 0) break;
                }
            }
        }

    private:
        struct Voice
        {
            int inst = -1, delay = 0;
            juce::int64 age = 0;
            float vel = 1.0f;
            bool choke = false;
            uint32_t rng = 1;
            double ph1 = 0, ph2 = 0;
            double metal[6] {};
            float hpX1 = 0, hpY1 = 0, hpX2 = 0, hpY2 = 0, chokeGain = 1.0f;
        };

        static float noise (Voice& v)
        {
            v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5;
            return (float) (v.rng & 0xffffff) / (float) 0x7fffff - 1.0f;
        }

        // Paso alto de un polo (dos en cascada para los platos)
        float highPass (float x, float& x1, float& y1, double cutoff) const
        {
            const float rc = (float) (1.0 / (juce::MathConstants<double>::twoPi * cutoff));
            const float a = rc / (rc + (float) (1.0 / sr));
            const float y = a * (y1 + x - x1);
            x1 = x; y1 = y;
            return y;
        }

        // Ruido "metálico" de platos: seis ondas cuadradas inarmónicas (como las cajas de ritmos clásicas)
        float metallic (Voice& v, double scale)
        {
            static const double f[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            float sum = 0.0f;
            for (int k = 0; k < 6; ++k)
            {
                v.metal[k] += f[k] * scale / sr;
                v.metal[k] -= std::floor (v.metal[k]);
                sum += v.metal[k] < 0.5 ? 1.0f : -1.0f;
            }
            return sum / 6.0f;
        }

        float next (Voice& v)
        {
            const double t = (double) v.age++ / sr;
            const double twoPi = juce::MathConstants<double>::twoPi;
            float out = 0.0f, end = 1.0f;

            switch (v.inst)
            {
                case kick:
                {
                    const double f = 48.0 + 115.0 * std::exp (-t * 30.0);
                    v.ph1 += twoPi * f / sr;
                    const float body = (float) (std::sin (v.ph1) * std::exp (-t * 6.5));
                    const float click = highPass (noise (v), v.hpX1, v.hpY1, 2000.0) * (float) std::exp (-t * 350.0) * 0.35f;
                    out = std::tanh ((body + click) * 1.3f) * 0.9f;
                    end = 0.7f;
                    break;
                }
                case snare:
                {
                    v.ph1 += twoPi * 185.0 / sr;
                    v.ph2 += twoPi * 330.0 / sr;
                    const float tone = (float) ((std::sin (v.ph1) * 0.6 + std::sin (v.ph2) * 0.4) * std::exp (-t * 24.0));
                    const float snares = highPass (noise (v), v.hpX1, v.hpY1, 1500.0) * (float) std::exp (-t * 15.0);
                    out = (tone * 0.55f + snares * 0.85f) * 0.72f;
                    end = 0.45f;
                    break;
                }
                case hatClosed:
                case hatOpen:
                {
                    float m = metallic (v, 1.7) * 0.6f + noise (v) * 0.4f;
                    m = highPass (highPass (m, v.hpX1, v.hpY1, 7000.0), v.hpX2, v.hpY2, 7000.0);
                    const bool open = v.inst == hatOpen;
                    if (v.choke) { v.chokeGain *= 0.995f; }
                    out = m * (float) std::exp (-t * (open ? 5.5 : 60.0)) * v.chokeGain * 1.2f;
                    end = open ? 0.8f : 0.15f;
                    if (v.chokeGain < 0.001f) end = 0.0f;
                    break;
                }
                case tomHigh:
                case tomLow:
                {
                    const double base = v.inst == tomHigh ? 175.0 : 100.0;
                    v.ph1 += twoPi * base * (1.0 + 0.55 * std::exp (-t * 18.0)) / sr;
                    out = ((float) (std::sin (v.ph1) * std::exp (-t * (v.inst == tomHigh ? 9.0 : 7.0)))
                           + noise (v) * (float) std::exp (-t * 70.0) * 0.12f) * 0.75f;
                    end = 0.8f;
                    break;
                }
                case crash:
                {
                    float m = metallic (v, 2.3) * 0.5f + noise (v) * 0.5f;
                    m = highPass (highPass (m, v.hpX1, v.hpY1, 4000.0), v.hpX2, v.hpY2, 3000.0);
                    out = m * (float) (std::exp (-t * 2.0) * juce::jmin (1.0, t * 400.0)) * 0.9f;
                    end = 2.4f;
                    break;
                }
                case ride:
                {
                    float m = metallic (v, 1.35) * 0.75f + noise (v) * 0.25f;
                    m = highPass (highPass (m, v.hpX1, v.hpY1, 3500.0), v.hpX2, v.hpY2, 3000.0);
                    v.ph1 += twoPi * 2950.0 / sr;
                    const float bell = (float) (std::sin (v.ph1) * std::exp (-t * 5.0)) * 0.25f;
                    out = (m * (float) std::exp (-t * 3.2) + bell) * 0.6f;
                    end = 1.6f;
                    break;
                }
                default: break;
            }

            if (t >= end)
                v.inst = -1;
            return out;
        }

        std::array<Voice, 32> voices;
        double sr = 48000.0;
        uint32_t counter = 0;
    };
}
