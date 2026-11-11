#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Text.h"
#if SIMPLEDAW_HAS_BINARYDATA
 #include "BinaryData.h"
#endif

// Colores de la marca
namespace Brand
{
    const juce::Colour yellow { 0xffffc801 };
    const juce::Colour black  { 0xff11120d };

    inline juce::Image loadImage (const char* resourceName)
    {
       #if SIMPLEDAW_HAS_BINARYDATA
        int size = 0;
        if (auto* data = BinaryData::getNamedResource (resourceName, size))
            return juce::ImageCache::getFromMemory (data, size);
       #endif
        juce::ignoreUnused (resourceName);
        return {};
    }
}

// ============================================================================
// Pantalla de arranque animada:
//   1. Las dos mitades del logotipo entran desde los lados (amarilla y negra).
//   2. Las barras del ecualizador de los auriculares cobran vida.
//   3. Abajo, una onda de barras amarillas y el estado de la carga.
//   4. Al terminar, se desvanece sobre la ventana principal.
// ============================================================================
class SplashWindow : public juce::Component, private juce::Timer
{
public:
    explicit SplashWindow (const juce::String& versionText) : version (versionText)
    {
        logo = Brand::loadImage ("logofull_png");
        if (logo.isValid())
        {
            const int half = logo.getWidth() / 2;
            leftHalf  = logo.getClippedImage ({ 0, 0, half, logo.getHeight() });
            rightHalf = logo.getClippedImage ({ half, 0, logo.getWidth() - half, logo.getHeight() });
        }

        setOpaque (false);
        setSize (640, 404);
        addToDesktop (juce::ComponentPeer::windowHasDropShadow | juce::ComponentPeer::windowIgnoresKeyPresses);
        centreWithSize (getWidth(), getHeight());
        setAlwaysOnTop (true);
        setVisible (true);
        toFront (false);

        startMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
    }

    void setStatus (const juce::String& s) { status = s; repaint(); }

    // Pide el cierre: se respeta la duración mínima de la animación.
    // onFadeStart: mostrar la ventana principal.  onDone: destruir esta pantalla.
    void finish (std::function<void()> onFadeStart, std::function<void()> onDone)
    {
        fadeStartCallback = std::move (onFadeStart);
        doneCallback = std::move (onDone);
        finishRequested = true;
    }

    void paint (juce::Graphics& g) override
    {
        const float t = elapsed();
        auto bounds = getLocalBounds().toFloat();

        // Fondo
        g.setColour (Brand::black);
        g.fillRoundedRectangle (bounds, 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 14.0f, 1.0f);

        auto logoArea = juce::Rectangle<float> (20.0f, 20.0f, 600.0f, 300.0f);

        // 1. Las dos mitades del logotipo entran desde los lados
        const float intro = easeOutCubic (juce::jlimit (0.0f, 1.0f, t / 0.65f));
        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path clip;
            clip.addRoundedRectangle (logoArea, 6.0f);
            g.reduceClipRegion (clip);

            const float shift = (1.0f - intro) * logoArea.getWidth() * 0.5f;
            auto leftR  = logoArea.withWidth (logoArea.getWidth() * 0.5f).translated (-shift, 0.0f);
            auto rightR = logoArea.withTrimmedLeft (logoArea.getWidth() * 0.5f).translated (shift, 0.0f);

            if (logo.isValid())
            {
                g.setOpacity (intro);
                g.drawImage (leftHalf, leftR, juce::RectanglePlacement::stretchToFit);
                g.drawImage (rightHalf, rightR, juce::RectanglePlacement::stretchToFit);
                g.setOpacity (1.0f);

                // 2. Barras del ecualizador de los auriculares, animadas
                drawHeadphoneBars (g, rightR, t);
            }
            else
            {
                g.setColour (Brand::yellow);
                g.fillRect (leftR);
                g.setColour (Brand::black);
                g.setFont (juce::FontOptions (64.0f, juce::Font::bold));
                g.drawFittedText ("SIMPLE\nDAW", leftR.reduced (30.0f).toNearestInt(), juce::Justification::centredLeft, 2);
            }
        }

        // 3. Onda inferior y estado
        auto bottom = juce::Rectangle<float> (20.0f, 334.0f, 600.0f, 34.0f);
        const float waveIn = juce::jlimit (0.0f, 1.0f, (t - 0.45f) / 0.5f);
        drawWave (g, bottom, t, waveIn);

        g.setFont (juce::FontOptions (11.5f));
        g.setColour (juce::Colours::white.withAlpha (0.55f * waveIn));
        g.drawText (status + "   " + version, juce::Rectangle<float> (22.0f, 374.0f, 300.0f, 18.0f), juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (11.0f));
        g.setColour (juce::Colours::white.withAlpha (0.5f * waveIn));
        g.drawText ("Creado por Paco Frade", juce::Rectangle<float> (330.0f, 374.0f, 288.0f, 18.0f), juce::Justification::centredRight);

        // Línea de progreso amarilla (avanza durante la duración mínima)
        const float progress = juce::jlimit (0.0f, 1.0f, t / minDuration);
        g.setColour (Brand::yellow.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (20.0f, 370.0f, 600.0f * easeInOut (progress), 2.0f));
    }

private:
    static float easeOutCubic (float x) { return 1.0f - std::pow (1.0f - x, 3.0f); }
    static float easeInOut (float x)    { return x < 0.5f ? 2.0f * x * x : 1.0f - std::pow (-2.0f * x + 2.0f, 2.0f) * 0.5f; }

    float elapsed() const { return (float) ((juce::Time::getMillisecondCounterHiRes() - startMs) * 0.001); }

    // Las 7 barras amarillas del logotipo (medidas sobre la imagen original de 1134x567)
    void drawHeadphoneBars (juce::Graphics& g, juce::Rectangle<float> rightHalfArea, float t)
    {
        const float scale = rightHalfArea.getWidth() / (float) rightHalf.getWidth();
        const float originX = (float) logo.getWidth() / 2.0f;
        auto map = [&] (float x, float y) { return juce::Point<float> (rightHalfArea.getX() + (x - originX) * scale,
                                                                       rightHalfArea.getY() + y * scale); };

        // Tapar las barras originales con el fondo negro del logotipo
        const auto a = map (750.0f, 262.0f), b = map (950.0f, 471.0f);
        g.setColour (Brand::black);
        g.fillRect (juce::Rectangle<float> (a, b));

        static const float centresX[]  = { 766.5f, 794.5f, 822.5f, 850.0f, 877.5f, 905.5f, 933.0f };
        static const float halfH[]     = { 39.5f, 58.0f, 75.5f, 95.0f, 75.5f, 58.0f, 39.0f };
        static const float phase[]     = { 0.0f, 1.7f, 0.6f, 2.4f, 1.1f, 2.9f, 0.3f };
        constexpr float centreY = 366.5f, width = 13.5f;

        // Las barras crecen al terminar la entrada del logo y luego "bailan"
        const float grow = easeOutCubic (juce::jlimit (0.0f, 1.0f, (t - 0.5f) / 0.45f));
        for (int i = 0; i < 7; ++i)
        {
            const float dance = 0.62f + 0.38f * std::sin (t * 7.0f + phase[i]) * std::sin (t * 3.1f + phase[i] * 1.7f);
            const float h = halfH[i] * grow * (t < 0.95f ? 1.0f : dance);
            if (h < 1.0f)
                continue;
            const auto top = map (centresX[i] - width * 0.5f, centreY - h);
            const auto bot = map (centresX[i] + width * 0.5f, centreY + h);
            g.setColour (Brand::yellow);
            g.fillRoundedRectangle (juce::Rectangle<float> (top, bot), width * scale * 0.5f);
        }
    }

    void drawWave (juce::Graphics& g, juce::Rectangle<float> area, float t, float appear)
    {
        constexpr int numBars = 60;
        const float step = area.getWidth() / numBars;
        for (int i = 0; i < numBars; ++i)
        {
            const float x = (float) i / (numBars - 1);
            // Envolvente en forma de rombo (como la del logotipo) con movimiento
            const float envelope = 1.0f - std::abs (x - 0.5f) * 1.6f;
            const float motion = 0.55f + 0.45f * std::sin (t * 5.0f - x * 14.0f) * std::sin (t * 2.3f + x * 6.0f);
            // Aparecen desde el centro hacia los lados
            const float reveal = juce::jlimit (0.0f, 1.0f, appear * 1.6f - std::abs (x - 0.5f) * 1.2f);
            const float h = juce::jmax (2.0f, area.getHeight() * juce::jmax (0.08f, envelope) * motion * reveal);
            g.setColour (Brand::yellow.withAlpha (0.25f + 0.75f * reveal * juce::jmax (0.3f, envelope)));
            g.fillRoundedRectangle (area.getX() + i * step + step * 0.3f, area.getCentreY() - h * 0.5f,
                                    step * 0.4f, h, step * 0.2f);
        }
    }

    void timerCallback() override
    {
        const float t = elapsed();

        if (finishRequested && fadeStart < 0.0f && t >= minDuration)
        {
            fadeStart = t;
            if (fadeStartCallback) fadeStartCallback();
        }

        if (fadeStart >= 0.0f)
        {
            const float a = 1.0f - (t - fadeStart) / fadeDuration;
            if (a <= 0.0f)
            {
                stopTimer();
                setVisible (false);
                if (auto cb = doneCallback)
                    juce::MessageManager::callAsync (cb);   // se destruye fuera de su propio temporizador
                return;
            }
            setAlpha (a);
        }
        repaint();
    }

    static constexpr float minDuration = 2.6f, fadeDuration = 0.4f;

    juce::Image logo, leftHalf, rightHalf;
    juce::String version, status { tr8 ("Iniciando...") };
    double startMs = 0.0;
    bool finishRequested = false;
    float fadeStart = -1.0f;
    std::function<void()> fadeStartCallback, doneCallback;
};
