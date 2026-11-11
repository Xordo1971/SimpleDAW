#!/bin/bash
# Compila SimpleDAW para macOS. Uso: en Terminal, dentro de esta carpeta:  sh compilar-mac.sh
set -e
cd "$(dirname "$0")"

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Faltan las herramientas de desarrollo de Apple."
    echo "Ejecuta:  xcode-select --install   y vuelve a lanzar este script."
    exit 1
fi

CMAKE="$(command -v cmake || true)"
if [ -z "$CMAKE" ] && [ -x /Applications/CMake.app/Contents/bin/cmake ]; then
    CMAKE=/Applications/CMake.app/Contents/bin/cmake
fi
if [ -z "$CMAKE" ]; then
    echo "No encuentro CMake. Descárgalo de cmake.org (versión macOS, .dmg) y arrástralo a Aplicaciones."
    exit 1
fi

echo "Configurando (la primera vez descarga JUCE, puede tardar)..."
"$CMAKE" -B build-mac -DCMAKE_BUILD_TYPE=Release

echo "Compilando..."
"$CMAKE" --build build-mac --config Release -j "$(sysctl -n hw.ncpu)"

APP="$(find build-mac/SimpleDAW_artefacts -maxdepth 3 -name 'SimpleDAW.app' | head -1)"
echo
echo "Listo: $APP"
open -R "$APP"
