#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr uint8_t COSMIC_GRAVITY_MAX_BODIES = 64;
static constexpr float COSMIC_GRAVITY_FRAME_MS = 18.0f;
static constexpr uint16_t COSMIC_GRAVITY_MIN_STAR_MS = 44;
static constexpr uint16_t COSMIC_GRAVITY_MIN_COMET_MS = 140;

class EmberVortexVisualizer : public AudioVisualizer {
public:
  const char* name() const override {
    return "cosmic-gravity";
  }

  void begin() override {
    reset();
  }

  void reset() override {
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      canvasR[i] = 0.0f;
      canvasG[i] = 0.0f;
      canvasB[i] = 0.0f;
    }

    for (uint8_t i = 0; i < COSMIC_GRAVITY_MAX_BODIES; i++) {
      bodies[i].active = false;
    }

    subBassEnergy = 0.0f;
    bassEnergy = 0.0f;
    kickEnergy = 0.0f;
    midEnergy = 0.0f;
    gravityField = 0.0f;
    trebleEnergy = 0.0f;
    loudEnergy = 0.0f;
    stereoBalance = 0.0f;
    spectralTilt = 0.5f;
    audioFlowX = 0.0f;
    audioFlowY = 0.0f;
    audioChaos = 0.0f;
    holeXNorm = 0.50f;
    holeYNorm = 0.52f;
    fieldPhase = 0.0f;
    holePulse = 0.0f;
    pendingStarBirth = 0.0f;
    previousKick = 0.0f;
    lastStarMs = 0;
    lastCometMs = 0;
    lastRenderMs = 0;
    lastAudioSequence = 0;
    rngState = 0xC05A1C41UL;
    ditherFrame = 0;
  }

  void render(Adafruit_NeoPixel& pixels, const AudioAnalysisFrame& audio) override {
    uint32_t now = millis();
    float frameScale = animationFrameScale(now);

    applyAudio(audio);
    fadeCanvas(frameScale);

    float holeSize = blackHoleSize();
    float mass = gravityMass(holeSize);
    fieldPhase += (0.012f + gravityField * 0.042f + mass * 0.050f + audioChaos * 0.012f) * frameScale;

    drawGravityWell(holeSize, mass);
    spawnTrebleBodies(now, holeSize, mass);
    updateBodies(frameScale, holeSize, mass);

    for (uint16_t i = 0; i < LED_COUNT; i++) {
      pixels.setPixelColor(i, visualizerColor(pixels, i, canvasR[i], canvasG[i], canvasB[i], ditherFrame));
    }

    holePulse *= powf(0.58f, frameScale);
    pendingStarBirth *= powf(0.62f, frameScale);
    ditherFrame++;
  }

private:
  struct Body {
    bool active;
    bool comet;
    float x;
    float y;
    float vx;
    float vy;
    float life;
    float maxLife;
    float heat;
    float size;
    float mass;
    float tone;
    float tail;
    float phase;
  };

  Body bodies[COSMIC_GRAVITY_MAX_BODIES];
  float canvasR[LED_COUNT];
  float canvasG[LED_COUNT];
  float canvasB[LED_COUNT];

  float subBassEnergy = 0.0f;
  float bassEnergy = 0.0f;
  float kickEnergy = 0.0f;
  float midEnergy = 0.0f;
  float gravityField = 0.0f;
  float trebleEnergy = 0.0f;
  float loudEnergy = 0.0f;
  float stereoBalance = 0.0f;
  float spectralTilt = 0.5f;
  float audioFlowX = 0.0f;
  float audioFlowY = 0.0f;
  float audioChaos = 0.0f;
  float holeXNorm = 0.50f;
  float holeYNorm = 0.52f;
  float fieldPhase = 0.0f;
  float holePulse = 0.0f;
  float pendingStarBirth = 0.0f;
  float previousKick = 0.0f;

  uint32_t lastStarMs = 0;
  uint32_t lastCometMs = 0;
  uint32_t lastRenderMs = 0;
  uint32_t lastAudioSequence = 0;
  uint32_t rngState = 0xC05A1C41UL;
  uint8_t ditherFrame = 0;

  void applyAudio(const AudioAnalysisFrame& audio) {
    if (!audio.ready) {
      settleTowardSilence();
      return;
    }

    if (audio.sequence == lastAudioSequence) {
      return;
    }

    lastAudioSequence = audio.sequence;
    rngState ^= audio.audioSeed + 0x9E3779B9UL + (rngState << 6) + (rngState >> 2);

    float previousTreble = trebleEnergy;
    float kick = clamp01(audio.kick);
    float kickRise = kick - previousKick;
    previousKick = kick;
    if (kickRise < 0.0f) {
      kickRise = 0.0f;
    }

    subBassEnergy = smoothAttackRelease(subBassEnergy, audio.subBass, 0.44f, 0.14f);
    bassEnergy = smoothAttackRelease(bassEnergy, clamp01(audio.bass * 0.58f + kick * 0.42f), 0.54f, 0.18f);
    kickEnergy = smoothAttackRelease(kickEnergy, kick, 0.66f, 0.26f);
    midEnergy = smoothAttackRelease(midEnergy, clamp01(audio.lowMid * 0.28f + audio.mid * 0.72f), 0.38f, 0.14f);
    gravityField = smoothAttackRelease(
      gravityField,
      clamp01(audio.mid * 0.62f + audio.lowMid * 0.24f + audio.audioChaos * 0.08f + audio.loudness * 0.06f),
      0.40f,
      0.15f
    );
    trebleEnergy = smoothAttackRelease(trebleEnergy, audio.treble, 0.58f, 0.20f);
    loudEnergy = smoothAttackRelease(loudEnergy, audio.loudness, 0.50f, 0.16f);
    stereoBalance = smoothToward(stereoBalance, audio.stereoBalance, 0.22f);
    spectralTilt = smoothToward(spectralTilt, audio.spectralTilt, 0.24f);
    audioFlowX = smoothToward(audioFlowX, audio.audioFlowX, 0.32f);
    audioFlowY = smoothToward(audioFlowY, audio.audioFlowY, 0.32f);
    audioChaos = smoothToward(audioChaos, audio.audioChaos, 0.30f);

    float xTarget = clamp01(0.50f + stereoBalance * 0.32f + audioFlowX * 0.12f);
    float yTarget = clamp01(0.52f + audioFlowY * 0.10f + (spectralTilt - 0.5f) * 0.14f + subBassEnergy * 0.025f);
    holeXNorm = smoothToward(holeXNorm, xTarget, 0.16f + gravityField * 0.04f);
    holeYNorm = smoothToward(holeYNorm, yTarget, 0.16f + gravityField * 0.04f);

    float bassHit = clamp01(kickRise * 1.72f + audio.bassTransient * (0.62f + kick * 0.34f) + audio.subBass * audio.bassTransient * 0.18f + audio.transient * 0.10f);
    if (bassHit > holePulse) {
      holePulse = bassHit;
    }

    float trebleRise = audio.treble - previousTreble;
    if (trebleRise < 0.0f) {
      trebleRise = 0.0f;
    }
    float birth = clamp01(audio.trebleTransient * 0.92f + trebleRise * 0.72f + audio.transient * 0.10f + audio.audioChaos * 0.06f - 0.12f);
    if (birth > pendingStarBirth) {
      pendingStarBirth = birth;
    }
  }

  void settleTowardSilence() {
    subBassEnergy = smoothToward(subBassEnergy, 0.0f, 0.05f);
    bassEnergy = smoothToward(bassEnergy, 0.0f, 0.06f);
    kickEnergy = smoothToward(kickEnergy, 0.0f, 0.07f);
    midEnergy = smoothToward(midEnergy, 0.0f, 0.05f);
    gravityField = smoothToward(gravityField, 0.0f, 0.05f);
    trebleEnergy = smoothToward(trebleEnergy, 0.0f, 0.06f);
    loudEnergy = smoothToward(loudEnergy, 0.0f, 0.05f);
    audioFlowX = smoothToward(audioFlowX, 0.0f, 0.04f);
    audioFlowY = smoothToward(audioFlowY, 0.0f, 0.04f);
    holePulse *= 0.74f;
    pendingStarBirth *= 0.72f;
  }

  float animationFrameScale(uint32_t now) {
    if (lastRenderMs == 0) {
      lastRenderMs = now;
      return 1.0f;
    }

    uint32_t elapsedMs = now - lastRenderMs;
    lastRenderMs = now;

    float scale = (float)elapsedMs / COSMIC_GRAVITY_FRAME_MS;
    if (scale < 0.62f) scale = 0.62f;
    if (scale > 1.35f) scale = 1.35f;
    return scale;
  }

  void fadeCanvas(float frameScale) {
    float holeSize = blackHoleSize();
    float fade = 0.840f + holeSize * 0.030f + gravityMass(holeSize) * 0.022f + trebleEnergy * 0.014f + loudEnergy * 0.012f;
    if (fade > 0.928f) {
      fade = 0.928f;
    }
    fade = powf(fade, frameScale);

    for (uint16_t i = 0; i < LED_COUNT; i++) {
      canvasR[i] *= fade;
      canvasG[i] *= fade;
      canvasB[i] *= fade;

      if (canvasR[i] < 0.07f) canvasR[i] = 0.0f;
      if (canvasG[i] < 0.07f) canvasG[i] = 0.0f;
      if (canvasB[i] < 0.07f) canvasB[i] = 0.0f;
    }
  }

  void drawGravityWell(float holeSize, float mass) {
    if (holeSize < 0.012f && mass < 0.010f) {
      return;
    }

    float centerX = holeXNorm * (float)(LED_DRIVER_GRID_WIDTH - 1);
    float centerY = holeYNorm * (float)(LED_DRIVER_GRID_HEIGHT - 1);
    float eventRadius = eventHorizonRadius(holeSize);
    float haloRadius = 3.40f + holeSize * (4.40f + mass * 2.10f) + subBassEnergy * 0.85f + holePulse * 0.90f;
    float fieldRadius = haloRadius + mass * (5.8f + gravityField * 4.8f);

    int16_t minX = (int16_t)(centerX - fieldRadius - 1.0f);
    int16_t maxX = (int16_t)(centerX + fieldRadius + 1.0f);
    int16_t minY = (int16_t)(centerY - fieldRadius - 1.0f);
    int16_t maxY = (int16_t)(centerY + fieldRadius + 1.0f);
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= LED_DRIVER_GRID_WIDTH) maxX = LED_DRIVER_GRID_WIDTH - 1;
    if (maxY >= LED_DRIVER_GRID_HEIGHT) maxY = LED_DRIVER_GRID_HEIGHT - 1;

    uint8_t shimmerPhase = (uint8_t)(fieldPhase * 47.0f + holePulse * 30.0f);
    for (int16_t y = minY; y <= maxY; y++) {
      for (int16_t x = minX; x <= maxX; x++) {
        float dx = ((float)x - centerX) * 0.92f;
        float dy = ((float)y - centerY) * 1.18f;
        float distance = sqrtf(dx * dx + dy * dy);
        if (distance > fieldRadius) {
          continue;
        }

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;

        float core = 1.0f - distance / (eventRadius + 0.001f);
        if (core > 0.0f) {
          core = core * core;
          float level = core * (0.003f + holeSize * 0.006f + holePulse * 0.004f);
          r += 42.0f * level;
          g += 2.0f * level;
          b += 18.0f * level;
        }

        float rim = 1.0f - fabsf(distance - eventRadius) / (0.62f + holeSize * 0.34f);
        if (rim > 0.0f) {
          rim = rim * rim * (3.0f - 2.0f * rim);
          float level = rim * (0.018f + holeSize * 0.060f + holePulse * 0.056f);
          r += 255.0f * level;
          g += (22.0f + holePulse * 30.0f) * level;
          b += (28.0f + mass * 46.0f) * level;
        }

        float halo = 1.0f - distance / (haloRadius + 0.001f);
        if (halo > 0.0f) {
          halo = halo * halo;
          float level = halo * (0.003f + subBassEnergy * 0.012f + holeSize * 0.010f + mass * 0.004f);
          r += 170.0f * level;
          g += 24.0f * level;
          b += 44.0f * level;
        }

        if (mass > 0.045f && gravityField > 0.050f) {
          float ringRadius = eventRadius + (fieldRadius - eventRadius) * (0.46f + triangleByte((uint8_t)(x * 9u + y * 3u + shimmerPhase)) * 0.12f);
          float ring = 1.0f - fabsf(distance - ringRadius) / (0.34f + gravityField * 0.38f);
          if (ring > 0.0f) {
            ring = ring * ring;
            float texture = 0.68f + triangleByte((uint8_t)(x * 13u + y * 19u + shimmerPhase * 2u)) * 0.32f;
            float level = ring * texture * (0.003f + mass * gravityField * 0.018f);
            r += 96.0f * level;
            g += 140.0f * level;
            b += 255.0f * level;
          }
        }

        if (r > 0.0f || g > 0.0f || b > 0.0f) {
          addPixel(ledIndexXY((uint16_t)x, (uint16_t)y), r, g, b);
        }
      }
    }
  }

  void spawnTrebleBodies(uint32_t now, float holeSize, float mass) {
    uint16_t minStarMs = COSMIC_GRAVITY_MIN_STAR_MS + (uint16_t)((1.0f - clamp01(trebleEnergy + audioChaos * 0.12f)) * 66.0f);
    bool spawned = false;

    if (pendingStarBirth > 0.240f && now - lastStarMs >= minStarMs) {
      uint8_t count = 1;
      if (pendingStarBirth > 0.62f && trebleEnergy > 0.34f) count++;
      if (pendingStarBirth > 0.92f && audioChaos > 0.30f) count++;
      if (count > 3) count = 3;

      for (uint8_t i = 0; i < count; i++) {
        bool comet = pendingStarBirth > 0.58f && now - lastCometMs >= COSMIC_GRAVITY_MIN_COMET_MS && (i == 0 || nextRandomFloat() < 0.16f + trebleEnergy * 0.16f);
        spawnBody(clamp01(pendingStarBirth * (1.0f - (float)i * 0.10f)), comet, holeSize, mass);
        if (comet) {
          lastCometMs = now;
        }
      }

      pendingStarBirth *= 0.34f;
      lastStarMs = now;
      spawned = true;
    }

    float simmerChance = clamp01((trebleEnergy - 0.34f) * (0.055f + trebleEnergy * 0.16f + audioChaos * 0.06f));
    if (!spawned && simmerChance > 0.018f && now - lastStarMs >= 118 && nextRandomFloat() < simmerChance) {
      spawnBody(clamp01(0.18f + trebleEnergy * 0.42f), false, holeSize, mass);
      lastStarMs = now;
    }
  }

  void spawnBody(float strength, bool comet, float holeSize, float mass) {
    Body& body = bodies[chooseBodySlot()];
    float centerX = holeXNorm * (float)(LED_DRIVER_GRID_WIDTH - 1);
    float centerY = holeYNorm * (float)(LED_DRIVER_GRID_HEIGHT - 1);
    float mu = gravityMu(holeSize, mass);
    float orbitalChance = clamp01(mass * gravityField * (0.36f + strength * 0.72f));
    bool orbitalBirth = holeSize > 0.14f && nextRandomFloat() < orbitalChance;

    if (orbitalBirth) {
      float angle = nextRandomFloat() * VISUALIZER_TWO_PI_F;
      float radius = 3.8f + holeSize * 5.8f + nextRandomFloat() * (2.6f + mass * 4.6f);
      body.x = centerX + cosf(angle) * radius;
      body.y = centerY + sinf(angle) * radius * 0.58f;

      float spin = nextRandomFloat() < 0.5f ? -1.0f : 1.0f;
      float circularSpeed = sqrtf(mu / (radius + 1.8f));
      float speed = circularSpeed * (1.04f + nextRandomFloat() * 0.54f) + strength * 0.014f;
      body.vx = -sinf(angle) * speed * spin + (nextRandomFloat() - 0.5f) * 0.012f;
      body.vy = cosf(angle) * speed * 0.58f * spin + (nextRandomFloat() - 0.5f) * 0.010f;
    } else {
      pickEdgeBirth(body.x, body.y);

      float dx = centerX - body.x;
      float dy = centerY - body.y;
      float distance = sqrtf(dx * dx + dy * dy) + 0.001f;
      float inwardX = dx / distance;
      float inwardY = dy / distance;
      float randomAngle = nextRandomFloat() * VISUALIZER_TWO_PI_F;
      float randomX = cosf(randomAngle);
      float randomY = sinf(randomAngle);
      float spin = nextRandomFloat() < 0.5f ? -1.0f : 1.0f;
      float tangentX = -inwardY * spin;
      float tangentY = inwardX * spin;
      float fieldBias = mass * (0.10f + gravityField * 0.20f);
      float inwardMix = comet ? (0.070f + fieldBias * 0.70f) : (0.020f + fieldBias * 0.34f);
      float tangentMix = comet ? (0.72f + fieldBias * 0.20f) : (0.64f + fieldBias * 0.22f);
      float randomMix = comet ? 0.20f : 0.26f;

      float vx = randomX * randomMix + inwardX * inwardMix + tangentX * tangentMix;
      float vy = randomY * randomMix + inwardY * inwardMix + tangentY * tangentMix;
      float velocity = sqrtf(vx * vx + vy * vy);
      if (velocity > 0.001f) {
        vx /= velocity;
        vy /= velocity;
      }

      float speed = comet
        ? (0.110f + strength * 0.100f + trebleEnergy * 0.042f)
        : (0.056f + strength * 0.050f + trebleEnergy * 0.024f);
      body.vx = vx * speed + audioFlowX * 0.014f + stereoBalance * 0.010f;
      body.vy = vy * speed + audioFlowY * 0.014f + (spectralTilt - 0.5f) * 0.010f;
    }

    clampBodyToPanel(body);
    body.active = true;
    body.comet = comet;
    body.life = 0.0f;
    body.maxLife = comet
      ? (58.0f + strength * 40.0f + trebleEnergy * 12.0f + mass * 18.0f)
      : (88.0f + strength * 54.0f + trebleEnergy * 16.0f + mass * 52.0f);
    body.heat = 0.34f + strength * 0.32f + trebleEnergy * 0.08f;
    body.size = comet
      ? (0.20f + strength * 0.10f + trebleEnergy * 0.04f)
      : (0.08f + strength * 0.07f + trebleEnergy * 0.03f);
    body.mass = 0.86f + nextRandomFloat() * 0.30f;
    body.tone = nextRandomFloat();
    body.tail = comet ? (0.36f + strength * 0.24f) : (0.09f + strength * 0.08f);
    body.phase = nextRandomFloat() * VISUALIZER_TWO_PI_F;
  }

  void pickEdgeBirth(float& x, float& y) {
    float width = (float)(LED_DRIVER_GRID_WIDTH - 1);
    float height = (float)(LED_DRIVER_GRID_HEIGHT - 1);
    uint8_t edge = nextRandomU32() & 0x03;
    float inset = nextRandomFloat() * 0.45f;

    if (edge == 0) {
      x = inset;
      y = nextRandomFloat() * height;
    } else if (edge == 1) {
      x = width - inset;
      y = nextRandomFloat() * height;
    } else if (edge == 2) {
      x = nextRandomFloat() * width;
      y = inset;
    } else {
      x = nextRandomFloat() * width;
      y = height - inset;
    }
  }

  void updateBodies(float frameScale, float holeSize, float mass) {
    float centerX = holeXNorm * (float)(LED_DRIVER_GRID_WIDTH - 1);
    float centerY = holeYNorm * (float)(LED_DRIVER_GRID_HEIGHT - 1);
    float eventRadius = eventHorizonRadius(holeSize);
    float gravityRadius = 5.8f + holeSize * 11.8f + mass * 7.4f;
    float horizontalReach = 1.0f + holeSize * 0.58f;
    float verticalReach = 1.0f + holeSize * 0.16f;
    float softening = eventRadius * 1.58f + 1.25f;
    float mu = gravityMu(holeSize, mass);
    float baseDrag = 0.994f + mass * 0.002f - audioChaos * 0.001f;
    if (baseDrag < 0.990f) baseDrag = 0.990f;
    if (baseDrag > 0.998f) baseDrag = 0.998f;
    float drag = powf(baseDrag, frameScale);

    for (uint8_t i = 0; i < COSMIC_GRAVITY_MAX_BODIES; i++) {
      Body& body = bodies[i];
      if (!body.active) {
        continue;
      }

      float dx = centerX - body.x;
      float dy = centerY - body.y;
      float distance = sqrtf(dx * dx + dy * dy) + 0.001f;
      float fieldDx = dx / horizontalReach;
      float fieldDy = dy / verticalReach;
      float fieldDistance = sqrtf(fieldDx * fieldDx + fieldDy * fieldDy) + 0.001f;
      float inwardX = dx / distance;
      float inwardY = dy / distance;

      float fieldInfluence = 1.0f - fieldDistance / (gravityRadius + 0.001f);
      if (fieldInfluence < 0.0f) {
        fieldInfluence = 0.0f;
      }
      fieldInfluence = fieldInfluence * fieldInfluence * (3.0f - 2.0f * fieldInfluence);

      float softenedDistanceSq = distance * distance + softening * softening;
      float gravity = mu * body.mass / softenedDistanceSq;
      gravity *= 0.38f + fieldInfluence * 0.36f;

      body.vx += (inwardX * gravity + audioFlowX * 0.0008f) * frameScale;
      body.vy += (inwardY * gravity + audioFlowY * 0.0008f) * frameScale;

      float accretionField = 1.0f - distance / (eventRadius * 1.25f + 0.001f);
      accretionField = clamp01(accretionField);
      if (accretionField > 0.0f && mass > 0.060f) {
        float accretionDrag = 1.0f - accretionField * accretionField * mass * (0.006f + holeSize * 0.014f) * frameScale;
        if (accretionDrag < 0.93f) {
          accretionDrag = 0.93f;
        }
        body.vx *= accretionDrag;
        body.vy *= accretionDrag;
      }
      body.vx *= drag;
      body.vy *= drag;

      body.x += body.vx * frameScale;
      body.y += body.vy * frameScale;
      body.life += frameScale * (body.comet ? 1.00f : 0.80f);
      body.heat *= powf(0.986f - trebleEnergy * 0.004f, frameScale);

      if (distance < eventRadius && holeSize > 0.08f) {
        float consumed = 1.0f - distance / (eventRadius + 0.001f);
        consumed = consumed * consumed;
        drawAbsorbWink(body.x, body.y, centerX, centerY, body.heat, consumed);
        body.active = false;
        continue;
      }

      if (body.x < -1.6f || body.x > (float)LED_DRIVER_GRID_WIDTH + 0.6f || body.y < -1.6f || body.y > (float)LED_DRIVER_GRID_HEIGHT + 0.6f || body.life >= body.maxLife || body.heat < 0.030f) {
        body.active = false;
        continue;
      }

      drawBody(body, fieldInfluence);
    }
  }

  void drawBody(const Body& body, float fieldInfluence) {
    float life = 1.0f - body.life / (body.maxLife + 0.001f);
    life = clamp01(life);
    float level = life * (0.18f + clamp01(body.heat) * 0.46f + fieldInfluence * 0.04f);
    if (level < 0.010f) {
      return;
    }

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    starPalette(body.tone, body.comet, clamp01(body.heat), r, g, b);
    r *= level;
    g *= level;
    b *= level;

    int16_t centerX = (int16_t)roundf(body.x);
    int16_t centerY = (int16_t)roundf(body.y);
    addPixelSafe(centerX, centerY, r, g, b);

    float speed = sqrtf(body.vx * body.vx + body.vy * body.vy);
    if (speed > 0.010f) {
      float tail = body.tail * (0.10f + speed * 1.00f);
      if (tail > 0.34f) tail = 0.34f;
      int16_t tailX = (int16_t)roundf(body.x - body.vx / speed);
      int16_t tailY = (int16_t)roundf(body.y - body.vy / speed);
      addPixelSafe(tailX, tailY, r * tail, g * tail * 0.72f, b * tail * 0.96f);

      if (body.comet && speed > 0.080f) {
        int16_t farTailX = (int16_t)roundf(body.x - body.vx / speed * 2.0f);
        int16_t farTailY = (int16_t)roundf(body.y - body.vy / speed * 2.0f);
        addPixelSafe(farTailX, farTailY, r * tail * 0.24f, g * tail * 0.17f, b * tail * 0.30f);
      }
    }

    if (body.size > 0.36f && level > 0.42f) {
      float side = body.size * level * 0.035f;
      addPixelSafe(centerX - 1, centerY, r * side, g * side, b * side);
      addPixelSafe(centerX + 1, centerY, r * side, g * side, b * side);
    }
  }

  void starPalette(float tone, bool comet, float heat, float& r, float& g, float& b) const {
    tone = wrap01(tone);
    if (comet || tone > 0.62f) {
      float t = comet ? clamp01(0.35f + tone * 0.65f) : tone;
      r = 74.0f + 70.0f * heat + 20.0f * t;
      g = 118.0f + 58.0f * heat;
      b = 178.0f + 52.0f * heat;
    } else if (tone < 0.34f) {
      r = 180.0f + 52.0f * heat;
      g = 102.0f + 52.0f * heat;
      b = 32.0f + 42.0f * heat;
    } else {
      r = 158.0f + 54.0f * heat;
      g = 154.0f + 58.0f * heat;
      b = 124.0f + 58.0f * heat;
    }
  }

  void drawAbsorbWink(float x, float y, float centerX, float centerY, float heat, float strength) {
    float level = clamp01(strength) * (0.045f + clamp01(heat) * 0.060f);
    addPixelSafe((int16_t)roundf(x), (int16_t)roundf(y), 255.0f * level, 120.0f * level, 66.0f * level);
    addPixelSafe((int16_t)roundf(centerX), (int16_t)roundf(centerY), 82.0f * level, 14.0f * level, 54.0f * level);
  }

  float blackHoleSize() const {
    return clamp01(0.08f + subBassEnergy * 0.28f + bassEnergy * 0.58f + kickEnergy * 0.08f + holePulse * 0.58f + loudEnergy * 0.04f);
  }

  float gravityMass(float holeSize) const {
    return clamp01(holeSize * (0.14f + gravityField * 1.78f + midEnergy * 0.34f));
  }

  float gravityMu(float holeSize, float mass) const {
    return mass * (0.120f + gravityField * 0.420f + holeSize * 0.200f + holePulse * 0.065f);
  }

  float eventHorizonRadius(float holeSize) const {
    return 1.35f + holeSize * 2.10f + holePulse * 0.72f;
  }

  void clampBodyToPanel(Body& body) {
    float width = (float)(LED_DRIVER_GRID_WIDTH - 1);
    float height = (float)(LED_DRIVER_GRID_HEIGHT - 1);
    if (body.x < 0.0f) body.x = 0.0f;
    if (body.x > width) body.x = width;
    if (body.y < 0.0f) body.y = 0.0f;
    if (body.y > height) body.y = height;
  }

  void addPixelSafe(int16_t x, int16_t y, float r, float g, float b) {
    if (x < 0 || x >= LED_DRIVER_GRID_WIDTH || y < 0 || y >= LED_DRIVER_GRID_HEIGHT) {
      return;
    }

    addPixel(ledIndexXY((uint16_t)x, (uint16_t)y), r, g, b);
  }

  void addPixel(uint16_t index, float r, float g, float b) {
    canvasR[index] += r;
    canvasG[index] += g;
    canvasB[index] += b;

    if (canvasR[index] > 255.0f) canvasR[index] = 255.0f;
    if (canvasG[index] > 255.0f) canvasG[index] = 255.0f;
    if (canvasB[index] > 255.0f) canvasB[index] = 255.0f;
  }

  uint8_t chooseBodySlot() const {
    uint8_t slot = 0;
    float oldestScore = -1.0f;

    for (uint8_t i = 0; i < COSMIC_GRAVITY_MAX_BODIES; i++) {
      if (!bodies[i].active) {
        return i;
      }

      float score = bodies[i].life / bodies[i].maxLife;
      if (score > oldestScore) {
        oldestScore = score;
        slot = i;
      }
    }

    return slot;
  }

  float smoothToward(float current, float target, float amount) const {
    return current * (1.0f - amount) + target * amount;
  }

  float smoothAttackRelease(float current, float target, float attack, float release) const {
    return smoothToward(current, target, target > current ? attack : release);
  }

  float triangleByte(uint8_t value) const {
    uint8_t folded = value < 128 ? value : (uint8_t)(255 - value);
    return (float)folded / 127.0f;
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
