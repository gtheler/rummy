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

Al iniciar sin argumentos, no se carga ningun archivo de configuracion automaticamente: entra directo en modo interactivo por stdin.

Elegir archivo de config:

```bash
./rummy-cli --config mi-partida.cfg
```

Defaults al iniciar sin cfg:

- `config juego rummikub`
- `config output parseable`
- `config simulaciones 300`
- `config top 3`
- `config horizonte 2`
- `config jugadores 2`

## Comandos

- `config <clave> <valor>`
- `mano <c1> <c2> ...`
- `recibo <carta>`
- `tomo_descarte`
- `descarto <carta>`
- `bajo <c1> <c2> <c3> [...]`
- `mesa <c1> <c2> <c3> [...]`
- `otro_baja <jugadorN> <c1> <c2> <c3> [...]`
- `cuelo <carta> en <cartas_del_juego>`
- `otro_cuela <jugadorN> <carta> en <cartas_del_juego>`
- `muevo <carta> de Mx a My`
- `descarte <carta>`
- `sale <carta>`
- `estado`
- `turno`
- `reset`
- `fin`

Formato cartas:

- Rangos principales: `1..13` (ej: `1N`, `12C`)
- Colores: `N R M C` (Negro, Rojo, Naranja, Celeste)
- Alias admitidos: `T J Q K` (ej: `TC`, `JR`)
- Joker impreso: `JO`

Ejemplos:

- `recibo 4N`
- `bajo 5C 6C 7C`
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
- Este preset coincide con el default de arranque actual.

## Notas

- La salida de `turno` devuelve ranking por accion con:
  - `P(win aprox)`
  - `P(mejorar)`
  - `Deadwood esperado`
  - `Deadwood inmediato`
- Si `config output parseable`, `turno` imprime lineas faciles de parsear por pipe:
  - `RECOMMENDED_PLAY "comando_en_stdin"`
  - `IMMEDIATE_PLAY "comandos_inmediatos_en_mesa"` (opcional)
  - `REARRANGE_PLAY "reacomodo_en_mesa"` (opcional)
  - `RESULT ...`
  - `IMMEDIATE summary=available|none melds=<n> layoffs=<n>`
  - `REARRANGE summary=available|none ...`
  - `PLAY rank=1 ...`
  - `PLAY rank=2 ...`
  - `PLAY rank=3 ...`
  - `END`

Notas de `RECOMMENDED_PLAY`:

- Siempre es la primera linea emitida por `turno`.
- Si la jugada recomendada es tomar descarte, devuelve `tomo_descarte\\ndescarto <carta>`.
- Si la jugada recomendada es tomar mazo, devuelve `recibo <CARTA_DEL_MAZO>\\nturno`.
- Si hay jugadas inmediatas en mesa detectadas desde la mano actual, `IMMEDIATE_PLAY` devuelve una secuencia de comandos `bajo`/`cuelo` escapados con `\\n`.
- Si hay un reacomodo de mesa que habilita mas jugadas, `REARRANGE_PLAY` devuelve una secuencia que empieza con `muevo <carta> de Mx a My` y sigue con las jugadas que se habilitan.
- El modelo usa mazo restante uniforme condicionado por historial visible (cartas vistas, descarte, mesa y mano).
- Esta version es un MVP: modela decisiones `tomar descarte` / `tomar mazo` + descarte y coladas heuristicas en mesa.
