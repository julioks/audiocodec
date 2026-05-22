#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr bool LED_SPECTRUM_REVERSE_X = true; // Flip this if low frequencies appear on the wrong side.
static constexpr float SPECTRUM_MIN_FREQ_HZ = 55.0f;
static constexpr float SPECTRUM_MAX_FREQ_HZ = 18000.0f;
static constexpr float SPECTRUM_DB_FLOOR = -70.0f;
static constexpr float SPECTRUM_DB_CEILING = -12.0f;
static constexpr float SPECTRUM_RELATIVE_GATE_DB = 18.0f;

class SpectrumGridVisualizer : public AudioVisualizer {
public:
  const char* name() const override {
    return "spectrum";
  }

  void begin() override {
    for (uint16_t i = 0; i < WINDOW_SAMPLES; i++) {
      window[i] = 0.5f - 0.5f * cosf(VISUALIZER_TWO_PI_F * (float)i / (float)(WINDOW_SAMPLES - 1));
      monoBuffer[i] = 0.0f;
    }

    for (uint8_t band = 0; band < LED_DRIVER_GRID_WIDTH; band++) {
      float fraction = ((float)band + 0.5f) / (float)LED_DRIVER_GRID_WIDTH;
      float targetFrequency = SPECTRUM_MIN_FREQ_HZ * powf(SPECTRUM_MAX_FREQ_HZ / SPECTRUM_MIN_FREQ_HZ, fraction);
      goertzelCoeff[band] = 2.0f * cosf(VISUALIZER_TWO_PI_F * targetFrequency / (float)SAMPLE_RATE);
      bandGain[band] = toneCompensationForBand(band);

      displayLevel[band] = 0.0f;
      peakLevel[band] = 0.0f;
      rawDb[band] = SPECTRUM_DB_FLOOR;
      rawTarget[band] = 0.0f;
    }
  }

  void reset() override {
    writeIndex = 0;
    collectedSamples = 0;
    dcEstimate = 0.0f;
    ditherFrame = 0;

    for (uint16_t i = 0; i < WINDOW_SAMPLES; i++) {
      monoBuffer[i] = 0.0f;
    }

    for (uint8_t band = 0; band < LED_DRIVER_GRID_WIDTH; band++) {
      displayLevel[band] = 0.0f;
      peakLevel[band] = 0.0f;
      rawDb[band] = SPECTRUM_DB_FLOOR;
      rawTarget[band] = 0.0f;
    }
  }

  void processAudio(const int32_t* interleavedStereo32, uint16_t frames) override {
    for (uint16_t frame = 0; frame < frames; frame++) {
      int32_t s24L = interleavedStereo32[frame * 2] >> 8;
      int32_t s24R = interleavedStereo32[frame * 2 + 1] >> 8;
      float mono = ((float)s24L + (float)s24R) * (0.5f / 8388608.0f);

      dcEstimate = dcEstimate * 0.995f + mono * 0.005f;
      mono -= dcEstimate;

      monoBuffer[writeIndex] = mono;
      writeIndex = (writeIndex + 1) & WINDOW_MASK;

      if (collectedSamples < WINDOW_SAMPLES) {
        collectedSamples++;
      }
    }
  }

  void render(Adafruit_NeoPixel& pixels) override {
    if (collectedSamples < WINDOW_SAMPLES) {
      pixels.clear();
      return;
    }

    updateLevels();

    for (uint8_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
      uint8_t band = LED_SPECTRUM_REVERSE_X
        ? (LED_DRIVER_GRID_WIDTH - 1 - x)
        : x;
      float level = displayLevel[band];
      uint8_t barHeight = (uint8_t)roundf(level * (float)LED_DRIVER_GRID_HEIGHT);

      for (uint8_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
        uint16_t pixelIndex = ledIndexXY(x, y);

        if (y < barHeight) {
          float yHeat = (float)y / (float)(LED_DRIVER_GRID_HEIGHT - 1);
          float heat = clamp01((yHeat * 0.65f) + (level * 0.35f));
          uint8_t value = (uint8_t)(70.0f + 185.0f * clamp01((level * 0.65f) + (yHeat * 0.35f)));
          pixels.setPixelColor(pixelIndex, colorBlueGreenRed(pixels, pixelIndex, heat, value));
        } else {
          pixels.setPixelColor(pixelIndex, 0);
        }
      }

      uint8_t peakY = peakPixelForLevel(peakLevel[band]);
      if (peakY < LED_DRIVER_GRID_HEIGHT) {
        uint16_t pixelIndex = ledIndexXY(x, peakY);
        pixels.setPixelColor(pixelIndex, colorBlueGreenRed(pixels, pixelIndex, peakLevel[band], 180));
      }
    }

    ditherFrame++;
  }

private:
  static constexpr uint16_t WINDOW_SAMPLES = 2048;
  static constexpr uint16_t WINDOW_MASK = WINDOW_SAMPLES - 1;

  float monoBuffer[WINDOW_SAMPLES];
  float window[WINDOW_SAMPLES];
  float goertzelCoeff[LED_DRIVER_GRID_WIDTH];
  float bandGain[LED_DRIVER_GRID_WIDTH];
  float rawDb[LED_DRIVER_GRID_WIDTH];
  float rawTarget[LED_DRIVER_GRID_WIDTH];
  float displayLevel[LED_DRIVER_GRID_WIDTH];
  float peakLevel[LED_DRIVER_GRID_WIDTH];
  uint16_t writeIndex = 0;
  uint16_t collectedSamples = 0;
  float dcEstimate = 0.0f;
  uint8_t ditherFrame = 0;

  void updateLevels() {
    uint16_t oldestIndex = writeIndex;
    float maxDb = SPECTRUM_DB_FLOOR;

    for (uint8_t band = 0; band < LED_DRIVER_GRID_WIDTH; band++) {
      float q0 = 0.0f;
      float q1 = 0.0f;
      float q2 = 0.0f;
      float coeff = goertzelCoeff[band];

      for (uint16_t n = 0; n < WINDOW_SAMPLES; n++) {
        uint16_t bufferIndex = (oldestIndex + n) & WINDOW_MASK;
        q0 = coeff * q1 - q2 + monoBuffer[bufferIndex] * window[n];
        q2 = q1;
        q1 = q0;
      }

      float power = q1 * q1 + q2 * q2 - q1 * q2 * coeff;
      if (power < 0.0f) {
        power = 0.0f;
      }

      float magnitude = sqrtf(power) / ((float)WINDOW_SAMPLES * 0.25f);
      magnitude *= bandGain[band];
      float db = 20.0f * log10f(magnitude + 0.0000001f);
      rawDb[band] = db;
      rawTarget[band] = clamp01((db - SPECTRUM_DB_FLOOR) / (SPECTRUM_DB_CEILING - SPECTRUM_DB_FLOOR));

      if (db > maxDb) {
        maxDb = db;
      }
    }

    for (uint8_t band = 0; band < LED_DRIVER_GRID_WIDTH; band++) {
      float relative = clamp01((rawDb[band] - maxDb + SPECTRUM_RELATIVE_GATE_DB) / SPECTRUM_RELATIVE_GATE_DB);
      float target = rawTarget[band] * relative * relative;

      if (target > displayLevel[band]) {
        displayLevel[band] = displayLevel[band] * 0.22f + target * 0.78f;
      } else {
        displayLevel[band] = displayLevel[band] * 0.72f + target * 0.28f;
      }

      if (displayLevel[band] > peakLevel[band]) {
        peakLevel[band] = displayLevel[band];
      } else {
        peakLevel[band] *= 0.965f;
      }
    }
  }

  float toneCompensationForBand(uint8_t band) const {
    float fraction = ((float)band + 0.5f) / (float)LED_DRIVER_GRID_WIDTH;
    if (fraction < 0.18f) {
      return 1.18f;
    }
    if (fraction > 0.82f) {
      return 1.10f;
    }
    return 1.0f;
  }

  uint32_t colorBlueGreenRed(Adafruit_NeoPixel& pixels, uint16_t pixelIndex, float heat, uint8_t value) {
    heat = clamp01(heat);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    if (heat < 0.5f) {
      float t = heat * 2.0f;
      g = (uint8_t)(255.0f * t);
      b = (uint8_t)(255.0f * (1.0f - t));
    } else {
      float t = (heat - 0.5f) * 2.0f;
      r = (uint8_t)(255.0f * t);
      g = (uint8_t)(255.0f * (1.0f - t));
    }

    r = (uint8_t)(((uint16_t)r * value) / 255);
    g = (uint8_t)(((uint16_t)g * value) / 255);
    b = (uint8_t)(((uint16_t)b * value) / 255);

    return visualizerColor(pixels, pixelIndex, r, g, b, ditherFrame);
  }

  uint8_t peakPixelForLevel(float level) const {
    if (level <= 0.02f) {
      return LED_DRIVER_GRID_HEIGHT;
    }

    uint8_t y = (uint8_t)roundf(level * (float)(LED_DRIVER_GRID_HEIGHT - 1));
    if (y >= LED_DRIVER_GRID_HEIGHT) {
      y = LED_DRIVER_GRID_HEIGHT - 1;
    }
    return y;
  }
};
