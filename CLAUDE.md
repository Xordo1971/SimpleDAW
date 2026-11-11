# SimpleDAW

DAW propio (estación de grabación) en C++17 con JUCE 8.0.12, para macOS (.app/.dmg) y Windows (.exe portable).
Autor: Paco Frade. Toda la interfaz, los comentarios y los mensajes están en español.

## Compilar
- Mac: `sh compilar-mac.sh` (CMake, carpeta `build-mac`). Windows: `compilar.bat` (intenta bajar el SDK de ASIO).
- Versión: `project(SimpleDAW VERSION x.y.z)` en CMakeLists.txt (sale en la pantalla de arranque).
- GitHub Actions (`.github/workflows/publicar.yml`): al subir una etiqueta `vX.Y.Z` (igual a la versión de
  CMakeLists) compila el DMG universal y el ZIP de Windows y crea la Release.

## Arquitectura (Source/)
- `AudioEngine.h`: motor. Pistas (`Track`), buses en huecos (`busSlots`, máx. 8, `addBus/detachBus/attachBus`),
  inserciones en 2 secciones (plugins/efectos, 4 cada una), grabación, ciclo, pinchado, automatización, batería,
  render offline. El hilo de audio usa `ScopedTryLock`; los cambios desde la interfaz, `engine.getLock()`.
  `isSane()` silencia y desactiva un efecto que se desboca (aviso en `MainComponent::timerCallback`); la salida se limita a ±1.
- `MainComponent.*`: ventana principal, menús, proyectos (.sdaw = carpeta con XML + Audio/), deshacer global
  (`recordUndo(nombre, deshacer, rehacer)`; el primer "perform" no hace nada), buses, presets, actualizaciones.
- `MixerComponent.h`: canales, envíos (potenciómetros, 3 por fila), `MixerHost`.
- `TrackComponent.h`: cabecera y carril de cada pista (regiones, batería por compases, automatización).
- `Effects.h`: efectos integrados (Delay, Reverb, Chorus, Flanger, Phaser, Pitch, Ecualizador, Compresor, Overdrive, Ampli).
- `AmpEditor.h`: frontal gráfico del ampli (Resources/amp-panel.jpg, 1537x649; los mandos se dibujan girando
  la propia foto; coordenadas en píxeles de la imagen). Franja inferior: presets, pantalla, puerta de ruido.
- `Presets.h` (formato .sdpreset, XML legible, valores en unidades reales) y `PresetChain.h` (aplicar a la pista).
- `ResourceMeter.h` (CPU/RAM), `Updater.h` (actualizaciones desde GitHub Releases), `Drums.h`/`DrumEditor.h`,
  `AudioEditor.h`, `Timeline.h`, `Tuner.h`, `Splash.h`, `Theme.h`, `AppPaths.h`, `Undo.h`.

## Convenciones
- Textos con acentos: `tr8 ("...")` (Text.h); nunca `juce::String` directo de un literal no ASCII.
- No capturar "structured bindings" en lambdas (MSVC en C++17 falla).
- Tras un cambio: compilar en Mac y probar; el motor tiene pruebas sin interfaz en `Tests/` (ver LEEME).
- Al entregar una versión nueva, subir el número en CMakeLists.txt.

## Pendiente
- Zumbido enorme (Master a +247 dB) al cargar el preset de fábrica «Solo con delay» en el Mac del usuario
  (Clarett+ 2Pre, búfer 32). No se reproduce en las simulaciones. La 1.3.3 añadió la protección/aviso de
  efecto desbocado: el aviso dirá qué efecto falla.
- Actualizaciones (Updater.h): código escrito y compilado, pero sin probar todavía con una Release real.
  En Mac un script espera a que se cierre la app, monta el DMG y sustituye la .app; en Windows un .bat copia el ZIP.
  El repositorio debe ser público (o al menos sus Releases) para que la app pueda consultar la API de GitHub.
