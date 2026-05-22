#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr uint16_t WAVEFORM_PLASMA_TRACE_POINTS = 128;
static constexpr uint8_t WAVEFORM_PLASMA_CAPTURE_STRIDE = 8;
static constexpr float WAVEFORM_PLASMA_TRAIL_FADE = 0.75f;
static constexpr float WAVEFORM_PLASMA_BASS_ALPHA = 0.010f;
static constexpr float WAVEFORM_PLASMA_TREBLE_ALPHA = 0.24f;

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
    captureCounter = 0;
    dcL = 0.0f;
    dcR = 0.0f;
    bassLow = 0.0f;
    trebleLow = 0.0f;
    previousMono = 0.0f;
    peakAverage = 0.020f;
    scopeGain = 5.0f;
    rmsEnergy = 0.0f;
    peakEnergy = 0.0f;
    bassEnergy = 0.0f;
    trebleEnergy = 0.0f;
    stereoWidth = 0.0f;
    stereoCorrelation = 0.0f;
    zeroCrossEnergy = 0.0f;
    spectralTilt = 0.5f;
    plasmaPhase = 0.0f;
    ditherFrame = 0;
  }

  void processAudio(const int32_t* interleavedStereo32, uint16_t frames) override {
    if (frames == 0) {
      return;
    }

    float rmsSum = 0.0f;
    float peak = 0.0f;
    float bassSum = 0.0f;
    float trebleSum = 0.0f;
    float sideSum = 0.0f;
    float monoSum = 0.0f;
    float leftPower = 0.0f;
    float rightPower = 0.0f;
    float crossPower = 0.0f;
    uint16_t crossings = 0;

    for (uint16_t frame = 0; frame < frames; frame++) {
      int32_t s24L = interleavedStereo32[frame * 2] >> 8;
      int32_t s24R = interleavedStereo32[frame * 2 + 1] >> 8;
      float left = (float)s24L / 8388608.0f;
      float right = (float)s24R / 8388608.0f;

      dcL = dcL * 0.995f + left * 0.005f;
      dcR = dcR * 0.995f + right * 0.005f;
      left -= dcL;
      right -= dcR;

      float mono = (left + right) * 0.5f;
      float side = (right - left) * 0.5f;
      bassLow += (mono - bassLow) * WAVEFORM_PLASMA_BASS_ALPHA;
      trebleLow += (mono - trebleLow) * WAVEFORM_PLASMA_TREBLE_ALPHA;
      float trebleHigh = mono - trebleLow;

      float absMono = fabsf(mono);
      if (absMono > peak) {
        peak = absMono;
      }
      rmsSum += mono * mono;
      bassSum += fabsf(bassLow);
      trebleSum += fabsf(trebleHigh);
      sideSum += fabsf(side);
      monoSum += absMono;
      leftPower += left * left;
      rightPower += right * right;
      crossPower += left * right;

      if ((mono >= 0.0f && previousMono < 0.0f) || (mono < 0.0f && previousMono >= 0.0f)) {
        crossings++;
      }
      previousMono = mono;

      captureCounter++;
      if (captureCounter >= WAVEFORM_PLASMA_CAPTURE_STRIDE) {
        captureCounter = 0;
        traceL[writeIndex] = clampSigned(left);
        traceR[writeIndex] = clampSigned(right);
        traceM[writeIndex] = clampSigned(mono);
        writeIndex = (writeIndex + 1) & (WAVEFORM_PLASMA_TRACE_POINTS - 1);
      }
    }

    float invFrames = 1.0f / (float)frames;
    float rms = sqrtf(rmsSum * invFrames);
    float bassBlock = bassSum * invFrames;
    float trebleBlock = trebleSum * invFrames;
    float sideBlock = sideSum * invFrames;
    float monoBlock = monoSum * invFrames;
    float widthBlock = clamp01(sideBlock / (monoBlock + sideBlock + 0.000001f) * 2.0f);
    float powerDenom = sqrtf(leftPower * rightPower) + 0.000001f;
    float correlation = crossPower / powerDenom;
    if (correlation < -1.0f) correlation = -1.0f;
    if (correlation > 1.0f) correlation = 1.0f;

    if (peak > peakAverage) {
      peakAverage = peakAverage * 0.86f + peak * 0.14f;
    } else {
      peakAverage = peakAverage * 0.995f + peak * 0.005f;
    }
    if (peakAverage < 0.012f) {
      peakAverage = 0.012f;
    }

    float gainTarget = 0.72f / peakAverage;
    if (gainTarget < 1.2f) gainTarget = 1.2f;
    if (gainTarget > 18.0f) gainTarget = 18.0f;
    scopeGain = scopeGain * 0.92f + gainTarget * 0.08f;

    float bassNorm = clamp01(bassBlock / (peakAverage * 0.46f + 0.00001f));
    float trebleNorm = clamp01(trebleBlock / (peakAverage * 0.54f + 0.00001f));
    float tiltBlock = trebleNorm / (bassNorm + trebleNorm + 0.0001f);
    float zeroBlock = clamp01((float)crossings / (float)frames * 32.0f);

    rmsEnergy = rmsEnergy * 0.76f + clamp01(rms / (peakAverage + 0.00001f)) * 0.24f;
    peakEnergy = peakEnergy * 0.76f + clamp01(peak / (peakAverage + 0.00001f)) * 0.24f;
    bassEnergy = bassEnergy * 0.72f + bassNorm * 0.28f;
    trebleEnergy = trebleEnergy * 0.68f + trebleNorm * 0.32f;
    stereoWidth = stereoWidth * 0.76f + widthBlock * 0.24f;
    stereoCorrelation = stereoCorrelation * 0.78f + correlation * 0.22f;
    zeroCrossEnergy = zeroCrossEnergy * 0.74f + zeroBlock * 0.26f;
    spectralTilt = spectralTilt * 0.78f + tiltBlock * 0.22f;
  }

  void render(Adafruit_NeoPixel& pixels) override {
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
  uint8_t captureCounter = 0;
  float dcL = 0.0f;
  float dcR = 0.0f;
  float bassLow = 0.0f;
  float trebleLow = 0.0f;
  float previousMono = 0.0f;
  float peakAverage = 0.020f;
  float scopeGain = 5.0f;
  float rmsEnergy = 0.0f;
  float peakEnergy = 0.0f;
  float bassEnergy = 0.0f;
  float trebleEnergy = 0.0f;
  float stereoWidth = 0.0f;
  float stereoCorrelation = 0.0f;
  float zeroCrossEnergy = 0.0f;
  float spectralTilt = 0.5f;
  float plasmaPhase = 0.0f;
  uint8_t ditherFrame = 0;

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
      float tone = wrap01(0.54f + spectralTilt * 0.28f + stereoWidth * 0.11f + age * 0.08f);
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

    for (uint8_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
      uint16_t traceOffset = ((uint32_t)x * (WAVEFORM_PLASMA_TRACE_POINTS - 1)) / (LED_DRIVER_GRID_WIDTH - 1);
      uint16_t index = (writeIndex + traceOffset) & (WAVEFORM_PLASMA_TRACE_POINTS - 1);
      float mono = clampSigned(traceM[index] * scopeGain);
      float shimmer = sinf(plasmaPhase * 1.7f + (float)x * 0.55f) * trebleEnergy * 0.55f;
      int16_t y = (int16_t)roundf(centerY + mono * heightScale + shimmer);
      float tone = wrap01(0.06f + spectralTilt * 0.20f + (float)x / (float)LED_DRIVER_GRID_WIDTH * 0.12f);
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
    if (bassEnergy < 0.18f) {
      return;
    }

    float centerX = ((float)LED_DRIVER_GRID_WIDTH - 1.0f) * 0.5f;
    float centerY = ((float)LED_DRIVER_GRID_HEIGHT - 1.0f) * 0.5f;
    float radius = 2.0f + bassEnergy * 7.5f + peakEnergy * 2.2f;
    float width = 0.72f + bassEnergy * 1.0f;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    plasmaColor(wrap01(0.72f + spectralTilt * 0.12f), bassEnergy * 0.42f, 0.0f, r, g, b);

    for (uint8_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
      for (uint8_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
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

    uint16_t index = ledIndexXY((uint8_t)x, (uint8_t)y);
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
