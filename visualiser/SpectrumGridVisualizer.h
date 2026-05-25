#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr bool LED_SPECTRUM_REVERSE_X = true; // Flip this if low frequencies appear on the wrong side.
static constexpr float LED_SPECTRUM_DB_FLOOR = -78.0f;
static constexpr float LED_SPECTRUM_DB_CEILING = -3.0f;
static constexpr float LED_SPECTRUM_SILENCE_DB = -96.0f;

class SpectrumGridVisualizer : public AudioVisualizer {
public:
  const char* name() const override {
    return "spectrum";
  }

  void begin() override {
    reset();
  }

  void reset() override {
    ditherFrame = 0;
    for (uint16_t column = 0; column < LED_DRIVER_GRID_WIDTH; column++) {
      columnDb[column] = LED_SPECTRUM_SILENCE_DB;
      columnLevel[column] = 0.0f;
    }
  }

  void render(Adafruit_NeoPixel& pixels, const AudioAnalysisFrame& audio) override {
    if (!audio.ready) {
      pixels.clear();
      return;
    }

    updateColumns(audio);

    for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
      uint16_t column = LED_SPECTRUM_REVERSE_X
        ? (LED_DRIVER_GRID_WIDTH - 1 - x)
        : x;
      float level = columnLevel[column];
      uint16_t barHeight = (uint16_t)roundf(level * (float)LED_DRIVER_GRID_HEIGHT);
      if (barHeight > LED_DRIVER_GRID_HEIGHT) {
        barHeight = LED_DRIVER_GRID_HEIGHT;
      }

      for (uint16_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
        uint16_t pixelIndex = ledIndexXY(x, y);
        if (y < barHeight) {
          pixels.setPixelColor(pixelIndex, spectrumColor(pixels, pixelIndex, level));
        } else {
          pixels.setPixelColor(pixelIndex, 0);
        }
      }
    }

    ditherFrame++;
  }

private:
  float columnDb[LED_DRIVER_GRID_WIDTH];
  float columnLevel[LED_DRIVER_GRID_WIDTH];
  uint8_t ditherFrame = 0;

  void updateColumns(const AudioAnalysisFrame& audio) {
    float minHz = audio.bandCenterHz[0];
    float maxHz = audio.bandCenterHz[AUDIO_ANALYSIS_BANDS - 1];
    if (minHz <= 0.0f || maxHz <= minHz) {
      minHz = 30.0f;
      maxHz = 20000.0f;
    }

    float ratio = maxHz / minHz;
    for (uint16_t column = 0; column < LED_DRIVER_GRID_WIDTH; column++) {
      float lowFraction = (float)column / (float)LED_DRIVER_GRID_WIDTH;
      float highFraction = (float)(column + 1) / (float)LED_DRIVER_GRID_WIDTH;
      float lowHz = minHz * powf(ratio, lowFraction);
      float highHz = minHz * powf(ratio, highFraction);
      columnDb[column] = strongestBandDbInRange(audio, lowHz, highHz);
      columnLevel[column] = dbToLevel(columnDb[column]);
    }
  }

  float strongestBandDbInRange(const AudioAnalysisFrame& audio, float lowHz, float highHz) const {
    float strongestDb = LED_SPECTRUM_SILENCE_DB;
    bool foundBand = false;

    for (uint16_t band = 0; band < AUDIO_ANALYSIS_BANDS; band++) {
      float frequency = audio.bandCenterHz[band];
      if (frequency < lowHz || frequency >= highHz) {
        continue;
      }

      if (!foundBand || audio.bandDb[band] > strongestDb) {
        strongestDb = audio.bandDb[band];
        foundBand = true;
      }
    }

    if (foundBand) {
      return strongestDb;
    }

    uint16_t nearestBand = nearestAnalysisBand(audio, sqrtf(lowHz * highHz));
    return audio.bandDb[nearestBand];
  }

  uint16_t nearestAnalysisBand(const AudioAnalysisFrame& audio, float frequencyHz) const {
    uint16_t nearestBand = 0;
    float nearestDistance = fabsf(audio.bandCenterHz[0] - frequencyHz);
    for (uint16_t band = 1; band < AUDIO_ANALYSIS_BANDS; band++) {
      float distance = fabsf(audio.bandCenterHz[band] - frequencyHz);
      if (distance < nearestDistance) {
        nearestDistance = distance;
        nearestBand = band;
      }
    }
    return nearestBand;
  }

  float dbToLevel(float db) const {
    return clamp01((db - LED_SPECTRUM_DB_FLOOR) / (LED_SPECTRUM_DB_CEILING - LED_SPECTRUM_DB_FLOOR));
  }

  uint32_t spectrumColor(Adafruit_NeoPixel& pixels, uint16_t pixelIndex, float level) {
    float heat = clamp01(level);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    if (heat < 0.50f) {
      float t = heat * 2.0f;
      g = (uint8_t)(255.0f * t);
      b = (uint8_t)(255.0f * (1.0f - t));
    } else {
      float t = (heat - 0.50f) * 2.0f;
      r = (uint8_t)(255.0f * t);
      g = (uint8_t)(255.0f * (1.0f - t));
    }

    return visualizerColor(pixels, pixelIndex, r, g, b, ditherFrame);
  }
};
