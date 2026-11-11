@echo off
cd /d "%~dp0"

rem --- SDK de ASIO (opcional): si no esta, se intenta descargar de Steinberg ---
if not exist "ThirdParty\asiosdk" (
    echo Intentando descargar el SDK de ASIO de Steinberg...
    powershell -NoProfile -Command "try { Invoke-WebRequest -Uri 'https://www.steinberg.net/asiosdk' -OutFile asiosdk.zip -UseBasicParsing; Expand-Archive asiosdk.zip -DestinationPath ThirdParty\asiosdk -Force; Remove-Item asiosdk.zip; exit 0 } catch { if (Test-Path asiosdk.zip) { Remove-Item asiosdk.zip }; if (Test-Path ThirdParty\asiosdk) { Remove-Item ThirdParty\asiosdk -Recurse -Force }; exit 1 }"
    if errorlevel 1 (
        echo.
        echo No se pudo descargar automaticamente. SimpleDAW se compilara sin ASIO.
        echo Para tener ASIO, mira ThirdParty\LEEME.txt
        echo.
    )
)

echo Configurando proyecto (la primera vez descarga JUCE, puede tardar)...
cmake -B build
if errorlevel 1 goto error
echo Compilando...
cmake --build build --config Release
if errorlevel 1 goto error
echo.
echo Listo: build\SimpleDAW_artefacts\Release\SimpleDAW.exe
pause
exit /b 0
:error
echo.
echo Ha habido un error durante la compilacion. Copia el mensaje de arriba y pasamelo.
pause
exit /b 1
