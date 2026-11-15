# Novedades de SimpleDAW

<!-- Cada versión empieza con "## x.y.z" (igual que en CMakeLists.txt). Al publicarla, su apartado
     se copia como texto de la Release en GitHub y la app lo enseña al avisar de la actualización. -->

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
