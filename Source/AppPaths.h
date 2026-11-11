#pragma once
#include <juce_core/juce_core.h>

// Dónde guarda SimpleDAW sus archivos.
// Windows: todo junto al .exe (portable).
// macOS: el ejecutable vive dentro de SimpleDAW.app, que no debe modificarse,
//        así que se usan las carpetas habituales del usuario.
namespace AppPaths
{
    inline juce::File dataFolder()
    {
       #if JUCE_MAC
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("Application Support").getChildFile ("SimpleDAW");
       #else
        auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
       #endif
        dir.createDirectory();
        return dir;
    }

    inline juce::File recordingsFolder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile ("SimpleDAW").getChildFile ("Grabaciones");
       #else
        return dataFolder().getChildFile ("Grabaciones");
       #endif
    }

    inline juce::File audioSettings()   { return dataFolder().getChildFile ("SimpleDAW-audio.xml"); }
    // Autoguardado: si SimpleDAW se cierra de golpe, al volver a abrirlo se ofrece recuperar la sesión
    inline juce::File recoveryFile()    { auto d = dataFolder().getChildFile ("Recuperacion"); d.createDirectory(); return d.getChildFile ("recuperacion.sdaw"); }
    inline juce::File appSettings()     { return dataFolder().getChildFile ("SimpleDAW-ajustes.xml"); }
    inline juce::File pluginList()      { return dataFolder().getChildFile ("SimpleDAW-plugins.xml"); }
    inline juce::File deadMansPedal()   { return dataFolder().getChildFile ("SimpleDAW-plugins-escaneo.tmp"); }
}
