#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Text.h"
#include "Theme.h"
#include "Dialogs.h"
#include "AudioEngine.h"

// Ajustes de audio: dispositivos (de JUCE) + compensación de latencia de grabación
class AudioSettingsPanel : public juce::Component, private juce::Timer
{
public:
    AudioSettingsPanel (juce::AudioDeviceManager& dm, AudioEngine& e, std::function<juce::StringArray()> inputNames,
                        std::function<void()> onLatencyChanged)
        : engine (e), getInputNames (std::move (inputNames)), latencyChanged (std::move (onLatencyChanged)),
          selector (dm, 0, 16, 0, 2, false, false, false, false)
    {
        addAndMakeVisible (selector);

        title.setText (tr8 ("Compensación de latencia de grabación"), juce::dontSendNotification);
        title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (title);

        info.setFont (juce::FontOptions (12.0f));
        info.setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (info);

        manual.setRange (-50.0, 100.0, 0.1);
        manual.setTextValueSuffix (" ms");
        manual.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        manual.setDoubleClickReturnValue (true, 0.0);
        manual.onValueChange = [this]
        {
            engine.manualLatencySamples = (int) std::lround (manual.getValue() * rate() / 1000.0);
            updateInfo();
            if (latencyChanged) latencyChanged();
        };
        addAndMakeVisible (manual);
        manualLabel.setText ("Ajuste manual", juce::dontSendNotification);
        manualLabel.setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (manualLabel);

        measureButton.onClick = [this] { askToMeasure(); };
        addAndMakeVisible (measureButton);
        addAndMakeVisible (measureInput);

        help.setText (tr8 ("Lo grabado se adelanta automáticamente lo que tarda el sonido en ir y volver por la interfaz. "
                           "Si notas las tomas un poco retrasadas, usa \"Medir\" con un cable o ajústalo a mano."),
                      juce::dontSendNotification);
        help.setFont (juce::FontOptions (11.0f));
        help.setColour (juce::Label::textColourId, Theme::textDim);
        help.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (help);

        manual.setValue (engine.manualLatencySamples.load() * 1000.0 / rate(), juce::dontSendNotification);
        refreshInputs();
        updateInfo();
        setSize (560, 640);
        startTimerHz (4);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::border);
        g.drawHorizontalLine (title.getY() - 10, 12.0f, (float) getWidth() - 12.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bottom = r.removeFromBottom (190).reduced (16, 8);
        selector.setBounds (r);

        bottom.removeFromTop (8);
        title.setBounds (bottom.removeFromTop (22));
        info.setBounds (bottom.removeFromTop (20));
        bottom.removeFromTop (6);
        auto row = bottom.removeFromTop (26);
        manualLabel.setBounds (row.removeFromLeft (110));
        manual.setBounds (row);
        bottom.removeFromTop (6);
        auto row2 = bottom.removeFromTop (26);
        measureButton.setBounds (row2.removeFromLeft (170));
        row2.removeFromLeft (8);
        measureInput.setBounds (row2.removeFromLeft (200));
        bottom.removeFromTop (6);
        help.setBounds (bottom);
    }

private:
    double rate() const { return engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0; }

    void refreshInputs()
    {
        const auto names = getInputNames();
        const int sel = juce::jmax (1, measureInput.getSelectedId());
        measureInput.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            measureInput.addItem (tr8 ("Vuelve por: ") + names[i], i + 1);
        measureInput.setSelectedId (juce::jmin (sel, juce::jmax (1, names.size())), juce::dontSendNotification);
    }

    void updateInfo()
    {
        const double ms = 1000.0 / rate();
        info.setText (tr8 ("Driver: ") + juce::String (engine.getReportedLatency() * ms, 1) + " ms   +   ajuste: "
                        + juce::String (manual.getValue(), 1) + " ms   =   total: " + juce::String (engine.getTotalLatency() * ms, 1) + " ms",
                      juce::dontSendNotification);
    }

    void askToMeasure()
    {
        if (engine.getState() != AudioEngine::stopped || measureInput.getNumItems() == 0)
            return;
        auto* aw = new Dialogs::Window (tr8 ("Medir la latencia"),
            tr8 ("1. Conecta con un cable la salida 1 de la interfaz a la entrada elegida (y quita la guitarra).\n"
                 "2. Baja el volumen de los monitores y auriculares: sonará un clic fuerte.\n"
                 "3. Pulsa \"Medir\"."), juce::MessageBoxIconType::InfoIcon);
        aw->addButton ("Medir", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<AudioSettingsPanel> self (this);
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([self] (int r)
        {
            if (self != nullptr && r == 1)
            {
                self->engine.startLatencyMeasurement (self->measureInput.getSelectedId() - 1);
                self->measuring = true;
                self->measureButton.setEnabled (false);
            }
        }), true);
    }

    void timerCallback() override
    {
        updateInfo();
        if (measureInput.getNumItems() != getInputNames().size())
            refreshInputs();

        if (measuring && ! engine.isMeasuringLatency())
        {
            measuring = false;
            measureButton.setEnabled (true);
            const int measured = engine.getMeasuredLatency();
            if (measured < 0)
            {
                Dialogs::show (juce::MessageBoxIconType::WarningIcon, tr8 ("Medir la latencia"),
                    tr8 ("No se ha detectado el clic. Comprueba el cable, la entrada elegida y que la ganancia de esa entrada no esté al mínimo."));
                return;
            }
            const double ms = (measured - engine.getReportedLatency()) * 1000.0 / rate();
            manual.setValue (juce::jlimit (-50.0, 100.0, std::round (ms * 10.0) / 10.0));
            Dialogs::show (juce::MessageBoxIconType::InfoIcon, tr8 ("Medir la latencia"),
                tr8 ("Latencia medida: ") + juce::String (measured * 1000.0 / rate(), 1) + tr8 (" ms. Ajuste aplicado."));
        }
    }

    AudioEngine& engine;
    std::function<juce::StringArray()> getInputNames;
    std::function<void()> latencyChanged;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label title, info, manualLabel, help;
    juce::Slider manual;
    juce::TextButton measureButton { tr8 ("Medir con un cable...") };
    juce::ComboBox measureInput;
    bool measuring = false;
};
