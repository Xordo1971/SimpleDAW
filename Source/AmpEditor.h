#pragma once
// Frontal del ampli integrado ("Simpleamp"): la foto del cabezal con
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
    static juce::Image loadPanel()
    {
       #if SIMPLEDAW_HAS_BINARYDATA
        int size = 0;
        if (auto* data = BinaryData::getNamedResource ("amppanel_jpg", size))
            return juce::ImageCache::getFromMemory (data, size);
       #endif
        return {};
    }

    explicit AmpEditor (juce::AudioProcessor& p) : juce::AudioProcessorEditor (p), panel (loadPanel())
    {
        // Potenciómetros (posición del centro en la foto original, 1537 x 649)
        struct K { const char* id; float x; };
        for (auto k : { K { "presence", 439.0f }, K { "bass", 551.5f }, K { "mid", 664.0f }, K { "treble", 776.5f },
                        K { "master", 889.5f }, K { "gain", 1001.5f }, K { "cleanVol", 1114.0f },
                        K { "cleanTreble", 1226.5f }, K { "cleanBass", 1337.5f } })
            if (auto* param = find (k.id))
                knobs.add (new Knob (*this, *param, { k.x, 514.0f }));

        // Interruptores pequeños y selector de canal (CLN - BE - HBE)
        struct S { const char* id; float x, y; };
        for (auto sw : { S { "fat", 495.5f, 568.5f }, S { "c45", 608.0f, 569.0f }, S { "sat", 720.5f, 569.0f },
                         S { "voice", 1055.0f, 569.0f }, S { "bright", 1167.0f, 568.5f }, S { "channel", 1279.5f, 569.0f },
                         S { "power", 292.0f, 516.0f } })
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
        cabinetBox.setTooltip ("Pantalla (altavoz) simulada");
        addAndMakeVisible (cabinetBox);

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
        setResizeLimits (700, (int) (700 * imgH / imgW) + stripH, 1537, (int) imgH + stripH);
        getConstrainer()->setFixedAspectRatio (922.0 / (389.0 + stripH));
        setSize (922, 389 + stripH);
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
            k->setBounds (toScreen (juce::Rectangle<float> (k->centre.x - 32.0f, k->centre.y - 32.0f, 64.0f, 64.0f)));
        for (auto* sw : switches)
            sw->setBounds (toScreen (sw->area()));

        auto strip = getLocalBounds().removeFromBottom (stripH).reduced (12, 8);
        presetLabel.setBounds (strip.removeFromLeft (52));
        presetButton.setBounds (strip.removeFromLeft (190).reduced (0, 1));
        strip.removeFromLeft (18);
        cabinetLabel.setBounds (strip.removeFromLeft (66));
        cabinetBox.setBounds (strip.removeFromLeft (108).reduced (0, 2));
        strip.removeFromLeft (18);
        gateLabel.setBounds (strip.removeFromLeft (108));
        gateSlider.setBounds (strip.removeFromLeft (170));
        status.setBounds (strip.withTrimmedLeft (8));
    }

private:
    static constexpr float imgW = 1537.0f, imgH = 649.0f;
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
        return juce::jmin ((float) getWidth() / imgW, (float) (getHeight() - stripH) / imgH);
    }

    juce::AffineTransform toScreen() const
    {
        const float s = scale();
        return juce::AffineTransform::scale (s).translated (((float) getWidth() - imgW * s) * 0.5f, 0.0f);
    }

    juce::Rectangle<int> toScreen (juce::Rectangle<float> r) const
    {
        return r.transformedBy (toScreen()).getSmallestIntegerContainer();
    }

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
        constexpr float r = 28.5f;
        static const juce::Point<float> source { 439.0f, 514.0f };   // el de PRESENCE

        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addEllipse (k.centre.x - r, k.centre.y - r, r * 2.0f, r * 2.0f);
        g.reduceClipRegion (clip);
        g.fillAll (juce::Colour (0xffe0c083));   // tapa el dibujo original del panel
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
            return power ? juce::Rectangle<float> (268.0f, 488.0f, 48.0f, 56.0f)
                         : juce::Rectangle<float> (centre.x - 15.0f, centre.y - 15.0f, 30.0f, 30.0f);
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
                return tr8 ("Canal:  ") + juce::StringArray { "CLN (limpio)", "BE (crunch)", "HBE (lead)" }[value];
            return param.getName (60) + ":  " + (value != 0 ? juce::String ("ON") : juce::String ("OFF"));
        }

        AmpEditor& editor;
        juce::RangedAudioParameter& param;
        juce::Point<float> centre;
        bool power;
        int value = 0, numStates = 2;
        juce::ParameterAttachment attachment;
    };

    void drawSwitch (juce::Graphics& g, Switch& sw)
    {
        const auto r = sw.area();
        const auto ri = r.toNearestInt();
        auto sprite = panel.getClippedImage (ri);

        if (sw.power)
        {
            // En la foto está hacia abajo y con el piloto encendido: así es "encendido"
            if (sw.value == 0)
            {
                g.drawImageTransformed (sprite, juce::AffineTransform::scale (1.0f, -1.0f).translated ((float) ri.getX(), (float) ri.getBottom()));
                const juce::Rectangle<float> led (336.0f, 499.0f, 32.0f, 33.0f);
                g.setColour (juce::Colour (0xff1a0402).withAlpha (0.78f));
                g.fillRoundedRectangle (led, 2.0f);
            }
            return;
        }

        // Palanca hacia la derecha (como en la foto) = activado / HBE
        const int right = sw.numStates - 1;
        if (sw.value == right)
            return;

        juce::Graphics::ScopedSaveState state (g);
        const auto mirror = juce::AffineTransform::scale (-1.0f, 1.0f).translated ((float) ri.getRight(), (float) ri.getY());
        if (sw.value == 0)
        {
            g.drawImageTransformed (sprite, mirror);
            return;
        }

        // Posición central: sin palanca a ningún lado, con la bola en el centro
        g.drawImageTransformed (sprite, mirror);
        g.reduceClipRegion (ri.withTrimmedRight (ri.getWidth() / 2));
        g.drawImageAt (sprite, ri.getX(), ri.getY());
        g.restoreState(); g.saveState();
        const auto c = sw.centre.translated (0.5f, 0.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f2f2), c.x - 1.5f, c.y - 2.0f,
                                                 juce::Colour (0xff3c3c3c), c.x + 3.5f, c.y + 4.0f, true));
        g.fillEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f, 0.6f);
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
        juce::PopupMenu fm;
        for (int i = 0; i < factory.size(); ++i)
            if (auto x = Presets::factory (i))
                fm.addItem (100 + i, x->getStringAttribute ("name"));
        m.addSubMenu (tr8 ("De fábrica"), fm);

        const auto user = Presets::userPresets();
        m.addSectionHeader ("Mis presets");
        if (user.isEmpty())
            m.addItem (-1, tr8 ("(todavía ninguno)"), false);
        for (int i = 0; i < user.size(); ++i)
            m.addItem (1000 + i, user[i].getFileNameWithoutExtension());

        m.addSeparator();
        m.addItem (1, tr8 ("Guardar preset..."));
        m.addItem (2, tr8 ("Importar preset..."));
        m.addItem (3, tr8 ("Abrir la carpeta de presets"));
        if (! user.isEmpty())
        {
            juce::PopupMenu del;
            for (int i = 0; i < user.size(); ++i)
                del.addItem (5000 + i, user[i].getFileNameWithoutExtension());
            m.addSubMenu ("Borrar preset", del);
        }
        m.addSeparator();
        m.addItem (4, "Valores iniciales del ampli");

        juce::Component::SafePointer<AmpEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [self, user] (int r)
        {
            if (self == nullptr || r == 0) return;
            if (r >= 100 && r < 1000) { if (auto x = Presets::factory (r - 100)) self->applyPreset (*x); }
            else if (r >= 1000 && r < 5000)
            {
                if (auto x = Presets::load (user[r - 1000])) self->applyPreset (*x);
                else self->applyPreset (juce::XmlElement ("NADA"));
            }
            else if (r >= 5000) self->deletePreset (user[r - 5000]);
            else if (r == 1) self->savePreset();
            else if (r == 2) self->importPreset();
            else if (r == 3) Presets::folder().startAsProcess();
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
        auto* withFx = new juce::ToggleButton (tr8 ("Incluir los demás efectos de la pista (reverb, delay...)"));
        withFx->setSize (380, 24);
        withFx->setToggleState (hasOtherEffects && hasOtherEffects(), juce::dontSendNotification);
        withFx->setEnabled (capturePresetHook != nullptr);
        aw->addCustomComponent (withFx);
        aw->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<AmpEditor> self (this);
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([self, aw, withFx] (int res)
        {
            std::unique_ptr<juce::ToggleButton> owned (withFx);   // la ventana no se queda con él
            if (self == nullptr || res != 1) return;
            const auto name = aw->getTextEditorContents ("name").trim();
            if (name.isEmpty()) return;
            self->writePreset (name, withFx->getToggleState());
        }), true);
    }

    void writePreset (const juce::String& name, bool includeEffects)
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
        const auto file = Presets::fileForName (name);
        if (x == nullptr || ! x->writeTo (file))
        {
            Dialogs::show (juce::MessageBoxIconType::WarningIcon, "Preset", tr8 ("No se ha podido guardar el preset."));
            return;
        }
        if (auto* a = dynamic_cast<AmpEffect*> (&processor)) a->presetName = name;
        updatePresetName();
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
    juce::ComboBox cabinetBox;
    juce::Slider gateSlider;
    juce::Label presetLabel, cabinetLabel, gateLabel, status;
    juce::TextButton presetButton;
    std::unique_ptr<juce::FileChooser> chooser;
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
