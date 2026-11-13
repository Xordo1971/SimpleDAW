#pragma once
#include "Text.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include <algorithm>
#include "Theme.h"
#include "Dialogs.h"

// Zoom y desplazamiento horizontal, compartidos por la regla y todas las pistas
struct Timeline
{
    static constexpr int headerWidth = 380;
    double pixelsPerSecond = 40.0;
    double viewStart = 0.0;

    float secondsToX (double seconds, int laneLeft) const
    {
        return (float) (laneLeft + (seconds - viewStart) * pixelsPerSecond);
    }

    double xToSeconds (float x, int laneLeft) const
    {
        return viewStart + (x - (float) laneLeft) / pixelsPerSecond;
    }

    // Marcadores ("Intro", "Estribillo"...): posición en muestras
    struct Marker { juce::int64 pos; juce::String name; };
    std::vector<Marker> markers;

    static juce::Colour markerColour (int index)
    {
        static const juce::uint32 colours[] = { 0xffffc801, 0xff6cc4ff, 0xffff7aa8, 0xff7ee08a, 0xffc39bff, 0xffffa25c };
        return juce::Colour (colours[index % 6]);
    }

    void sortMarkers() { std::sort (markers.begin(), markers.end(), [] (const Marker& a, const Marker& b) { return a.pos < b.pos; }); }

    // Tempo y compás (copia de la interfaz; el motor tiene los suyos)
    double bpm = 120.0;
    int sigNum = 4, sigDen = 4;

    double beatSeconds() const { return 60.0 / bpm * 4.0 / sigDen; }
    double barSeconds() const  { return beatSeconds() * sigNum; }

    // Cada cuántos compases se numera la regla, según el zoom
    int barLabelStep() const
    {
        const double barPx = barSeconds() * pixelsPerSecond;
        for (int n : { 1, 2, 4, 8, 16, 32, 64 })
            if (n * barPx >= 42.0)
                return n;
        return 128;
    }

    // Dibuja las líneas de compás y de parte en un área (regla o carril de pista)
    template <typename LineFn>
    void forEachGridLine (int widthPx, LineFn&& fn) const
    {
        const double beat = beatSeconds(), bar = barSeconds();
        const double beatPx = beat * pixelsPerSecond, barPx = bar * pixelsPerSecond;
        const bool showBeats = beatPx >= 7.0;
        const int barStep = barPx >= 6.0 ? 1 : barLabelStep();

        const double endSec = viewStart + widthPx / pixelsPerSecond;
        const auto firstBar = (juce::int64) std::floor (viewStart / bar);
        for (auto b = juce::jmax ((juce::int64) 0, firstBar); b * bar <= endSec; ++b)
        {
            if (b % barStep == 0)
                fn (b * bar, (int) b, 0);                   // línea de compás
            if (showBeats)
                for (int k = 1; k < sigNum; ++k)
                    fn (b * bar + k * beat, (int) b, k);    // línea de parte
        }
    }
};

inline juce::String formatTime (double seconds)
{
    const auto totalMs = (juce::int64) std::llround (juce::jmax (0.0, seconds) * 1000.0);
    return juce::String::formatted ("%02d:%02d.%03d",
                                    (int) (totalMs / 60000),
                                    (int) ((totalMs / 1000) % 60),
                                    (int) (totalMs % 1000));
}

// Regla de tiempo. Clic o arrastre para mover el cursor de reproducción.
// Regla: arriba la franja del ciclo (arrastrar para crearlo o moverlo, bordes para ajustarlo,
// doble clic para activarlo); abajo los compases (clic o arrastre para mover el cursor).
class Ruler : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int bandHeight = 13, markerRowHeight = 15;
    std::function<void()> onLoopChanged;
    std::function<void()> onMarkersChanged;

    Ruler (AudioEngine& e, Timeline& t) : engine (e), timeline (t)
    {
        setTooltip (tr8 ("Arriba: arrastra para marcar el ciclo (doble clic: activar, clic derecho: opciones). "
                         "Centro: marcadores (doble clic: añadir; clic derecho: renombrar o borrar). Abajo: mueve el cursor."));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (Theme::panelLight, 0, 0, Theme::panel, 0, r.getBottom(), false));
        g.fillRect (r);
        g.setColour (Theme::border);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, r.getRight());

        // Franja del ciclo
        auto band = r.removeFromTop ((float) bandHeight);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (band);
        if (engine.hasValidLoop())
        {
            const float x0 = timeline.secondsToX (toSeconds (engine.loopStart.load()), 0);
            const float x1 = timeline.secondsToX (toSeconds (engine.loopEnd.load()), 0);
            const bool punch = engine.punchOn.load(), on = engine.loopOn.load() || punch;
            const auto colour = punch ? Theme::recordRed : juce::Colour (0xffffc801);
            auto loopRect = juce::Rectangle<float> (x0, band.getY() + 1.0f, x1 - x0, band.getHeight() - 2.0f);
            g.setColour (on ? colour.withAlpha (0.85f) : Theme::textDim.withAlpha (0.35f));
            g.fillRoundedRectangle (loopRect, 3.0f);
            if (on)
            {
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                g.drawText (punch ? "PUNCH" : "CICLO", loopRect.reduced (5.0f, 0.0f), juce::Justification::centredLeft, false);
            }
        }

        // Marcadores
        auto markerRow = r.removeFromTop ((float) markerRowHeight);
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRect (markerRow);
        const double sr = rate();
        for (size_t i = 0; i < timeline.markers.size(); ++i)
        {
            const auto& m = timeline.markers[i];
            const float x = timeline.secondsToX ((double) m.pos / sr, 0);
            const float nextX = i + 1 < timeline.markers.size() ? timeline.secondsToX ((double) timeline.markers[i + 1].pos / sr, 0)
                                                                 : (float) getWidth() + 1000.0f;
            if (nextX < 0 || x > (float) getWidth())
                continue;
            const auto c = Timeline::markerColour ((int) i);
            const float w = juce::jmin (nextX - x - 2.0f, 160.0f);
            g.setColour (c.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (x, markerRow.getY() + 1.0f, 2.0f, markerRow.getHeight() - 1.0f));
            if (w > 14.0f)
            {
                auto flag = juce::Rectangle<float> (x, markerRow.getY() + 1.0f, w, markerRow.getHeight() - 2.0f);
                g.setColour (c.withAlpha (draggingMarker == (int) i ? 0.6f : 0.35f));
                g.fillRect (flag);
                g.setColour (Theme::strongText);
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText (m.name, flag.reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
            }
        }

        const int labelStep = timeline.barLabelStep();
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        const float top = (float) (bandHeight + markerRowHeight), h = (float) getHeight() - top;

        timeline.forEachGridLine (getWidth(), [&] (double sec, int bar, int beatInBar)
        {
            const int x = (int) timeline.secondsToX (sec, 0);
            if (beatInBar == 0)
            {
                g.setColour (Theme::text.withAlpha (0.6f));
                g.drawVerticalLine (x, top + h * 0.4f, (float) getHeight() - 1);
                if (bar % labelStep == 0)
                {
                    g.setColour (Theme::text);
                    g.drawText (juce::String (bar + 1), x + 4, (int) top, 50, (int) (h * 0.6f), juce::Justification::centredLeft);
                }
            }
            else
            {
                g.setColour (Theme::textDim.withAlpha (0.45f));
                g.drawVerticalLine (x, top + h * 0.72f, (float) getHeight() - 1);
            }
        });

        // Cabeza del cursor de reproducción
        const float px = timeline.secondsToX (engine.getPositionSeconds(), 0);
        juce::Path head;
        head.addTriangle (px - 6.0f, (float) getHeight() - 9.0f, px + 6.0f, (float) getHeight() - 9.0f, px, (float) getHeight());
        g.setColour (Theme::playhead);
        g.fillPath (head);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto mode = e.y < bandHeight ? hitLoop (e.x) : DragMode::none;
        setMouseCursor (mode == DragMode::startEdge || mode == DragMode::endEdge ? juce::MouseCursor::LeftRightResizeCursor
                        : mode == DragMode::move ? juce::MouseCursor::DraggingHandCursor
                                                 : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragMode = DragMode::none;
        draggingMarker = -1;

        if (isInMarkerRow (e.y))
        {
            const int hit = markerAt (e.x);
            if (hit >= 0 && e.mods.isPopupMenu())
                showMarkerMenu (hit);
            else if (hit >= 0)
            {
                draggingMarker = hit;   // clic: ir al marcador; arrastrar: moverlo
                markerDragStart = timeline.markers[(size_t) hit].pos;
                if (! engine.isRecording())
                    engine.setPosition (markerDragStart);
                repaint();
            }
            return;
        }

        if (e.y >= bandHeight)
        {
            moveTo (e.x);
            return;
        }

        if (e.mods.isPopupMenu())
        {
            showLoopMenu();
            return;
        }

        dragMode = hitLoop (e.x);
        dragStartLoop = { engine.loopStart.load(), engine.loopEnd.load() };
        if (dragMode == DragMode::none)
        {
            dragMode = DragMode::create;
            dragAnchor = snap (e.x, e.mods);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingMarker >= 0 && draggingMarker < (int) timeline.markers.size())
        {
            if (e.getDistanceFromDragStartX() != 0)
            {
                timeline.markers[(size_t) draggingMarker].pos = snap (e.x, e.mods);
                markersChanged (false);
            }
            return;
        }

        if (dragMode == DragMode::none)
        {
            if (e.getMouseDownY() >= bandHeight + markerRowHeight)
                moveTo (e.x);
            return;
        }

        const auto s = snap (e.x, e.mods);
        const double sr = rate();
        auto start = dragStartLoop.first, end = dragStartLoop.second;

        switch (dragMode)
        {
            case DragMode::create:
                start = juce::jmin (dragAnchor, s);
                end = juce::jmax (dragAnchor, s);
                break;
            case DragMode::startEdge: start = juce::jmin (s, end - 1); break;
            case DragMode::endEdge:   end = juce::jmax (s, start + 1); break;
            case DragMode::move:
            {
                const auto delta = snapDelta ((juce::int64) ((e.x - e.getMouseDownX()) / timeline.pixelsPerSecond * sr), e.mods);
                const auto len = end - start;
                start = juce::jmax ((juce::int64) 0, dragStartLoop.first + delta);
                end = start + len;
                break;
            }
            default: break;
        }

        engine.loopStart = juce::jmax ((juce::int64) 0, start);
        engine.loopEnd = juce::jmax (engine.loopStart.load(), end);
        if (dragMode == DragMode::create && engine.hasValidLoop() && ! engine.punchOn.load())
            engine.loopOn = true;   // al dibujar un ciclo nuevo, se activa
        changed();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingMarker >= 0)
        {
            draggingMarker = -1;
            markersChanged (true);
        }
    }

    // Añade un marcador (posición en muestras) y devuelve su índice
    int addMarker (juce::int64 pos)
    {
        timeline.markers.push_back ({ pos, "Marcador " + juce::String ((int) timeline.markers.size() + 1) });
        markersChanged (true);
        for (size_t i = 0; i < timeline.markers.size(); ++i)
            if (timeline.markers[i].pos == pos)
                return (int) i;
        return -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (isInMarkerRow (e.y))
        {
            const int hit = markerAt (e.x);
            if (hit >= 0)
                showMarkerMenu (hit);
            else
                addMarker (snap (e.x, e.mods));
            return;
        }

        if (e.y < bandHeight && engine.hasValidLoop())
        {
            engine.loopOn = ! engine.loopOn.load();
            changed();
        }
    }

private:
    enum class DragMode { none, create, startEdge, endEdge, move };

    bool isInMarkerRow (int y) const { return y >= bandHeight && y < bandHeight + markerRowHeight; }

    int markerAt (int x) const
    {
        // El último marcador que empieza a la izquierda del clic (su etiqueta llega hasta el siguiente)
        int hit = -1;
        for (size_t i = 0; i < timeline.markers.size(); ++i)
        {
            const float mx = timeline.secondsToX ((double) timeline.markers[i].pos / rate(), 0);
            if ((float) x >= mx - 3.0f && (float) x <= mx + 160.0f)
                hit = (int) i;
        }
        return hit;
    }

    void markersChanged (bool resort)
    {
        if (resort)
            timeline.sortMarkers();
        repaint();
        if (onMarkersChanged) onMarkersChanged();
    }

    void showMarkerMenu (int index)
    {
        static const char* presets[] = { "Intro", "Estrofa", "Pre-estribillo", "Estribillo", "Puente", "Solo", "Final" };
        juce::PopupMenu names;
        for (int i = 0; i < 7; ++i)
            names.addItem (10 + i, tr8 (presets[i]));
        names.addSeparator();
        names.addItem (2, "Otro nombre...");

        juce::PopupMenu m;
        m.addSectionHeader (timeline.markers[(size_t) index].name);
        m.addSubMenu ("Renombrar", names);
        m.addItem (3, "Borrar marcador");

        juce::Component::SafePointer<Ruler> self (this);
        const auto pos = timeline.markers[(size_t) index].pos;
        m.showMenuAsync (juce::PopupMenu::Options(), [self, pos] (int r)
        {
            if (self == nullptr || r == 0)
                return;
            auto& markers = self->timeline.markers;
            auto it = std::find_if (markers.begin(), markers.end(), [pos] (const Timeline::Marker& mk) { return mk.pos == pos; });
            if (it == markers.end())
                return;

            if (r >= 10)
            {
                it->name = tr8 (presets[r - 10]);
                self->markersChanged (false);
            }
            else if (r == 3)
            {
                markers.erase (it);
                self->markersChanged (false);
            }
            else if (r == 2)
            {
                auto* aw = new Dialogs::Window ("Nombre del marcador", {}, juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor ("name", it->name);
                aw->addButton ("Aceptar", 1, juce::KeyPress (juce::KeyPress::returnKey));
                aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                aw->enterModalState (true, juce::ModalCallbackFunction::create ([self, pos, aw] (int res)
                {
                    if (self == nullptr || res != 1)
                        return;
                    const auto text = aw->getTextEditorContents ("name").trim();
                    for (auto& mk : self->timeline.markers)
                        if (mk.pos == pos && text.isNotEmpty())
                            mk.name = text;
                    self->markersChanged (false);
                }), true);
            }
        });
    }

    double rate() const { return engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0; }
    double toSeconds (juce::int64 samples) const { return (double) samples / rate(); }

    DragMode hitLoop (int x) const
    {
        if (! engine.hasValidLoop())
            return DragMode::none;
        const float x0 = timeline.secondsToX (toSeconds (engine.loopStart.load()), 0);
        const float x1 = timeline.secondsToX (toSeconds (engine.loopEnd.load()), 0);
        if (std::abs ((float) x - x0) <= 5.0f) return DragMode::startEdge;
        if (std::abs ((float) x - x1) <= 5.0f) return DragMode::endEdge;
        if ((float) x > x0 && (float) x < x1)  return DragMode::move;
        return DragMode::none;
    }

    // Ajuste a la parte más cercana del compás (Alt: sin ajuste)
    juce::int64 snap (int x, juce::ModifierKeys mods) const
    {
        double sec = juce::jmax (0.0, timeline.xToSeconds ((float) x, 0));
        if (! mods.isAltDown())
            sec = std::round (sec / timeline.beatSeconds()) * timeline.beatSeconds();
        return (juce::int64) std::llround (sec * rate());
    }

    juce::int64 snapDelta (juce::int64 delta, juce::ModifierKeys mods) const
    {
        if (mods.isAltDown())
            return delta;
        const double beat = timeline.beatSeconds() * rate();
        return (juce::int64) std::llround (std::round ((double) delta / beat) * beat);
    }

    void showLoopMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Ciclo", engine.hasValidLoop(), engine.loopOn.load());
        m.addItem (2, tr8 ("Pinchado automático (punch in/out)"), engine.hasValidLoop(), engine.punchOn.load());
        m.addSeparator();
        m.addItem (3, tr8 ("Quitar el rango"), engine.hasValidLoop());
        juce::Component::SafePointer<Ruler> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self] (int r)
        {
            if (self == nullptr || r == 0) return;
            auto& en = self->engine;
            if (r == 1) en.loopOn = ! en.loopOn.load();
            if (r == 2) en.punchOn = ! en.punchOn.load();
            if (r == 3) { en.loopOn = false; en.punchOn = false; en.loopStart = 0; en.loopEnd = 0; }
            self->changed();
        });
    }

    void changed()
    {
        repaint();
        if (onLoopChanged) onLoopChanged();
    }

    void moveTo (int x)
    {
        if (engine.isRecording())
            return;
        const double seconds = juce::jmax (0.0, timeline.xToSeconds ((float) x, 0));
        engine.setPosition ((juce::int64) (seconds * engine.getSampleRate()));
        repaint();
    }

    AudioEngine& engine;
    Timeline& timeline;
    DragMode dragMode = DragMode::none;
    int draggingMarker = -1;
    juce::int64 markerDragStart = 0;
    std::pair<juce::int64, juce::int64> dragStartLoop;
    juce::int64 dragAnchor = 0;
};
