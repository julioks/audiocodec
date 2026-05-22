#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr uint8_t SPARK_PARTICLE_MAX_PARTICLES = 72;
static constexpr float SPARK_PARTICLE_TRAIL_FADE = 0.62f;
static constexpr float SPARK_PARTICLE_BASS_ALPHA = 0.010f;
static constexpr float SPARK_PARTICLE_TREBLE_ALPHA = 0.24f;
static constexpr float SPARK_PARTICLE_HIT_THRESHOLD = 0.50f;
static constexpr uint16_t SPARK_PARTICLE_BASS_BURST_MS = 55;

class SparkParticleVisualizer : public AudioVisualizer {
public:
  const char* name() const override {
    return "spark-particles";
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

    for (uint8_t i = 0; i < SPARK_PARTICLE_MAX_PARTICLES; i++) {
      particles[i].active = false;
    }

    bassLow = 0.0f;
    trebleLow = 0.0f;
    dcEstimate = 0.0f;
    bassAverage = 0.00008f;
    bassPeak = 0.00012f;
    treblePeak = 0.00012f;
    bassEnergy = 0.0f;
    trebleEnergy = 0.0f;
    loudEnergy = 0.0f;
    motionEnergy = 0.0f;
    stereoBalance = 0.0f;
    spectralTilt = 0.5f;
    audioFlowX = 0.0f;
    audioFlowY = 0.0f;
    audioCurl = 0.0f;
    audioChaos = 0.0f;
    colorBase = 0.0f;
    previousMonoForFlow = 0.0f;
    previousSideForFlow = 0.0f;
    emitterXNorm = 0.5f;
    emitterYNorm = 0.50f;
    pendingBassBurst = 0.0f;
    pendingTrebleSpark = 0.0f;
    lastBassBurstMs = 0;
    rngState = 0xA73C9E2DUL;
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
    float flowXSum = 0.0f;
    float flowYSum = 0.0f;
    float slopeSum = 0.0f;
    float curlSum = 0.0f;
    float signedWaveSum = 0.0f;
    uint32_t audioSeed = 0;

    for (uint16_t frame = 0; frame < frames; frame++) {
      int32_t s24L = interleavedStereo32[frame * 2] >> 8;
      int32_t s24R = interleavedStereo32[frame * 2 + 1] >> 8;
      float left = (float)s24L / 8388608.0f;
      float right = (float)s24R / 8388608.0f;
      float mono = (left + right) * 0.5f;
      float side = (right - left) * 0.5f;

      dcEstimate = dcEstimate * 0.995f + mono * 0.005f;
      mono -= dcEstimate;

      bassLow += (mono - bassLow) * SPARK_PARTICLE_BASS_ALPHA;
      trebleLow += (mono - trebleLow) * SPARK_PARTICLE_TREBLE_ALPHA;

      float monoDelta = mono - previousMonoForFlow;
      float sideDelta = side - previousSideForFlow;
      float trebleHigh = mono - trebleLow;
      bassSum += fabsf(bassLow);
      trebleSum += fabsf(trebleHigh);
      loudSum += fabsf(mono);
      leftSum += fabsf(left);
      rightSum += fabsf(right);
      flowXSum += side * 0.34f + sideDelta * 3.20f;
      flowYSum += mono * 0.22f + monoDelta * 3.60f;
      slopeSum += fabsf(monoDelta) + fabsf(sideDelta) * 0.70f;
      curlSum += mono * sideDelta - side * monoDelta;
      signedWaveSum += mono;
      previousMonoForFlow = mono;
      previousSideForFlow = side;

      if ((frame & 0x0F) == 0) {
        audioSeed ^= (uint32_t)s24L + ((uint32_t)s24R << 9) + ((uint32_t)frame << 18);
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
      treblePeak *= 0.994f;
    }
    if (treblePeak < 0.00012f) {
      treblePeak = 0.00012f;
    }

    float bassNorm = clamp01(bassBlock / (bassPeak * 0.88f + 0.00001f));
    float trebleNorm = clamp01(trebleBlock / (treblePeak * 0.82f + 0.00001f));
    float loudNorm = clamp01(loudBlock / (bassPeak + treblePeak + 0.00002f));
    float bassRange = bassPeak - oldBassAverage;
    if (bassRange < 0.00002f) {
      bassRange = 0.00002f;
    }

    float bassTransient = clamp01((bassBlock - oldBassAverage * 1.34f) / (bassRange * 0.78f));
    float bassHit = 0.0f;
    if (bassTransient > 0.16f && bassNorm > 0.34f) {
      bassHit = clamp01((bassTransient - 0.16f) * 1.28f) * (0.70f + bassNorm * 0.30f);
    }
    if (bassHit > pendingBassBurst) {
      pendingBassBurst = bassHit;
    }

    float trebleSpark = clamp01((trebleNorm - 0.44f) * 1.65f);
    if (trebleSpark > pendingTrebleSpark) {
      pendingTrebleSpark = trebleSpark;
    }

    float lrTotal = leftBlock + rightBlock + 0.000001f;
    float blockBalance = (rightBlock - leftBlock) / lrTotal;
    float blockTilt = trebleNorm / (bassNorm + trebleNorm + 0.0001f);
    float flowDenom = loudBlock + trebleBlock + 0.00002f;
    float blockFlowX = clampSigned((flowXSum * invFrames) / flowDenom * 2.2f);
    float blockFlowY = clampSigned((flowYSum * invFrames) / flowDenom * 2.2f);
    float blockChaos = clamp01((slopeSum * invFrames) / flowDenom * 4.5f);
    float blockCurl = clampSigned((curlSum * invFrames) / (flowDenom * flowDenom + 0.000001f) * 0.30f);
    float waveBias = clampSigned((signedWaveSum * invFrames) / (loudBlock + 0.000001f));

    bassEnergy = bassEnergy * 0.72f + bassNorm * 0.28f;
    trebleEnergy = trebleEnergy * 0.68f + trebleNorm * 0.32f;
    loudEnergy = loudEnergy * 0.76f + loudNorm * 0.24f;
    motionEnergy = motionEnergy * 0.68f + clamp01(bassTransient * 0.38f + trebleNorm * 0.42f + loudNorm * 0.20f) * 0.32f;
    stereoBalance = stereoBalance * 0.80f + blockBalance * 0.20f;
    spectralTilt = spectralTilt * 0.76f + blockTilt * 0.24f;
    audioFlowX = audioFlowX * 0.68f + blockFlowX * 0.32f;
    audioFlowY = audioFlowY * 0.68f + blockFlowY * 0.32f;
    audioCurl = audioCurl * 0.76f + blockCurl * 0.24f;
    audioChaos = audioChaos * 0.70f + blockChaos * 0.30f;
    colorBase = wrap01(colorBase * 0.86f + wrap01(blockTilt * 0.52f + blockBalance * 0.16f + blockCurl * 0.10f + waveBias * 0.08f) * 0.14f);

    float xTarget = clamp01(0.5f + stereoBalance * 0.46f + (trebleNorm - bassNorm) * 0.05f);
    float yTarget = clamp01(0.22f + blockTilt * 0.56f + loudNorm * 0.10f);
    emitterXNorm = emitterXNorm * 0.70f + xTarget * 0.30f;
    emitterYNorm = emitterYNorm * 0.70f + yTarget * 0.30f;
  }

  void render(Adafruit_NeoPixel& pixels) override {
    fadeCanvas();

    uint32_t now = millis();
    spawnFromAudio(now);
    updateParticles();

    for (uint16_t i = 0; i < LED_COUNT; i++) {
      pixels.setPixelColor(i, visualizerColor(pixels, i, canvasR[i], canvasG[i], canvasB[i], ditherFrame));
    }

    ditherFrame++;
    pendingBassBurst *= 0.62f;
    pendingTrebleSpark *= 0.70f;
  }

private:
  struct Particle {
    bool active;
    float x;
    float y;
    float vx;
    float vy;
    float life;
    float maxLife;
    float tone;
    float toneVelocity;
    float energy;
    float size;
    float curl;
    float turbulence;
  };

  Particle particles[SPARK_PARTICLE_MAX_PARTICLES];
  float canvasR[LED_COUNT];
  float canvasG[LED_COUNT];
  float canvasB[LED_COUNT];
  float bassLow = 0.0f;
  float trebleLow = 0.0f;
  float dcEstimate = 0.0f;
  float bassAverage = 0.00008f;
  float bassPeak = 0.00012f;
  float treblePeak = 0.00012f;
  float bassEnergy = 0.0f;
  float trebleEnergy = 0.0f;
  float loudEnergy = 0.0f;
  float motionEnergy = 0.0f;
  float stereoBalance = 0.0f;
  float spectralTilt = 0.5f;
  float audioFlowX = 0.0f;
  float audioFlowY = 0.0f;
  float audioCurl = 0.0f;
  float audioChaos = 0.0f;
  float colorBase = 0.0f;
  float previousMonoForFlow = 0.0f;
  float previousSideForFlow = 0.0f;
  float emitterXNorm = 0.5f;
  float emitterYNorm = 0.50f;
  float pendingBassBurst = 0.0f;
  float pendingTrebleSpark = 0.0f;
  uint32_t lastBassBurstMs = 0;
  uint32_t rngState = 0xA73C9E2DUL;
  uint8_t ditherFrame = 0;

  void fadeCanvas() {
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      canvasR[i] *= SPARK_PARTICLE_TRAIL_FADE;
      canvasG[i] *= SPARK_PARTICLE_TRAIL_FADE;
      canvasB[i] *= SPARK_PARTICLE_TRAIL_FADE;

      if (canvasR[i] < 0.06f) canvasR[i] = 0.0f;
      if (canvasG[i] < 0.06f) canvasG[i] = 0.0f;
      if (canvasB[i] < 0.06f) canvasB[i] = 0.0f;
    }
  }

  void spawnFromAudio(uint32_t now) {
    if (pendingBassBurst >= SPARK_PARTICLE_HIT_THRESHOLD && now - lastBassBurstMs >= SPARK_PARTICLE_BASS_BURST_MS) {
      uint8_t count = 2 + (uint8_t)(pendingBassBurst * 6.0f) + (uint8_t)(bassEnergy * 3.0f) + (uint8_t)(audioChaos * 3.0f);
      if (count > 12) {
        count = 12;
      }

      for (uint8_t i = 0; i < count; i++) {
        spawnParticle(pendingBassBurst, true);
      }

      lastBassBurstMs = now;
      pendingBassBurst = 0.0f;
    }

    float trebleChance = pendingTrebleSpark * (0.34f + trebleEnergy * 0.54f + audioChaos * 0.38f);
    uint8_t sparkleCount = 0;
    if (trebleChance > 0.26f && nextRandomFloat() < trebleChance) {
      sparkleCount = 1;
      if (trebleChance > 0.78f && nextRandomFloat() < trebleChance - 0.38f) {
        sparkleCount = 2;
      }
      if (audioChaos > 0.74f && nextRandomFloat() < audioChaos - 0.42f) {
        sparkleCount++;
      }
    }

    for (uint8_t i = 0; i < sparkleCount; i++) {
      spawnParticle(clamp01(0.35f + pendingTrebleSpark * 0.65f), false);
    }
  }

  void updateParticles() {
    float driftX = audioFlowX * (0.014f + motionEnergy * 0.032f) + stereoBalance * 0.010f;
    float driftY = audioFlowY * (0.014f + motionEnergy * 0.032f) + (spectralTilt - 0.5f) * 0.010f;
    float drag = 0.986f - motionEnergy * 0.014f - audioChaos * 0.010f;
    if (drag < 0.950f) {
      drag = 0.950f;
    }

    for (uint8_t i = 0; i < SPARK_PARTICLE_MAX_PARTICLES; i++) {
      Particle& p = particles[i];
      if (!p.active) {
        continue;
      }

      p.x += p.vx;
      p.y += p.vy;
      float oldVx = p.vx;
      float oldVy = p.vy;
      float swirl = p.curl * (0.010f + audioChaos * 0.026f + trebleEnergy * 0.012f);
      float wiggle = p.turbulence * (0.006f + trebleEnergy * 0.022f + audioChaos * 0.018f);
      p.vx += driftX - oldVy * swirl + sinf(p.life * 0.31f + p.tone * VISUALIZER_TWO_PI_F) * wiggle;
      p.vy += driftY + oldVx * swirl + cosf(p.life * 0.27f + p.tone * VISUALIZER_TWO_PI_F) * wiggle;
      p.vx *= drag;
      p.vy *= drag;
      p.life += 1.0f;
      p.tone = wrap01(p.tone + p.toneVelocity + audioCurl * 0.0025f + trebleEnergy * 0.0015f);

      if (p.x < -1.0f || p.x > (float)LED_DRIVER_GRID_WIDTH || p.y < -1.0f || p.y > (float)LED_DRIVER_GRID_HEIGHT || p.life >= p.maxLife) {
        p.active = false;
        continue;
      }

      float life = 1.0f - p.life / p.maxLife;
      float level = p.energy * life * (0.72f + loudEnergy * 0.45f);
      if (level <= 0.012f) {
        continue;
      }

      uint8_t r = 0;
      uint8_t g = 0;
      uint8_t b = 0;
      float tone = wrap01(p.tone + colorBase * 0.10f + trebleEnergy * 0.06f + life * p.turbulence * 0.05f);
      synthwaveSparkColor(tone, clamp01(level), trebleEnergy * life * (0.12f + audioChaos * 0.12f), r, g, b);
      drawParticle(p.x, p.y, p.size, r, g, b);
    }
  }

  void spawnParticle(float strength, bool bassBurst) {
    Particle& p = particles[chooseParticleSlot()];
    float width = (float)(LED_DRIVER_GRID_WIDTH - 1);
    float height = (float)(LED_DRIVER_GRID_HEIGHT - 1);
    float xNorm = 0.5f;
    float yNorm = 0.5f;
    pickSpawnPosition(bassBurst, xNorm, yNorm);

    p.x = xNorm * width;
    p.y = yNorm * height;

    float randomDirection = nextRandomFloat() * VISUALIZER_TWO_PI_F;
    float randomX = cosf(randomDirection);
    float randomY = sinf(randomDirection);
    float flowMag = sqrtf(audioFlowX * audioFlowX + audioFlowY * audioFlowY);
    float flowX = 0.0f;
    float flowY = 0.0f;
    if (flowMag > 0.001f) {
      flowX = audioFlowX / flowMag;
      flowY = audioFlowY / flowMag;
    }
    float flowMix = clamp01(0.18f + flowMag * 0.55f + audioChaos * 0.26f + (bassBurst ? bassEnergy * 0.12f : trebleEnergy * 0.16f));
    float tangentX = -flowY * audioCurl;
    float tangentY = flowX * audioCurl;

    float speed = bassBurst
      ? (0.46f + strength * 0.70f + loudEnergy * 0.28f + audioChaos * 0.32f)
      : (0.62f + strength * 0.70f + trebleEnergy * 0.35f + audioChaos * 0.40f);

    float outwardX = xNorm - 0.5f;
    float outwardY = yNorm - 0.5f;
    float outwardLength = sqrtf(outwardX * outwardX + outwardY * outwardY);
    if (outwardLength > 0.001f) {
      outwardX /= outwardLength;
      outwardY /= outwardLength;
    }

    float vx = randomX * (1.0f - flowMix) + flowX * flowMix + tangentX * 0.38f + outwardX * strength * 0.20f;
    float vy = randomY * (1.0f - flowMix) + flowY * flowMix + tangentY * 0.38f + outwardY * strength * 0.16f;
    float velocityLength = sqrtf(vx * vx + vy * vy);
    if (velocityLength < 0.001f) {
      vx = randomX;
      vy = randomY;
    } else {
      vx /= velocityLength;
      vy /= velocityLength;
    }

    p.vx = vx * speed + stereoBalance * 0.16f;
    p.vy = vy * speed + (spectralTilt - 0.5f) * 0.14f;
    p.life = 0.0f;
    p.maxLife = bassBurst
      ? (24.0f + strength * 36.0f + bassEnergy * 12.0f)
      : (13.0f + strength * 18.0f + trebleEnergy * 10.0f);
    p.energy = bassBurst
      ? (0.50f + strength * 0.78f)
      : (0.36f + strength * 0.68f);
    p.size = bassBurst
      ? (0.18f + bassEnergy * 0.24f)
      : (0.06f + trebleEnergy * 0.18f);
    p.tone = chooseParticleTone(strength, bassBurst);
    p.toneVelocity = (nextRandomFloat() - 0.5f) * (0.006f + trebleEnergy * 0.020f + audioChaos * 0.024f) + audioCurl * 0.006f;
    p.curl = audioCurl * 0.75f + (nextRandomFloat() - 0.5f) * (0.55f + audioChaos * 0.90f);
    p.turbulence = clamp01(0.18f + nextRandomFloat() * 0.70f + audioChaos * 0.42f + trebleEnergy * 0.18f);
    p.active = true;
  }

  void pickSpawnPosition(bool bassBurst, float& xNorm, float& yNorm) {
    float roll = nextRandomFloat();

    if (bassBurst && roll < 0.24f) {
      float jitterX = 0.22f + motionEnergy * 0.20f;
      float jitterY = 0.22f + trebleEnergy * 0.18f;
      xNorm = emitterXNorm + (nextRandomFloat() - 0.5f) * jitterX;
      yNorm = emitterYNorm + (nextRandomFloat() - 0.5f) * jitterY;
    } else if (!bassBurst && roll < 0.30f) {
      float jitterX = 0.34f + trebleEnergy * 0.22f;
      float jitterY = 0.30f + trebleEnergy * 0.18f;
      xNorm = emitterXNorm + (nextRandomFloat() - 0.5f) * jitterX;
      yNorm = emitterYNorm + (nextRandomFloat() - 0.5f) * jitterY;
    } else if (roll > 0.70f) {
      uint8_t edge = nextRandomU32() & 0x03;
      float inset = 0.03f + nextRandomFloat() * 0.10f;

      if (edge == 0) {
        xNorm = inset;
        yNorm = nextRandomFloat();
      } else if (edge == 1) {
        xNorm = 1.0f - inset;
        yNorm = nextRandomFloat();
      } else if (edge == 2) {
        xNorm = nextRandomFloat();
        yNorm = inset;
      } else {
        xNorm = nextRandomFloat();
        yNorm = 1.0f - inset;
      }
    } else {
      xNorm = nextRandomFloat();
      yNorm = nextRandomFloat();
    }

    xNorm = clamp01(xNorm);
    yNorm = clamp01(yNorm);
  }

  float chooseParticleTone(float strength, bool bassBurst) {
    float spread = bassBurst
      ? (0.34f + audioChaos * 0.34f + bassEnergy * 0.10f)
      : (0.46f + audioChaos * 0.44f + trebleEnergy * 0.16f);
    float audioPush = spectralTilt * 0.18f + stereoBalance * 0.06f + audioCurl * 0.12f + strength * 0.05f;
    return wrap01(colorBase + audioPush + (nextRandomFloat() - 0.5f) * spread);
  }

  void drawParticle(float x, float y, float size, uint8_t r, uint8_t g, uint8_t b) {
    int16_t centerX = (int16_t)roundf(x);
    int16_t centerY = (int16_t)roundf(y);

    addPixelSafe(centerX, centerY, r, g, b);

    uint8_t sideR = (uint8_t)((float)r * size * 0.13f);
    uint8_t sideG = (uint8_t)((float)g * size * 0.13f);
    uint8_t sideB = (uint8_t)((float)b * size * 0.13f);

    if (size > 0.30f) {
      addPixelSafe(centerX - 1, centerY, sideR, sideG, sideB);
      addPixelSafe(centerX + 1, centerY, sideR, sideG, sideB);
    }
    if (size > 0.38f) {
      addPixelSafe(centerX, centerY - 1, sideR, sideG, sideB);
      addPixelSafe(centerX, centerY + 1, sideR, sideG, sideB);
    }
  }

  void synthwaveSparkColor(float tone, float level, float white, uint8_t& r, uint8_t& g, uint8_t& b) {
    tone = wrap01(tone);

    float baseR = 0.0f;
    float baseG = 0.0f;
    float baseB = 0.0f;

    if (tone < 0.18f) {
      float t = tone / 0.18f;
      baseR = 0.0f + 28.0f * t;
      baseG = 220.0f + (88.0f - 220.0f) * t;
      baseB = 255.0f;
    } else if (tone < 0.38f) {
      float t = (tone - 0.18f) / 0.20f;
      baseR = 28.0f + (128.0f - 28.0f) * t;
      baseG = 88.0f + (20.0f - 88.0f) * t;
      baseB = 255.0f;
    } else if (tone < 0.58f) {
      float t = (tone - 0.38f) / 0.20f;
      baseR = 128.0f + (255.0f - 128.0f) * t;
      baseG = 20.0f;
      baseB = 255.0f + (210.0f - 255.0f) * t;
    } else if (tone < 0.78f) {
      float t = (tone - 0.58f) / 0.20f;
      baseR = 255.0f;
      baseG = 20.0f + (120.0f - 20.0f) * t;
      baseB = 210.0f + (40.0f - 210.0f) * t;
    } else {
      float t = (tone - 0.78f) / 0.22f;
      baseR = 255.0f + (255.0f - 255.0f) * t;
      baseG = 120.0f + (245.0f - 120.0f) * t;
      baseB = 40.0f + (255.0f - 40.0f) * t;
    }

    float glow = 0.18f + level * 0.92f;
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

  uint8_t chooseParticleSlot() {
    uint8_t slot = 0;
    float oldestScore = -1.0f;

    for (uint8_t i = 0; i < SPARK_PARTICLE_MAX_PARTICLES; i++) {
      if (!particles[i].active) {
        return i;
      }

      float score = particles[i].life / particles[i].maxLife;
      if (score > oldestScore) {
        oldestScore = score;
        slot = i;
      }
    }

    return slot;
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

  float clampSigned(float value) const {
    if (value < -1.0f) {
      return -1.0f;
    }
    if (value > 1.0f) {
      return 1.0f;
    }
    return value;
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
