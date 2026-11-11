#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>
#include <functional>

// ============================================================================
// Transposición de alta calidad (offline) que mantiene la duración:
//   1. Phase vocoder con "bloqueo de fase" (Laroche-Dolson): estira el audio
//      en el tiempo por el factor de transposición sin cambiar el tono.
//   2. Remuestreo (sinc enventanado) por el mismo factor: vuelve a la duración
//      original y el tono queda desplazado.
// ============================================================================
class PitchShifter
{
public:
    // Transpone "len" muestras de "in" y escribe exactamente "len" muestras en "out".
    // progress (opcional) recibe 0..1 y devuelve false para cancelar (entonces devuelve false).
    static bool process (const float* in, int len, float* out, double semitones,
                         const std::function<bool (double)>& progress = {})
    {
        if (len <= 0)
            return true;

        const double ratio = std::pow (2.0, semitones / 12.0);
        if (std::abs (ratio - 1.0) < 1.0e-6)
        {
            std::copy (in, in + len, out);
            return true;
        }

        // 1. Estirar en el tiempo por "ratio" (mismo tono)  2. Remuestrear: vuelve a "len" con el tono desplazado
        constexpr int pad = 4096;
        std::vector<float> stretched;
        if (! stretchPadded (in, len, ratio, stretched, progress))
            return false;

        const int needed = len + 2 * pad;
        std::vector<float> resampled ((size_t) needed, 0.0f);
        juce::WindowedSincInterpolator interp;
        interp.process (ratio, stretched.data(), resampled.data(), needed, (int) stretched.size(), 0);

        // Las ventanas del phase vocoder están centradas: el contenido se desplaza (N/2)·(r-1)/r muestras
        const int start = pad - (int) std::lround (2048.0 * (ratio - 1.0) / ratio);
        std::copy (resampled.begin() + start, resampled.begin() + start + len, out);
        return true;
    }

    // Cambia la duración sin cambiar el tono. speed = 0.7 -> 70 % de velocidad (dura 1/0.7 veces más)
    static bool changeSpeed (const float* in, int len, double speed, std::vector<float>& out,
                             const std::function<bool (double)>& progress = {})
    {
        const double stretch = 1.0 / juce::jlimit (0.25, 4.0, speed);
        const int outLen = (int) std::llround (len * stretch);
        out.assign ((size_t) outLen, 0.0f);
        if (len <= 0)
            return true;
        if (std::abs (stretch - 1.0) < 1.0e-6)
        {
            std::copy (in, in + len, out.begin());
            return true;
        }

        constexpr int pad = 4096;
        std::vector<float> stretched;
        if (! stretchPadded (in, len, stretch, stretched, progress))
            return false;

        // El silencio inicial añadido también se ha estirado; y las ventanas están centradas
        const auto offset = (size_t) std::llround (pad * stretch - 2048.0 * (stretch - 1.0));
        for (size_t i = 0; i < (size_t) outLen && offset + i < stretched.size(); ++i)
            out[i] = stretched[offset + i];
        return true;
    }

private:
    // Phase vocoder: estira "in" (con 4096 muestras de silencio delante y 8192 detrás) por "stretch"
    static bool stretchPadded (const float* in, int len, double stretch, std::vector<float>& stretched,
                               const std::function<bool (double)>& progress)
    {
        const double ratio = stretch;

        constexpr int order = 12, N = 1 << order;   // ventanas de 4096 muestras (bien para bajo y guitarra)
        constexpr int hopS = N / 4;                 // salto de síntesis (75 % de solapamiento)
        const double hopA = hopS / ratio;           // salto de análisis

        // Entrada con márgenes de silencio para que los extremos se procesen completos
        const int pad = N;
        std::vector<float> src ((size_t) (len + 3 * pad), 0.0f);
        std::copy (in, in + len, src.begin() + pad);

        const int numFrames = (int) std::ceil ((double) (len + 2 * pad) / hopA);
        stretched.assign ((size_t) (numFrames * hopS + N + 8), 0.0f);

        juce::dsp::FFT fft (order);
        std::vector<float> window ((size_t) N), frame ((size_t) N * 2);
        for (int i = 0; i < N; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);

        constexpr int bins = N / 2 + 1;
        std::vector<float> mag (bins), phase (bins), prevPhase (bins, 0.0f), synthPhase (bins, 0.0f), prevSynth (bins, 0.0f);
        std::vector<int> peakOf (bins);

        int prevPos = 0;
        for (int f = 0; f < numFrames; ++f)
        {
            const int pos = (int) std::llround (f * hopA);
            if (pos + N > (int) src.size())
                break;
            if (progress && (f & 31) == 0 && ! progress ((double) f / numFrames))
                return false;

            // Análisis
            std::fill (frame.begin(), frame.end(), 0.0f);
            for (int i = 0; i < N; ++i)
                frame[(size_t) i] = src[(size_t) (pos + i)] * window[(size_t) i];
            fft.performRealOnlyForwardTransform (frame.data(), true);

            for (int k = 0; k < bins; ++k)
            {
                const float re = frame[(size_t) (2 * k)], im = frame[(size_t) (2 * k + 1)];
                mag[(size_t) k] = std::sqrt (re * re + im * im);
                phase[(size_t) k] = std::atan2 (im, re);
            }

            if (f == 0)
            {
                synthPhase = phase;
            }
            else
            {
                // Picos espectrales y su zona de influencia
                std::vector<int> peaks;
                for (int k = 2; k < bins - 2; ++k)
                    if (mag[(size_t) k] > mag[(size_t) k - 1] && mag[(size_t) k] >= mag[(size_t) k + 1]
                         && mag[(size_t) k] > mag[(size_t) k - 2] && mag[(size_t) k] >= mag[(size_t) k + 2])
                        peaks.push_back (k);
                if (peaks.empty())
                    peaks.push_back (1);

                size_t p = 0;
                for (int k = 0; k < bins; ++k)
                {
                    while (p + 1 < peaks.size() && std::abs (peaks[p + 1] - k) < std::abs (peaks[p] - k))
                        ++p;
                    peakOf[(size_t) k] = peaks[p];
                }

                // Avance de fase de cada pico según su frecuencia instantánea
                const double actualHop = (double) (pos - prevPos);
                for (int pk : peaks)
                {
                    const double omega = juce::MathConstants<double>::twoPi * pk / N;
                    double delta = phase[(size_t) pk] - prevPhase[(size_t) pk] - omega * actualHop;
                    delta -= juce::MathConstants<double>::twoPi * std::round (delta / juce::MathConstants<double>::twoPi);
                    const double instFreq = omega + delta / juce::jmax (1.0, actualHop);
                    synthPhase[(size_t) pk] = (float) (prevSynth[(size_t) pk] + instFreq * hopS);
                }

                // El resto de bins conservan su relación de fase con el pico (menos efecto "fase")
                for (int k = 0; k < bins; ++k)
                {
                    const int pk = peakOf[(size_t) k];
                    if (pk != k)
                        synthPhase[(size_t) k] = synthPhase[(size_t) pk] + (phase[(size_t) k] - phase[(size_t) pk]);
                }
            }

            prevPhase = phase;
            prevSynth = synthPhase;
            prevPos = pos;

            // Síntesis
            for (int k = 0; k < bins; ++k)
            {
                frame[(size_t) (2 * k)]     = mag[(size_t) k] * std::cos (synthPhase[(size_t) k]);
                frame[(size_t) (2 * k + 1)] = mag[(size_t) k] * std::sin (synthPhase[(size_t) k]);
            }
            fft.performRealOnlyInverseTransform (frame.data());

            const int outPos = f * hopS;
            for (int i = 0; i < N; ++i)
                stretched[(size_t) (outPos + i)] += frame[(size_t) i] * window[(size_t) i] / 1.5f;   // suma de ventanas² = 1,5
        }

        return true;
    }
};
