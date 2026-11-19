# KOF '98 Color Select

Mod para **The King of Fighters '98 Ultimate Match Final Edition** (Steam, x64).
Las plantillas de `dist\colors` salen de la guía de Steam
[Character Colors](https://steamcommunity.com/sharedfiles/filedetails/?id=363216126):
arriba A, B, C, D y abajo las mezclas A+B, C+D, A+D y B+C, igual que en la guía.
El archivo `NN.bmp` es el personaje con ID `NN` en el juego (0 a 50, incluidas las versiones Orochi y Omega Rugal).
Mientras la ventana está abierta, el cursor del juego no se mueve.

## Uso

1. Abre Steam con la sesión iniciada.
2. Cierra el juego si estaba abierto.
3. Ejecuta `dist\Jugar KOF98 con colores.bat`.
4. Confirma el personaje como siempre. Abajo sale la ventana.

| Acción | Mando (Steam Deck) | Teclado |
|---|---|---|
| Moverse entre colores | Cruceta o stick | Flechas |
| Usar el color de esa casilla | A, C, D o START | Enter o clic |
| Dejar el color anterior | BACK | Esc |

Sirve para cada puesto del equipo y para el jugador 2. Mientras la ventana está
abierta, el mando no mueve el cursor del juego.

El color del combate es el de la casilla resaltada en la plantilla de Steam,
no el de los botones A/B/C/D. Da igual si el juego o otro mod los remapeó:
elijas el dibujo, ese traje es el que entra a la pelea.

Hasta que confirmas el color, el personaje sigue elegido y el juego no pasa
a la pantalla de orden. Si P1 y P2 (o la CPU) eligen el mismo traje, el
segundo se cambia al color A, o al siguiente que esté libre.

`dist\kof98color.log` guarda lo que pasó en la sesión.

Para jugar sin el mod, cierra el juego y ábrelo desde Steam.

## Compilar

Hace falta Visual Studio 2022 y el SDK de .NET 9.

```bat
build.bat
```
