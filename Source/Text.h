#pragma once
#include <juce_core/juce_core.h>

// Los literales con acentos o símbolos (á, ñ, ·...) deben convertirse desde UTF-8;
// si no, JUCE los muestra mal (p. ej. "Â·").
inline juce::String tr8 (const char* utf8) { return juce::String::fromUTF8 (utf8); }
