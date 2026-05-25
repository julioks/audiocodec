# audiocodec

ESP32 audio-reactive LED matrix sketch for a PCM1861-style I2S audio ADC and a
32 x 16 WS2812/NeoPixel panel.

The sketch samples stereo audio at 96 kHz, builds a 128-band analysis frame on
the ESP32, and renders several real-time visualizers to the LED grid.

## Features

- 96 kHz I2S receive path with 24-bit PCM in 32-bit stereo slots.
- On-device audio analysis with FFT bands, peaks, transients, stereo width,
  waveform capture, spectral centroid, and dominant frequency.
- Seven visualizers: spectrum grid, drift ripples, spark particles, waveform
  plasma, candy peak spectrum, afterglow, and ember vortex.
- Serial commands for switching visualizers and changing LED brightness.
- Local Arduino CLI wrapper for Windows users.

## Hardware

Default wiring is configured in `audiocodec.ino`.

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

Use a power supply sized for the LED panel, tie grounds together, and use a
proper data level shifter for reliable WS2812 signalling.

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

## Local Config

`arduino-cli.yaml` is ignored because it contains machine-specific paths. If
you want the wrapper script to use a project-local config, copy the example and
edit the paths for your machine:

```powershell
Copy-Item arduino-cli.example.yaml arduino-cli.yaml
```

The wrapper first looks for `.tools\arduino-cli.exe`, then falls back to the
Arduino IDE-bundled CLI.

## Build And Upload

Replace `COM3` with the serial port for your ESP32:

```powershell
.\arduino-cli.cmd compile --fqbn esp32:esp32:esp32 .
.\arduino-cli.cmd upload -p COM3 --fqbn esp32:esp32:esp32 .
.\arduino-cli.cmd monitor -p COM3 --config baudrate=115200
```

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

