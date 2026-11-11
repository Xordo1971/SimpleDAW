# Pruebas automáticas del motor (sin interfaz)

Programas de consola que comprueban el motor de audio, los efectos y los presets
sin abrir ventanas. Se escribieron y ejecutaron en Linux.

Compilación (Linux), con `J` = carpeta `modules` de JUCE 8.0.12:

    for m in juce_core juce_events juce_data_structures juce_audio_basics juce_audio_formats \
             juce_audio_devices juce_audio_processors_headless juce_dsp; do
      echo "#include \"settings.h\"
    #include <$m/$m.cpp>" > $m.cpp
      g++ -std=c++17 -O1 -w -include settings.h -I$J -c $m.cpp -o $m.o
    done
    g++ -std=c++17 -O1 -include settings.h -Ishim -I$J -I../Source test_t1.cpp cd.cpp *.o -o test_t1 -lpthread -ldl
    ./test_t1      # termina con "TODO BIEN (0 fallos)"

- `test_t1` grabación, latencia, ciclo, pinchado, precuenta, exportar
- `test_t3` buses y envíos, automatización · `test_t4` deshacer · `test_t5` batería
- `test_amp` ampli · `test_presets` presets · `test_runaway` protección contra efectos desbocados
- `test_solo`, `test_live32`, `test_blow` diagnóstico del zumbido del preset «Solo con delay»
- `test_drumxml`, `test_speed` guardado de batería y cambio de velocidad

En Mac (clang), con `J` = `build-mac/_deps/juce-src/modules`: igual, pero compilando como
Objective-C++ (`clang++ -x objective-c++`, módulos en archivos `.mm`) y enlazando los frameworks
`Cocoa CoreAudio CoreMIDI AudioToolbox Accelerate IOKit CoreAudioKit AudioUnit Security QuartzCore`.
