# SimpleDAW

DAW propio (estación de grabación) en C++17 con JUCE 8.0.12, para macOS (.app/.dmg) y Windows (.exe portable).
Autor: Paco Frade. Toda la interfaz, los comentarios y los mensajes están en español.

## Compilar
- Mac: `sh compilar-mac.sh` (CMake, carpeta `build-mac`). Windows: `compilar.bat` (intenta bajar el SDK de ASIO).
- Versión: `project(SimpleDAW VERSION x.y.z)` en CMakeLists.txt (sale en la pantalla de arranque).
- GitHub Actions (`.github/workflows/publicar.yml`): al subir una etiqueta `vX.Y.Z` (igual a la versión de
  CMakeLists) compila el DMG universal y el ZIP de Windows y crea la Release.
- Repositorio público: github.com/Xordo1971/SimpleDAW (rama `main`). La 1.4.0 fue la primera Release.
  `compilar.yml` compila de prueba en cada subida: sirve para detectar errores de MSVC sin tener Windows.

## Arquitectura (Source/)
- `AudioEngine.h`: motor. Pistas (`Track`), buses en huecos (`busSlots`, máx. 8, `addBus/detachBus/attachBus`),
  inserciones en 2 secciones (plugins/efectos; 4 huecos a la vista que crecen sin límite al llenarse), grabación, ciclo, pinchado, automatización, batería,
  render offline. El hilo de audio usa `ScopedTryLock`; los cambios desde la interfaz, `engine.getLock()`.
  `isSane()` silencia y desactiva un efecto que se desboca (aviso en `MainComponent::timerCallback`); la salida se limita a ±1.
- `MainComponent.*`: ventana principal, menús, proyectos (.sdaw = carpeta con XML + Audio/), deshacer global
  (`recordUndo(nombre, deshacer, rehacer)`; el primer "perform" no hace nada), buses, presets, actualizaciones.
- `MixerComponent.h`: canales al estilo de Logic (`StripLook`, `StripLookAndFeel`: gris medio, fader metálico,
  envíos en filas, volumen/pico, R/I/M/S; todos reservan las mismas filas de envíos y huecos para alinear faders), `MixerHost`.
- `TrackComponent.h`: cabecera y carril de cada pista (regiones, batería por compases, automatización).
- `Effects.h`: efectos integrados (Delay, Reverb, Chorus, Flanger, Phaser, Pitch, Ecualizador, Compresor, Overdrive, Ampli).
- `AmpEditor.h`: frontal gráfico del ampli (Resources/amp-panel.jpg, 1537x649; los mandos se dibujan girando
  la propia foto; coordenadas en píxeles de la imagen). Franja inferior: presets, pantalla, puerta de ruido.
- `Dialogs.h`: ventanas emergentes propias (sustituyen a `juce::AlertWindow`, misma forma de uso): marco gris, icono
  amarillo, título grande y el texto en una caja clara desplazable; `Dialogs::show`, `Dialogs::okCancel`, `Dialogs::Window`.
- `Presets.h` (formato .sdpreset, XML legible, valores en unidades reales) y `PresetChain.h` (aplicar a la pista).
- `ResourceMeter.h` (CPU/RAM), `Updater.h` (actualizaciones desde GitHub Releases; al arrancar avisa y,
  tras actualizar, enseña las novedades del CHANGELOG.md incluido en la app), `Drums.h`/`DrumEditor.h`,
  `AudioEditor.h`, `Timeline.h`, `Tuner.h`, `Splash.h` (también "Ver > Acerca de"), `Theme.h`, `AppPaths.h`, `Undo.h`.

## Convenciones
- Textos con acentos: `tr8 ("...")` (Text.h); nunca `juce::String` directo de un literal no ASCII.
- No capturar "structured bindings" en lambdas (MSVC en C++17 falla).
- Tras un cambio: compilar en Mac y probar; el motor tiene pruebas sin interfaz en `Tests/` (ver LEEME).
- Revisar el diseño sin pantalla: `SIMPLEDAW_CAPTURA=carpeta [SIMPLEDAW_CAPTURA_AUDIO=archivo.wav]
  build-mac/.../SimpleDAW.app/Contents/MacOS/SimpleDAW` guarda PNG de la ventana (barra, nombres, editor con
  onda, ancho mínimo) y se cierra (`MainComponent::saveDiagnosticCaptures`).
- Al entregar una versión nueva, subir el número en CMakeLists.txt y escribir sus novedades en CHANGELOG.md
  (apartado `## x.y.z`): es el texto de la Release y lo que la app enseña al actualizar.

## Pendiente
- Zumbido enorme (Master a +247 dB) al cargar el preset de fábrica «Solo con delay» en el Mac del usuario
  (Clarett+ 2Pre, búfer 32). No se reproduce en las simulaciones. La 1.3.3 añadió la protección/aviso de
  efecto desbocado: el aviso dirá qué efecto falla.
- Actualizaciones: probada en Mac la actualización automática de 1.6.1 a 1.6.2 (funciona). Falta probarla en Windows.
  (El guion del Mac se escribe con saltos de línea "
": con los "
" por defecto de JUCE, sh falla.)
