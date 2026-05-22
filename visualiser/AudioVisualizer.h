#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

static constexpr float VISUALIZER_TWO_PI_F = 6.28318530718f;

extern uint8_t currentLedBrightness;

static inline float clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

static inline float wrap01(float value) {
  while (value < 0.0f) value += 1.0f;
  while (value >= 1.0f) value -= 1.0f;
  return value;
}

static inline uint8_t visualizerCorrectChannel(float value, uint16_t pixelIndex, uint8_t channel, uint8_t ditherFrame) {
  if (value <= 0.0f || currentLedBrightness == 0) {
    return 0;
  }
  if (value > 255.0f) {
    value = 255.0f;
  }

  float normalized = value / 255.0f;
  float corrected = normalized * normalized * sqrtf(normalized) * 255.0f;

  if (currentLedBrightness < 255) {
    corrected *= 255.0f / (float)currentLedBrightness;
  }
  if (corrected > 255.0f) {
    corrected = 255.0f;
  }

  uint8_t base = (uint8_t)corrected;
  float fraction = corrected - (float)base;
  uint8_t threshold = (uint8_t)((pixelIndex * 37u + channel * 67u + ditherFrame * 29u) & 0xFF);

  if (fraction * 255.0f > (float)threshold && base < 255) {
    base++;
  }

  return base;
}

static inline uint32_t visualizerColor(Adafruit_NeoPixel& pixels, uint16_t pixelIndex, float r, float g, float b, uint8_t ditherFrame) {
  return pixels.Color(
    visualizerCorrectChannel(r, pixelIndex, 0, ditherFrame),
    visualizerCorrectChannel(g, pixelIndex, 1, ditherFrame),
    visualizerCorrectChannel(b, pixelIndex, 2, ditherFrame)
  );
}

class AudioVisualizer {
public:
  virtual const char* name() const = 0;
  virtual void begin() {}
  virtual void reset() {}
  virtual void processAudio(const int32_t* interleavedStereo32, uint16_t frames) = 0;
  virtual void render(Adafruit_NeoPixel& pixels) = 0;
};
