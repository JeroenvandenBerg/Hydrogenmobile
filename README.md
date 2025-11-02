Hydrofgen mobile — README
==========================

Hydrofgen mobile — detailed README
=================================

Overview
--------
Hydrofgen mobile is an ESP32-driven LED demo that visualizes a simplified hydrogen + electricity system. The firmware drives multiple LED segments (WS2812/NeoPixel) to show flows (wind → electricity → hydrogen → storage → consumption) and status LEDs. The code has been refactored to centralize runtime state and to make effects modular and testable.

Key goals of the refactor
- Make ownership explicit (LED buffer and helpers owned by `SystemState`).
- Replace scattered globals with `SystemState` and `Timers` structs.
- Move hardware setup to a single module and consolidate effects into one place (for now).

High-level architecture
-----------------------

ASCII diagram (quick view)

	+-----------------+         +------------------+        +-------------------+
	|                 |         |                  |        |                   |
	|  Hardware layer  | <-----> |  SystemState     | <----> |  Effects (logic)  |
	|  (`Hardware.cpp`) |         |  (`SystemState.h`) |        |  (`Effects.cpp`)  |
	|                 |         | - leds[]         |        | - updateWindEffect |
	+-----------------+         | - fadeEffect*    |        | - updateStorage... |
															+------------------+        +-------------------+
																			^  |
																			|  v
															 +---------------+
															 |  LED helpers  |
															 | (`LEDs.cpp`)  |
															 +---------------+

Mermaid diagram (GitHub/MkDocs may render this):

```mermaid
graph LR
	HW[Hardware (Hardware.cpp)] -->|initializes| SS[SystemState]
	SS -->|owns| LEDs[leds[] / fadeEffect]
	SS --> Effects[Effects.cpp]
	Effects --> LEDs
	LEDs -->|writes| HW[LED strip]
```

Files and responsibilities
--------------------------
- `platformio.ini` — build config and board settings.
- `include/Config.h` — pin numbers, LED index ranges, colors and timing macros. (Quick edits here change LED mapping and timings.)
- `include/SystemState.h` — `SystemState` and `Timers` definitions. `SystemState` owns `CRGB leds[NUM_LEDS]` and `fadeLeds *fadeEffect`.
- `include/Hardware.h` / `src/Hardware.cpp` — hardware init (`hardwareInit(SystemState &state)`), relay control, button input wiring. `hardwareInit` calls `FastLED.addLeds(...)` using `state.leds`.
- `include/LEDs.h` / `src/utils/LEDs.cpp` — small helpers: `setPixelSafe(SystemState &state, int idx, const CRGB &col)` and `clearSegment(SystemState &state, int start, int end)`.
- `include/effects/Effects.h` / `src/effects/Effects.cpp` — all effect functions live here and accept `(SystemState &state, Timers &timers)`. They call into `runningLeds`, `fireEffect`, and `fadeEffect` (via `state.fadeEffect`).
- `src/main.cpp` — thin orchestrator: creates `SystemState state; Timers timers;`, calls `hardwareInit(state)`, allocates `state.fadeEffect = new fadeLeds(...)`, and runs the main loop: check button, update segments, update relays, FastLED.show().

How data flows (runtime)
-----------------------
1. `setup()` calls `hardwareInit(state)` which attaches `state.leds` to FastLED and configures GPIOs.
2. `state.fadeEffect` is allocated and used by one or more effects to create fading animations over a range of LEDs.
3. `loop()` runs `checkButtonState()`, then `updateSegments()` which runs all `update*Effect(state,timers)` functions (contained in `Effects.cpp`).
4. Each effect updates ranges of `state.leds` via helper functions or library helpers (`runningLeds`, `fireEffect`, `fill_solid`, etc.).
5. `FastLED.show()` flushes `state.leds` to the physical strip.

Build & flash (macOS / zsh)
---------------------------
From the project root (`Hydrofgen mobile`):

```bash
cd "/Users/jeroenvandenberg/Documents/PlatformIO/Projects/Hydrofgen mobile"
platformio run               # build
platformio run --target upload  # build + upload to the configured port
```

If the serial port is different than the default, set `upload_port` in `platformio.ini` or use:

```bash
platformio run --target upload --upload-port /dev/tty.SOMETHING
```

Developer notes
---------------
- Ownership: `state.leds` is owned by `SystemState` and has stable memory; do not create other global LED buffers.
- `fadeEffect` is now owned by `state.fadeEffect` and allocated in `setup()` to avoid accidental cross-file globals. If you prefer stack/embedded/no-heap, we can instead make `fadeLeds` a value member of `SystemState` and add an explicit constructor.
- Effects API: add new effects as `void updateNewThing(SystemState &state, Timers &timers)` and add them to `updateSegments()` call order in `main.cpp`.
- Safety: helpers like `setPixelSafe` perform bounds checks using `NUM_LEDS` from `Config.h`.

Troubleshooting & FAQs
-----------------------
- Q: Build fails with missing macros or indices
	- A: Open `include/Config.h` — it contains many #defines for LED start/end indices and color constants; these must match your physical LED wiring and strip length.
- Q: Effects don't show up or LEDs stay dark
	- A: Check `hardwareInit(state)` was called and `FastLED.addLeds(...)` uses the correct `DATA_PIN` and `COLOR_ORDER` from `Config.h`.
- Q: Memory concerns
	- A: `state.leds` consumes 3 bytes per LED. For large `NUM_LEDS` values, verify available RAM (PlatformIO memory report printed at link time). Consider reducing `NUM_LEDS` or moving to lower-color-depth approaches.

Next steps & suggestions
------------------------
- Add a top-level `README.md` explaining the overall mono-repo if you maintain multiple PlatformIO projects here.
- Add simple unit tests (e.g., assert `NUM_LEDS` sizes, mock out `FastLED.show()` in a host build) if you want CI that validates refactors.
- If you want less heap usage, I can refactor `SystemState` to contain a value `fadeLeds fadeEffect;` and add a constructor to `SystemState` to initialize it without `new`.

Contact / notes
---------------
If you want the diagram converted to a PNG or a nicer graphic, I can generate a PlantUML source and export it (you'll need to commit the generated image). Tell me what format you prefer.

---

If you'd like any part shortened, expanded or converted into developer docs in a `docs/` folder, say the word and I'll add it.
