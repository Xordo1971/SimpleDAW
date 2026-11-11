#pragma once
// Actualizaciones automáticas desde las "Releases" de GitHub.
//   1. Al arrancar se consulta la última versión publicada en el repositorio.
//   2. Si es más nueva, se ofrece descargarla (DMG en Mac, ZIP en Windows).
//   3. Se instala sola al cerrar SimpleDAW, y se vuelve a abrir ya actualizada.
// El repositorio (usuario/nombre) lo pone CMake: SIMPLEDAW_GITHUB_REPO.
#include <juce_gui_basics/juce_gui_basics.h>
#include "AppPaths.h"
#include "Text.h"
#include <array>
#include <cstdlib>

#if JUCE_MAC || JUCE_LINUX
 #include <unistd.h>
#elif JUCE_WINDOWS
 extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentProcessId();
#endif

namespace Updater
{
    inline juce::String repository()
    {
       #ifdef SIMPLEDAW_GITHUB_REPO
        return SIMPLEDAW_GITHUB_REPO;
       #else
        return {};
       #endif
    }

    inline juce::String currentVersion() { return JUCE_APPLICATION_VERSION_STRING; }

    inline juce::String assetSuffix()
    {
       #if JUCE_MAC
        return "-mac.dmg";
       #elif JUCE_WINDOWS
        return "-windows.zip";
       #else
        return "-linux.zip";
       #endif
    }

    // "1.10.0" > "1.9.3"
    inline bool isNewer (juce::String candidate, juce::String current)
    {
        auto parts = [] (juce::String v)
        {
            v = v.trim().trimCharactersAtStart ("vV");
            juce::StringArray s;
            s.addTokens (v.upToFirstOccurrenceOf ("-", false, false), ".", {});
            std::array<int, 3> n {};
            for (int i = 0; i < 3 && i < s.size(); ++i) n[(size_t) i] = s[i].getIntValue();
            return n;
        };
        return parts (candidate) > parts (current);
    }

    struct Release
    {
        juce::String version, notes, assetUrl, assetName, pageUrl;
        juce::int64 assetSize = 0;
    };

    // Pregunta a GitHub por la última versión (bloquea: llamar desde un hilo aparte)
    inline bool fetchLatest (Release& out, juce::String& error)
    {
        const auto repo = repository();
        if (repo.isEmpty()) { error = "sin repositorio"; return false; }

        juce::URL url ("https://api.github.com/repos/" + repo + "/releases/latest");
        int status = 0;
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                 .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: SimpleDAW")
                                                 .withConnectionTimeoutMs (10000)
                                                 .withStatusCode (&status));
        if (stream == nullptr) { error = tr8 ("No hay conexión con GitHub."); return false; }
        const auto text = stream->readEntireStreamAsString();
        if (status == 404) { error = tr8 ("El repositorio no tiene versiones publicadas (o es privado)."); return false; }
        if (status != 200) { error = "GitHub ha respondido con el error " + juce::String (status) + "."; return false; }

        const auto json = juce::JSON::parse (text);
        out.version = json["tag_name"].toString().trimCharactersAtStart ("vV");
        out.notes   = json["body"].toString();
        out.pageUrl = json["html_url"].toString();
        if (auto* assets = json["assets"].getArray())
            for (auto& a : *assets)
                if (a["name"].toString().endsWithIgnoreCase (assetSuffix()))
                {
                    out.assetName = a["name"].toString();
                    out.assetUrl  = a["browser_download_url"].toString();
                    out.assetSize = (juce::int64) a["size"];
                }
        if (out.version.isEmpty()) { error = tr8 ("Respuesta de GitHub no válida."); return false; }
        return true;
    }

    inline juce::File downloadFolder()
    {
        auto d = AppPaths::dataFolder().getChildFile ("Actualizacion");
        d.createDirectory();
        return d;
    }

    // Descarga con barra de progreso (se borra sola al terminar)
    class DownloadJob : public juce::ThreadWithProgressWindow
    {
    public:
        DownloadJob (Release r, std::function<void (juce::File)> done)
            : juce::ThreadWithProgressWindow (tr8 ("Descargando SimpleDAW ") + r.version + "...", true, true),
              release (std::move (r)), onDone (std::move (done)) {}

        void run() override
        {
            target = downloadFolder().getChildFile (release.assetName);
            target.deleteFile();
            auto in = juce::URL (release.assetUrl).createInputStream (
                juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withExtraHeaders ("User-Agent: SimpleDAW")
                    .withConnectionTimeoutMs (15000)
                    .withNumRedirectsToFollow (10));
            if (in == nullptr) { failed = true; return; }

            const auto total = in->getTotalLength() > 0 ? in->getTotalLength() : release.assetSize;
            juce::FileOutputStream out (target);
            if (! out.openedOk()) { failed = true; return; }

            juce::HeapBlock<char> buffer (65536);
            juce::int64 done = 0;
            while (! in->isExhausted() && ! threadShouldExit())
            {
                const int n = in->read (buffer, 65536);
                if (n <= 0) break;
                out.write (buffer, (size_t) n);
                done += n;
                if (total > 0)
                {
                    setProgress ((double) done / (double) total);
                    setStatusMessage (juce::String (done / 1048576.0, 1) + " de " + juce::String (total / 1048576.0, 1) + " MB");
                }
            }
            out.flush();
            failed = threadShouldExit() || done == 0 || (total > 0 && done < total);
        }

        void threadComplete (bool cancelled) override
        {
            if (cancelled || failed)
            {
                target.deleteFile();
                if (! cancelled)
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr8 ("Actualización"),
                                                            tr8 ("No se ha podido descargar la actualización. Inténtalo más tarde."));
            }
            else if (onDone)
                onDone (target);
            delete this;
        }

    private:
        Release release;
        std::function<void (juce::File)> onDone;
        juce::File target;
        bool failed = false;
    };

    // Deja preparado un pequeño programa que espera a que SimpleDAW se cierre, cambia la
    // aplicación por la nueva y la vuelve a abrir. Devuelve false si no se pudo preparar.
    inline bool scheduleInstall (const juce::File& package)
    {
       #if JUCE_MAC
        const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);   // .../SimpleDAW.app
        const auto script = downloadFolder().getChildFile ("instalar.sh");
        script.replaceWithText (R"SH(#!/bin/sh
# Instalador de actualizaciones de SimpleDAW (lo crea la propia aplicación)
PID="$1"; DMG="$2"; APP="$3"
while kill -0 "$PID" 2>/dev/null; do sleep 0.5; done
MNT=$(mktemp -d /tmp/simpledaw-act.XXXXXX)
if hdiutil attach -nobrowse -noautoopen -mountpoint "$MNT" "$DMG"; then
  NEW=$(find "$MNT" -maxdepth 1 -name '*.app' | head -1)
  if [ -n "$NEW" ] && rm -rf "$APP.anterior" && mv "$APP" "$APP.anterior" && ditto "$NEW" "$APP"; then
    rm -rf "$APP.anterior"
    xattr -dr com.apple.quarantine "$APP" 2>/dev/null
    hdiutil detach "$MNT" -quiet
    rm -f "$DMG"
    open "$APP"
    exit 0
  fi
  # No se pudo sustituir (permisos, app abierta desde el DMG...): se deja todo como estaba
  [ -d "$APP.anterior" ] && [ ! -d "$APP" ] && mv "$APP.anterior" "$APP"
  hdiutil detach "$MNT" -quiet
fi
# Plan B: se abre el DMG para arrastrar la app a mano
open "$DMG"
)SH");
        const auto cmd = "nohup /bin/sh '" + script.getFullPathName() + "' " + juce::String ((int) getpid())
                         + " '" + package.getFullPathName() + "' '" + app.getFullPathName() + "' >/dev/null 2>&1 &";
        return std::system (cmd.toRawUTF8()) == 0;

       #elif JUCE_WINDOWS
        // El ZIP se descomprime ya; el .bat copia los archivos cuando SimpleDAW se ha cerrado
        const auto unpacked = downloadFolder().getChildFile ("nueva");
        unpacked.deleteRecursively();
        juce::ZipFile zip (package);
        if (zip.getNumEntries() == 0 || zip.uncompressTo (unpacked).failed())
            return false;
        const auto dest = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        const auto bat = downloadFolder().getChildFile ("instalar.bat");
        bat.replaceWithText ("@echo off\r\n"
                             "rem Instalador de actualizaciones de SimpleDAW (lo crea la propia aplicacion)\r\n"
                             ":esperar\r\n"
                             "tasklist /FI \"PID eq %1\" 2>nul | find \"%1\" >nul\r\n"
                             "if not errorlevel 1 (\r\n"
                             "  ping -n 2 127.0.0.1 >nul\r\n"
                             "  goto esperar\r\n"
                             ")\r\n"
                             "xcopy \"%~2\\*\" \"%~3\\\" /E /Y /Q >nul\r\n"
                             "start \"\" \"%~3\\SimpleDAW.exe\"\r\n"
                             "rmdir /s /q \"%~2\"\r\n"
                             "del \"%~4\"\r\n");
        static juce::ChildProcess process;   // sigue vivo aunque SimpleDAW se cierre
        return process.start ("cmd.exe /c \"\"" + bat.getFullPathName() + "\" " + juce::String ((juce::int64) GetCurrentProcessId())
                              + " \"" + unpacked.getFullPathName() + "\" \"" + dest.getFullPathName() + "\" \""
                              + package.getFullPathName() + "\"\"", 0);
       #else
        juce::ignoreUnused (package);
        return false;
       #endif
    }
}
