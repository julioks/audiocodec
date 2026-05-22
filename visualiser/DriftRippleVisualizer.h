#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr uint8_t DRIFT_RIPPLE_MAX_RIPPLES = 16;
static constexpr float DRIFT_RIPPLE_TRAIL_FADE = 0.82f;
static constexpr float DRIFT_RIPPLE_BASS_ALPHA = 0.012f;
static constexpr float DRIFT_RIPPLE_TREBLE_ALPHA = 0.22f;
static constexpr float DRIFT_RIPPLE_HIT_THRESHOLD = 0.55f;
static constexpr uint16_t DRIFT_RIPPLE_MIN_SPAWN_MS = 105;
static constexpr uint16_t DRIFT_RIPPLE_EXTRA_QUIET_SPAWN_MS = 105;

class DriftRippleVisualizer : public AudioVisualizer {
public:
  const char* name() const override {
    return "drift-ripples";
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

    for (uint8_t i = 0; i < DRIFT_RIPPLE_MAX_RIPPLES; i++) {
      ripples[i].active = false;
    }

    bassLow = 0.0f;
    trebleLow = 0.0f;
    dcEstimate = 0.0f;
    bassEnergy = 0.0f;
    trebleEnergy = 0.0f;
    loudEnergy = 0.0f;
    stereoBalance = 0.0f;
    bassTransientEnergy = 0.0f;
    spectralTilt = 0.5f;
    spawnXNorm = 0.5f;
    spawnYNorm = 0.35f;
    motionEnergy = 0.0f;
    bassAverage = 0.00008f;
    bassPeak = 0.00012f;
    treblePeak = 0.00012f;
    pendingBassHit = 0.0f;
    treblePhase = 0.0f;
    lastSpawnMs = 0;
    rngState = 0x5A17C0DEUL;
    ditherFrame = 0;
  }

  void processAudio(const int32_t* interleavedStereo32, uint16_t frames) override {
    if (frames == 0) {
      return;
    }

    float bassSum = 0.0f;
    float trebleSum = 0.0f;
    float loudSum = 0.0f;
    float leftSum = 0.0f;
    float rightSum = 0.0f;
    uint32_t audioSeed = 0;

    for (uint16_t frame = 0; frame < frames; frame++) {
      int32_t s24L = interleavedStereo32[frame * 2] >> 8;
      int32_t s24R = interleavedStereo32[frame * 2 + 1] >> 8;
      float left = (float)s24L / 8388608.0f;
      float right = (float)s24R / 8388608.0f;
      float mono = (left + right) * 0.5f;

      dcEstimate = dcEstimate * 0.995f + mono * 0.005f;
      mono -= dcEstimate;

      bassLow += (mono - bassLow) * DRIFT_RIPPLE_BASS_ALPHA;
      trebleLow += (mono - trebleLow) * DRIFT_RIPPLE_TREBLE_ALPHA;

      float trebleHigh = mono - trebleLow;
      bassSum += fabsf(bassLow);
      trebleSum += fabsf(trebleHigh);
      loudSum += fabsf(mono);
      leftSum += fabsf(left);
      rightSum += fabsf(right);

      if ((frame & 0x0F) == 0) {
        audioSeed ^= (uint32_t)s24L + ((uint32_t)s24R << 7) + ((uint32_t)frame << 17);
      }
    }

    rngState ^= audioSeed + 0x9E3779B9UL + (rngState << 6) + (rngState >> 2);

    float invFrames = 1.0f / (float)frames;
    float bassBlock = bassSum * invFrames;
    float trebleBlock = trebleSum * invFrames;
    float loudBlock = loudSum * invFrames;
    float leftBlock = leftSum * invFrames;
    float rightBlock = rightSum * invFrames;

    float oldBassAverage = bassAverage;
    bassAverage = bassAverage * 0.970f + bassBlock * 0.030f;

    if (bassBlock > bassPeak) {
      bassPeak = bassBlock;
    } else {
      bassPeak *= 0.996f;
    }
    if (bassPeak < 0.00012f) {
      bassPeak = 0.00012f;
    }

    if (trebleBlock > treblePeak) {
      treblePeak = trebleBlock;
    } else {
      treblePeak *= 0.996f;
    }
    if (treblePeak < 0.00012f) {
      treblePeak = 0.00012f;
    }

    float bassNorm = clamp01(bassBlock / (bassPeak * 0.88f + 0.00001f));
    float trebleNorm = clamp01(trebleBlock / (treblePeak * 0.82f + 0.00001f));
    float bassRange = bassPeak - oldBassAverage;
    if (bassRange < 0.00002f) {
      bassRange = 0.00002f;
    }

    float bassTransient = clamp01((bassBlock - oldBassAverage * 1.42f) / (bassRange * 0.82f));
    float hit = 0.0f;
    if (bassTransient > 0.18f && bassNorm > 0.38f) {
      hit = clamp01((bassTransient - 0.18f) * 1.22f) * (0.72f + bassNorm * 0.28f);
    }
    if (hit > pendingBassHit) {
      pendingBassHit = hit;
    }

    float loudNorm = clamp01(loudBlock / (bassPeak + treblePeak + 0.00002f));
    float blockTilt = trebleNorm / (bassNorm + trebleNorm + 0.0001f);

    bassEnergy = bassEnergy * 0.72f + bassNorm * 0.28f;
    trebleEnergy = trebleEnergy * 0.70f + trebleNorm * 0.30f;
    loudEnergy = loudEnergy * 0.76f + loudNorm * 0.24f;
    bassTransientEnergy = bassTransientEnergy * 0.64f + bassTransient * 0.36f;
    spectralTilt = spectralTilt * 0.78f + blockTilt * 0.22f;
    motionEnergy = motionEnergy * 0.70f + clamp01(bassTransient * 0.45f + trebleNorm * 0.35f + loudNorm * 0.20f) * 0.30f;

    float lrTotal = leftBlock + rightBlock + 0.000001f;
    float blockBalance = (rightBlock - leftBlock) / lrTotal;
    stereoBalance = stereoBalance * 0.82f + blockBalance * 0.18f;

    float xTarget = clamp01(0.5f + stereoBalance * 0.42f + (trebleNorm - bassNorm) * 0.07f);
    float yTarget = clamp01(0.10f + blockTilt * 0.72f + loudNorm * 0.12f);
    spawnXNorm = spawnXNorm * 0.72f + xTarget * 0.28f;
    spawnYNorm = spawnYNorm * 0.72f + yTarget * 0.28f;
  }

  void render(Adafruit_NeoPixel& pixels) override {
    fadeCanvas();

    uint32_t now = millis();
    maybeSpawnRipples(now);

    treblePhase += 0.18f + trebleEnergy * 0.45f;

    for (uint8_t i = 0; i < DRIFT_RIPPLE_MAX_RIPPLES; i++) {
      if (ripples[i].active) {
        drawRipple(ripples[i]);
      }
    }

    for (uint16_t i = 0; i < LED_COUNT; i++) {
      pixels.setPixelColor(
        i,
        visualizerColor(pixels, i, canvasR[i], canvasG[i], canvasB[i], ditherFrame)
      );
    }

    ditherFrame++;
    pendingBassHit *= 0.72f;
  }

private:
  struct Ripple {
    bool active;
    float x;
    float y;
    float radius;
    float startRadius;
    float speed;
    float strength;
    float ringWidth;
    float distortion;
    float maxRadius;
    float maxAge;
    float age;
    float seed;
    float tone;
  };

  Ripple ripples[DRIFT_RIPPLE_MAX_RIPPLES];
  float canvasR[LED_COUNT];
  float canvasG[LED_COUNT];
  float canvasB[LED_COUNT];
  float bassLow = 0.0f;
  float trebleLow = 0.0f;
  float dcEstimate = 0.0f;
  float bassEnergy = 0.0f;
  float trebleEnergy = 0.0f;
  float loudEnergy = 0.0f;
  float stereoBalance = 0.0f;
  float bassTransientEnergy = 0.0f;
  float spectralTilt = 0.5f;
  float spawnXNorm = 0.5f;
  float spawnYNorm = 0.35f;
  float motionEnergy = 0.0f;
  float bassAverage = 0.00008f;
  float bassPeak = 0.00012f;
  float treblePeak = 0.00012f;
  float pendingBassHit = 0.0f;
  float treblePhase = 0.0f;
  uint32_t lastSpawnMs = 0;
  uint32_t rngState = 0x5A17C0DEUL;
  uint8_t ditherFrame = 0;

  void fadeCanvas() {
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      canvasR[i] *= DRIFT_RIPPLE_TRAIL_FADE;
      canvasG[i] *= DRIFT_RIPPLE_TRAIL_FADE;
      canvasB[i] *= DRIFT_RIPPLE_TRAIL_FADE;

      if (canvasR[i] < 0.06f) canvasR[i] = 0.0f;
      if (canvasG[i] < 0.06f) canvasG[i] = 0.0f;
      if (canvasB[i] < 0.06f) canvasB[i] = 0.0f;
    }
  }

  void maybeSpawnRipples(uint32_t now) {
    uint16_t minSpawnMs = DRIFT_RIPPLE_MIN_SPAWN_MS
      + (uint16_t)((1.0f - bassEnergy) * (float)DRIFT_RIPPLE_EXTRA_QUIET_SPAWN_MS);

    if (pendingBassHit < DRIFT_RIPPLE_HIT_THRESHOLD || now - lastSpawnMs < minSpawnMs) {
      return;
    }

    float strength = pendingBassHit;
    uint8_t spawnCount = 1;
    if (strength > 0.92f) {
      spawnCount = 2;
    }

    for (uint8_t i = 0; i < spawnCount; i++) {
      float rippleStrength = strength * (1.0f - (float)i * 0.18f);
      spawnRipple(clamp01(rippleStrength));
    }

    lastSpawnMs = now;
    pendingBassHit = 0.0f;
  }

  void spawnRipple(float strength) {
    uint8_t slot = chooseRippleSlot();
    Ripple& ripple = ripples[slot];

    float audioWeight = 0.62f + strength * 0.24f;
    float jitter = 0.10f + trebleEnergy * 0.22f + motionEnergy * 0.08f;
    float xNorm = spawnXNorm + (nextRandomFloat() - 0.5f) * jitter;
    float yNorm = spawnYNorm + (nextRandomFloat() - 0.5f) * jitter;

    xNorm = xNorm * audioWeight + nextRandomFloat() * (1.0f - audioWeight);
    yNorm = yNorm * audioWeight + nextRandomFloat() * (1.0f - audioWeight);
    xNorm = clamp01(xNorm);
    yNorm = clamp01(yNorm);

    if (xNorm > 0.38f && xNorm < 0.62f && yNorm > 0.38f && yNorm < 0.62f) {
      xNorm += (xNorm < 0.5f) ? -0.23f : 0.23f;
      yNorm += (yNorm < 0.5f) ? -0.17f : 0.17f;
      xNorm = clamp01(xNorm);
      yNorm = clamp01(yNorm);
    }

    ripple.x = xNorm * (float)(LED_DRIVER_GRID_WIDTH - 1);
    ripple.y = yNorm * (float)(LED_DRIVER_GRID_HEIGHT - 1);
    ripple.radius = 0.08f + strength * 0.46f + bassTransientEnergy * 0.36f;
    ripple.startRadius = ripple.radius;
    ripple.speed = 0.26f + strength * 0.24f + trebleEnergy * 0.34f + loudEnergy * 0.18f + motionEnergy * 0.20f;
    ripple.strength = 0.42f + strength * 0.68f;
    ripple.ringWidth = 0.30f + bassEnergy * 0.34f + loudEnergy * 0.22f;
    ripple.distortion = 0.05f + trebleEnergy * 0.36f + motionEnergy * 0.16f;
    ripple.maxRadius = 5.5f + strength * 8.5f + bassEnergy * 4.5f + loudEnergy * 2.0f - spectralTilt * 1.6f;
    if (ripple.maxRadius < 5.0f) ripple.maxRadius = 5.0f;
    if (ripple.maxRadius > 22.0f) ripple.maxRadius = 22.0f;
    ripple.maxAge = (ripple.maxRadius - ripple.radius) / (ripple.speed + 0.001f) + 3.0f;
    ripple.age = 0.0f;
    ripple.seed = nextRandomFloat() * VISUALIZER_TWO_PI_F;
    ripple.tone = chooseRippleTone(strength);
    ripple.active = true;
  }

  void drawRipple(Ripple& ripple) {
    ripple.radius += ripple.speed * (0.74f + loudEnergy * 0.42f);
    ripple.age += 1.0f;

    if (ripple.age >= ripple.maxAge || ripple.radius > ripple.maxRadius) {
      ripple.active = false;
      return;
    }

    float radiusProgress = clamp01((ripple.radius - ripple.startRadius) / (ripple.maxRadius - ripple.startRadius + 0.001f));
    float growthFade = 1.0f - radiusProgress;
    float waterFade = growthFade * growthFade * (3.0f - 2.0f * growthFade);
    float life = waterFade * (0.72f + growthFade * 0.28f);
    float startupBoost = 0.92f + growthFade * 0.18f;
    float ringWidth = ripple.ringWidth + trebleEnergy * 0.18f;
    float distortionAmount = ripple.distortion + trebleEnergy * 0.18f;

    for (uint8_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
      for (uint8_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
        float dx = (float)x - ripple.x;
        float dy = (float)y - ripple.y;
        float distance = sqrtf(dx * dx + dy * dy);
        float maxReach = ringWidth + distortionAmount * 2.0f;

        if (fabsf(distance - ripple.radius) > maxReach) {
          continue;
        }

        float distortion =
          sinf(dx * 1.35f + dy * 0.74f + treblePhase + ripple.seed) +
          sinf((dx - dy) * 1.08f - treblePhase * 1.31f + ripple.seed * 0.63f);
        float warpedDistance = distance + distortion * distortionAmount;
        float edge = 1.0f - fabsf(warpedDistance - ripple.radius) / ringWidth;

        if (edge <= 0.0f) {
          continue;
        }

        edge = edge * edge * (3.0f - 2.0f * edge);
        float level = edge * life * startupBoost * ripple.strength * (0.62f + bassEnergy * 0.46f + motionEnergy * 0.22f);
        if (level <= 0.004f) {
          continue;
        }

        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        float tone = wrap01(ripple.tone + trebleEnergy * 0.10f + edge * 0.055f + sinf(treblePhase + ripple.seed) * 0.035f);
        float white = edge * trebleEnergy * 0.08f;
        synthwaveColor(tone, clamp01(level), clamp01(white), r, g, b);
        addPixel(ledIndexXY(x, y), r, g, b);
      }
    }
  }

  void synthwaveColor(float tone, float level, float white, uint8_t& r, uint8_t& g, uint8_t& b) {
    tone = wrap01(tone);

    float baseR = 0.0f;
    float baseG = 0.0f;
    float baseB = 0.0f;

    if (tone < 0.17f) {
      float t = tone / 0.17f;
      baseR = 0.0f + 24.0f * t;
      baseG = 210.0f + (72.0f - 210.0f) * t;
      baseB = 255.0f;
    } else if (tone < 0.34f) {
      float t = (tone - 0.17f) / 0.17f;
      baseR = 24.0f + (118.0f - 24.0f) * t;
      baseG = 72.0f + (20.0f - 72.0f) * t;
      baseB = 255.0f;
    } else if (tone < 0.52f) {
      float t = (tone - 0.34f) / 0.18f;
      baseR = 118.0f + (255.0f - 118.0f) * t;
      baseG = 20.0f;
      baseB = 255.0f + (220.0f - 255.0f) * t;
    } else if (tone < 0.70f) {
      float t = (tone - 0.52f) / 0.18f;
      baseR = 255.0f;
      baseG = 20.0f + (45.0f - 20.0f) * t;
      baseB = 220.0f + (115.0f - 220.0f) * t;
    } else if (tone < 0.86f) {
      float t = (tone - 0.70f) / 0.16f;
      baseR = 255.0f;
      baseG = 45.0f + (150.0f - 45.0f) * t;
      baseB = 115.0f + (35.0f - 115.0f) * t;
    } else {
      float t = (tone - 0.86f) / 0.14f;
      baseR = 255.0f + (0.0f - 255.0f) * t;
      baseG = 150.0f + (210.0f - 150.0f) * t;
      baseB = 35.0f + (255.0f - 35.0f) * t;
    }

    float glow = 0.10f + level * 0.70f;
    float whiteBoost = white * 255.0f;
    r = clampChannel(baseR * glow + whiteBoost);
    g = clampChannel(baseG * glow + whiteBoost);
    b = clampChannel(baseB * glow + whiteBoost);
  }

  float chooseRippleTone(float strength) {
    float roll = nextRandomFloat();
    float baseTone = 0.0f;

    if (roll < 0.22f) {
      baseTone = 0.02f + nextRandomFloat() * 0.14f;
    } else if (roll < 0.45f) {
      baseTone = 0.20f + nextRandomFloat() * 0.14f;
    } else if (roll < 0.68f) {
      baseTone = 0.40f + nextRandomFloat() * 0.16f;
    } else if (roll < 0.88f) {
      baseTone = 0.58f + nextRandomFloat() * 0.18f;
    } else {
      baseTone = 0.78f + nextRandomFloat() * 0.12f;
    }

    return wrap01(baseTone + trebleEnergy * 0.12f + strength * 0.06f + stereoBalance * 0.05f);
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

  void addPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    canvasR[index] += (float)r;
    canvasG[index] += (float)g;
    canvasB[index] += (float)b;

    if (canvasR[index] > 255.0f) canvasR[index] = 255.0f;
    if (canvasG[index] > 255.0f) canvasG[index] = 255.0f;
    if (canvasB[index] > 255.0f) canvasB[index] = 255.0f;
  }

  uint8_t chooseRippleSlot() {
    uint8_t slot = 0;
    float oldestScore = -1.0f;

    for (uint8_t i = 0; i < DRIFT_RIPPLE_MAX_RIPPLES; i++) {
      if (!ripples[i].active) {
        return i;
      }

      float score = ripples[i].age / ripples[i].maxAge;
      if (score > oldestScore) {
        oldestScore = score;
        slot = i;
      }
    }

    return slot;
  }

  uint32_t nextRandomU32() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
  }

  float nextRandomFloat() {
    return (float)(nextRandomU32() & 0x00FFFFFFUL) / 16777215.0f;
  }
};
