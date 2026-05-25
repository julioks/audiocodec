# audiocodec

ESP32 audio-reactive LED matrix firmware for a PCM1861-style I2S audio ADC and
a 32 x 16 WS2812/NeoPixel panel.

It samples stereo audio at 96 kHz, builds a 128-band analysis frame on the
ESP32, and renders the result to a LED matrix as real-time music visualizers.

## Features

- 96 kHz I2S receive path with 24-bit PCM in 32-bit stereo slots.
- On-device audio analysis with FFT bands, peaks, transients, stereo width,
  waveform capture, spectral centroid, and dominant frequency.
- Seven built-in visualizers:
  - spectrum grid
  - drift ripples
  - spark particles
  - waveform plasma
  - candy peak spectrum
  - afterglow
  - ember vortex
- Serial commands for switching visualizers and changing LED brightness.
- Windows helper script for Arduino CLI, while still working with a normal
  Arduino IDE setup.

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

With Arduino CLI:

```powershell
.\arduino-cli.cmd core update-index
.\arduino-cli.cmd core install esp32:esp32
.\arduino-cli.cmd lib install "Adafruit NeoPixel"
```

## Local Arduino Config

The real `arduino-cli.yaml` is intentionally not tracked. Arduino CLI config
usually contains paths such as the local Arduino data directory, download
cache, and sketchbook location, which are different on every machine.

The helper script works without a project config. If a local `arduino-cli.yaml`
exists, it uses it; otherwise it falls back to Arduino CLI's default config.
It first looks for `.tools\arduino-cli.exe`, then falls back to the CLI bundled
with Arduino IDE on Windows.

## Build And Upload

Replace `COM3` with the serial port for your ESP32:

```powershell
.\arduino-cli.cmd compile --fqbn esp32:esp32:esp32 .
.\arduino-cli.cmd upload -p COM3 --fqbn esp32:esp32:esp32 .
.\arduino-cli.cmd monitor -p COM3 --config baudrate=115200
```

The compile command was last verified successfully with the tested toolchain
listed above.

## Serial Commands

Open the serial monitor at 115200 baud and send:

- `list`
- `next`
- `spectrum`
- `ripples`
- `sparks`
- `plasma`
- `candy`
- `afterglow`
- `cosmic` or `gravity`
- `brightness <0-255>`

The number aliases `0` through `6` also select the visualizers in the order
shown by `list`.

## Project Layout

- `audiocodec.ino` contains the ESP32 setup, I2S input loop, LED driver setup,
  serial command handling, and visualizer selection.
- `audio/` contains the analysis frame and FFT/audio feature extraction.
- `visualiser/` contains the shared visualizer interface and the individual
  rendering modes.
- `arduino-cli.cmd` is a Windows convenience wrapper. The sketch itself is not
  tied to that script.

## Repository Notes

The repository tracks source and portable project files only. Local Arduino CLI
configuration, downloaded tools, generated build output, and temporary publish
folders are ignored.

No open-source license is included, so the default copyright rules apply.
