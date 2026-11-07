# car-synth

A small C library that makes the sound of a car from numbers. It has no sound files for the engine. The library calculates the engine, the tyres on the ground, the slide, the starter motor and the engine stall in real time.

## Background

This library is part of my project for Game Jam 2026, the game **Holy Shift**. The car synth was the audio engine of that game. I have turned it into an open source library, so that other people can use it in their own projects.

The library is public domain. See [LICENSE](LICENSE).

## Features

- Engine sound with four cylinders, an exhaust resonance, an idle that wanders, and a rev limiter.
- Engine lugging (shake at low RPM under load) and engine stall (sputter before it stops).
- Starter motor sound.
- Ground noise that changes on road and on grass.
- Slide noise and tyre screech.
- Gear change sound.
- Written in C17 with no dependencies except the C math library. The library does not allocate memory and does not use locks. You can call the render function from an audio thread.

## Build

You need a C compiler and `make`.

```sh
make          # builds build/libcarsynth.a
```

To use the library, add `include/` to your include path and link `build/libcarsynth.a` and `-lm`. You can also add `src/car_synth.c` and `include/car_synth.h` directly to your project.

## Use the library

The synth makes mono samples from -1 to 1 at 44100 Hz (`CAR_SYNTH_SAMPLE_RATE`). You give it the state of the car. It writes the samples to a buffer that you give it.

```c
#include "car_synth.h"

CarSynth synth;
CarSynthInit(&synth);

// Optional: give the synth recordings. The synth does not copy them.
// The samples must be mono floats at CAR_SYNTH_SAMPLE_RATE.
CarSynthSetScreech(&synth, screech_samples, screech_count);
CarSynthSetGear(&synth, gear_samples, gear_count);

// In your audio callback:
CarSynthInput input = {
    .rpm = 3000.0f,
    .throttle = 1.0f,
    .speed = 200.0f,
    .slip = 0.0f,
    .grass = false,
    .stall = 0.0f,
    .running = true,
    .cranking = false,
};
CarSynthRender(&synth, &input, buffer, frames);

// When the driver changes gear:
CarSynthClunk(&synth);
```

### Input values

| Field      | Meaning                                                                      |
| ---------- | ---------------------------------------------------------------------------- |
| `rpm`      | Engine speed in revolutions each minute.                                     |
| `throttle` | Throttle position, from 0 to 1.                                              |
| `speed`    | Speed of the car. Always positive. The ground noise is full at about 300.    |
| `slip`     | Sideways speed of the tyres. Always positive. The screech is full at about 90. |
| `grass`    | `true` on grass. The tyres do not screech on grass.                          |
| `stall`    | From 0 to 1. The engine sputters more when this value increases.             |
| `running`  | `false` when the engine is off. Also `false` while the starter motor turns.  |
| `cranking` | `true` while the starter motor turns the engine.                             |

The synth smooths all input values. You can change them at any time without clicks.

### Functions

- `CarSynthInit(synth)` sets the synth to its initial state. Call it one time first.
- `CarSynthSetScreech(synth, samples, count)` sets the loop of the tyre screech. Give `NULL` for no screech.
- `CarSynthSetGear(synth, samples, count)` sets the gear change sound. Give `NULL` to get a short synthetic thud.
- `CarSynthClunk(synth)` starts the gear change sound.
- `CarSynthRender(synth, input, out, frames)` writes `frames` mono samples to `out`.

### Settings

The rev limiter operates at `CAR_SYNTH_RPM_LIMIT` (default 6300). To use a different value, define the macro before you include the header, or build with `make CFLAGS="-O2 -std=c17 -DCAR_SYNTH_RPM_LIMIT=8000.0f"`.

### Thread safety

Do not call `CarSynthRender` from two threads at the same time. If you render in an audio thread and control from a game thread, share the input values with atomic variables, as the demo does.

## Demo

The demo opens a window with text only. It has no car graphics. A small simulated car makes the input values for the synth, and you hear the result. It uses [raylib](https://www.raylib.com/) for the window, the keyboard and the audio output.

```sh
make run
```

If raylib is not in `/opt/homebrew`, set the `RAYLIB` variable to the folder that has `include/` and `lib/` of raylib:

```sh
make run RAYLIB=/usr/local
```

Start the demo from the project root. It loads its sounds from `demo/assets/`.

| Key             | Action                                                |
| --------------- | ----------------------------------------------------- |
| `W` / Up        | Throttle                                              |
| `S` / Down      | Brake                                                 |
| `A` `D` / Left Right | Steer (makes the tyres slide at speed)           |
| `Space`         | Handbrake                                             |
| `E`             | Shift up                                              |
| `Q`             | Shift down. This also starts a stopped engine.        |
| `G`             | Change between road and grass                         |

Things to try:

- Hold `W` in neutral to rev the engine and hit the rev limiter.
- Shift to gear 1 with `E`, then accelerate and shift up through the gears.
- Drive fast, then press `A` or `D` or `Space` to hear the slide and the screech.
- Shift to gear 1, release the throttle and wait. The engine stalls. Press `Q` to start it again.

## Project layout

```
include/car_synth.h   public header
src/car_synth.c       the synth
demo/main.c           raylib demo
demo/assets/          sounds for the demo (screech loop and gear change)
```

## License

This is free and unencumbered software released into the public domain. See [LICENSE](LICENSE) (The Unlicense).
