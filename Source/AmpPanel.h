#pragma once
// Frontal del Simpleamp dibujado por código (sin fotos ni diseños ajenos).
// Mismo tamaño y mismas posiciones que usa AmpEditor (1537 x 649 "píxeles de imagen"):
//   - potenciómetros centrados en y = 514 (el de PRESENCE, en x = 439, apuntando al 5: AmpEditor lo gira)
//   - interruptores pequeños con la palanca hacia la derecha (= activado; AmpEditor los voltea)
//   - interruptor de encendido con la palanca hacia abajo y el piloto encendido
#include <juce_gui_basics/juce_gui_basics.h>
#include "Text.h"

namespace AmpPanelArt
{
    constexpr int width = 1537, height = 649;

    inline void drawText (juce::Graphics& g, const juce::String& text, float x, float y, float w, float h,
                          float size, juce::Colour colour, juce::Justification just = juce::Justification::centred)
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (size, juce::Font::bold));
        g.drawText (text, juce::Rectangle<float> (x, y, w, h), just, false);
    }

    // Potenciómetro negro con faldón y raya blanca hacia arriba (posición 5)
    inline void drawKnob (juce::Graphics& g, juce::Point<float> c)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (c.x - 26.0f, c.y - 23.0f, 52.0f, 51.0f);   // sombra (dentro del círculo que AmpEditor gira)

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), c.x - 10.0f, c.y - 22.0f,
                                                 juce::Colour (0xff050505), c.x + 12.0f, c.y + 24.0f, false));
        g.fillEllipse (c.x - 26.0f, c.y - 26.0f, 52.0f, 52.0f);   // faldón

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2e2e), c.x - 8.0f, c.y - 16.0f,
                                                 juce::Colour (0xff0b0b0b), c.x + 8.0f, c.y + 16.0f, false));
        g.fillEllipse (c.x - 19.0f, c.y - 19.0f, 38.0f, 38.0f);   // tapa
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (c.x - 19.0f, c.y - 19.0f, 38.0f, 38.0f, 1.2f);

        g.setColour (juce::Colour (0xfff2f2f2));
        g.drawLine (c.x, c.y - 6.0f, c.x, c.y - 24.0f, 3.2f);     // indicador
    }

    // Escala serigrafiada 0-10 alrededor (300 grados)
    inline void drawScale (juce::Graphics& g, juce::Point<float> c)
    {
        for (int v = 0; v <= 10; v += 2)
        {
            const float a = ((float) v / 10.0f - 0.5f) * juce::degreesToRadians (300.0f);
            const auto p = c + juce::Point<float> (std::sin (a), -std::cos (a)) * 43.0f;
            drawText (g, juce::String (v), p.x - 12.0f, p.y - 8.0f, 24.0f, 16.0f, 13.0f, juce::Colour (0xff1a1a1a));
        }
    }

    // Palanca cromada: tuerca + palo hacia "dir" + bola en la punta
    inline void drawToggle (juce::Graphics& g, juce::Point<float> c, juce::Point<float> dir, float nutR, float lever, float thick)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (c.x - nutR + 1.5f, c.y - nutR + 2.0f, nutR * 2.0f, nutR * 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f4f4), c.x - nutR * 0.6f, c.y - nutR * 0.6f,
                                                 juce::Colour (0xff6a6a6a), c.x + nutR, c.y + nutR, true));
        g.fillEllipse (c.x - nutR, c.y - nutR, nutR * 2.0f, nutR * 2.0f);
        g.setColour (juce::Colour (0xff2a2a2a));
        g.drawEllipse (c.x - nutR, c.y - nutR, nutR * 2.0f, nutR * 2.0f, 0.8f);

        const auto tip = c + dir * lever;
        juce::Path stick;
        stick.startNewSubPath (c);
        stick.lineTo (tip);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), c.x, c.y - thick,
                                                 juce::Colour (0xff8a8a8a), c.x, c.y + thick, false));
        g.strokePath (stick, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float ball = thick * 0.95f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), tip.x - ball * 0.5f, tip.y - ball * 0.5f,
                                                 juce::Colour (0xff7a7a7a), tip.x + ball, tip.y + ball, true));
        g.fillEllipse (tip.x - ball, tip.y - ball, ball * 2.0f, ball * 2.0f);
    }

    inline juce::Image draw()
    {
        juce::Image img (juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
        juce::Graphics g (img);
        const float W = (float) width, H = (float) height;

        // Fondo (detrás del cabezal)
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2622), 0.0f, 0.0f, juce::Colour (0xff0d0c0b), 0.0f, H, false));
        g.fillAll();

        // Asa
        g.setColour (juce::Colour (0xff101010));
        g.fillRoundedRectangle (566.0f, 24.0f, 404.0f, 44.0f, 20.0f);
        g.setColour (juce::Colour (0xff2c2c2c));
        g.drawRoundedRectangle (566.0f, 24.0f, 404.0f, 44.0f, 20.0f, 2.0f);
        g.setColour (juce::Colour (0xff8a8a8a));
        g.fillRoundedRectangle (548.0f, 52.0f, 26.0f, 16.0f, 3.0f);
        g.fillRoundedRectangle (962.0f, 52.0f, 26.0f, 16.0f, 3.0f);

        // Patas
        g.setColour (juce::Colour (0xff0a0a0a));
        g.fillRoundedRectangle (170.0f, 606.0f, 84.0f, 36.0f, 8.0f);
        g.fillRoundedRectangle (1284.0f, 606.0f, 84.0f, 36.0f, 8.0f);

        // Cabezal con tolex negro (textura de puntos aleatorios, siempre la misma)
        const juce::Rectangle<float> head (14.0f, 60.0f, W - 28.0f, 556.0f);
        g.setColour (juce::Colour (0xff121212));
        g.fillRoundedRectangle (head, 18.0f);
        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path clip;
            clip.addRoundedRectangle (head, 18.0f);
            g.reduceClipRegion (clip);
            juce::Random rnd (1971);
            for (int i = 0; i < 70000; ++i)
            {
                const float x = head.getX() + rnd.nextFloat() * head.getWidth();
                const float y = head.getY() + rnd.nextFloat() * head.getHeight();
                g.setColour (rnd.nextBool() ? juce::Colours::white.withAlpha (0.045f) : juce::Colours::black.withAlpha (0.5f));
                g.fillRect (x, y, 1.6f, 1.6f);
            }
            // Brillo suave arriba
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0.0f, head.getY(),
                                                     juce::Colours::transparentWhite, 0.0f, head.getY() + 220.0f, false));
            g.fillRect (head);
        }
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (head, 18.0f, 2.0f);

        // Ribete dorado y separación entre la rejilla de arriba y el panel de abajo
        const juce::Colour piping (0xffb89a5c);
        g.setColour (piping);
        g.drawRoundedRectangle (40.0f, 90.0f, W - 80.0f, 502.0f, 6.0f, 2.2f);
        g.setColour (piping.withAlpha (0.8f));
        g.drawLine (42.0f, 293.0f, W - 42.0f, 293.0f, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawLine (42.0f, 296.0f, W - 42.0f, 296.0f, 2.0f);

        // Logotipo "Simpleamp" (blanco con halo)
        {
            const juce::Rectangle<float> logo (300.0f, 140.0f, W - 600.0f, 140.0f);
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::shear (-0.2f, 0.0f).translated (0.2f * logo.getCentreY(), 0.0f));   // en cursiva
            g.setFont (juce::FontOptions (118.0f, juce::Font::bold));
            for (int k = 6; k >= 1; --k)
            {
                g.setColour (juce::Colours::white.withAlpha (0.035f));
                g.drawText ("Simpleamp", logo.translated (0.0f, 0.0f).expanded ((float) k), juce::Justification::centred, false);
            }
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawText ("Simpleamp", logo.translated (3.0f, 4.0f), juce::Justification::centred, false);
            g.setColour (juce::Colour (0xfff7f7f7));
            g.drawText ("Simpleamp", logo, juce::Justification::centred, false);
        }

        // Insignia SIMPLE DAW (a la izquierda del panel)
        {
            const juce::Rectangle<float> badge (68.0f, 438.0f, 90.0f, 114.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff0d27a), badge.getX(), badge.getY(),
                                                     juce::Colour (0xff8a6a24), badge.getRight(), badge.getBottom(), false));
            g.fillRoundedRectangle (badge, 4.0f);
            g.setColour (juce::Colour (0xff3a2a08));
            g.drawRoundedRectangle (badge, 4.0f, 2.0f);
            drawText (g, "SIMPLE", badge.getX(), badge.getY() + 14.0f, badge.getWidth(), 30.0f, 23.0f, juce::Colour (0xff121212));
            drawText (g, "DAW", badge.getX(), badge.getY() + 48.0f, badge.getWidth(), 50.0f, 36.0f, juce::Colour (0xff121212));
        }

        // Panel de mandos dorado (el mismo color con el que AmpEditor tapa el mando antes de girarlo)
        const juce::Colour plateColour (0xffe0c083);
        const juce::Rectangle<float> plate (240.0f, 436.0f, 1180.0f, 156.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (plate.expanded (6.0f), 12.0f);
        g.setColour (plateColour);
        g.fillRoundedRectangle (plate, 10.0f);
        g.setColour (juce::Colour (0xff8a6d36));
        g.drawRoundedRectangle (plate, 10.0f, 1.6f);

        // Potenciómetros (mismas posiciones que en AmpEditor)
        struct K { const char* label; float x; };
        for (auto k : { K { "PRESENCE", 439.0f }, K { "BASS", 551.5f }, K { "MIDDLE", 664.0f }, K { "TREBLE", 776.5f },
                        K { "MASTER", 889.5f }, K { "GAIN", 1001.5f }, K { "CLEAN VOLUME", 1114.0f },
                        K { "TREBLE", 1226.5f }, K { "BASS", 1337.5f } })
        {
            const juce::Point<float> c (k.x, 514.0f);
            drawText (g, k.label, k.x - 60.0f, 449.0f, 120.0f, 20.0f, 13.5f, juce::Colour (0xff151515));
            drawScale (g, c);
            drawKnob (g, c);
        }

        // Interruptores pequeños: palanca hacia la derecha (= activado / canal Lead)
        struct S { const char* label; float x, y; };
        for (auto s : { S { "FAT", 495.5f, 568.5f }, S { "BITE", 608.0f, 569.0f }, S { "SAT", 720.5f, 569.0f },
                        S { "VOICE", 1055.0f, 569.0f }, S { "BRIGHT", 1167.0f, 568.5f }, S { "CANAL", 1279.5f, 569.0f } })
        {
            drawToggle (g, { s.x, s.y }, juce::Point<float> (0.95f, -0.3f), 7.0f, 10.5f, 4.0f);   // cabe en los 30 x 30 de AmpEditor
            drawText (g, s.label, s.x + 20.0f, s.y - 8.0f, 90.0f, 16.0f, 11.0f, juce::Colour (0xff151515),
                      juce::Justification::centredLeft);
        }
        // Encendido: palanca hacia abajo y piloto rojo encendido
        drawToggle (g, { 292.0f, 516.0f }, juce::Point<float> (0.0f, 1.0f), 11.0f, 21.0f, 6.0f);
        drawText (g, "POWER", 262.0f, 562.0f, 60.0f, 18.0f, 12.0f, juce::Colour (0xff151515));
        {
            const juce::Rectangle<float> led (336.0f, 499.0f, 32.0f, 33.0f);
            g.setColour (juce::Colour (0xff1c1c1c));
            g.fillRoundedRectangle (led.expanded (3.0f), 3.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff6a40), led.getCentreX(), led.getCentreY(),
                                                     juce::Colour (0xffa00a00), led.getRight(), led.getBottom(), true));
            g.fillRoundedRectangle (led, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillEllipse (led.getX() + 7.0f, led.getY() + 6.0f, 8.0f, 6.0f);
        }

        return img;
    }
}
