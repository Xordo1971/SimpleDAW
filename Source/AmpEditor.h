#pragma once
// Frontal del ampli integrado ("Simpleamp"): la imagen del cabezal con
// potenciómetros que giran e interruptores que se mueven de verdad.
// Se incluye desde Effects.h (solo en la aplicación, no en las pruebas).
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "Dialogs.h"
#include "Presets.h"
#if SIMPLEDAW_HAS_BINARYDATA
 #include "BinaryData.h"
#endif

class AmpEditor : public juce::AudioProcessorEditor
{
public:
    // Imagen del cabezal (Resources/amp-panel.jpg, 1774 x 887, diseño propio del autor)
    static juce::Image loadPanel()
    {
       #if SIMPLEDAW_HAS_BINARYDATA
        int size = 0;
        if (auto* data = BinaryData::getNamedResource ("amppanel_jpg", size))
            return juce::ImageCache::getFromMemory (data, size);
       #endif
        return {};
    }

    static juce::Image loadCabinet (int index)   // 1 = 1x12, 2 = 2x12, 3 = 4x12
    {
       #if SIMPLEDAW_HAS_BINARYDATA
        static const char* names[] = { nullptr, "cab1x12s_jpg", "cab2x12s_jpg", "cab4x12s_jpg" };
        int size = 0;
        if (index >= 1 && index <= 3)
            if (auto* data = BinaryData::getNamedResource (names[index], size))
                return withoutWhiteBackground (juce::ImageCache::getFromMemory (data, size));
       #endif
        juce::ignoreUnused (index);
        return {};
    }

    // Las fotos de las pantallas tienen fondo blanco: se vuelve transparente (solo el blanco conectado
    // con el borde de la imagen, para no tocar el logotipo ni los detalles claros de dentro)
    static juce::Image withoutWhiteBackground (const juce::Image& source)
    {
        if (! source.isValid())
            return source;
        auto img = source.convertedToFormat (juce::Image::ARGB);
        img = img.createCopy();
        const int w = img.getWidth(), h = img.getHeight();
        juce::Image::BitmapData px (img, juce::Image::BitmapData::readWrite);
        auto isWhite = [&] (int x, int y) { auto c = px.getPixelColour (x, y); return c.getAlpha() > 0 && juce::jmin (c.getRed(), c.getGreen(), c.getBlue()) >= 232; };
        std::vector<char> clear ((size_t) (w * h), 0);
        std::vector<int> todo;
        auto push = [&] (int x, int y)
        {
            if (x < 0 || y < 0 || x >= w || y >= h || clear[(size_t) (y * w + x)] || ! isWhite (x, y)) return;
            clear[(size_t) (y * w + x)] = 1;
            todo.push_back (y * w + x);
        };
        for (int x = 0; x < w; ++x) { push (x, 0); push (x, h - 1); }
        for (int y = 0; y < h; ++y) { push (0, y); push (w - 1, y); }
        while (! todo.empty())
        {
            const int i = todo.back(); todo.pop_back();
            const int x = i % w, y = i / w;
            push (x + 1, y); push (x - 1, y); push (x, y + 1); push (x, y - 1);
        }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                if (clear[(size_t) (y * w + x)]) { px.setPixelColour (x, y, juce::Colours::transparentBlack); continue; }
                // Borde: los píxeles claros pegados al fondo quitado se suavizan para no dejar un halo blanco
                bool next = false;
                for (int dy = -1; dy <= 1 && ! next; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < w && ny < h && clear[(size_t) (ny * w + nx)]) { next = true; break; }
                    }
                if (next)
                {
                    auto c = px.getPixelColour (x, y);
                    const int light = juce::jmin (c.getRed(), c.getGreen(), c.getBlue());
                    px.setPixelColour (x, y, c.withAlpha (light > 150 ? 0.0f : light > 90 ? 0.5f : 1.0f));
                }
            }
        return img;
    }

    explicit AmpEditor (juce::AudioProcessor& p) : juce::AudioProcessorEditor (p), panel (loadPanel())
    {
        // Potenciómetros (centro medido en la imagen, 1774 x 887)
        struct K { const char* id; float x; };
        for (auto k : { K { "presence", 507.2f }, K { "bass", 635.7f }, K { "mid", 765.2f }, K { "treble", 894.5f },
                        K { "master", 1024.2f }, K { "gain", 1152.7f }, K { "cleanVol", 1281.8f },
                        K { "cleanTreble", 1410.5f }, K { "cleanBass", 1537.7f } })
            if (auto* param = find (k.id))
                knobs.add (new Knob (*this, *param, { k.x, knobY }));

        // Interruptores pequeños y selector de canal (limpio - crunch - lead)
        struct S { const char* id; float x, y; };
        for (auto sw : { S { "fat", 571.0f, 686.0f }, S { "c45", 699.5f, 686.0f }, S { "sat", 829.0f, 686.0f },
                         S { "voice", 1216.0f, 686.5f }, S { "bright", 1346.0f, 686.5f }, S { "channel", 1475.0f, 686.5f },
                         S { "power", 334.0f, 621.5f } })
            if (auto* param = find (sw.id))
                switches.add (new Switch (*this, *param, { sw.x, sw.y }, juce::String (sw.id) == "power"));

        // Abajo: presets, pantalla, puerta de ruido y el valor del control que se toca
        presetButton.setTooltip (tr8 ("Presets: cargar, guardar o importar ajustes (pueden incluir reverb, delay...)"));
        presetButton.onClick = [this] { showPresetMenu(); };
        addAndMakeVisible (presetButton);
        updatePresetName();

        if (auto* cab = find ("cabinet"))
        {
            cabinetBox.addItemList (cab->getAllValueStrings(), 1);
            cabinetAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*cab, cabinetBox);
        }
        cabinetBox.setTooltip (tr8 ("Pantalla (altavoz): simuladas, o «IR (archivo)» para usar una real"));
        // Al elegir «IR (archivo)» sin tener ninguna cargada, se pide el archivo
        cabinetBox.onChange = [this]
        {
            auto* amp = dynamic_cast<AmpEffect*> (&processor);
            if (amp != nullptr && cabinetBox.getSelectedItemIndex() == AmpEffect::irCabinetIndex && ! amp->hasIr())
                chooseIr();
        };
        addAndMakeVisible (cabinetBox);

        irButton.setButtonText ("IR...");
        irButton.setTooltip (tr8 ("Cargar una pantalla real: un archivo de respuesta al impulso (.wav, .aiff o .flac)"));
        irButton.onClick = [this] { chooseIr(); };
        addAndMakeVisible (irButton);
        updateIrTooltip();

        if (auto* gate = find ("gate"))
        {
            gateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            gateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
            gateAttachment = std::make_unique<juce::SliderParameterAttachment> (*gate, gateSlider);
            gateSlider.textFromValueFunction = [] (double v) { return v <= -89.0 ? juce::String ("Apagada") : juce::String (juce::roundToInt (v)) + " dB"; };
            gateSlider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
            gateSlider.updateText();
            gateSlider.setDoubleClickReturnValue (true, gate->convertFrom0to1 (gate->getDefaultValue()));
            gateSlider.setTooltip (tr8 ("Puerta de ruido: por debajo de este nivel corta el zumbido (todo a la izquierda: apagada)"));
        }
        addAndMakeVisible (gateSlider);

        // Panel de la derecha: la pantalla con el micro delante (se arrastra), el modelo de micro y su distancia
        pad = std::make_unique<CabinetPad> (*this);
        addAndMakeVisible (*pad);
        if (auto* m = find ("mic"))
        {
            micBox.addItemList (m->getAllValueStrings(), 1);
            micAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*m, micBox);
        }
        micBox.setTooltip (tr8 ("Micrófono delante de la pantalla (solo suena si hay pantalla)"));
        addAndMakeVisible (micBox);
        if (auto* d = find ("micDist"))
        {
            distSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            distSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            distAttachment = std::make_unique<juce::SliderParameterAttachment> (*d, distSlider);
            distSlider.setDoubleClickReturnValue (true, d->convertFrom0to1 (d->getDefaultValue()));
            distSlider.setTooltip (tr8 ("Distancia del micro a la pantalla: pegado (más graves, más directo) o lejos (más suave)"));
        }
        addAndMakeVisible (distSlider);
        for (auto* l : { &micLabel, &distLabel })
        {
            l->setFont (juce::FontOptions (10.5f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, juce::Colour (0xffa9adb4));
            addAndMakeVisible (l);
        }
        micLabel.setText ("MICRO", juce::dontSendNotification);
        distLabel.setText ("DISTANCIA", juce::dontSendNotification);

        for (auto* l : { &presetLabel, &cabinetLabel, &gateLabel })
        {
            l->setFont (juce::FontOptions (10.5f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, Theme::textDim);
            addAndMakeVisible (l);
        }
        presetLabel.setText ("PRESET", juce::dontSendNotification);
        cabinetLabel.setText ("PANTALLA", juce::dontSendNotification);
        gateLabel.setText ("PUERTA DE RUIDO", juce::dontSendNotification);

        status.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        status.setColour (juce::Label::textColourId, Theme::text);
        status.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (status);
        showStatus (nullptr);

        setResizable (true, true);
        // Tamaño inicial: el cabezal a escala 0,52 más el panel de pantalla y micro
        constexpr float s0 = 0.52f;
        const int w0 = juce::roundToInt (totalW * s0), h0 = juce::roundToInt (imgH * s0) + stripH;
        setResizeLimits (840, juce::roundToInt (840.0f * imgH / totalW) + stripH, (int) totalW, (int) imgH + stripH);
        getConstrainer()->setFixedAspectRatio ((double) w0 / (double) h0);
        setSize (w0, h0);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff141414));
        {
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (toScreen());
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImageAt (panel, 0, 0);

            for (auto* sw : switches) drawSwitch (g, *sw);
            for (auto* k : knobs)     drawKnob (g, *k);

            // Fondo del panel de pantalla y micro (a la derecha de la foto del cabezal)
            juce::Rectangle<float> side (imgW, 0.0f, sideW, imgH);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222326), side.getX(), 0, juce::Colour (0xff141415), side.getRight(), 0, false));
            g.fillRect (side);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRect (side.withWidth (3.0f));
            g.setColour (juce::Colour (0xffa9adb4));
            g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
            g.drawText (tr8 ("PANTALLA Y MICRO"), juce::Rectangle<float> (imgW + 35.0f, 28.0f, sideW - 70.0f, 36.0f), juce::Justification::centredLeft);
        }

        // Franja inferior
        auto strip = getLocalBounds().removeFromBottom (stripH).toFloat();
        g.setColour (Theme::panel);
        g.fillRect (strip);
        g.setColour (Theme::border);
        g.drawHorizontalLine ((int) strip.getY(), 0.0f, strip.getRight());
    }

    void resized() override
    {
        // Las piezas interactivas se colocan sobre la foto, en su sitio
        for (auto* k : knobs)
            k->setBounds (toScreen (juce::Rectangle<float> (k->centre.x - 36.0f, k->centre.y - 36.0f, 72.0f, 72.0f)));
        for (auto* sw : switches)
            sw->setBounds (toScreen (sw->area()));

        pad->setBounds (toScreen (juce::Rectangle<float> (imgW + 30.0f, 84.0f, sideW - 60.0f, 640.0f)));
        micLabel.setBounds  (toScreen (juce::Rectangle<float> (imgW + 35.0f, 752.0f, 200.0f, 28.0f)));
        micBox.setBounds    (toScreen (juce::Rectangle<float> (imgW + 35.0f, 784.0f, 270.0f, 52.0f)));
        distLabel.setBounds (toScreen (juce::Rectangle<float> (imgW + 335.0f, 752.0f, 200.0f, 28.0f)));
        distSlider.setBounds (toScreen (juce::Rectangle<float> (imgW + 335.0f, 784.0f, 250.0f, 52.0f)));

        auto strip = getLocalBounds().removeFromBottom (stripH).reduced (12, 8);
        presetLabel.setBounds (strip.removeFromLeft (52));
        presetButton.setBounds (strip.removeFromLeft (190).reduced (0, 1));
        strip.removeFromLeft (18);
        cabinetLabel.setBounds (strip.removeFromLeft (66));
        cabinetBox.setBounds (strip.removeFromLeft (108).reduced (0, 2));
        strip.removeFromLeft (4);
        irButton.setBounds (strip.removeFromLeft (40).reduced (0, 2));
        strip.removeFromLeft (14);
        gateLabel.setBounds (strip.removeFromLeft (108));
        gateSlider.setBounds (strip.removeFromLeft (170));
        status.setBounds (strip.withTrimmedLeft (8));
    }

private:
    void updateIrTooltip()
    {
        auto* amp = dynamic_cast<AmpEffect*> (&processor);
        irButton.setTooltip (amp != nullptr && amp->hasIr()
            ? tr8 ("IR cargada: ") + amp->getIrFile().getFileName() + tr8 (". Pulsa para cargar otra (.wav, .aiff o .flac)")
            : tr8 ("Cargar una pantalla real: un archivo de respuesta al impulso (.wav, .aiff o .flac)"));
    }

    void chooseIr()
    {
        auto* amp = dynamic_cast<AmpEffect*> (&processor);
        if (amp == nullptr)
            return;
        auto start = amp->hasIr() ? amp->getIrFile().getParentDirectory()
                                  : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        irChooser = std::make_unique<juce::FileChooser> (tr8 ("Cargar una pantalla (IR)"), start, "*.wav;*.aif;*.aiff;*.flac");
        juce::Component::SafePointer<AmpEditor> self (this);
        irChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [self, amp] (const juce::FileChooser& fc)
            {
                if (self == nullptr)
                    return;
                const auto file = fc.getResult();
                if (file == juce::File())
                {
                    // Cancelado: si se había elegido «IR (archivo)» sin tener una, se vuelve a la pantalla de antes
                    if (! amp->hasIr())
                        if (auto* cab = self->find ("cabinet"))
                            if (cab->convertFrom0to1 (cab->getValue()) > (float) AmpEffect::irCabinetIndex - 0.5f)
                                cab->setValueNotifyingHost (cab->convertTo0to1 (3.0f));
                    return;
                }
                if (! amp->loadIr (file))
                {
                    Dialogs::show (juce::MessageBoxIconType::WarningIcon, "Pantalla (IR)",
                                   tr8 ("No se pudo leer ese archivo. Usa un .wav, .aiff o .flac con la respuesta al impulso."));
                    return;
                }
                if (auto* cab = self->find ("cabinet"))
                    cab->setValueNotifyingHost (cab->convertTo0to1 ((float) AmpEffect::irCabinetIndex));
                self->updateIrTooltip();
            });
    }

    static constexpr float imgW = 1774.0f, imgH = 887.0f;
    static constexpr float knobY = 624.5f;   // altura del centro de los potenciómetros en la imagen
    static constexpr float sideW = 620.0f, totalW = imgW + sideW;   // el cabezal + el panel de pantalla y micro
    static constexpr int stripH = 44;

    juce::RangedAudioParameter* find (const juce::String& id) const
    {
        for (auto* p : processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (r->paramID == id)
                    return r;
        return nullptr;
    }

    float scale() const
    {
        return juce::jmin ((float) getWidth() / totalW, (float) (getHeight() - stripH) / imgH);
    }

    juce::AffineTransform toScreen() const
    {
        const float s = scale();
        return juce::AffineTransform::scale (s).translated (((float) getWidth() - totalW * s) * 0.5f, 0.0f);
    }

    juce::Rectangle<int> toScreen (juce::Rectangle<float> r) const
    {
        return r.transformedBy (toScreen()).getSmallestIntegerContainer();
    }

    // ---------------- Pantalla con el micro ----------------
    // Muestra la pantalla elegida (1x12, 2x12 o 4x12) y el micro delante. Se arrastra el micro sobre un
    // altavoz: del centro del cono (posición 0) al borde (posición 1). El ángulo solo es visual.
    class CabinetPad : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit CabinetPad (AmpEditor& e) : editor (e)
        {
            for (int i = 1; i <= 3; ++i)
                images[i] = loadCabinet (i);
            if (auto* p = e.find ("cabinet"))
                cabAttach = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { cabinet = juce::roundToInt (v); repaint(); });
            if (auto* p = e.find ("mic"))
                micAttach = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { mic = juce::roundToInt (v); repaint(); });
            if (auto* p = e.find ("micPos"))
                posAttach = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { pos = v; repaint(); });
            if (auto* p = e.find ("micDist"))
                distAttach = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { dist = v; repaint(); });
            for (auto* a : { cabAttach.get(), micAttach.get(), posAttach.get(), distAttach.get() })
                if (a != nullptr) a->sendInitialUpdate();
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip (tr8 ("Arrastra el micro sobre el altavoz: en el centro del cono suena más brillante; hacia el borde, más suave"));
        }

        void paint (juce::Graphics& g) override
        {
            const auto area = getLocalBounds().toFloat();
            if (cabinet < 1 || cabinet > 3 || ! images[cabinet].isValid())
            {
                g.setColour (juce::Colour (0xff7d8189));
                g.setFont (juce::FontOptions (juce::jmax (11.0f, area.getHeight() * 0.045f)));
                g.drawFittedText (cabinet >= 4 ? tr8 ("Pantalla por IR: el micro ya va incluido en la respuesta")
                                               : tr8 ("Sin pantalla: elige una abajo para usar el micro"),
                                  getLocalBounds().reduced (20), juce::Justification::centred, 3);
                return;
            }
            const auto& img = images[cabinet];
            const auto drawn = fitted (img);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (img, drawn, juce::RectanglePlacement::stretchToFit);

            // Sombra del micro y el micro (el cuerpo sale hacia fuera del cono, la punta apunta al centro)
            const auto& cone = cones (cabinet)[(size_t) juce::jlimit (0, numCones (cabinet) - 1, activeCone)];
            const float k = drawn.getWidth() / 900.0f;
            const auto centre = juce::Point<float> (drawn.getX() + cone.x * k, drawn.getY() + cone.y * k);
            const float radius = cone.r * k;
            const auto tip = centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * (pos * radius);
            drawMic (g, tip, angle, drawn.getWidth(), mic, dist);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (cabinet < 1 || cabinet > 3 || posAttach == nullptr) return;
            // Se elige el altavoz más cercano al pulsar
            const auto drawn = fitted (images[cabinet]);
            const float k = drawn.getWidth() / 900.0f;
            float best = 1.0e9f;
            for (int i = 0; i < numCones (cabinet); ++i)
            {
                const auto& c = cones (cabinet)[(size_t) i];
                const float d = e.position.getDistanceFrom ({ drawn.getX() + c.x * k, drawn.getY() + c.y * k });
                if (d < best) { best = d; activeCone = i; }
            }
            posAttach->beginGesture();
            drag (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
        void mouseUp (const juce::MouseEvent&) override     { if (posAttach != nullptr && dragging) posAttach->endGesture(); dragging = false; }
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (posAttach != nullptr)
                posAttach->setValueAsCompleteGesture (0.3f);   // posición inicial
        }

    private:
        struct Cone { float x, y, r; };   // en píxeles de la imagen reducida a 900 de ancho

        static int numCones (int cab) { return cab == 1 ? 1 : cab == 2 ? 2 : 4; }
        static const Cone* cones (int cab)
        {
            static const Cone one[]  = { { 448.0f, 465.0f, 205.0f } };
            static const Cone two[]  = { { 280.0f, 350.0f, 140.0f }, { 627.0f, 350.0f, 140.0f } };
            static const Cone four[] = { { 297.0f, 305.0f, 115.0f }, { 612.0f, 305.0f, 115.0f },
                                         { 297.0f, 570.0f, 115.0f }, { 612.0f, 570.0f, 115.0f } };
            return cab == 1 ? one : cab == 2 ? two : four;
        }

        juce::Rectangle<float> fitted (const juce::Image& img) const
        {
            return juce::RectanglePlacement (juce::RectanglePlacement::centred).appliedTo (
                juce::Rectangle<float> (0, 0, (float) img.getWidth(), (float) img.getHeight()), getLocalBounds().toFloat());
        }

        void drag (const juce::MouseEvent& e)
        {
            if (posAttach == nullptr || cabinet < 1 || cabinet > 3) return;
            dragging = true;
            const auto drawn = fitted (images[cabinet]);
            const float k = drawn.getWidth() / 900.0f;
            const auto& c = cones (cabinet)[(size_t) juce::jlimit (0, numCones (cabinet) - 1, activeCone)];
            const juce::Point<float> centre (drawn.getX() + c.x * k, drawn.getY() + c.y * k);
            const auto v = e.position - centre;
            const float d = v.getDistanceFromOrigin();
            if (d > 3.0f)
                angle = std::atan2 (v.y, v.x);
            posAttach->setValueAsPartOfGesture (juce::jlimit (0.0f, 1.0f, d / (c.r * k)));
        }

        // Micro dibujado con la punta en `tip` y el cuerpo hacia fuera (dirección `ang`).
        // Los de condensador son dorados, los dinámicos grises con rejilla plateada y el de cinta, negro.
        static void drawMic (juce::Graphics& g, juce::Point<float> tip, float ang, float cabW, int model, float distance)
        {
            const float s = cabW / 900.0f * 1.35f * (1.0f - 0.35f * distance);   // más lejos, más pequeño
            const bool condenser = model <= 1, ribbon = model == 6;
            const juce::Colour head = condenser ? juce::Colour (0xffd9c58c) : ribbon ? juce::Colour (0xffc9ced6) : juce::Colour (0xffc7cacf);
            const juce::Colour body = condenser ? juce::Colour (0xff8c7a47) : ribbon ? juce::Colour (0xff1a1b1d) : juce::Colour (0xff3a3c40);
            const float headL = 70.0f * s, headW = (condenser ? 38.0f : ribbon ? 26.0f : 34.0f) * s;
            const float bodyL = 150.0f * s, bodyW = 24.0f * s;

            juce::Graphics::ScopedSaveState state (g);
            // Sombra suave en el suelo del cono (más separada cuanto más lejos)
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (tip.x - headW * 0.5f + distance * 14.0f * s, tip.y - headW * 0.5f + distance * 14.0f * s, headW, headW);

            g.addTransform (juce::AffineTransform::rotation (ang).translated (tip));
            // Cuerpo
            juce::Rectangle<float> bodyR (headL * 0.9f, -bodyW * 0.5f, bodyL, bodyW);
            g.setGradientFill (juce::ColourGradient (body.brighter (0.4f), 0, -bodyW * 0.5f, body.darker (0.5f), 0, bodyW * 0.5f, false));
            g.fillRoundedRectangle (bodyR, bodyW * 0.3f);
            // Cabeza con rejilla
            juce::Rectangle<float> headR (0.0f, -headW * 0.5f, headL, headW);
            g.setGradientFill (juce::ColourGradient (head.brighter (0.5f), 0, -headW * 0.5f, head.darker (0.45f), 0, headW * 0.5f, false));
            g.fillRoundedRectangle (headR, headW * 0.35f);
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            for (float x = headR.getX() + 4.0f * s; x < headR.getRight(); x += 6.0f * s)
                g.drawLine (x, headR.getY() + 2.0f * s, x, headR.getBottom() - 2.0f * s, juce::jmax (0.6f, 1.0f * s));
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (headR, headW * 0.35f, juce::jmax (0.8f, 1.2f * s));
            g.drawRoundedRectangle (bodyR, bodyW * 0.3f, juce::jmax (0.8f, 1.2f * s));
            // Cable
            g.setColour (juce::Colour (0xff0d0d0e));
            g.drawLine (bodyR.getRight(), 0.0f, bodyR.getRight() + 60.0f * s, 0.0f, juce::jmax (1.5f, 4.0f * s));
        }

        AmpEditor& editor;
        juce::Image images[4];
        std::unique_ptr<juce::ParameterAttachment> cabAttach, micAttach, posAttach, distAttach;
        int cabinet = 0, mic = 3, activeCone = 0;
        float pos = 0.3f, dist = 0.2f, angle = -0.8f;   // ángulo del cuerpo del micro (solo visual)
        bool dragging = false;
    };

    // ---------------- Potenciómetro ----------------
    struct Knob : public juce::Slider
    {
        Knob (AmpEditor& e, juce::RangedAudioParameter& p, juce::Point<float> c) : editor (e), param (p), centre (c)
        {
            setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            setMouseDragSensitivity (220);
            setWantsKeyboardFocus (false);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            attachment = std::make_unique<juce::SliderParameterAttachment> (p, *this);
            setDoubleClickReturnValue (true, p.convertFrom0to1 (p.getDefaultValue()));
            onValueChange = [this]
            {
                editor.repaint (getBounds().expanded (2));
                if (isMouseOverOrDragging()) editor.showStatus (this);
            };
            e.addAndMakeVisible (this);
        }

        void paint (juce::Graphics&) override {}   // lo dibuja el frontal (la foto girada)
        void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); editor.showStatus (this); }
        void mouseExit (const juce::MouseEvent& e) override  { juce::Slider::mouseExit (e);  editor.showStatus (nullptr); }

        juce::String describe() const
        {
            const float v = param.convertFrom0to1 (param.getValue());
            return param.getName (40) + ":  " + juce::String (v, 1) + (param.getLabel().isNotEmpty() ? " " + param.getLabel() : juce::String());
        }

        AmpEditor& editor;
        juce::RangedAudioParameter& param;
        juce::Point<float> centre;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };

    void drawKnob (juce::Graphics& g, Knob& k)
    {
        // Se gira la imagen del propio potenciómetro de la foto (que apunta al 5).
        // Escala de 0 a 10 en 300 grados, igual que los números serigrafiados.
        const float norm = (float) k.valueToProportionOfLength (k.getValue());
        const float angle = (norm - 0.5f) * juce::degreesToRadians (300.0f);
        // El círculo girado es el propio mando (algo más pequeño que su borde), así que tapa el original
        constexpr float r = 32.5f;
        static const juce::Point<float> source { 507.2f, knobY - 0.3f };   // el de PRESENCE (apunta al 5)

        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addEllipse (k.centre.x - r, k.centre.y - r, r * 2.0f, r * 2.0f);
        g.reduceClipRegion (clip);
        g.drawImageTransformed (panel, juce::AffineTransform::translation (-source.x, -source.y)
                                           .rotated (angle).translated (k.centre.x, k.centre.y));
    }

    // ---------------- Interruptores ----------------
    struct Switch : public juce::Component
    {
        Switch (AmpEditor& e, juce::RangedAudioParameter& p, juce::Point<float> c, bool isPower)
            : editor (e), param (p), centre (c), power (isPower),
              attachment (p, [this] (float v) { value = juce::roundToInt (v); editor.repaint (getBounds().expanded (2)); editor.repaintLed(); })
        {
            numStates = p.getNumSteps() >= 3 && p.getNumSteps() < 10 ? p.getNumSteps() : 2;
            attachment.sendInitialUpdate();
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setRepaintsOnMouseActivity (false);
            e.addAndMakeVisible (this);
        }

        juce::Rectangle<float> area() const
        {
            // El de encendido: la tuerca y la palanca hacia abajo, centrado en la tuerca (se voltea arriba-abajo)
            return power ? juce::Rectangle<float> (centre.x - 27.0f, centre.y - 36.0f, 54.0f, 72.0f)
                         : juce::Rectangle<float> (centre.x - 16.0f, centre.y - 16.0f, 32.0f, 32.0f);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            int next;
            if (numStates == 2)
                next = value == 0 ? 1 : 0;
            else   // selector de 3 posiciones: lado izquierdo baja, lado derecho sube
                next = juce::jlimit (0, numStates - 1, value + (e.x < getWidth() / 2 ? -1 : 1));
            if (next != value)
                attachment.setValueAsCompleteGesture ((float) next);
            editor.showStatus (this);
        }

        void mouseEnter (const juce::MouseEvent&) override { editor.showStatus (this); }
        void mouseExit (const juce::MouseEvent&) override  { editor.showStatus (nullptr); }

        juce::String describe() const
        {
            if (numStates > 2)
                return tr8 ("Canal:  ") + juce::StringArray { "Limpio", "Crunch", "Lead" }[value];
            return param.getName (60) + ":  " + (value != 0 ? juce::String ("ON") : juce::String ("OFF"));
        }

        AmpEditor& editor;
        juce::RangedAudioParameter& param;
        juce::Point<float> centre;
        bool power;
        int value = 0, numStates = 2;
        juce::ParameterAttachment attachment;
        juce::Image normal, mirrored, flipped;   // trozos de la foto ya girados (se preparan la primera vez)
    };

    // Trozo de la foto como imagen independiente, opcionalmente en espejo (izquierda-derecha) o
    // volteado (arriba-abajo). En Windows (Direct2D), dibujar con transformación un trozo
    // recortado de otra imagen no siempre se veía: los interruptores no cambiaban de posición.
    juce::Image spriteFor (juce::Rectangle<int> ri, bool mirror, bool flip) const
    {
        juce::Image img (juce::Image::ARGB, ri.getWidth(), ri.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g (img);
        auto t = juce::AffineTransform::translation ((float) -ri.getX(), (float) -ri.getY());
        if (mirror) t = t.scaled (-1.0f, 1.0f).translated ((float) ri.getWidth(), 0.0f);
        if (flip)   t = t.scaled (1.0f, -1.0f).translated (0.0f, (float) ri.getHeight());
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImageTransformed (panel, t);
        return img;
    }

    void drawSwitch (juce::Graphics& g, Switch& sw)
    {
        const auto r = sw.area();
        const auto ri = r.toNearestInt();
        if (! sw.normal.isValid())
        {
            // Los pequeños usan todos el de FAT, que en la imagen tiene la palanca claramente a la derecha
            // (en otros, como BITE o SAT, apunta casi de frente y no se vería el cambio)
            const auto src = sw.power ? ri : juce::Rectangle<float> (571.0f - 16.0f, 686.0f - 16.0f, 32.0f, 32.0f).toNearestInt();
            sw.normal   = spriteFor (src, false, false);
            sw.mirrored = spriteFor (src, true, false);
            sw.flipped  = spriteFor (src, false, true);
        }

        if (sw.power)
        {
            // En la foto está hacia abajo y con el piloto encendido: así es "encendido"
            if (sw.value == 0)
            {
                g.drawImageAt (sw.flipped, ri.getX(), ri.getY());
                const juce::Rectangle<float> led (389.0f, 602.0f, 39.0f, 41.0f);
                g.setColour (juce::Colour (0xff1a0402).withAlpha (0.78f));
                g.fillRoundedRectangle (led, 2.0f);
                // ...y su reflejo rojo en la mesa
                const juce::Point<float> glow (409.0f, 862.0f);
                const juce::Colour wood (0xffb96825);
                g.setGradientFill (juce::ColourGradient (wood.withAlpha (0.92f), glow.x, glow.y,
                                                         wood.withAlpha (0.0f), glow.x + 30.0f, glow.y, true));
                g.fillEllipse (glow.x - 30.0f, glow.y - 40.0f, 60.0f, 80.0f);
            }
            return;
        }

        // Palanca hacia la derecha = activado / canal Lead
        const int right = sw.numStates - 1;
        if (sw.value == right)
        {
            g.drawImageAt (sw.normal, ri.getX(), ri.getY());
            return;
        }

        juce::Graphics::ScopedSaveState state (g);
        if (sw.value == 0)
        {
            g.drawImageAt (sw.mirrored, ri.getX(), ri.getY());
            return;
        }

        // Posición central: sin palanca a ningún lado, con la bola en el centro
        g.drawImageAt (sw.mirrored, ri.getX(), ri.getY());
        g.reduceClipRegion (ri.withTrimmedRight (ri.getWidth() / 2));
        g.drawImageAt (sw.normal, ri.getX(), ri.getY());
        g.restoreState(); g.saveState();
        const auto c = sw.centre.translated (0.5f, 0.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f2f2), c.x - 1.5f, c.y - 2.0f,
                                                 juce::Colour (0xff3c3c3c), c.x + 3.5f, c.y + 4.0f, true));
        g.fillEllipse (c.x - 5.5f, c.y - 5.5f, 11.0f, 11.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (c.x - 5.5f, c.y - 5.5f, 11.0f, 11.0f, 0.7f);
    }

    void repaintLed()
    {
        repaint (toScreen (juce::Rectangle<float> (330.0f, 494.0f, 44.0f, 44.0f)));
    }

    void showStatus (juce::Component* c)
    {
        if (auto* k = dynamic_cast<Knob*> (c))
            status.setText (k->describe(), juce::dontSendNotification);
        else if (auto* s = dynamic_cast<Switch*> (c))
            status.setText (s->describe(), juce::dontSendNotification);
        else
            status.setText (tr8 ("Doble clic: valor inicial"), juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, c != nullptr ? Theme::text : Theme::textDim);
    }

public:
    // Lo conecta la ventana principal para que el preset pueda cambiar los demás efectos de la pista.
    // Sin conectar, los presets solo afectan al ampli.
    std::function<int (const juce::XmlElement&)> applyPresetHook;                          // devuelve efectos que no cupieron, o -1
    std::function<std::unique_ptr<juce::XmlElement> (bool, const juce::String&)> capturePresetHook;
    std::function<bool()> hasOtherEffects;

private:
    void updatePresetName()
    {
        juce::String name;
        if (auto* a = dynamic_cast<AmpEffect*> (&processor))
            name = a->presetName;
        presetButton.setButtonText (name.isNotEmpty() ? name : tr8 ("(sin preset)"));
    }

    void applyPreset (const juce::XmlElement& preset)
    {
        if (! Presets::isValid (preset))
        {
            Dialogs::show (juce::MessageBoxIconType::WarningIcon, "Preset",
                tr8 ("Este archivo no es un preset válido de SimpleDAW."));
            return;
        }
        int skipped = 0;
        if (applyPresetHook)
            skipped = applyPresetHook (preset);
        else
        {
            Presets::readParams (processor, *Presets::ampElement (preset));
            if (auto* a = dynamic_cast<AmpEffect*> (&processor))
                a->presetName = preset.getStringAttribute ("name");
        }
        updatePresetName();
        repaint();
        if (skipped > 0)
            Dialogs::show (juce::MessageBoxIconType::InfoIcon, "Preset",
                juce::String (skipped) + tr8 (" efecto(s) del preset no se han añadido: no se reconocen."));
    }

    void showPresetMenu()
    {
        juce::PopupMenu m;
        const auto factory = Presets::factoryXml();
        juce::StringArray factoryNames;
        for (int i = 0; i < factory.size(); ++i)
        {
            auto x = Presets::factory (i);
            factoryNames.add (x != nullptr ? x->getStringAttribute ("name") : juce::String());
        }

        // Todos los presets propios en una lista (primero los sueltos, luego los de cada carpeta)
        juce::Array<juce::File> user = Presets::presetsIn (Presets::folder());
        const auto folders = Presets::userFolders();
        juce::Array<int> folderStart;
        for (auto& dir : folders)
        {
            folderStart.add (user.size());
            user.addArray (Presets::presetsIn (dir));
        }
        folderStart.add (user.size());

        // Favoritos: arriba del todo
        const auto favs = Presets::favorites();
        juce::Array<int> favTargets;   // >= 0: preset de fábrica; < 0: -(índice en user) - 1
        for (auto& name : favs)
        {
            const int f = factoryNames.indexOf (name);
            int u = -1;
            for (int i = 0; i < user.size() && u < 0; ++i)
                if (user[i].getFileNameWithoutExtension() == juce::File::createLegalFileName (name))
                    u = i;
            if (u >= 0)            { favTargets.add (-u - 1); m.addItem (20000 + favTargets.size() - 1, juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 ")) + name); }
            else if (f >= 0)       { favTargets.add (f);      m.addItem (20000 + favTargets.size() - 1, juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 ")) + name); }
        }
        if (! favTargets.isEmpty())
            m.addSeparator();

        juce::PopupMenu fm;
        for (int i = 0; i < factoryNames.size(); ++i)
            fm.addItem (100 + i, factoryNames[i]);
        m.addSubMenu (tr8 ("De fábrica"), fm);

        m.addSectionHeader ("Mis presets");
        if (user.isEmpty() && folders.isEmpty())
            m.addItem (-1, tr8 ("(todavía ninguno)"), false);
        for (int k = 0; k < folders.size(); ++k)
        {
            juce::PopupMenu sub;
            for (int i = folderStart[k]; i < folderStart[k + 1]; ++i)
                sub.addItem (1000 + i, user[i].getFileNameWithoutExtension());
            if (folderStart[k] == folderStart[k + 1])
                sub.addItem (-1, tr8 ("(vacía)"), false);
            m.addSubMenu (folders[k].getFileName(), sub);
        }
        for (int i = 0; i < (folders.isEmpty() ? user.size() : folderStart[0]); ++i)
            m.addItem (1000 + i, user[i].getFileNameWithoutExtension());

        m.addSeparator();
        juce::String current;
        if (auto* a = dynamic_cast<AmpEffect*> (&processor)) current = a->presetName;
        const bool currentIsFav = current.isNotEmpty() && favs.contains (current);
        m.addItem (7, current.isEmpty() ? tr8 ("Añadir a favoritos (primero carga o guarda un preset)")
                                        : currentIsFav ? tr8 ("Quitar «") + current + tr8 ("» de favoritos")
                                                       : tr8 ("Añadir «") + current + tr8 ("» a favoritos"),
                   current.isNotEmpty());
        m.addItem (1, tr8 ("Guardar preset..."));
        m.addItem (6, tr8 ("Exportar para compartir..."));
        m.addItem (2, tr8 ("Importar preset..."));
        m.addItem (8, tr8 ("Nueva carpeta..."));
        m.addItem (3, tr8 ("Abrir la carpeta de presets"));
        if (! user.isEmpty())
        {
            juce::PopupMenu del;
            for (int i = 0; i < user.size(); ++i)
            {
                const auto parent = user[i].getParentDirectory();
                del.addItem (5000 + i, (parent == Presets::folder() ? juce::String() : parent.getFileName() + " / ")
                                           + user[i].getFileNameWithoutExtension());
            }
            m.addSubMenu ("Borrar preset", del);
        }
        m.addSeparator();
        m.addItem (4, "Valores iniciales del ampli");

        juce::Component::SafePointer<AmpEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [self, user, favTargets, current, currentIsFav] (int r)
        {
            if (self == nullptr || r == 0) return;
            if (r >= 20000)
            {
                const int t = favTargets[r - 20000];
                if (t >= 0) { if (auto x = Presets::factory (t)) self->applyPreset (*x); }
                else if (auto x = Presets::load (user[-t - 1])) self->applyPreset (*x);
                else self->applyPreset (juce::XmlElement ("NADA"));
            }
            else if (r >= 100 && r < 1000) { if (auto x = Presets::factory (r - 100)) self->applyPreset (*x); }
            else if (r >= 1000 && r < 5000)
            {
                if (auto x = Presets::load (user[r - 1000])) self->applyPreset (*x);
                else self->applyPreset (juce::XmlElement ("NADA"));
            }
            else if (r >= 5000) self->deletePreset (user[r - 5000]);
            else if (r == 1) self->savePreset();
            else if (r == 2) self->importPreset();
            else if (r == 3) Presets::folder().startAsProcess();
            else if (r == 6) self->exportPreset();
            else if (r == 7) Presets::setFavorite (current, ! currentIsFav);
            else if (r == 8) self->newPresetFolder();
            else if (r == 4)
            {
                juce::XmlElement x (Presets::rootTag());
                x.createNewChildElement ("EFFECT")->setAttribute ("type", "Ampli");
                Presets::readParams (self->processor, *Presets::ampElement (x));   // solo el ampli
                if (auto* a = dynamic_cast<AmpEffect*> (&self->processor)) a->presetName.clear();
                self->updatePresetName();
            }
        });
    }

    void savePreset()
    {
        auto* aw = new Dialogs::Window ("Guardar preset", tr8 ("Nombre del preset:"), juce::MessageBoxIconType::NoIcon);
        juce::String current;
        if (auto* a = dynamic_cast<AmpEffect*> (&processor)) current = a->presetName;
        aw->addTextEditor ("name", current.isNotEmpty() ? current : juce::String ("Mi sonido"));

        // Carpeta donde guardarlo
        const auto folders = Presets::userFolders();
        auto* folderBox = new juce::ComboBox();
        folderBox->addItem (tr8 ("Carpeta: (ninguna, en «Mis presets»)"), 1);
        for (int i = 0; i < folders.size(); ++i)
            folderBox->addItem (tr8 ("Carpeta: ") + folders[i].getFileName(), i + 2);
        folderBox->setSelectedId (1, juce::dontSendNotification);
        folderBox->setSize (380, 26);
        aw->addCustomComponent (folderBox);

        auto* withFx = new juce::ToggleButton (tr8 ("Incluir los demás efectos de la pista (reverb, delay...)"));
        withFx->setSize (380, 24);
        withFx->setToggleState (hasOtherEffects && hasOtherEffects(), juce::dontSendNotification);
        withFx->setEnabled (capturePresetHook != nullptr);
        aw->addCustomComponent (withFx);
        aw->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<AmpEditor> self (this);
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([self, aw, withFx, folderBox, folders] (int res)
        {
            std::unique_ptr<juce::ToggleButton> owned (withFx);   // la ventana no se queda con ellos
            std::unique_ptr<juce::ComboBox> ownedBox (folderBox);
            if (self == nullptr || res != 1) return;
            const auto name = aw->getTextEditorContents ("name").trim();
            if (name.isEmpty()) return;
            const int idx = folderBox->getSelectedId() - 2;
            const auto dir = idx >= 0 && idx < folders.size() ? folders[idx] : Presets::folder();
            if (auto x = self->capturePreset (name, withFx->getToggleState()))
            {
                if (! x->writeTo (Presets::fileForName (name, dir)))
                {
                    Dialogs::show (juce::MessageBoxIconType::WarningIcon, "Preset", tr8 ("No se ha podido guardar el preset."));
                    return;
                }
                if (auto* a = dynamic_cast<AmpEffect*> (&self->processor)) a->presetName = name;
                self->updatePresetName();
            }
        }), true);
    }

    // Los ajustes actuales como preset (con o sin los demás efectos de la pista)
    std::unique_ptr<juce::XmlElement> capturePreset (const juce::String& name, bool includeEffects)
    {
        std::unique_ptr<juce::XmlElement> x;
        if (capturePresetHook)
            x = capturePresetHook (includeEffects, name);
        else
        {
            x = std::make_unique<juce::XmlElement> (Presets::rootTag());
            x->setAttribute ("name", name);
            auto* e = x->createNewChildElement ("EFFECT");
            e->setAttribute ("type", "Ampli");
            Presets::writeParams (processor, *e);
        }
        return x;
    }

    void writePreset (const juce::String& name, bool includeEffects)
    {
        auto x = capturePreset (name, includeEffects);
        const auto file = Presets::fileForName (name);
        if (x == nullptr || ! x->writeTo (file))
        {
            Dialogs::show (juce::MessageBoxIconType::WarningIcon, "Preset", tr8 ("No se ha podido guardar el preset."));
            return;
        }
        if (auto* a = dynamic_cast<AmpEffect*> (&processor)) a->presetName = name;
        updatePresetName();
    }

    // Guarda los ajustes actuales (ampli y demás efectos de la pista) en un archivo .sdpreset donde se quiera,
    // para mandárselo a otra persona (que lo abre con «Importar preset...»)
    void exportPreset()
    {
        juce::String name;
        if (auto* a = dynamic_cast<AmpEffect*> (&processor)) name = a->presetName;
        if (name.isEmpty())
            name = "Mi sonido";
        const auto start = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                               .getChildFile (juce::File::createLegalFileName (name) + Presets::extension());
        chooser = std::make_unique<juce::FileChooser> (tr8 ("Exportar preset para compartir"), start, "*" + Presets::extension());
        juce::Component::SafePointer<AmpEditor> self (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
            [self, name] (const juce::FileChooser& fc)
            {
                if (self == nullptr || fc.getResult() == juce::File()) return;
                auto file = fc.getResult().withFileExtension (Presets::extension());
                auto x = self->capturePreset (name, self->hasOtherEffects && self->hasOtherEffects());
                if (x == nullptr || ! x->writeTo (file))
                    Dialogs::show (juce::MessageBoxIconType::WarningIcon, "Preset", tr8 ("No se ha podido exportar el preset."));
                else
                    Dialogs::show (juce::MessageBoxIconType::InfoIcon, "Preset",
                                   tr8 ("Preset exportado en «") + file.getFullPathName()
                                   + tr8 ("». Quien lo reciba puede abrirlo con «Importar preset...» en el ampli."));
            });
    }

    void newPresetFolder()
    {
        auto* aw = new Dialogs::Window ("Nueva carpeta", tr8 ("Nombre de la carpeta de presets:"), juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", "Mis sonidos");
        aw->addButton ("Crear", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw] (int res)
        {
            if (res != 1) return;
            const auto name = juce::File::createLegalFileName (aw->getTextEditorContents ("name").trim());
            if (name.isNotEmpty())
                Presets::folder().getChildFile (name).createDirectory();
        }), true);
    }

    void importPreset()
    {
        chooser = std::make_unique<juce::FileChooser> ("Importar preset", juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"),
                                                       "*.sdpreset;*.xml");
        juce::Component::SafePointer<AmpEditor> self (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self] (const juce::FileChooser& fc)
        {
            if (self == nullptr || fc.getResult() == juce::File()) return;
            auto x = Presets::load (fc.getResult());
            if (x == nullptr)
            {
                self->applyPreset (juce::XmlElement ("NADA"));   // avisa de que no es válido
                return;
            }
            // Se copia a la carpeta de presets para tenerlo siempre en el menú
            auto dest = Presets::fileForName (x->getStringAttribute ("name"));
            if (dest != fc.getResult())
                dest = dest.getNonexistentSibling (false);
            x->writeTo (dest);
            self->applyPreset (*x);
        });
    }

    void deletePreset (const juce::File& f)
    {
        Dialogs::okCancel (juce::MessageBoxIconType::QuestionIcon, "Borrar preset",
            tr8 ("¿Borrar el preset \"") + f.getFileNameWithoutExtension() + "\"?", "Borrar", "Cancelar", nullptr,
            juce::ModalCallbackFunction::create ([f] (int r) { if (r == 1) f.moveToTrash(); }));
    }

    juce::Image panel;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Switch> switches;
    juce::ComboBox cabinetBox, micBox;
    juce::Slider gateSlider, distSlider;
    std::unique_ptr<CabinetPad> pad;
    std::unique_ptr<juce::ComboBoxParameterAttachment> micAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> distAttachment;
    juce::Label presetLabel, cabinetLabel, gateLabel, micLabel, distLabel, status;
    juce::TextButton presetButton, irButton;
    std::unique_ptr<juce::FileChooser> chooser, irChooser;
    std::unique_ptr<juce::ComboBoxParameterAttachment> cabinetAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> gateAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};

inline juce::AudioProcessorEditor* AmpEffect::createEditor()
{
    if (AmpEditor::loadPanel().isValid())
        return new AmpEditor (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}
