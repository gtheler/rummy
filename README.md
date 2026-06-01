# Rummy CLI backend (C++20)

Backend por stdin/stdout para evaluar decisiones de Rummy con Monte Carlo y ranking EV/probabilidad en formato humano.

## Compilar

```bash
make
```

Variantes por compilacion:

```bash
make clean && make VARIANT=GIN
make clean && make VARIANT=INDIAN
make clean && make VARIANT=R500
```

## Ejecutar

```bash
./rummy-cli
```

Al iniciar, si existe `rummy.cfg` en el directorio actual, se ejecutan sus lineas como comandos (igual que por stdin) y luego el programa sigue en modo interactivo.

Elegir archivo de config:

```bash
./rummy-cli --config mi-partida.cfg
```

Desactivar carga automatica:

```bash
./rummy-cli --no-config
```

## Comandos

- `config <clave> <valor>`
- `mano <c1> <c2> ...`
- `recibo <carta>`
- `descarto <carta>`
- `bajo <c1> <c2> <c3> [...]`
- `mesa <c1> <c2> <c3> [...]`
- `otro_baja <jugadorN> <c1> <c2> <c3> [...]`
- `cuelo <carta> en <cartas_del_juego>`
- `otro_cuela <jugadorN> <carta> en <cartas_del_juego>`
- `descarte <carta>`
- `sale <carta>`
- `estado`
- `turno`
- `reset`
- `fin`

Formato cartas:

- Rangos principales: `1..13` (ej: `1N`, `12A`)
- Colores: `N R M A` (Negro, Rojo, Marron, Azul)
- Alias admitidos: `T J Q K` (ej: `TA`, `JR`)
- Joker impreso: `JO`

Ejemplos:

- `recibo 4N`
- `bajo 5A 6A 7A`
- `cuelo JO en 10R 11R 12R`

## Claves de configuracion

- `jugadores` (2..8)
- `mazos` (1..6)
- `jokers_impresos` (0..8 por mazo)
- `joker_formato` (`none`, `printed`, `printed_plus_wild`)
- `wild_rank` (`1..13` o alias `A/T/J/Q/K`)
- `wraps` (`on/off`, `si/no`)
- `jokers_por_juego` (0..4)
- `simulaciones` (10..20000)
- `output` (`human`, `parseable`/`pipe`/`kv`/`script`)
- `top` (1..50, por defecto 3)
- `horizonte` (1..15)
- `mano_inicial` (7..20)
- `seed` (>=0)
- `variante` (texto libre)
- `juego` (preset rapido, por ejemplo `rummikub`)

Preset recomendado para Rummikub:

- `config juego rummikub` aplica 2 mazos y 2 jokers totales (1 joker impreso por mazo), sin wild rank.

## Notas

- La salida de `turno` devuelve ranking por accion con:
  - `P(win aprox)`
  - `P(mejorar)`
  - `Deadwood esperado`
  - `Deadwood inmediato`
- Si `config output parseable`, `turno` imprime lineas faciles de parsear por pipe:
  - `RECOMMENDED_PLAY "comando_en_stdin"`
  - `RESULT ...`
  - `PLAY rank=1 ...`
  - `PLAY rank=2 ...`
  - `PLAY rank=3 ...`
  - `END`

Notas de `RECOMMENDED_PLAY`:

- Siempre es la primera linea emitida por `turno`.
- Si la jugada recomendada es tomar descarte, devuelve dos comandos escapados con `\\n`.
- Si la jugada recomendada es tomar mazo, devuelve `recibo <CARTA_DEL_MAZO>\\nturno`.
- El modelo usa mazo restante uniforme condicionado por historial visible (cartas vistas, descarte, mesa y mano).
- Esta version es un MVP: modela decisiones `tomar descarte` / `tomar mazo` + descarte y coladas heuristicas en mesa.
