# Novedades de SimpleDAW

<!-- Cada versión empieza con "## x.y.z" (igual que en CMakeLists.txt). Al publicarla, su apartado
     se copia como texto de la Release en GitHub y la app lo enseña al avisar de la actualización. -->

## 2.3.0

- EQ de canal en cada pista y en el Master (en el Master afecta a toda la mezcla): 8 bandas al estilo Logic (corte de graves, estante de graves, cuatro campanas, estante de agudos y corte de agudos). Se abre con el botón «EQ» de la cabecera de la pista o con el recuadro «Ecualizador» de arriba de cada canal del mezclador, que enseña la curva.
- En el editor del EQ, cada banda es un punto de color: se arrastra para cambiar la frecuencia y la ganancia, con la rueda se cambia la Q y con doble clic se activa o desactiva la banda. Debajo están los valores, que se cambian arrastrando o escribiéndolos. El EQ se guarda con el proyecto.
- El mezclador lateral enseña ahora solo el Master y la pista seleccionada, como Logic: al seleccionar otra pista cambia, sin ocupar más sitio.
- Nuevo «Mezclador completo» en su propia ventana (Ver > Mezclador completo, Mayús+X o Mayús+clic en el botón del mezclador): Master, todas las pistas y los buses. Se puede mover a otra pantalla y cerrar. Clic en el nombre de un canal para seleccionar la pista.
- Los proyectos nuevos ya no traen los buses de Reverb y Delay: los efectos se ponen pista a pista. Los buses se siguen pudiendo añadir con «+ Bus» en el mezclador completo. Los proyectos antiguos conservan los suyos.

## 2.2.1

- Ampli: el micro de la pantalla ahora se oye. Antes sus cambios caían en agudos que la pantalla ya había quitado y no se notaban.
- Posición: en el centro del cono suena brillante y agresivo; hacia el borde, claramente más oscuro.
- Distancia: pegado, más graves (efecto de proximidad); lejos, menos graves y agudos y el rebote en el suelo, que da color de sala.
- Cada micro tiene su carácter: el 57 con su pico de medios-agudos, el 421 con medios ahuecados, el 609 chillón, el RE20 cálido y casi sin proximidad, la cinta 121 oscura y con muchos graves de cerca, y los condensadores 87 y 414 más abiertos.

## 2.2.0

- Cuatro temas nuevos de aspecto, en «Ver > Tema»: Grafito (oscuro con el amarillo de la marca), Papel (crema cálido con acento terracota), Medianoche (azul oscuro con cian) y Analógico (nogal y latón). Cambian en caliente y se recuerdan al reabrir.
- «Predeterminado» es el aspecto de siempre, con su modo oscuro y claro. «Modo claro» y el botón sol/luna vuelven a él.

## 2.1.1

- Pantalla de arranque y «Acerca de»: el texto de la versión, abajo a la derecha, se veía amontonado; ahora se lee bien.

## 2.1.0

- Ampli: nuevo panel «Pantalla y micro» a la derecha del cabezal, con la foto de la pantalla elegida (1x12, 2x12 o 4x12) y un micrófono delante que se arrastra sobre el altavoz: en el centro del cono suena más brillante y hacia el borde más suave.
- Siete micros para elegir: Condenser 87, Condenser 414, Dynamic 20, Dynamic 57, Dynamic 421, Dynamic 609 y Ribbon 121, y un regulador de distancia (pegado: más graves y más directo; lejos: más suave). El micro solo suena si hay pantalla.
- La posición, el modelo y la distancia del micro se guardan con el proyecto y con los presets del ampli. Los presets antiguos siguen sonando casi igual (micro Dynamic 57 por defecto).
- La ventana del ampli es ahora más ancha para dar sitio al panel. Si usas una pantalla por IR, el micro no actúa: la respuesta ya lleva el micro de quien la grabó.

## 2.0.1

- Frontal nuevo del ampli Simpleamp, con un diseño propio. Los mandos giran y las palancas se mueven sobre la imagen nueva; al apagarlo, la palanca de POWER sube y el piloto se apaga.
- Arreglo: en Windows las palancas del ampli (FAT, BITE, SAT, VOICE, BRIGHT, CHANNEL y POWER) no se veían cambiar de posición, aunque el sonido sí cambiaba.
- Nombres más claros: el interruptor C45 pasa a llamarse BITE y el selector de canal muestra «Limpio», «Crunch» o «Lead». Los presets y proyectos guardados siguen funcionando igual. El preset de fábrica «Rock (BE + C45)» se llama ahora «Rock (crunch + bite)».

## 2.0.0

- Velocidad de práctica: junto al afinador hay un selector de velocidad (del 100 % al 50 %). Toda la reproducción va más despacio sin cambiar el tono y sin tocar el audio, para sacar solos poco a poco. La guitarra que entra por la tarjeta suena en tiempo real; el metrónomo y la batería siguen la velocidad. Al elegir una velocidad, cada audio se prepara en segundo plano unos segundos (la barra inferior lo indica). Por debajo del 100 % no se puede grabar, y al exportar la mezcla siempre sale al 100 %.
- Tomas sin ciclo: si vuelves a grabar empezando exactamente donde empieza una región de la pista (desde un marcador o desde el inicio), lo nuevo se guarda como otra toma de esa región y se elige con clic derecho, igual que las tomas de la grabación en ciclo. Si empiezas en otro punto, queda encima como siempre.
- Presets del ampli: favoritos (salen arriba del menú, con una estrella), carpetas para organizarlos (se elige la carpeta al guardar) y «Exportar para compartir...», que guarda el ajuste actual donde quieras para mandárselo a otra persona.

## 1.11.0

- Pantallas reales en el ampli: la opción «Pantalla» tiene ahora «IR (archivo)», y un botón «IR...» para cargar una respuesta al impulso (.wav, .aiff o .flac) de un altavoz o una pantalla de verdad. Se guarda con el proyecto; si el archivo se mueve o se borra, la pantalla queda sin IR.
- Tres efectos nuevos en el menú de efectos: Tremolo (velocidad, profundidad, forma de onda y modo estéreo), Wah (con el pedal a mano, automático con LFO o según cómo se toca) y Puerta de ruido (umbral, ataque, mantener, relajación y reducción).
- Al importar un audio, la pista nueva queda en «Sin entrada» en vez de escuchar la entrada 1 de la tarjeta.

## 1.10.0

- «Importar audio» (Ctrl/Cmd+I) pone ahora el audio en una pista nueva, desde el principio, y avisa en la barra inferior de a qué pista ha ido (que pasa a ser la pista activa). Así no se pisa por error la pista donde estabas trabajando. Para importar a la pista activa, desde el cursor, hay una opción nueva: «Pista > Importar a la pista activa» (Ctrl/Cmd+Mayús+I).
- Clic derecho en un marcador > «Repetir esta sección»: activa el ciclo desde ese marcador hasta el siguiente (o hasta el final de lo grabado) para repetir un pasaje sin tocar nada más.
- La precuenta al grabar puede ser de 1 a 4 compases (clic derecho en el botón del metrónomo) y se guarda con el proyecto.

## 1.9.0

- La pista activa se ve más oscura (cabecera y carril), para saber a cuál va lo que importas o grabas. Al añadir una pista nueva pasa a ser la activa automáticamente.
- Tres presets de fábrica nuevos para el ampli: «Ry Cooder - Feeling Bad Blues», «Sultans of Swing» y «War Raog - Gwendal». Son aproximaciones hechas a oído: retócalas al gusto.
- Arreglo: al cerrar la ventana del ampli y volver a abrirla salía un panel de barras en lugar de la carátula del Simpleamp. Ahora sale siempre la carátula.

## 1.8.2

- Nueva tipografía: toda la interfaz (menús, botones, pistas, mezclador, ventanas emergentes) usa ahora Montserrat, con la versión Bold para los textos en negrita y la Regular para el resto. Va incluida en la aplicación, no hace falta instalar nada. Las pantallas de tiempo y tempo conservan su letra de siempre.

## 1.8.1

- Arranque más seguro con el audio: si no se puede abrir el dispositivo guardado (por ejemplo un driver ASIO de otro fabricante sin su tarjeta conectada), SimpleDAW ya no prueba otros a ciegas. Reintenta una vez, usa el dispositivo por defecto del sistema, avisa de cuál no se pudo abrir y conserva tu configuración guardada hasta que vuelvas a elegir el dispositivo en Audio.

## 1.8.0

- Modo claro: en «Ver > Modo claro» la aplicación entera pasa a colores claros (barra, pistas, editor, mezclador, ventanas emergentes y menús), y se vuelve al modo oscuro desmarcándolo. Hay también un botón con un sol/luna junto al afinador que lo activa y desactiva. El cambio es inmediato y se recuerda al volver a abrir SimpleDAW. La ventana tiene ahora 50 píxeles más de ancho mínimo para que quepa el botón nuevo. Los medidores y las cajas de volumen siguen oscuros para que se lean bien.

## 1.7.0

- Mezclador con aspecto nuevo, al estilo de Logic: canales gris medio, huecos de plugins y efectos con una marca de color (azul plugin, verde efecto), envíos en filas con el nombre y el color del bus, potenciómetro de panorama claro, volumen y pico uno junto al otro, fader metálico con la escala junto al medidor, y botones R (grabar) e I (monitorizar) encima de M y S.
- Vuelve la tipografía del sistema (la Avenir Next dejaba la pantalla de tiempo demasiado pequeña).

## 1.6.2

- Barra superior: los botones de ir al principio, parar, reproducir, grabar y ciclo pasan a la izquierda de la pantalla de tiempo; a su derecha quedan solo el metrónomo y el afinador.
- Ventanas emergentes nuevas: marco gris oscuro, título grande y el texto en una caja más clara con deslizadores si no cabe (avisos, nueva versión, novedades...).
- Toda la aplicación usa la tipografía Avenir Next (normal y negrita). En Windows, si no está instalada, se usa la de siempre.
- Incluye el arreglo de la 1.6.1: en Mac la actualización automática ya se instala sola. Si tienes la 1.6.0 o anterior, esta versión hay que instalarla a mano una vez (descargando el DMG).

## 1.6.1

- Arreglada de verdad la actualización automática en Mac: el instalador se guardaba con un formato de texto que el Mac no entendía y no llegaba a arrancar.
- Importante: desde la 1.6.0 o anteriores esta actualización hay que instalarla a mano una vez (descargando el DMG). A partir de la 1.6.1 las siguientes ya se instalarán solas.

## 1.6.0
- La ventana se abre más grande (1810 x 1035, o lo que quepa en la pantalla).
- Barra superior nueva: los botones van solo con icono y a la izquierda (pista, mezclador, audio, plugins,
  importar, editor y afinador). La pantalla de posición/tempo/compás queda centrada en la ventana, con el
  transporte a su derecha. Una franja plegable bajo la barra muestra el nombre de cada botón.
- El editor de audio muestra la onda completa, ajustada al hueco, al abrirse.
- Zoom con reguladores, a lo ancho y a lo alto: en las pistas (tiempo y altura de las pistas) y en el
  editor (tiempo y amplitud de la onda).

## 1.5.1
- Arreglado: en Mac, la actualización automática se descargaba pero no se instalaba al cerrar SimpleDAW.
  (Desde la 1.4.0 o la 1.5.0 hay que instalar esta versión a mano una vez; las siguientes ya se instalan solas.)

## 1.5.0
- Plugins y efectos sin límite: cuando se ocupan los cuatro huecos de una sección, aparece uno nuevo (y así sucesivamente).
- Nuevo menú «Ver > Acerca de SimpleDAW», con la pantalla de inicio y la versión.
- La pantalla de inicio muestra la versión junto a «Creado por Paco Frade».
- Después de actualizar, SimpleDAW muestra las novedades de la versión nueva.

## 1.4.0
- Primera versión publicada en GitHub, con aviso de actualizaciones al arrancar.
- Correcciones para compilar en Windows.
