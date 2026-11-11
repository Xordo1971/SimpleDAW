#pragma once
#include "Text.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

// Paleta y estilo visual de SimpleDAW: oscuro, con volumen (degradados, relieves y biseles suaves)
namespace Theme
{
    const juce::Colour background   { 0xff1c1d20 };
    const juce::Colour panel        { 0xff2c2e33 };
    const juce::Colour panelLight   { 0xff3a3d43 };
    const juce::Colour panelDark    { 0xff222327 };
    const juce::Colour lane         { 0xff26282c };
    const juce::Colour laneAlt      { 0xff2a2c30 };
    const juce::Colour border       { 0xff111214 };
    const juce::Colour text         { 0xffd8d9dc };
    const juce::Colour textDim      { 0xff8d9097 };
    const juce::Colour accent       { 0xff4ea1ff };
    const juce::Colour playGreen    { 0xff3fc46b };
    const juce::Colour recordRed    { 0xffe5484d };
    const juce::Colour soloYellow   { 0xffe8c547 };
    const juce::Colour muteBlue     { 0xff4a90d9 };
    const juce::Colour monitorOrange{ 0xffe89a3c };
    const juce::Colour playhead     { 0xffeeeeee };

    // Relleno con degradado vertical y borde, típico de las superficies "con volumen"
    inline void fillRaised (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, float radius)
    {
        g.setGradientFill (juce::ColourGradient (base.brighter (0.18f), r.getX(), r.getY(),
                                                 base.darker (0.25f),  r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    // Texto con las cifras en casillas del mismo ancho (Avenir Next tiene cifras de ancho variable y
    // los contadores "bailarían" al cambiar). Alineado a la izquierda y centrado en vertical.
    inline void drawFixedDigits (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area)
    {
        const auto font = g.getCurrentFont();
        float digitW = 0.0f;
        for (char c = '0'; c <= '9'; ++c)
            digitW = juce::jmax (digitW, juce::GlyphArrangement::getStringWidth (font, juce::String::charToString (c)));
        float x = area.getX();
        for (auto ch : text)
        {
            const auto s = juce::String::charToString (ch);
            const bool digit = ch >= '0' && ch <= '9';
            const float w = digit ? digitW : juce::GlyphArrangement::getStringWidth (font, s);
            g.drawText (s, juce::Rectangle<float> (x, area.getY(), w, area.getHeight()), juce::Justification::centred, false);
            x += w;
        }
    }

    // Superficie hundida (pantallas, carriles de faders, medidores)
    inline void fillInset (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, float radius)
    {
        g.setGradientFill (juce::ColourGradient (base.darker (0.35f), r.getX(), r.getY(),
                                                 base, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX() + radius, r.getRight() - radius);
    }
    // Icono de zoom junto a un regulador: flecha doble horizontal (tiempo) o vertical (altura)
    inline void drawZoomIcon (juce::Graphics& g, juce::Rectangle<int> area, bool vertical)
    {
        if (area.isEmpty())
            return;
        const auto c = area.toFloat().getCentre();
        const float l = 6.0f, a = 3.0f;
        juce::Path p;
        if (vertical)
        {
            p.startNewSubPath (c.x, c.y - l); p.lineTo (c.x, c.y + l);
            p.startNewSubPath (c.x - a, c.y - l + a); p.lineTo (c.x, c.y - l); p.lineTo (c.x + a, c.y - l + a);
            p.startNewSubPath (c.x - a, c.y + l - a); p.lineTo (c.x, c.y + l); p.lineTo (c.x + a, c.y + l - a);
        }
        else
        {
            p.startNewSubPath (c.x - l, c.y); p.lineTo (c.x + l, c.y);
            p.startNewSubPath (c.x - l + a, c.y - a); p.lineTo (c.x - l, c.y); p.lineTo (c.x - l + a, c.y + a);
            p.startNewSubPath (c.x + l - a, c.y - a); p.lineTo (c.x + l, c.y); p.lineTo (c.x + l - a, c.y + a);
        }
        g.setColour (textDim);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
}

// Indicador de pico máximo en dB: "-inf", "-12.3", "CLIP" (rojo si se llega a 0 dBFS o más)
namespace PeakReadout
{
    inline bool isClip (float gain) { return gain >= 0.999f; }

    inline juce::String text (float gain, bool sayClip)
    {
        if (gain < 1.0e-5f)                return "-inf";
        if (sayClip && isClip (gain))      return "CLIP";
        const float db = juce::Decibels::gainToDecibels (gain);
        return (db > 0.05f ? "+" : "") + juce::String (db, 1);
    }

    inline void draw (juce::Graphics& g, juce::Rectangle<float> r, float gain, bool sayClip)
    {
        const bool clip = isClip (gain);
        const float db = juce::Decibels::gainToDecibels (gain, -100.0f);
        Theme::fillInset (g, r, clip ? Theme::recordRed.darker (0.2f) : juce::Colour (0xff121315), 3.0f);
        g.setColour (clip ? juce::Colours::white : db > -6.0f ? Theme::soloYellow : db > -60.0f ? Theme::text : Theme::textDim);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold));
        g.drawText (text (gain, sayClip), r, juce::Justification::centred);
    }
}

class SimpleDAWLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Tipografía de toda la aplicación: Avenir Next (normal y negrita). Se aplica a cualquier texto con la
    // fuente por defecto, también la "monoespaciada". Si el sistema no la tiene (Windows), se usa la de siempre.
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override
    {
        const auto& name = font.getTypefaceName();
        if (name == juce::Font::getDefaultSansSerifFontName() || name == juce::Font::getDefaultMonospacedFontName()
             || name == juce::Font::getDefaultSerifFontName())
        {
            if (hasAvenir())
            {
                juce::Font f (font);
                f.setTypefaceName ("Avenir Next");
                f.setTypefaceStyle (font.isBold() ? (font.isItalic() ? "Bold Italic" : "Bold")
                                                  : (font.isItalic() ? "Italic" : "Regular"));
                if (auto t = juce::Typeface::createSystemTypefaceFor (f))
                    return t;
            }
        }
        return juce::LookAndFeel_V4::getTypefaceForFont (font);
    }

    static bool hasAvenir()
    {
        static const bool found = juce::Font::findAllTypefaceNames().contains ("Avenir Next");
        return found;
    }

    SimpleDAWLookAndFeel() : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::getDarkColourScheme())
    {
        using namespace Theme;
        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::TextButton::buttonColourId, panelLight);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setColour (juce::ComboBox::backgroundColourId, panelDark);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, border);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Slider::textBoxTextColourId, textDim);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, panelLight);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, border);
        setColour (juce::ScrollBar::thumbColourId, panelLight.brighter (0.2f));
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::ListBox::backgroundColourId, panelDark);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto base = on ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
        if (down)             base = base.darker (0.2f);
        else if (highlighted) base = base.brighter (0.1f);

        Theme::fillRaised (g, r, base, 4.0f);

        if (on)   // brillo suave alrededor de los botones activos
        {
            g.setColour (base.withAlpha (0.35f));
            g.drawRoundedRectangle (r.expanded (0.5f), 4.5f, 1.5f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, height * 0.55f), juce::Font::bold));
    }

    // Fader horizontal: carril hundido con relleno de color y una "tapa" de fader
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style == juce::Slider::LinearVertical)
        {
            // Fader de mesa de mezclas: carril fino hundido y tapa ancha con línea central
            const float cx = x + w * 0.5f;
            juce::Rectangle<float> track (cx - 2.5f, (float) y, 5.0f, (float) h);
            Theme::fillInset (g, track, Theme::panelDark, 2.5f);
            g.setColour (Theme::accent.withAlpha (0.6f));
            g.fillRoundedRectangle (track.withTop (pos).reduced (1.0f), 1.5f);

            juce::Rectangle<float> cap (cx - 13.0f, pos - 8.0f, 26.0f, 16.0f);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);
            Theme::fillRaised (g, cap, juce::Colour (0xffa4a8af), 3.0f);
            g.setColour (Theme::border.withAlpha (0.8f));
            g.drawHorizontalLine ((int) pos, cap.getX() + 3.0f, cap.getRight() - 3.0f);
            return;
        }

        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
            return;
        }

        const float cy = y + h * 0.5f;
        juce::Rectangle<float> track ((float) x, cy - 3.0f, (float) w, 6.0f);
        Theme::fillInset (g, track, Theme::panelDark, 3.0f);

        g.setColour (Theme::accent.withAlpha (0.75f));
        g.fillRoundedRectangle (track.withRight (pos).reduced (1.0f), 2.0f);

        juce::Rectangle<float> cap (pos - 6.0f, cy - 9.0f, 12.0f, 18.0f);
        Theme::fillRaised (g, cap, juce::Colour (0xff9da1a8), 2.5f);
        g.setColour (Theme::border.withAlpha (0.7f));
        g.drawVerticalLine ((int) pos, cap.getY() + 4.0f, cap.getBottom() - 4.0f);
    }

    // Potenciómetro pequeño (panorama)
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float prop,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        // Los envíos dibujan el arco desde cero y con el color de su bus
        const bool fromStart = (bool) slider.getProperties().getWithDefault ("arcFromStart", false);
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        auto knob = r.withSizeKeepingCentre (size, size);
        const auto c = knob.getCentre();
        const float radius = size * 0.5f;
        const float angle = startAngle + prop * (endAngle - startAngle);
        const float centreAngle = fromStart ? startAngle : (startAngle + endAngle) * 0.5f;

        // Arco de valor desde el centro (panorama)
        juce::Path bgArc, valArc;
        bgArc.addCentredArc (c.x, c.y, radius - 1.0f, radius - 1.0f, 0, startAngle, endAngle, true);
        g.setColour (Theme::border);
        g.strokePath (bgArc, juce::PathStrokeType (2.5f));
        valArc.addCentredArc (c.x, c.y, radius - 1.0f, radius - 1.0f, 0,
                              juce::jmin (centreAngle, angle), juce::jmax (centreAngle, angle), true);
        g.setColour (fromStart ? slider.findColour (juce::Slider::rotarySliderFillColourId) : Theme::accent);
        g.strokePath (valArc, juce::PathStrokeType (2.5f));

        auto inner = knob.reduced (4.0f);
        g.setGradientFill (juce::ColourGradient (Theme::panelLight.brighter (0.25f), inner.getX(), inner.getY(),
                                                 Theme::panelDark, inner.getX(), inner.getBottom(), false));
        g.fillEllipse (inner);
        g.setColour (Theme::border);
        g.drawEllipse (inner, 1.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.0f, -inner.getHeight() * 0.5f + 1.5f, 2.0f, inner.getHeight() * 0.35f, 1.0f);
        g.setColour (Theme::text);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c));
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        Theme::fillInset (g, r, Theme::panelDark, 4.0f);

        juce::Path arrow;
        const float ax = w - 14.0f, ay = h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (arrow);
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override
    {
        g.setColour (Theme::panelDark);
        g.fillRoundedRectangle ((float) x, (float) y, (float) w, (float) h, 4.0f);

        juce::Rectangle<float> thumb = vertical
            ? juce::Rectangle<float> ((float) x + 2, (float) thumbStart, (float) w - 4, (float) thumbSize)
            : juce::Rectangle<float> ((float) thumbStart, (float) y + 2, (float) thumbSize, (float) h - 4);
        g.setColour (Theme::panelLight.brighter (over || down ? 0.35f : 0.15f));
        g.fillRoundedRectangle (thumb, 3.0f);
    }
};

// Botón con icono dibujado (transporte y botones de pista)
class IconButton : public juce::Button
{
public:
    enum class Icon { toStart, play, record, stop, zoomIn, zoomOut, plus, settings, recordSmall, close, mixer, plugin, import, wave, metronome, tuner, cycle };

    IconButton (const juce::String& name, Icon i, juce::Colour onColour = Theme::accent)
        : juce::Button (name), icon (i), activeColour (onColour)
    {
        setWantsKeyboardFocus (false);
    }

    void setIconLabel (const juce::String& s) { label = s; repaint(); }

    std::function<void()> onRightClick;   // opciones extra con clic derecho (o Ctrl+clic en Mac)

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick)
        {
            onRightClick();
            return;
        }
        juce::Button::mouseDown (e);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();

        auto base = Theme::panelLight;
        if (on && fillWhenOn) base = activeColour;
        if (down)             base = base.darker (0.2f);
        else if (highlighted) base = base.brighter (0.1f);
        Theme::fillRaised (g, r, base, 5.0f);

        auto iconColour = on ? (fillWhenOn ? juce::Colours::black.withAlpha (0.8f) : activeColour)
                             : (icon == Icon::record || icon == Icon::recordSmall ? activeColour.withAlpha (0.85f)
                                                                                  : Theme::text);
        g.setColour (iconColour);

        auto area = r;
        if (label.isNotEmpty())
        {
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            auto textArea = area.removeFromRight (area.getWidth() - area.getHeight());
            g.drawText (label, textArea.withTrimmedRight (6.0f), juce::Justification::centred);
        }
        drawIcon (g, area.withSizeKeepingCentre (area.getHeight(), area.getHeight()).reduced (area.getHeight() * 0.3f));
    }

    bool fillWhenOn = false;   // true: el fondo entero se colorea al activarse

private:
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> a)
    {
        juce::Path p;
        switch (icon)
        {
            case Icon::play:
                p.addTriangle (a.getX() + a.getWidth() * 0.15f, a.getY(),
                               a.getX() + a.getWidth() * 0.15f, a.getBottom(),
                               a.getRight(), a.getCentreY());
                break;
            case Icon::stop:
                p.addRoundedRectangle (a.reduced (a.getWidth() * 0.06f), 1.5f);
                break;
            case Icon::record:
            case Icon::recordSmall:
                p.addEllipse (a.reduced (icon == Icon::recordSmall ? a.getWidth() * 0.1f : 0.0f));
                break;
            case Icon::toStart:
                p.addRectangle (a.getX(), a.getY(), a.getWidth() * 0.16f, a.getHeight());
                p.addTriangle (a.getRight(), a.getY(), a.getRight(), a.getBottom(),
                               a.getX() + a.getWidth() * 0.2f, a.getCentreY());
                break;
            case Icon::plus:
            case Icon::zoomIn:
                p.addRectangle (a.getCentreX() - 1.0f, a.getY(), 2.0f, a.getHeight());
                p.addRectangle (a.getX(), a.getCentreY() - 1.0f, a.getWidth(), 2.0f);
                break;
            case Icon::zoomOut:
                p.addRectangle (a.getX(), a.getCentreY() - 1.0f, a.getWidth(), 2.0f);
                break;
            case Icon::close:
            {
                juce::Path x;
                x.startNewSubPath (a.getTopLeft());     x.lineTo (a.getBottomRight());
                x.startNewSubPath (a.getTopRight());    x.lineTo (a.getBottomLeft());
                g.strokePath (x, juce::PathStrokeType (1.6f));
                return;
            }
            case Icon::mixer:
            {
                // Tres faders verticales
                for (int i = 0; i < 3; ++i)
                {
                    const float x = a.getX() + a.getWidth() * (0.15f + 0.35f * i);
                    g.fillRect (x - 0.75f, a.getY(), 1.5f, a.getHeight());
                    const float ky = a.getY() + a.getHeight() * (i == 1 ? 0.3f : 0.65f);
                    g.fillRoundedRectangle (x - 3.0f, ky - 2.0f, 6.0f, 4.0f, 1.0f);
                }
                return;
            }
            case Icon::plugin:
            {
                // Clavija de enchufe
                g.fillRoundedRectangle (a.getX() + a.getWidth() * 0.15f, a.getY() + a.getHeight() * 0.35f,
                                        a.getWidth() * 0.7f, a.getHeight() * 0.45f, 2.0f);
                g.fillRect (a.getX() + a.getWidth() * 0.3f, a.getY(), 1.8f, a.getHeight() * 0.4f);
                g.fillRect (a.getX() + a.getWidth() * 0.62f, a.getY(), 1.8f, a.getHeight() * 0.4f);
                g.fillRect (a.getCentreX() - 0.9f, a.getY() + a.getHeight() * 0.75f, 1.8f, a.getHeight() * 0.25f);
                return;
            }
            case Icon::cycle:
            {
                // Dos flechas en círculo
                juce::Path p1, p2;
                const auto c = a.getCentre();
                const float rr = a.getWidth() * 0.45f;
                p1.addCentredArc (c.x, c.y, rr, rr * 0.75f, 0.0f, -2.6f, 0.2f, true);
                p2.addCentredArc (c.x, c.y, rr, rr * 0.75f, 0.0f, 0.55f, 3.35f, true);
                g.strokePath (p1, juce::PathStrokeType (1.7f));
                g.strokePath (p2, juce::PathStrokeType (1.7f));
                auto arrow = [&g] (juce::Point<float> tip, float dir)
                {
                    juce::Path ar;
                    ar.addTriangle (tip.x, tip.y - 3.5f, tip.x, tip.y + 3.5f, tip.x + 4.5f * dir, tip.y);
                    g.fillPath (ar);
                };
                arrow (c.getPointOnCircumference (rr, 0.2f).withY (c.y - rr * 0.55f), 1.0f);
                arrow (c.getPointOnCircumference (rr, 3.35f).withY (c.y + rr * 0.55f), -1.0f);
                return;
            }
            case Icon::metronome:
            {
                // Cuerpo trapezoidal con el péndulo inclinado
                juce::Path body;
                body.startNewSubPath (a.getX() + a.getWidth() * 0.3f, a.getY());
                body.lineTo (a.getX() + a.getWidth() * 0.7f, a.getY());
                body.lineTo (a.getRight(), a.getBottom());
                body.lineTo (a.getX(), a.getBottom());
                body.closeSubPath();
                g.strokePath (body, juce::PathStrokeType (1.6f));
                g.drawLine (a.getCentreX(), a.getBottom() - 2.0f, a.getRight() - 1.0f, a.getY() + 1.0f, 1.8f);
                g.fillEllipse (a.getX() + a.getWidth() * 0.62f, a.getY() + a.getHeight() * 0.3f, 4.0f, 4.0f);
                return;
            }
            case Icon::tuner:
            {
                // Diapasón
                const float cx = a.getCentreX();
                juce::Path fork;
                fork.startNewSubPath (cx - 3.5f, a.getY());
                fork.lineTo (cx - 3.5f, a.getY() + a.getHeight() * 0.55f);
                fork.quadraticTo (cx - 3.5f, a.getY() + a.getHeight() * 0.72f, cx, a.getY() + a.getHeight() * 0.72f);
                fork.quadraticTo (cx + 3.5f, a.getY() + a.getHeight() * 0.72f, cx + 3.5f, a.getY() + a.getHeight() * 0.55f);
                fork.lineTo (cx + 3.5f, a.getY());
                g.strokePath (fork, juce::PathStrokeType (1.8f));
                g.fillRect (cx - 0.9f, a.getY() + a.getHeight() * 0.72f, 1.8f, a.getHeight() * 0.28f);
                return;
            }
            case Icon::import:
            {
                // Flecha hacia abajo sobre una bandeja
                const float cx = a.getCentreX();
                g.fillRect (cx - 0.9f, a.getY(), 1.8f, a.getHeight() * 0.6f);
                juce::Path arrow;
                arrow.addTriangle (cx - 4.5f, a.getY() + a.getHeight() * 0.42f, cx + 4.5f, a.getY() + a.getHeight() * 0.42f,
                                   cx, a.getY() + a.getHeight() * 0.75f);
                g.fillPath (arrow);
                g.fillRect (a.getX(), a.getBottom() - 1.8f, a.getWidth(), 1.8f);
                g.fillRect (a.getX(), a.getBottom() - a.getHeight() * 0.3f, 1.8f, a.getHeight() * 0.3f);
                g.fillRect (a.getRight() - 1.8f, a.getBottom() - a.getHeight() * 0.3f, 1.8f, a.getHeight() * 0.3f);
                return;
            }
            case Icon::wave:
            {
                // Forma de onda
                static const float h[] = { 0.25f, 0.6f, 1.0f, 0.45f, 0.8f, 0.35f, 0.65f, 0.2f };
                const float step = a.getWidth() / 8.0f;
                for (int i = 0; i < 8; ++i)
                {
                    const float bh = a.getHeight() * h[i];
                    g.fillRoundedRectangle (a.getX() + i * step + step * 0.2f, a.getCentreY() - bh * 0.5f, step * 0.6f, bh, 0.8f);
                }
                return;
            }
            case Icon::settings:
            {
                // Tres barras con mandos: icono de "ajustes de audio"
                for (int i = 0; i < 3; ++i)
                {
                    const float y = a.getY() + a.getHeight() * (0.15f + 0.35f * i);
                    g.fillRect (a.getX(), y - 0.75f, a.getWidth(), 1.5f);
                    const float kx = a.getX() + a.getWidth() * (i == 1 ? 0.7f : 0.3f);
                    g.fillEllipse (kx - 2.5f, y - 2.5f, 5.0f, 5.0f);
                }
                return;
            }
        }
        g.fillPath (p);
    }

    Icon icon;
    juce::Colour activeColour;
    juce::String label;
};

// Pantalla central de la barra de control: posición en compases, tiempo, tempo y compás.
// El tempo se cambia arrastrando arriba/abajo (Mayús: décimas) o con doble clic para escribirlo.
// El compás se elige con un clic.
class LcdDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (double)> onTempoChanged;
    std::function<void (int, int)> onSignatureChanged;

    LcdDisplay()
    {
        tempoEditor.setJustification (juce::Justification::centred);
        tempoEditor.setInputRestrictions (6, "0123456789.,");
        tempoEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff23262b));
        tempoEditor.setColour (juce::TextEditor::outlineColourId, Theme::accent);
        tempoEditor.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        tempoEditor.onReturnKey = [this] { commitTempoText(); };
        tempoEditor.onFocusLost = [this] { commitTempoText(); };
        tempoEditor.onEscapeKey = [this] { tempoEditor.setVisible (false); };
        addChildComponent (tempoEditor);
    }

    void setValues (const juce::String& newPosition, const juce::String& newTime, double newBpm,
                    int num, int den, int newState, bool countIn)
    {
        if (newPosition == position && newTime == time && newBpm == bpm && num == sigNum && den == sigDen
             && newState == state && countIn == inCountIn)
            return;
        position = newPosition; time = newTime; bpm = newBpm; sigNum = num; sigDen = den;
        state = newState; inCountIn = countIn;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        Theme::fillInset (g, r, juce::Colour (0xff15171a), 6.0f);

        const auto mono = juce::Font::getDefaultMonospacedFontName();
        auto label = [&g] (juce::Rectangle<float> a, const juce::String& t, juce::Colour c)
        {
            g.setColour (c);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (t, a.removeFromTop (13.0f).withTrimmedTop (3.0f), juce::Justification::centredLeft);
        };
        auto separator = [&g, &r] (juce::Rectangle<int> a)
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawVerticalLine (a.getRight() + 4, r.getY() + 6.0f, r.getBottom() - 6.0f);
        };

        // Posición (compás . parte . semicorchea) y estado
        const juce::String stateText = inCountIn ? "PRECUENTA" : state == 2 ? "GRABANDO" : state == 1 ? "REPRODUCIENDO" : juce::String();
        const auto stateColour = state == 2 ? Theme::recordRed : Theme::playGreen;
        label (posArea.toFloat(), tr8 ("POSICIÓN"), Theme::textDim);
        if (stateText.isNotEmpty())
        {
            g.setColour (stateColour);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (stateText, posArea.toFloat().removeFromTop (13.0f).withTrimmedTop (3.0f), juce::Justification::centredRight);
        }
        g.setColour (juce::Colour (0xffe9edf2));
        g.setFont (juce::FontOptions (mono, 21.0f, juce::Font::plain));
        Theme::drawFixedDigits (g, position, posArea.toFloat().withTrimmedTop (12.0f));
        separator (posArea);

        label (timeArea.toFloat(), "TIEMPO", Theme::textDim);
        g.setColour (juce::Colour (0xffc9ced6));
        g.setFont (juce::FontOptions (mono, 14.0f, juce::Font::plain));
        Theme::drawFixedDigits (g, time, timeArea.toFloat().withTrimmedTop (12.0f));
        separator (timeArea);

        const bool overTempo = isMouseOver() && tempoArea.contains (getMouseXYRelative());
        label (tempoArea.toFloat(), "TEMPO", overTempo ? Theme::accent : Theme::textDim);
        g.setColour (juce::Colour (0xffe9edf2));
        g.setFont (juce::FontOptions (mono, 17.0f, juce::Font::plain));
        g.drawText (juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.05 ? 0 : 1),
                    tempoArea.toFloat().withTrimmedTop (12.0f), juce::Justification::centredLeft);
        separator (tempoArea);

        const bool overSig = isMouseOver() && sigArea.contains (getMouseXYRelative());
        label (sigArea.toFloat(), tr8 ("COMPÁS"), overSig ? Theme::accent : Theme::textDim);
        g.setColour (juce::Colour (0xffe9edf2));
        g.setFont (juce::FontOptions (mono, 17.0f, juce::Font::plain));
        g.drawText (juce::String (sigNum) + "/" + juce::String (sigDen), sigArea.toFloat().withTrimmedTop (12.0f),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 2);
        posArea = r.removeFromLeft (128);   r.removeFromLeft (10);
        timeArea = r.removeFromLeft (88);   r.removeFromLeft (10);
        tempoArea = r.removeFromLeft (60);  r.removeFromLeft (10);
        sigArea = r;
        tempoEditor.setBounds (tempoArea.withTrimmedTop (12).expanded (2, 0).withTrimmedBottom (2));
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (tempoArea.contains (e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor
                        : sigArea.contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                             : juce::MouseCursor::NormalCursor);
        setTooltip (tempoArea.contains (e.getPosition()) ? tr8 ("Tempo: arrastra arriba/abajo (Mayús: décimas) o doble clic para escribirlo")
                    : sigArea.contains (e.getPosition()) ? tr8 ("Compás: haz clic para elegirlo") : juce::String());
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragBpm = bpm;
        if (sigArea.contains (e.getPosition()))
            showSignatureMenu();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! tempoArea.contains (e.getMouseDownPosition()) || ! onTempoChanged)
            return;
        const bool fine = e.mods.isShiftDown();
        double v = dragBpm - e.getDistanceFromDragStartY() * (fine ? 0.1 : 0.5);
        v = fine ? std::round (v * 10.0) / 10.0 : std::round (v);
        onTempoChanged (juce::jlimit (20.0, 300.0, v));
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! tempoArea.contains (e.getPosition()))
            return;
        tempoEditor.setText (juce::String (bpm, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."), false);
        tempoEditor.setVisible (true);
        tempoEditor.grabKeyboardFocus();
        tempoEditor.selectAll();
    }

private:
    void commitTempoText()
    {
        if (! tempoEditor.isVisible())
            return;
        const auto v = tempoEditor.getText().replaceCharacter (',', '.').getDoubleValue();
        tempoEditor.setVisible (false);
        if (v >= 20.0 && v <= 300.0 && onTempoChanged)
            onTempoChanged (std::round (v * 10.0) / 10.0);
    }

    void showSignatureMenu()
    {
        static const int sigs[][2] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 4 }, { 7, 4 },
                                       { 3, 8 }, { 5, 8 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } };
        juce::PopupMenu m;
        m.addSectionHeader (tr8 ("Compás"));
        for (int i = 0; i < 12; ++i)
        {
            if (i == 6) m.addSeparator();
            m.addItem (i + 1, juce::String (sigs[i][0]) + "/" + juce::String (sigs[i][1]), true,
                       sigs[i][0] == sigNum && sigs[i][1] == sigDen);
        }
        juce::Component::SafePointer<LcdDisplay> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (sigArea)),
            [self] (int r)
            {
                if (self != nullptr && r > 0 && self->onSignatureChanged)
                    self->onSignatureChanged (sigs[r - 1][0], sigs[r - 1][1]);
            });
    }

    juce::String position, time;
    double bpm = 120.0, dragBpm = 120.0;
    int sigNum = 4, sigDen = 4, state = -1;
    bool inCountIn = false;
    juce::Rectangle<int> posArea, timeArea, tempoArea, sigArea;
    juce::TextEditor tempoEditor;
};
