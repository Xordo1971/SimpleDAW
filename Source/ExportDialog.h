#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Text.h"
#include "Theme.h"

// Opciones de exportación de la mezcla
struct ExportSettings
{
    enum Format { wav = 0, aiff, flac, ogg };
    Format format = wav;
    bool stems = false;         // true: cada pista en su propio archivo
    int bits = 24;              // 16, 24 o 32 (32 = coma flotante)
    int oggQualityIndex = 6;    // 192 kbps

    juce::String extension() const
    {
        switch (format)
        {
            case aiff: return ".aiff";
            case flac: return ".flac";
            case ogg:  return ".ogg";
            default:   return ".wav";
        }
    }

    std::unique_ptr<juce::AudioFormat> createFormat() const
    {
        switch (format)
        {
            case aiff: return std::make_unique<juce::AiffAudioFormat>();
            case flac: return std::make_unique<juce::FlacAudioFormat>();
            case ogg:  return std::make_unique<juce::OggVorbisAudioFormat>();
            default:   return std::make_unique<juce::WavAudioFormat>();
        }
    }

    juce::AudioFormatWriterOptions writerOptions (double sampleRate) const
    {
        using SF = juce::AudioFormatWriterOptions::SampleFormat;
        auto o = juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (2);
        if (format == ogg)
            return o.withBitsPerSample (16).withQualityOptionIndex (oggQualityIndex);
        if (bits == 32)
            return o.withBitsPerSample (32).withSampleFormat (SF::floatingPoint);
        return o.withBitsPerSample (bits).withSampleFormat (SF::integral);
    }
};

class ExportDialog : public juce::Component
{
public:
    explicit ExportDialog (std::function<void (ExportSettings)> onExportChosen) : onExport (std::move (onExportChosen))
    {
        whatBox.addItem ("Mezcla completa (un archivo)", 1);
        whatBox.addItem (tr8 ("Cada pista por separado (stems)"), 2);
        whatBox.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (whatBox);

        formatBox.addItemList ({ tr8 ("WAV (sin pérdida, universal)"), tr8 ("AIFF (sin pérdida, Apple)"),
                                 tr8 ("FLAC (sin pérdida, comprimido)"), tr8 ("Ogg Vorbis (comprimido, para compartir)") }, 1);
        formatBox.setSelectedId (1, juce::dontSendNotification);
        formatBox.onChange = [this] { updateQualityOptions(); };
        addAndMakeVisible (formatBox);
        addAndMakeVisible (qualityBox);

        for (auto* l : { &whatLabel, &formatLabel, &qualityLabel })
        {
            l->setColour (juce::Label::textColourId, Theme::textDim);
            l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            addAndMakeVisible (l);
        }

        note.setText (tr8 ("Se exporta desde el inicio hasta el final de la última región, más la cola de los efectos. "
                           "En stems, cada archivo lleva su pista con sus efectos y su envío a reverb/delay; todos duran lo mismo.\n"
                           "MP3 no está disponible: necesita un codificador externo. Ogg o FLAC son buenas alternativas."),
                      juce::dontSendNotification);
        note.setColour (juce::Label::textColourId, Theme::textDim);
        note.setFont (juce::FontOptions (11.5f));
        note.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (note);

        exportButton.onClick = [this] { finish (true); };
        cancelButton.onClick = [this] { finish (false); };
        exportButton.setColour (juce::TextButton::buttonColourId, Theme::accent.darker (0.3f));
        addAndMakeVisible (exportButton);
        addAndMakeVisible (cancelButton);

        updateQualityOptions();
        setSize (480, 290);
    }

    void paint (juce::Graphics& g) override { g.fillAll (Theme::panel); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18);
        auto row = [&r] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (8); return x; };
        auto w = row (26);  whatLabel.setBounds (w.removeFromLeft (100)); whatBox.setBounds (w);
        auto f = row (26);  formatLabel.setBounds (f.removeFromLeft (100));  formatBox.setBounds (f);
        auto q = row (26);  qualityLabel.setBounds (q.removeFromLeft (100)); qualityBox.setBounds (q);
        auto buttons = r.removeFromBottom (30);
        exportButton.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (100));
        r.removeFromBottom (8);
        note.setBounds (r);
    }

private:
    void updateQualityOptions()
    {
        const int fmt = formatBox.getSelectedId() - 1;
        qualityBox.clear (juce::dontSendNotification);
        if (fmt == ExportSettings::ogg)
        {
            qualityLabel.setText ("Calidad", juce::dontSendNotification);
            qualityBox.addItemList (juce::OggVorbisAudioFormat().getQualityOptions(), 1);
            qualityBox.setSelectedId (7, juce::dontSendNotification);   // 192 kbps
        }
        else
        {
            qualityLabel.setText (tr8 ("Resolución"), juce::dontSendNotification);
            qualityBox.addItem ("16 bits (CD)", 16);
            qualityBox.addItem ("24 bits", 24);
            if (fmt != ExportSettings::flac)
                qualityBox.addItem ("32 bits (coma flotante)", 32);
            qualityBox.setSelectedId (24, juce::dontSendNotification);
        }
    }

    void finish (bool doExport)
    {
        ExportSettings s;
        s.stems = whatBox.getSelectedId() == 2;
        s.format = (ExportSettings::Format) (formatBox.getSelectedId() - 1);
        if (s.format == ExportSettings::ogg) s.oggQualityIndex = qualityBox.getSelectedId() - 1;
        else                                 s.bits = qualityBox.getSelectedId();

        auto callback = onExport;
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);   // cierra (y destruye) el diálogo

        if (doExport && callback)
            juce::MessageManager::callAsync ([callback, s] { callback (s); });
    }

    std::function<void (ExportSettings)> onExport;
    juce::Label whatLabel { {}, "Exportar" }, formatLabel { {}, "Formato" }, qualityLabel { {}, tr8 ("Resolución") }, note;
    juce::ComboBox whatBox, formatBox, qualityBox;
    juce::TextButton exportButton { "Exportar..." }, cancelButton { "Cancelar" };
};
