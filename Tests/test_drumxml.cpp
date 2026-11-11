#include "enginetest_common.h"
namespace DrumColours { inline juce::String letter (int p) { return juce::String::charToString ((juce::juce_wchar) ('A' + p)); } }
void drumToXml (const Drums::DrumData& d, juce::XmlElement& x)
{
    x.setAttribute ("swing", (double) d.swing);
    x.setAttribute ("humanize", d.humanize);
    juce::StringArray lv;
    for (auto l : d.levels) lv.add (juce::String (l, 3));
    x.setAttribute ("levels", lv.joinIntoString (","));

    for (int p = 0; p < Drums::numPatterns; ++p)
    {
        auto* px = x.createNewChildElement ("PATTERN");
        px->setAttribute ("index", p);
        for (int i = 0; i < Drums::numInstruments; ++i)
        {
            juce::String row;
            for (auto v : d.patterns[(size_t) p].steps[(size_t) i]) row << (int) v;
            px->setAttribute ("i" + juce::String (i), row);
        }
    }

    juce::String song;
    for (int b = 0; b <= d.lastBar(); ++b)
        song << (d.arrangement[(size_t) b] < 0 ? juce::String ("-") : DrumColours::letter (d.arrangement[(size_t) b]));
    x.setAttribute ("song", song);
}
void drumFromXml (Drums::DrumData& d, const juce::XmlElement& x)
{
    d = Drums::DrumData();
    d.swing = (float) x.getDoubleAttribute ("swing", 0.0);
    d.humanize = x.getBoolAttribute ("humanize", true);
    auto lv = juce::StringArray::fromTokens (x.getStringAttribute ("levels"), ",", {});
    for (int i = 0; i < juce::jmin (lv.size(), (int) Drums::numInstruments); ++i)
        d.levels[(size_t) i] = lv[i].getFloatValue();

    for (auto* px : x.getChildWithTagNameIterator ("PATTERN"))
    {
        const int p = px->getIntAttribute ("index", -1);
        if (p < 0 || p >= Drums::numPatterns) continue;
        for (int i = 0; i < Drums::numInstruments; ++i)
        {
            const auto row = px->getStringAttribute ("i" + juce::String (i));
            for (int s = 0; s < juce::jmin (row.length(), Drums::maxSteps); ++s)
                d.patterns[(size_t) p].steps[(size_t) i][(size_t) s] = (uint8_t) juce::jlimit (0, 3, row[s] - '0');
        }
    }

    const auto song = x.getStringAttribute ("song");
    for (int b = 0; b < juce::jmin (song.length(), Drums::maxBars); ++b)
        d.arrangement[(size_t) b] = song[b] == '-' ? (int8_t) -1 : (int8_t) juce::jlimit (0, Drums::numPatterns - 1, (int) (song[b] - 'A'));
}

int main()
{
    Drums::DrumData a; Drums::applyStyle (a, 4, 16); a.levels[3] = 0.33f; a.humanize = false;
    a.patterns[3].steps[Drums::ride][15] = 1;
    for (int b = 0; b < 8; ++b) a.arrangement[(size_t) b] = (int8_t) (b % 4);
    a.arrangement[5] = -1; a.arrangement[40] = 2;
    juce::XmlElement x ("DRUM"); drumToXml (a, x);
    const auto text = x.toString();
    auto parsed = juce::XmlDocument::parse (text);
    Drums::DrumData b; drumFromXml (b, *parsed);
    CHECK (std::memcmp (&a.patterns, &b.patterns, sizeof (a.patterns)) == 0, "los 4 patrones vuelven idénticos");
    CHECK (std::memcmp (&a.arrangement, &b.arrangement, sizeof (a.arrangement)) == 0, "el orden de compases vuelve idéntico (incluido el compás 41)");
    CHECK (std::abs (a.swing - b.swing) < 1e-6f && b.humanize == false && std::abs (b.levels[3] - 0.33f) < 1e-3f, "swing (%.2f), humanizar y volúmenes", b.swing);
    printf ("     ocupa %d caracteres en el proyecto\n", text.length());
    printf ("%s (%d fallos)\n", failures ? "HAY FALLOS" : "TODO BIEN", failures);
    return failures;
}
