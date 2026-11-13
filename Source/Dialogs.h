#pragma once
#include "Theme.h"

// Ventanas emergentes de SimpleDAW: marco gris oscuro, icono y título grandes arriba, y el mensaje
// dentro de una caja más clara que muestra deslizadores si el texto no cabe.
// Se usan igual que juce::AlertWindow (addButton, addTextEditor, addCustomComponent, enterModalState).
namespace Dialogs
{
    using Icon = juce::MessageBoxIconType;

    // Icono redondo amarillo: "!" aviso, "i" información, "?" pregunta
    inline void drawIcon (juce::Graphics& g, juce::Rectangle<float> r, Icon type)
    {
        if (type == Icon::NoIcon)
            return;
        g.setColour (juce::Colour (0xfff5b301));
        g.fillEllipse (r);
        g.setColour (juce::Colour (0xff1c1d20));
        g.setFont (juce::FontOptions (r.getHeight() * 0.7f, juce::Font::bold));
        g.drawText (type == Icon::WarningIcon ? "!" : type == Icon::InfoIcon ? "i" : "?", r.translated (0.0f, 1.0f),
                    juce::Justification::centred);
    }

    // Caja interior clara, con título opcional (p. ej. "CHANGELOG") y texto desplazable
    // Las listas del CHANGELOG ("- algo") se ven con viñetas
    inline juce::String bullets (const juce::String& text)
    {
        auto t = "\n" + text;
        t = t.replace ("\n- ", juce::String ("\n") + juce::String::charToString ((juce::juce_wchar) 0x2022) + " ");
        return t.substring (1);
    }

    class MessageBox : public juce::Component
    {
    public:
        static constexpr int pad = 22, maxTextHeight = 330;

        MessageBox (const juce::String& headingText, const juce::String& text, int width) : heading (headingText)
        {
            const juce::Font font (juce::FontOptions (14.5f));
            editor.setMultiLine (true, true);
            editor.setReadOnly (true);
            editor.setCaretVisible (false);
            editor.setScrollbarsShown (true);
            editor.setWantsKeyboardFocus (false);
            editor.setEscapeAndReturnKeysConsumed (false);
            editor.setPopupMenuEnabled (false);
            editor.setBorder (juce::BorderSize<int> (0));
            editor.setIndents (0, 0);
            editor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
            editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
            editor.setColour (juce::TextEditor::textColourId, Theme::dialogText);
            editor.setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.4f));
            editor.setFont (font);
            editor.setText (bullets (text), false);
            addAndMakeVisible (editor);

            // Alto justo para el texto, con un máximo: si no cabe, aparecen los deslizadores
            juce::AttributedString s;
            s.append (bullets (text), font);
            s.setWordWrap (juce::AttributedString::byWord);
            juce::TextLayout layout;
            layout.createLayout (s, (float) (width - 2 * pad - 14));
            const int textH = juce::jlimit (22, maxTextHeight, (int) std::ceil (layout.getHeight()) + 6);
            setSize (width, 2 * pad + headingHeight() + textH);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (Theme::dialogBox);
            g.fillRoundedRectangle (r, 3.0f);

            // Sombra interior: los bordes se oscurecen hacia dentro, como una caja hundida
            for (int i = 0; i < 10; ++i)
            {
                g.setColour (juce::Colours::black.withAlpha ((Theme::light ? 0.07f : 0.22f) * (1.0f - (float) i / 10.0f)));
                g.drawRoundedRectangle (r.reduced ((float) i + 0.5f), 3.0f, 1.0f);
            }

            if (heading.isNotEmpty())
            {
                g.setColour (Theme::dialogText);
                g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
                g.drawText (heading, getLocalBounds().reduced (pad).removeFromTop (20), juce::Justification::centredLeft);
            }
        }

        void resized() override
        {
            editor.setBounds (getLocalBounds().reduced (pad).withTrimmedTop (headingHeight()));
        }

    private:
        int headingHeight() const { return heading.isNotEmpty() ? 32 : 0; }

        juce::String heading;
        juce::TextEditor editor;
    };

    class Window : public juce::TopLevelWindow
    {
    public:
        // message: el texto de la ventana (va en la caja clara).
        // Si se da boxText, message queda como frase bajo el título y la caja lleva boxHeading y boxText.
        Window (const juce::String& title, const juce::String& message, Icon iconType = Icon::NoIcon,
                const juce::String& boxHeading = {}, const juce::String& boxText = {}, juce::Component* associated = nullptr)
            : juce::TopLevelWindow (title, true), icon (iconType), associatedComponent (associated)
        {
            setAlwaysOnTop (juce::WindowUtils::areThereAnyAlwaysOnTopWindows());
            setOpaque (true);

            const bool separateBox = boxText.isNotEmpty();
            intro = separateBox ? message : juce::String();
            const auto inBox = separateBox ? boxText : message;
            width = inBox.length() > 160 ? 600 : 470;

            if (inBox.isNotEmpty())
            {
                box = std::make_unique<MessageBox> (boxHeading, inBox, width - 2 * margin);
                addAndMakeVisible (*box);
            }
            layout();
        }

        ~Window() override
        {
            for (auto* e : editors)
                e->setWantsKeyboardFocus (false);
            giveAwayKeyboardFocus();
        }

        void addButton (const juce::String& name, int result, const juce::KeyPress& key1 = {}, const juce::KeyPress& key2 = {})
        {
            auto* b = buttons.add (new juce::TextButton (name));
            if (key1.isValid()) b->addShortcut (key1);
            if (key2.isValid()) b->addShortcut (key2);
            const int textW = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)), name);
            b->setSize (juce::jmax (84, textW + 40), 34);
            b->onClick = [this, result] { exitModalState (result); };
            addAndMakeVisible (b);
            layout();
        }

        void addTextEditor (const juce::String& name, const juce::String& initialText)
        {
            auto* e = editors.add (new juce::TextEditor (name));
            e->setFont (juce::FontOptions (15.0f));
            e->setText (initialText, false);
            e->setSelectAllWhenFocused (true);
            e->setEscapeAndReturnKeysConsumed (false);   // Intro y Esc llegan a los botones
            e->setIndents (8, 6);
            e->setColour (juce::TextEditor::backgroundColourId, Theme::field);
            e->setColour (juce::TextEditor::outlineColourId, Theme::border);
            e->setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
            e->setColour (juce::TextEditor::textColourId, Theme::text);
            addAndMakeVisible (e);
            layout();
        }

        juce::String getTextEditorContents (const juce::String& name) const
        {
            for (auto* e : editors)
                if (e->getName() == name)
                    return e->getText();
            return {};
        }

        // La ventana no se queda con el componente (como en juce::AlertWindow)
        void addCustomComponent (juce::Component* c)
        {
            custom.add (c);
            addAndMakeVisible (c);
            layout();
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (Theme::panel);
            g.fillRect (r);
            g.setColour (Theme::border);
            g.drawRect (r, 1.0f);

            drawIcon (g, iconArea.toFloat(), icon);

            g.setColour (Theme::strongText);
            titleLayout.draw (g, titleArea.toFloat());

            if (intro.isNotEmpty())
            {
                g.setColour (Theme::text);
                introLayout.draw (g, introArea.toFloat());
            }
        }

        void mouseDown (const juce::MouseEvent& e) override { dragger.startDraggingComponent (this, e); }
        void mouseDrag (const juce::MouseEvent& e) override { dragger.dragComponent (this, e, nullptr); }

        bool keyPressed (const juce::KeyPress& key) override
        {
            for (auto* b : buttons)
                if (b->isRegisteredForShortcut (key))
                {
                    b->triggerClick();
                    return true;
                }
            if (key.isKeyCode (juce::KeyPress::escapeKey) && escapeKeyCancels)
            {
                exitModalState (0);
                return true;
            }
            if (key.isKeyCode (juce::KeyPress::returnKey) && buttons.size() == 1)
            {
                buttons.getFirst()->triggerClick();
                return true;
            }
            return false;
        }

        void visibilityChanged() override
        {
            juce::TopLevelWindow::visibilityChanged();
            if (isVisible() && ! editors.isEmpty())
            {
                juce::Component::SafePointer<juce::TextEditor> first (editors.getFirst());
                juce::MessageManager::callAsync ([first] { if (first != nullptr) first->grabKeyboardFocus(); });
            }
        }

        void setEscapeKeyCancels (bool cancels) { escapeKeyCancels = cancels; }

        int getDesktopWindowStyleFlags() const override
        {
            return juce::ComponentPeer::windowAppearsOnTaskbar | juce::ComponentPeer::windowHasDropShadow;
        }

    private:
        static constexpr int margin = 26;

        void layout()
        {
            const int inner = width - 2 * margin;
            const bool hasIcon = icon != Icon::NoIcon;
            const int iconSize = 46;
            int y = margin;

            // Título en negrita, a la derecha del icono
            const int titleX = margin + (hasIcon ? iconSize + 14 : 0);
            juce::AttributedString t;
            t.append (getName(), juce::Font (juce::FontOptions (24.0f, juce::Font::bold)), Theme::strongText);
            t.setWordWrap (juce::AttributedString::byWord);
            titleLayout.createLayout (t, (float) (width - margin - titleX));
            const int titleH = (int) std::ceil (titleLayout.getHeight());
            const int rowH = juce::jmax (hasIcon ? iconSize : 0, titleH);
            iconArea = { margin, y + (rowH - iconSize) / 2, iconSize, iconSize };
            titleArea = { titleX, y + (rowH - titleH) / 2, width - margin - titleX, titleH };
            y += rowH + 18;

            if (intro.isNotEmpty())
            {
                juce::AttributedString s;
                s.append (intro, juce::Font (juce::FontOptions (14.5f)), Theme::text);
                s.setWordWrap (juce::AttributedString::byWord);
                introLayout.createLayout (s, (float) inner);
                const int h = (int) std::ceil (introLayout.getHeight());
                introArea = { margin, y, inner, h };
                y += h + 16;
            }

            if (box != nullptr)
            {
                box->setTopLeftPosition (margin, y);
                y += box->getHeight() + 18;
            }

            for (auto* e : editors)
            {
                e->setBounds (margin, y, inner, 32);
                y += 32 + 12;
            }

            for (auto* c : custom)
            {
                c->setTopLeftPosition (margin, y);
                y += c->getHeight() + 12;
            }

            // Botones centrados abajo
            if (! buttons.isEmpty())
            {
                const int gap = 12;
                int total = -gap;
                for (auto* b : buttons)
                    total += b->getWidth() + gap;
                int x = (width - total) / 2;
                y += 4;
                for (auto* b : buttons)
                {
                    b->setTopLeftPosition (x, y);
                    x += b->getWidth() + gap;
                }
                y += 34;
            }
            y += margin - 4;

            if (! isVisible())
            {
                auto* around = associatedComponent != nullptr ? associatedComponent
                                                              : juce::TopLevelWindow::getActiveTopLevelWindow();
                if (around == this)
                    around = nullptr;
                centreAroundComponent (around, width, y);
            }
            else
                setBounds (getBounds().withSizeKeepingCentre (width, y));
            repaint();
        }

        Icon icon;
        juce::Component* associatedComponent;
        juce::String intro;
        int width = 470;
        bool escapeKeyCancels = true;
        std::unique_ptr<MessageBox> box;
        juce::OwnedArray<juce::TextButton> buttons;
        juce::OwnedArray<juce::TextEditor> editors;
        juce::Array<juce::Component*> custom;
        juce::TextLayout titleLayout, introLayout;
        juce::Rectangle<int> iconArea, titleArea, introArea;
        juce::ComponentDragger dragger;
    };

    // Mensaje con un solo botón (sustituye a juce::AlertWindow::showMessageBoxAsync)
    inline void show (Icon icon, const juce::String& title, const juce::String& message,
                      const juce::String& buttonText = {}, juce::Component* associated = nullptr,
                      juce::ModalComponentManager::Callback* callback = nullptr)
    {
        auto* w = new Window (title, message, icon, {}, {}, associated);
        w->addButton (buttonText.isNotEmpty() ? buttonText : juce::String ("OK"), 0,
                      juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, callback, true);
    }

    // Pregunta con dos botones: el primero devuelve 1 y el segundo 0 (como showOkCancelBox)
    inline void okCancel (Icon icon, const juce::String& title, const juce::String& message,
                          const juce::String& button1, const juce::String& button2,
                          juce::Component* associated, juce::ModalComponentManager::Callback* callback)
    {
        auto* w = new Window (title, message, icon, {}, {}, associated);
        w->addButton (button1, 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (button2, 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, callback, true);
    }
}
