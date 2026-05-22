#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "driver/i2s.h"
#include <stdlib.h>
#include <string.h>

#define I2S_PORT        I2S_NUM_0

#define PIN_I2S_MCLK    0    // Classic ESP32: usually GPIO0/GPIO1/GPIO3 only
#define PIN_I2S_BCLK    26
#define PIN_I2S_LRCK    25
#define PIN_I2S_DIN     35   // input-only GPIO is fine for DOUT -> ESP32

#define SAMPLE_RATE     96000
#define MCLK_HZ         24576000  // 96k * 256

#define LED_PIN         23   // NeoPixel data output. GPIO23 is output-capable and does not overlap the I2S pins.

static constexpr uint8_t LED_DRIVER_GRID_WIDTH = 32;
static constexpr uint8_t LED_DRIVER_GRID_HEIGHT = 16;
static constexpr uint16_t LED_COUNT = LED_DRIVER_GRID_WIDTH * LED_DRIVER_GRID_HEIGHT;
static constexpr uint8_t LED_BRIGHTNESS = 48;
static constexpr uint16_t LED_REFRESH_INTERVAL_MS = 16; // About 60 FPS. 256 WS2812 pixels take ~8 ms to push.
static constexpr bool DEBUG_AUDIO_PRINTS = true;
static constexpr uint8_t DEFAULT_VISUALIZER_INDEX = 1;
static constexpr uint16_t AUDIO_FRAMES_PER_BLOCK = 256;

enum LED_DRIVER_LAYOUT : uint8_t {
  LED_DRIVER_LAYOUT_COLUMN_SERPENTINE
};

static constexpr LED_DRIVER_LAYOUT LED_LAYOUT = LED_DRIVER_LAYOUT_COLUMN_SERPENTINE;
static constexpr bool LED_FIRST_PIXEL_IS_BOTTOM_LEFT = true; // Flip this if the visualizers draw upside down.

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
uint8_t currentLedBrightness = LED_BRIGHTNESS;

#ifndef I2S_COMM_FORMAT_STAND_I2S
  #define I2S_COMM_FORMAT_STAND_I2S I2S_COMM_FORMAT_I2S
#endif

uint16_t ledIndexXY(uint8_t x, uint8_t yFromBottom) {
  if (x >= LED_DRIVER_GRID_WIDTH || yFromBottom >= LED_DRIVER_GRID_HEIGHT) {
    return 0;
  }

  uint8_t stripY = LED_FIRST_PIXEL_IS_BOTTOM_LEFT
    ? yFromBottom
    : (LED_DRIVER_GRID_HEIGHT - 1 - yFromBottom);

  switch (LED_LAYOUT) {
    case LED_DRIVER_LAYOUT_COLUMN_SERPENTINE:
    default:
      if (x & 0x01) {
        stripY = LED_DRIVER_GRID_HEIGHT - 1 - stripY;
      }
      return (uint16_t)x * LED_DRIVER_GRID_HEIGHT + stripY;
  }
}

#include "visualiser/SpectrumGridVisualizer.h"
#include "visualiser/DriftRippleVisualizer.h"
#include "visualiser/SparkParticleVisualizer.h"
#include "visualiser/WaveformPlasmaVisualizer.h"

SpectrumGridVisualizer spectrumVisualizer;
DriftRippleVisualizer driftRippleVisualizer;
SparkParticleVisualizer sparkParticleVisualizer;
WaveformPlasmaVisualizer waveformPlasmaVisualizer;

// Add future user-coded visualizers here, then switch them at runtime by index/name.
AudioVisualizer* visualizers[] = {
  &spectrumVisualizer,
  &driftRippleVisualizer,
  &sparkParticleVisualizer,
  &waveformPlasmaVisualizer
};

static constexpr uint8_t VISUALIZER_COUNT = sizeof(visualizers) / sizeof(visualizers[0]);
uint8_t activeVisualizerIndex = 0;
uint32_t lastLedRefresh = 0;

void clearStrip() {
  strip.clear();
  strip.show();
}

void setActiveVisualizer(uint8_t index) {
  if (index >= VISUALIZER_COUNT) {
    return;
  }

  activeVisualizerIndex = index;
  visualizers[activeVisualizerIndex]->reset();
  clearStrip();

  Serial.print("Active visualizer: ");
  Serial.println(visualizers[activeVisualizerIndex]->name());
}

void printVisualizerList() {
  Serial.println("Visualizers:");
  for (uint8_t i = 0; i < VISUALIZER_COUNT; i++) {
    Serial.print("  ");
    Serial.print(i);
    Serial.print(": ");
    Serial.println(visualizers[i]->name());
  }
  Serial.println("Commands: list, next, spectrum, ripples, sparks, plasma, brightness <0-255>");
}

void handleVisualizerCommand(char* command) {
  while (*command == ' ') {
    command++;
  }

  char* end = command + strlen(command);
  while (end > command && *(end - 1) == ' ') {
    *(--end) = '\0';
  }

  if (strcmp(command, "list") == 0) {
    printVisualizerList();
    return;
  }

  if (strcmp(command, "next") == 0) {
    setActiveVisualizer((activeVisualizerIndex + 1) % VISUALIZER_COUNT);
    return;
  }

  if (strcmp(command, "spectrum") == 0 || strcmp(command, "0") == 0) {
    setActiveVisualizer(0);
    return;
  }

  if (strcmp(command, "ripples") == 0 || strcmp(command, "drift") == 0 || strcmp(command, "drift-ripples") == 0 || strcmp(command, "1") == 0) {
    setActiveVisualizer(1);
    return;
  }

  if (strcmp(command, "sparks") == 0 || strcmp(command, "particles") == 0 || strcmp(command, "spark-particles") == 0 || strcmp(command, "2") == 0) {
    setActiveVisualizer(2);
    return;
  }

  if (strcmp(command, "plasma") == 0 || strcmp(command, "waveform") == 0 || strcmp(command, "scope") == 0 || strcmp(command, "waveform-plasma") == 0 || strcmp(command, "3") == 0) {
    setActiveVisualizer(3);
    return;
  }

  if (strncmp(command, "brightness ", 11) == 0) {
    int brightness = atoi(command + 11);
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;
    currentLedBrightness = (uint8_t)brightness;
    strip.setBrightness(currentLedBrightness);
    strip.show();
    Serial.print("LED brightness: ");
    Serial.println(brightness);
    return;
  }

  Serial.print("Unknown command: ");
  Serial.println(command);
  printVisualizerList();
}

void pollSerialCommands() {
  static char commandBuffer[48];
  static uint8_t commandLength = 0;

  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\n' || c == '\r') {
      if (commandLength > 0) {
        commandBuffer[commandLength] = '\0';
        handleVisualizerCommand(commandBuffer);
        commandLength = 0;
      }
      continue;
    }

    if (commandLength < sizeof(commandBuffer) - 1) {
      if (c >= 'A' && c <= 'Z') {
        c = c - 'A' + 'a';
      }
      commandBuffer[commandLength++] = c;
    }
  }
}

void setupI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,

    // PCM1861 outputs 24-bit audio, but use 32-bit slots:
    // stereo * 32 bits = 64 BCK per LRCK, exactly what the PCM1861 likes.
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,

    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,

    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,

    // Use ESP32 audio PLL for a less-janky audio clock.
    .use_apll = true,

    .tx_desc_auto_clear = false,

    // Force correct MCLK if your core supports this field.
    .fixed_mclk = MCLK_HZ,

    // These exist in newer Arduino-ESP32 / ESP-IDF 4.x builds.
    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    .bits_per_chan = I2S_BITS_PER_CHAN_32BIT
  };

  i2s_pin_config_t pins = {
    .mck_io_num = PIN_I2S_MCLK,
    .bck_io_num = PIN_I2S_BCLK,
    .ws_io_num = PIN_I2S_LRCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PIN_I2S_DIN
  };

  ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, NULL));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
  ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_PORT));
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  strip.begin();
  strip.setBrightness(currentLedBrightness);
  clearStrip();

  for (uint8_t i = 0; i < VISUALIZER_COUNT; i++) {
    visualizers[i]->begin();
  }

  setupI2S();

  Serial.println("PCM1861 I2S RX started at 96 kHz.");
  printVisualizerList();
  if (DEFAULT_VISUALIZER_INDEX < VISUALIZER_COUNT) {
    setActiveVisualizer(DEFAULT_VISUALIZER_INDEX);
  } else {
    setActiveVisualizer(0);
  }
}

void loop() {
  pollSerialCommands();

  static int32_t samples[AUDIO_FRAMES_PER_BLOCK * 2]; // Stereo frames, L/R interleaved
  size_t bytesRead = 0;

  esp_err_t err = i2s_read(
    I2S_PORT,
    samples,
    sizeof(samples),
    &bytesRead,
    portMAX_DELAY
  );

  if (err != ESP_OK || bytesRead == 0) {
    Serial.println("I2S read failed or got no data.");
    return;
  }

  int frames = bytesRead / 8; // 2 channels * 32-bit
  visualizers[activeVisualizerIndex]->processAudio(samples, frames);

  uint32_t now = millis();
  if (now - lastLedRefresh >= LED_REFRESH_INTERVAL_MS) {
    lastLedRefresh = now;
    visualizers[activeVisualizerIndex]->render(strip);
    strip.show();
  }

  // Print a few raw samples now and then.
  // PCM1861 is 24-bit MSB-first inside a 32-bit-ish slot.
  // Usually shift right by 8 to get signed 24-bit-ish values.
  static uint32_t lastPrint = 0;
  if (DEBUG_AUDIO_PRINTS && now - lastPrint > 500) {
    lastPrint = now;

    int32_t rawL = samples[0];
    int32_t rawR = samples[1];

    int32_t s24L = rawL >> 8;
    int32_t s24R = rawR >> 8;

    Serial.print("bytes=");
    Serial.print(bytesRead);
    Serial.print(" frames=");
    Serial.print(frames);
    Serial.print(" rawL=0x");
    Serial.print((uint32_t)rawL, HEX);
    Serial.print(" rawR=0x");
    Serial.print((uint32_t)rawR, HEX);
    Serial.print(" s24L=");
    Serial.print(s24L);
    Serial.print(" s24R=");
    Serial.println(s24R);
  }
}
