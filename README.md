# Rummikub CLI backend (C++20)

Backend por stdin/stdout para analizar jugadas de Rummikub online con fichas.

## Compilar

```bash
make
```

## Ejecutar

```bash
./rummy-cli
```

Tambien podes cargar comandos desde archivo:

```bash
./rummy-cli --config mi-partida.cfg
```

## Reglas del motor

- Modalidad fija: Rummikub online.
- No hay comando de tope de descarte.
- Robar del pozo (`recibo`) cierra tu turno para jugadas propias.
- Cuando vuelva a tocarte, pedi `turno` y el motor inicia ese nuevo turno automaticamente.
- La primera bajada propia debe sumar al menos 30 puntos (entre los juegos que bajes para abrir).
- Para abrir con varios juegos en un mismo turno, usa `bajo <j1...> bajo <j2...> ...` en forma atomica.
- No podes colar en juegos ajenos hasta haber bajado al menos un juego propio.

## Comandos

- `config <clave> <valor>`
- `mano <f1> <f2> ...`
- `recibo <ficha>`
- `bajo <j1...> [bajo <j2...> ...]` (sirve para uno o varios juegos)
- `mesa <f1> <f2> <f3> [...]`
- `otro_baja <jugadorN> <f1> <f2> <f3> [...]`
- `cuelo <ficha> en <fichas_del_juego>`
- `otro_cuela <jugadorN> <ficha> en <fichas_del_juego>`
- `muevo <ficha> de Mx a My`
- `sale <ficha>`
- `estado`
- `turno`
- `reset`
- `fin`

## Formato de ficha

- Rangos: `1..13` (ej: `1N`, `12C`)
- Colores: `N R M C` (Negro, Rojo, Naranja, Celeste)
- Alias de rango: `T J Q K` (ej: `TC`, `JR`)
- Joker impreso: `JO`

## Configuracion

Claves principales:

- `jugadores` (2..8)
- `simulaciones` (10..20000)
- `output` (`human`, `parseable`)
- `top` (1..50)
- `horizonte` (1..15)
- `seed` (>=0)

## Salida parseable de `turno`

- `RECOMMENDED_PLAY "..."`
- `RESULT ...`
- `PLAY rank=...` (opcional, segun `top_n`)
- `END`
