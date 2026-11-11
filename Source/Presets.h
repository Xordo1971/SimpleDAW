#pragma once
// Presets del ampli (.sdpreset): los ajustes del Simpleamp y, si se quiere, la
// cadena de efectos integrados que lo acompaña en la pista (reverb, delay...).
//
// Formato (XML legible, se puede escribir a mano):
//   <SIMPLEDAW_PRESET name="Lead con delay">
//     <EFFECT type="Ampli">
//       <PARAM id="channel" value="Lead"/>     (opciones: por nombre o por número)
//       <PARAM id="gain" value="7"/>           (números: en las unidades del control)
//       <PARAM id="sat" value="on"/>           (interruptores: on / off)
//     </EFFECT>
//     <EFFECT type="Delay"> <PARAM id="mix" value="0.2"/> </EFFECT>
//   </SIMPLEDAW_PRESET>
// Los parámetros que no aparecen toman su valor inicial. El orden de los EFFECT es
// el orden de la cadena; debe haber un único "Ampli".
#include <juce_audio_processors/juce_audio_processors.h>
#include "AppPaths.h"
#include "Text.h"

namespace Presets
{
    inline const char* rootTag() { return "SIMPLEDAW_PRESET"; }
    inline juce::String extension() { return ".sdpreset"; }

    inline juce::File folder()
    {
       #if JUCE_MAC
        auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SimpleDAW").getChildFile ("Presets");
       #else
        auto dir = AppPaths::dataFolder().getChildFile ("Presets");
       #endif
        dir.createDirectory();
        return dir;
    }

    // ---------- Parámetros <-> XML ----------
    inline void writeParams (juce::AudioProcessor& p, juce::XmlElement& e)
    {
        for (auto* raw : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (raw))
            {
                auto* pe = e.createNewChildElement ("PARAM");
                pe->setAttribute ("id", r->paramID);
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (r))
                    pe->setAttribute ("value", c->getCurrentChoiceName());
                else if (auto* b = dynamic_cast<juce::AudioParameterBool*> (r))
                    pe->setAttribute ("value", b->get() ? "on" : "off");
                else
                {
                    const double v = r->convertFrom0to1 (r->getValue());
                    pe->setAttribute ("value", juce::String (std::round (v * 1000.0) / 1000.0));
                }
            }
    }

    inline void readParams (juce::AudioProcessor& p, const juce::XmlElement& e)
    {
        for (auto* raw : p.getParameters())
        {
            auto* r = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (r == nullptr)
                continue;
            float norm = r->getDefaultValue();
            for (auto* pe : e.getChildWithTagNameIterator ("PARAM"))
            {
                if (pe->getStringAttribute ("id") != r->paramID)
                    continue;
                const auto v = pe->getStringAttribute ("value").trim();
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (r))
                {
                    int idx = c->choices.indexOf (v, true);
                    if (idx < 0) idx = v.getIntValue();
                    norm = c->convertTo0to1 ((float) juce::jlimit (0, c->choices.size() - 1, idx));
                }
                else if (dynamic_cast<juce::AudioParameterBool*> (r) != nullptr)
                {
                    const bool on = v.equalsIgnoreCase ("on") || v.equalsIgnoreCase ("true") || v == "1"
                                    || v.equalsIgnoreCase ("si") || v.equalsIgnoreCase (juce::String::fromUTF8 ("s\xc3\xad"));
                    norm = on ? 1.0f : 0.0f;
                }
                else
                {
                    const auto& range = r->getNormalisableRange();
                    norm = r->convertTo0to1 (juce::jlimit (range.start, range.end, v.getFloatValue()));
                }
                break;
            }
            r->setValueNotifyingHost (norm);
        }
    }

    inline const juce::XmlElement* ampElement (const juce::XmlElement& preset)
    {
        for (auto* e : preset.getChildWithTagNameIterator ("EFFECT"))
            if (e->getStringAttribute ("type") == "Ampli")
                return e;
        return nullptr;
    }

    inline bool isValid (const juce::XmlElement& preset)
    {
        return preset.hasTagName (rootTag()) && ampElement (preset) != nullptr;
    }

    // ---------- Archivos ----------
    inline std::unique_ptr<juce::XmlElement> load (const juce::File& f)
    {
        auto x = juce::XmlDocument::parse (f);
        if (x == nullptr || ! isValid (*x))
            return nullptr;
        if (x->getStringAttribute ("name").isEmpty())
            x->setAttribute ("name", f.getFileNameWithoutExtension());
        return x;
    }

    inline juce::Array<juce::File> userPresets()
    {
        auto files = folder().findChildFiles (juce::File::findFiles, false, "*" + extension());
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        return files;
    }

    inline juce::File fileForName (const juce::String& name)
    {
        return folder().getChildFile (juce::File::createLegalFileName (name.trim()) + extension());
    }

    // ---------- Presets de fábrica ----------
    inline juce::StringArray factoryXml()
    {
        return {
            tr8 (R"P(<SIMPLEDAW_PRESET name="Limpio cristalino">
                 <EFFECT type="Ampli"><PARAM id="channel" value="Limpio"/><PARAM id="cleanVol" value="4"/>
                   <PARAM id="cleanTreble" value="6.5"/><PARAM id="cleanBass" value="4.5"/><PARAM id="bright" value="on"/>
                   <PARAM id="presence" value="6"/><PARAM id="master" value="-4"/></EFFECT>
                 <EFFECT type="Chorus"><PARAM id="mix" value="0.3"/><PARAM id="depth" value="0.3"/></EFFECT>
                 <EFFECT type="Reverb"><PARAM id="size" value="0.5"/><PARAM id="mix" value="0.18"/></EFFECT>
               </SIMPLEDAW_PRESET>)P"),
            tr8 (R"P(<SIMPLEDAW_PRESET name="Limpio con cuerpo (jazz)">
                 <EFFECT type="Ampli"><PARAM id="channel" value="Limpio"/><PARAM id="cleanVol" value="3.5"/>
                   <PARAM id="cleanTreble" value="3.5"/><PARAM id="cleanBass" value="6"/><PARAM id="voice" value="on"/>
                   <PARAM id="presence" value="3.5"/><PARAM id="master" value="-3"/></EFFECT>
                 <EFFECT type="Reverb"><PARAM id="size" value="0.35"/><PARAM id="mix" value="0.12"/></EFFECT>
               </SIMPLEDAW_PRESET>)P"),
            tr8 (R"P(<SIMPLEDAW_PRESET name="Blues">
                 <EFFECT type="Ampli"><PARAM id="channel" value="Crunch"/><PARAM id="gain" value="3.5"/>
                   <PARAM id="bass" value="5"/><PARAM id="mid" value="6.5"/><PARAM id="treble" value="5.5"/>
                   <PARAM id="fat" value="on"/><PARAM id="master" value="-4"/></EFFECT>
                 <EFFECT type="Reverb"><PARAM id="size" value="0.45"/><PARAM id="mix" value="0.15"/></EFFECT>
               </SIMPLEDAW_PRESET>)P"),
            tr8 (R"P(<SIMPLEDAW_PRESET name="Crunch clásico">
                 <EFFECT type="Ampli"><PARAM id="channel" value="Crunch"/><PARAM id="gain" value="6"/>
                   <PARAM id="bass" value="5"/><PARAM id="mid" value="6"/><PARAM id="treble" value="6"/>
                   <PARAM id="presence" value="5.5"/><PARAM id="master" value="-5"/></EFFECT>
               </SIMPLEDAW_PRESET>)P"),
            tr8 (R"P(<SIMPLEDAW_PRESET name="Rock (BE + C45)">
                 <EFFECT type="Ampli"><PARAM id="channel" value="Crunch"/><PARAM id="gain" value="8"/>
                   <PARAM id="bass" value="5.5"/><PARAM id="mid" value="6"/><PARAM id="treble" value="6"/>
                   <PARAM id="c45" value="on"/><PARAM id="master" value="-6"/><PARAM id="gate" value="-70"/></EFFECT>
               </SIMPLEDAW_PRESET>)P"),
            tr8 (R"P(<SIMPLEDAW_PRESET name="Solo con delay">
                 <EFFECT type="Ampli"><PARAM id="channel" value="Crunch"/><PARAM id="gain" value="7"/>
                   <PARAM id="bass" value="4.5"/><PARAM id="mid" value="7"/><PARAM id="treble" value="5"/>
                   <PARAM id="presence" value="4.5"/><PARAM id="master" value="-6"/><PARAM id="gate" value="-60"/></EFFECT>
                 <EFFECT type="Delay"><PARAM id="time" value="420"/><PARAM id="feedback" value="0.35"/>
                   <PARAM id="tone" value="4000"/><PARAM id="mix" value="0.22"/></EFFECT>
                 <EFFECT type="Reverb"><PARAM id="size" value="0.6"/><PARAM id="mix" value="0.15"/></EFFECT>
               </SIMPLEDAW_PRESET>)P"),
            tr8 (R"P(<SIMPLEDAW_PRESET name="Metal moderno">
                 <EFFECT type="Ampli"><PARAM id="channel" value="Lead"/><PARAM id="gain" value="8.5"/>
                   <PARAM id="bass" value="6"/><PARAM id="mid" value="4"/><PARAM id="treble" value="6.5"/>
                   <PARAM id="presence" value="6"/><PARAM id="c45" value="on"/><PARAM id="master" value="-8"/>
                   <PARAM id="gate" value="-58"/></EFFECT>
               </SIMPLEDAW_PRESET>)P"),
            tr8 (R"P(<SIMPLEDAW_PRESET name="Ambiente (delay + reverb grande)">
                 <EFFECT type="Ampli"><PARAM id="channel" value="Limpio"/><PARAM id="cleanVol" value="4.5"/>
                   <PARAM id="cleanTreble" value="5.5"/><PARAM id="bright" value="on"/><PARAM id="master" value="-5"/></EFFECT>
                 <EFFECT type="Delay"><PARAM id="time" value="550"/><PARAM id="feedback" value="0.5"/>
                   <PARAM id="tone" value="3500"/><PARAM id="mix" value="0.3"/></EFFECT>
                 <EFFECT type="Reverb"><PARAM id="size" value="0.9"/><PARAM id="damping" value="0.3"/><PARAM id="mix" value="0.35"/></EFFECT>
               </SIMPLEDAW_PRESET>)P")
        };
    }

    inline std::unique_ptr<juce::XmlElement> factory (int index)
    {
        const auto all = factoryXml();
        if (index < 0 || index >= all.size())
            return nullptr;
        return juce::XmlDocument::parse (all[index]);
    }
}
