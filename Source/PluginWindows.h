#pragma once
#include "Text.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

// Ventana con la interfaz de un efecto o plugin
class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow (juce::AudioProcessor& p, const juce::String& title, std::function<void (PluginEditorWindow*)> onClose)
        : juce::DocumentWindow (title, Theme::panel, juce::DocumentWindow::closeButton), processor (p), closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);

        juce::AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorIfNeeded() : nullptr;
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (p);

        // Los editores genéricos (efectos integrados) se ven mejor algo más anchos
        if (dynamic_cast<juce::GenericAudioProcessorEditor*> (editor) != nullptr)
            editor->setSize (juce::jmax (420, editor->getWidth()), editor->getHeight());

        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~PluginEditorWindow() override
    {
        clearContentComponent();   // el editor se destruye antes que el procesador
    }

    juce::AudioProcessor& getProcessor() { return processor; }

    void closeButtonPressed() override
    {
        if (closeCallback)
            closeCallback (this);
    }

private:
    juce::AudioProcessor& processor;
    std::function<void (PluginEditorWindow*)> closeCallback;
};

