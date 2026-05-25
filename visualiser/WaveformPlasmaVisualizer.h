#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr uint16_t WAVEFORM_PLASMA_TRACE_POINTS = AUDIO_ANALYSIS_WAVEFORM_POINTS;
static constexpr float WAVEFORM_PLASMA_TRAIL_FADE = 0.75f;

class WaveformPlasmaVisualizer : public AudioVisualizer {
public:
  const char* name() const override {
    return "waveform-plasma";
  }

  void begin() override {
    reset();
  }

  void reset() override {
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      canvasR[i] = 0;
      canvasG[i] = 0;
      canvasB[i] = 0;
    }

    for (uint16_t i = 0; i < WAVEFORM_PLASMA_TRACE_POINTS; i++) {
      traceL[i] = 0.0f;
      traceR[i] = 0.0f;
      traceM[i] = 0.0f;
    }

    writeIndex = 0;
    scopeGain = 5.0f;
    rmsEnergy = 0.0f;
    peakEnergy = 0.0f;
    bassEnergy = 0.0f;
    bassPulseEnergy = 0.0f;
    trebleEnergy = 0.0f;
    stereoWidth = 0.0f;
    stereoCorrelation = 0.0f;
    zeroCrossEnergy = 0.0f;
    spectralTilt = 0.5f;
    colorBase = 0.5f;
    plasmaPhase = 0.0f;
    lastAudioSequence = 0;
    ditherFrame = 0;
  }

  void render(Adafruit_NeoPixel& pixels, const AudioAnalysisFrame& audio) override {
    applyAudio(audio);
    fadeCanvas();

    plasmaPhase += 0.10f + trebleEnergy * 0.26f + zeroCrossEnergy * 0.08f;
    drawVectorscope();
    drawMonoSpine();
    drawBassPulse();

    for (uint16_t i = 0; i < LED_COUNT; i++) {
      pixels.setPixelColor(i, visualizerColor(pixels, i, canvasR[i], canvasG[i], canvasB[i], ditherFrame));
    }

    ditherFrame++;
  }

private:
  float canvasR[LED_COUNT];
  float canvasG[LED_COUNT];
  float canvasB[LED_COUNT];
  float traceL[WAVEFORM_PLASMA_TRACE_POINTS];
  float traceR[WAVEFORM_PLASMA_TRACE_POINTS];
  float traceM[WAVEFORM_PLASMA_TRACE_POINTS];
  uint16_t writeIndex = 0;
  float scopeGain = 5.0f;
  float rmsEnergy = 0.0f;
  float peakEnergy = 0.0f;
  float bassEnergy = 0.0f;
  float bassPulseEnergy = 0.0f;
  float trebleEnergy = 0.0f;
  float stereoWidth = 0.0f;
  float stereoCorrelation = 0.0f;
  float zeroCrossEnergy = 0.0f;
  float spectralTilt = 0.5f;
  float colorBase = 0.5f;
  float plasmaPhase = 0.0f;
  uint32_t lastAudioSequence = 0;
  uint8_t ditherFrame = 0;

  void applyAudio(const AudioAnalysisFrame& audio) {
    if (!audio.ready || audio.sequence == lastAudioSequence) {
      return;
    }

    lastAudioSequence = audio.sequence;
    for (uint16_t i = 0; i < WAVEFORM_PLASMA_TRACE_POINTS; i++) {
      traceL[i] = audio.waveformL[i];
      traceR[i] = audio.waveformR[i];
      traceM[i] = audio.waveformM[i];
    }
    writeIndex = 0;

    scopeGain = smoothToward(scopeGain, audio.waveformGain, 0.22f);
    rmsEnergy = smoothToward(rmsEnergy, audio.loudness, 0.28f);
    peakEnergy = smoothToward(peakEnergy, clamp01(audio.loudness * 0.70f + audio.transient * 0.42f), 0.28f);
    bassEnergy = smoothToward(bassEnergy, clamp01(audio.bass * 0.62f + audio.kick * 0.38f), 0.34f);
    float pulseTarget = clamp01(audio.bassTransient * 0.72f + audio.kick * audio.bassTransient * 0.26f + audio.transient * 0.18f);
    bassPulseEnergy = smoothToward(bassPulseEnergy, pulseTarget, pulseTarget > bassPulseEnergy ? 0.46f : 0.12f);
    trebleEnergy = smoothToward(trebleEnergy, audio.treble, 0.36f);
    stereoWidth = smoothToward(stereoWidth, audio.stereoWidth, 0.28f);
    stereoCorrelation = smoothToward(stereoCorrelation, audio.stereoCorrelation, 0.28f);
    zeroCrossEnergy = smoothToward(zeroCrossEnergy, audio.zeroCrossing, 0.30f);
    spectralTilt = smoothToward(spectralTilt, audio.spectralTilt, 0.28f);
    float colorTarget = wrap01(spectralTilt * 0.50f + stereoWidth * 0.12f + zeroCrossEnergy * 0.08f + bassPulseEnergy * 0.06f);
    colorBase = wrap01(colorBase * 0.90f + colorTarget * 0.10f + trebleEnergy * 0.003f);
  }

  float smoothToward(float current, float target, float amount) const {
    return current * (1.0f - amount) + target * amount;
  }

  void fadeCanvas() {
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      canvasR[i] *= WAVEFORM_PLASMA_TRAIL_FADE;
      canvasG[i] *= WAVEFORM_PLASMA_TRAIL_FADE;
      canvasB[i] *= WAVEFORM_PLASMA_TRAIL_FADE;

      if (canvasR[i] < 0.06f) canvasR[i] = 0.0f;
      if (canvasG[i] < 0.06f) canvasG[i] = 0.0f;
      if (canvasB[i] < 0.06f) canvasB[i] = 0.0f;
    }
  }

  void drawVectorscope() {
    float centerX = ((float)LED_DRIVER_GRID_WIDTH - 1.0f) * 0.5f;
    float centerY = ((float)LED_DRIVER_GRID_HEIGHT - 1.0f) * 0.5f;
    float widthScale = ((float)LED_DRIVER_GRID_WIDTH - 1.0f) * (0.19f + stereoWidth * 0.15f);
    float heightScale = ((float)LED_DRIVER_GRID_HEIGHT - 1.0f) * (0.32f + rmsEnergy * 0.08f);
    float monoLift = bassEnergy * 1.4f;
    float rotation = (1.0f - stereoCorrelation) * 0.54f + (spectralTilt - 0.5f) * 0.30f;
    float cosRot = cosf(rotation);
    float sinRot = sinf(rotation);

    int16_t lastX = -1000;
    int16_t lastY = -1000;

    for (uint16_t i = 0; i < WAVEFORM_PLASMA_TRACE_POINTS; i++) {
      uint16_t index = (writeIndex + i) & (WAVEFORM_PLASMA_TRACE_POINTS - 1);
      float age = (float)i / (float)(WAVEFORM_PLASMA_TRACE_POINTS - 1);
      float left = clampSigned(traceL[index] * scopeGain);
      float right = clampSigned(traceR[index] * scopeGain);
      float mono = clampSigned(traceM[index] * scopeGain);
      float side = clampSigned((right - left) * 0.5f);

      float plasmaWarp = sinf(plasmaPhase + age * 8.2f + side * 2.5f) * trebleEnergy * 0.65f;
      float rawX = side * widthScale + plasmaWarp;
      float rawY = mono * heightScale + monoLift * sinf(age * VISUALIZER_TWO_PI_F + plasmaPhase);
      float x = centerX + rawX * cosRot - rawY * sinRot * 0.45f;
      float y = centerY + rawX * sinRot * 0.18f + rawY * cosRot;
      int16_t px = (int16_t)roundf(x);
      int16_t py = (int16_t)roundf(y);

      float level = (0.22f + age * 0.70f) * (0.58f + peakEnergy * 0.60f);
      float tone = wrap01(colorBase + 0.08f + spectralTilt * 0.22f + stereoWidth * 0.11f + age * 0.08f);
      uint8_t r = 0;
      uint8_t g = 0;
      uint8_t b = 0;
      plasmaColor(tone, clamp01(level), trebleEnergy * age * 0.30f, r, g, b);

      if (lastX > -999) {
        drawLine(lastX, lastY, px, py, r, g, b);
      }
      drawGlow(px, py, 0.38f + bassEnergy * 0.50f + trebleEnergy * 0.25f, r, g, b);

      lastX = px;
      lastY = py;
    }
  }

  void drawMonoSpine() {
    float centerY = ((float)LED_DRIVER_GRID_HEIGHT - 1.0f) * 0.5f;
    float heightScale = ((float)LED_DRIVER_GRID_HEIGHT - 1.0f) * (0.26f + bassEnergy * 0.12f);
    int16_t lastX = -1000;
    int16_t lastY = -1000;

    for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
      uint16_t traceOffset = LED_DRIVER_GRID_WIDTH > 1
        ? ((uint32_t)x * (WAVEFORM_PLASMA_TRACE_POINTS - 1)) / (LED_DRIVER_GRID_WIDTH - 1)
        : (WAVEFORM_PLASMA_TRACE_POINTS - 1);
      uint16_t index = (writeIndex + traceOffset) & (WAVEFORM_PLASMA_TRACE_POINTS - 1);
      float mono = clampSigned(traceM[index] * scopeGain);
      float shimmer = sinf(plasmaPhase * 1.7f + (float)x * 0.55f) * trebleEnergy * 0.55f;
      int16_t y = (int16_t)roundf(centerY + mono * heightScale + shimmer);
      float tone = wrap01(colorBase + 0.56f + spectralTilt * 0.20f + (float)x / (float)LED_DRIVER_GRID_WIDTH * 0.12f);
      uint8_t r = 0;
      uint8_t g = 0;
      uint8_t b = 0;
      plasmaColor(tone, clamp01(0.28f + rmsEnergy * 0.48f), trebleEnergy * 0.16f, r, g, b);

      if (lastX > -999) {
        drawLine(lastX, lastY, x, y, r, g, b);
      }
      lastX = x;
      lastY = y;
    }
  }

  void drawBassPulse() {
    if (bassPulseEnergy < 0.16f) {
      return;
    }

    float centerX = ((float)LED_DRIVER_GRID_WIDTH - 1.0f) * 0.5f;
    float centerY = ((float)LED_DRIVER_GRID_HEIGHT - 1.0f) * 0.5f;
    float radius = 1.6f + bassPulseEnergy * 8.4f + bassEnergy * 2.0f + peakEnergy * 1.4f;
    float width = 0.58f + bassPulseEnergy * 1.10f;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    plasmaColor(wrap01(colorBase + 0.20f + spectralTilt * 0.12f), bassPulseEnergy * 0.46f, 0.0f, r, g, b);

    for (uint16_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
      for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
        float dx = (float)x - centerX;
        float dy = ((float)y - centerY) * 1.55f;
        float distance = sqrtf(dx * dx + dy * dy);
        float edge = 1.0f - fabsf(distance - radius) / width;
        if (edge <= 0.0f) {
          continue;
        }
        addPixelSafe(x, y, (uint8_t)(r * edge), (uint8_t)(g * edge), (uint8_t)(b * edge));
      }
    }
  }

  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t r, uint8_t g, uint8_t b) {
    int16_t dx = abs(x1 - x0);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t dy = -abs(y1 - y0);
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;

    while (true) {
      addPixelSafe(x0, y0, r, g, b);
      if (x0 == x1 && y0 == y1) {
        break;
      }

      int16_t e2 = err * 2;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }

  void drawGlow(int16_t x, int16_t y, float size, uint8_t r, uint8_t g, uint8_t b) {
    addPixelSafe(x, y, r, g, b);

    uint8_t sideR = (uint8_t)((float)r * size * 0.30f);
    uint8_t sideG = (uint8_t)((float)g * size * 0.30f);
    uint8_t sideB = (uint8_t)((float)b * size * 0.30f);

    if (size > 0.35f) {
      addPixelSafe(x - 1, y, sideR, sideG, sideB);
      addPixelSafe(x + 1, y, sideR, sideG, sideB);
    }
    if (size > 0.55f) {
      addPixelSafe(x, y - 1, sideR, sideG, sideB);
      addPixelSafe(x, y + 1, sideR, sideG, sideB);
    }
  }

  void plasmaColor(float tone, float level, float white, uint8_t& r, uint8_t& g, uint8_t& b) {
    tone = wrap01(tone);

    float baseR = 0.0f;
    float baseG = 0.0f;
    float baseB = 0.0f;

    if (tone < 0.20f) {
      float t = tone / 0.20f;
      baseR = 0.0f + 35.0f * t;
      baseG = 220.0f + (90.0f - 220.0f) * t;
      baseB = 255.0f;
    } else if (tone < 0.42f) {
      float t = (tone - 0.20f) / 0.22f;
      baseR = 35.0f + (145.0f - 35.0f) * t;
      baseG = 90.0f + (18.0f - 90.0f) * t;
      baseB = 255.0f;
    } else if (tone < 0.64f) {
      float t = (tone - 0.42f) / 0.22f;
      baseR = 145.0f + (255.0f - 145.0f) * t;
      baseG = 18.0f;
      baseB = 255.0f + (185.0f - 255.0f) * t;
    } else if (tone < 0.84f) {
      float t = (tone - 0.64f) / 0.20f;
      baseR = 255.0f;
      baseG = 18.0f + (135.0f - 18.0f) * t;
      baseB = 185.0f + (32.0f - 185.0f) * t;
    } else {
      float t = (tone - 0.84f) / 0.16f;
      baseR = 255.0f + (0.0f - 255.0f) * t;
      baseG = 135.0f + (220.0f - 135.0f) * t;
      baseB = 32.0f + (255.0f - 32.0f) * t;
    }

    float glow = 0.12f + level * 0.95f;
    float whiteBoost = clamp01(white) * 255.0f;
    r = clampChannel(baseR * glow + whiteBoost);
    g = clampChannel(baseG * glow + whiteBoost);
    b = clampChannel(baseB * glow + whiteBoost);
  }

  void addPixelSafe(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= LED_DRIVER_GRID_WIDTH || y < 0 || y >= LED_DRIVER_GRID_HEIGHT) {
      return;
    }

    uint16_t index = ledIndexXY((uint16_t)x, (uint16_t)y);
    canvasR[index] += (float)r;
    canvasG[index] += (float)g;
    canvasB[index] += (float)b;

    if (canvasR[index] > 255.0f) canvasR[index] = 255.0f;
    if (canvasG[index] > 255.0f) canvasG[index] = 255.0f;
    if (canvasB[index] > 255.0f) canvasB[index] = 255.0f;
  }

  float clampSigned(float value) const {
    if (value < -1.0f) {
      return -1.0f;
    }
    if (value > 1.0f) {
      return 1.0f;
    }
    return value;
  }

  uint8_t clampChannel(float value) const {
    if (value < 0.0f) {
      return 0;
    }
    if (value > 255.0f) {
      return 255;
    }
    return (uint8_t)value;
  }
};
