# audiocodec

ESP32 audio-reactive LED matrix firmware for a PCM1861-style I2S audio ADC and
a 32 x 16 WS2812/NeoPixel panel.

It samples stereo audio at 96 kHz, builds a 128-band analysis frame on the
ESP32, and renders the result to a LED matrix as a composable real-time
visualizer stack.

## Features

- 96 kHz I2S receive path with 24-bit PCM in 32-bit stereo slots.
- On-device audio analysis with FFT bands, peaks, transients, stereo width,
  waveform capture, spectral centroid, and dominant frequency.
- Mode 0 boot/clear spectrum using the standard blue-green-red palette.
- Three stackable effect parts:
  - `e1` drift ripples
  - `e2` sparkles
  - `e3` afterglow-style four-point stars
- Palette commands for the latest effect layer:
  - `p0` standard blue-green-red
  - `p1` warm candy
  - `p2` aurora
- Frequency commands for the latest effect layer, using the analyzer bins:
  - `lf100` lower limit at 100 Hz
  - `hf250` upper limit at 250 Hz
- Noise floor commands for the latest effect layer:
  - `f20` or `floor20` ignores the lowest 20% of that layer's signal
- Serial commands for building the visualizer stack and changing LED brightness.

## Tested Toolchain

This sketch currently builds with:

- Arduino ESP32 core `3.0.4`
- Adafruit NeoPixel `1.15.4`
- Board FQBN: `esp32:esp32:esp32`

Other ESP32 board definitions may work, but the pin choices and I2S MCLK support
should be checked before uploading.

## Hardware

Default wiring is configured near the top of `audiocodec.ino`.

| Signal | ESP32 pin | Notes |
| --- | ---: | --- |
| I2S MCLK | GPIO0 | Classic ESP32 MCLK-capable pin. |
| I2S BCLK | GPIO26 | PCM1861 bit clock. |
| I2S LRCK | GPIO25 | PCM1861 word select. |
| I2S DIN | GPIO35 | PCM1861 data output into ESP32. |
| LED data | GPIO23 | WS2812/NeoPixel data output. |

The LED layout defaults to a 32 x 16 column-serpentine matrix with the first
pixel at the bottom left. Adjust `LED_DRIVER_GRID_WIDTH`,
`LED_DRIVER_GRID_HEIGHT`, `LED_LAYOUT`, and `LED_FIRST_PIXEL_IS_BOTTOM_LEFT`
if your panel is wired differently.

Power the LED panel from a supply sized for the number of pixels, tie the LED
and ESP32 grounds together, and use a proper data level shifter for reliable
WS2812 signalling.

## Dependencies

- Arduino IDE or Arduino CLI.
- ESP32 Arduino board package (`esp32:esp32`).
- Adafruit NeoPixel library.

With Arduino CLI installed and available on `PATH`:

```powershell
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit NeoPixel"
```

## Build And Upload

Replace `COM3` with the serial port for your ESP32:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32 .
arduino-cli monitor -p COM3 --config baudrate=115200
```

The compile command was last verified successfully with the tested toolchain
listed above.

## Serial Commands

Open the serial monitor at 115200 baud and send:

- `list`
- `clear`
- `e1`
- `e2`
- `e3`
- `p0`
- `p1`
- `p2`
- `lf100`
- `hf250`
- `f20`
- `floor20`
- `brightness <0-255>`

Commands can be comma-separated. For example, `e3,e1,p1,p2,e2,p1` builds a
stack with afterglow stars on the default palette, ripples recolored to `p2`,
and sparkles on `p1` as the top layer. The stack keeps appending until `clear`
is sent, which returns to mode 0 spectrum on `p0`.

Frequency commands apply to the latest effect until another `eN` is added, so
`e1,lf100,hf250,e2` limits only the ripples to 100-250 Hz. If `lf`/`hf` are
sent before any effect, they are held for the next effect: `lf100,hf250,e1`.

Noise floor commands work the same way. `e2,f20` makes sparkles ignore the
bottom 20% of their selected bins and remaps the remaining signal back to the
normal range. `f0` clears the floor for that layer.

## Project Layout

- `audiocodec.ino` contains the ESP32 setup, I2S input loop, LED driver setup,
  and serial command forwarding.
- `audio/` contains the analysis frame and FFT/audio feature extraction.
- `visualiser/` contains the shared visualizer canvas/palette code, stack
  handler, mode 0 spectrum, and the three composable effect parts.
