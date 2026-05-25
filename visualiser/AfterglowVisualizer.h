#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr uint8_t AFTERGLOW_MAX_GLINTS = 36;
static constexpr float AFTERGLOW_FRAME_MS = 18.0f;
static constexpr uint16_t AFTERGLOW_MIN_GLINT_MS = 42;
static constexpr uint16_t AFTERGLOW_MIN_STROBE_MS = 220;

class AfterglowVisualizer : public AudioVisualizer {
public:
  const char* name() const override {
    return "afterglow";
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

    for (uint8_t i = 0; i < AFTERGLOW_MAX_GLINTS; i++) {
      glints[i].active = false;
    }

    subBassEnergy = 0.0f;
    bassEnergy = 0.0f;
    kickEnergy = 0.0f;
    lowMidEnergy = 0.0f;
    midEnergy = 0.0f;
    trebleEnergy = 0.0f;
    loudEnergy = 0.0f;
    roomHaze = 0.0f;
    motionEnergy = 0.0f;
    stereoBalance = 0.0f;
    spectralTilt = 0.5f;
    audioFlowX = 0.0f;
    audioFlowY = 0.0f;
    audioCurl = 0.0f;
    audioChaos = 0.0f;
    colorBase = 0.36f;
    paletteDrift = 0.0f;
    sweepPhase = 0.0f;
    bassBloom = 0.0f;
    strobeFlash = 0.0f;
    pendingTrebleGlint = 0.0f;
    previousKick = 0.0f;
    kickRise = 0.0f;
    lastGlintMs = 0;
    lastStrobeMs = 0;
    lastRenderMs = 0;
    lastAudioSequence = 0;
    rngState = 0xAF734B11UL;
    ditherFrame = 0;
  }

  void render(Adafruit_NeoPixel& pixels, const AudioAnalysisFrame& audio) override {
    uint32_t now = millis();
    float frameScale = animationFrameScale(now);

    applyAudio(audio, now);
    fadeCanvas(frameScale);

    sweepPhase += (0.040f + motionEnergy * 0.070f + audioChaos * 0.050f) * frameScale;
    paletteDrift = wrap01(paletteDrift + (0.0011f + trebleEnergy * 0.0028f + audioChaos * 0.0024f) * frameScale);

    bool accentFrame = (ditherFrame & 0x01) == 0 || bassBloom > 0.46f;
    drawLightField();
    if (accentFrame) {
      spawnGlints(now);
      updateGlints(frameScale);
    }

    for (uint16_t i = 0; i < LED_COUNT; i++) {
      pixels.setPixelColor(i, visualizerColor(pixels, i, canvasR[i], canvasG[i], canvasB[i], ditherFrame));
    }

    bassBloom *= powf(0.76f, frameScale);
    strobeFlash *= powf(0.16f, frameScale);
    pendingTrebleGlint *= powf(0.72f, frameScale);
    ditherFrame++;
  }

private:
  struct Glint {
    bool active;
    float x;
    float y;
    float vx;
    float vy;
    float life;
    float maxLife;
    float brightness;
    float tone;
    float size;
  };

  float canvasR[LED_COUNT];
  float canvasG[LED_COUNT];
  float canvasB[LED_COUNT];
  Glint glints[AFTERGLOW_MAX_GLINTS];

  float subBassEnergy = 0.0f;
  float bassEnergy = 0.0f;
  float kickEnergy = 0.0f;
  float lowMidEnergy = 0.0f;
  float midEnergy = 0.0f;
  float trebleEnergy = 0.0f;
  float loudEnergy = 0.0f;
  float roomHaze = 0.0f;
  float motionEnergy = 0.0f;
  float stereoBalance = 0.0f;
  float spectralTilt = 0.5f;
  float audioFlowX = 0.0f;
  float audioFlowY = 0.0f;
  float audioCurl = 0.0f;
  float audioChaos = 0.0f;
  float colorBase = 0.36f;
  float paletteDrift = 0.0f;
  float sweepPhase = 0.0f;
  float bassBloom = 0.0f;
  float strobeFlash = 0.0f;
  float pendingTrebleGlint = 0.0f;
  float previousKick = 0.0f;
  float kickRise = 0.0f;

  uint32_t lastGlintMs = 0;
  uint32_t lastStrobeMs = 0;
  uint32_t lastRenderMs = 0;
  uint32_t lastAudioSequence = 0;
  uint32_t rngState = 0xAF734B11UL;
  uint8_t ditherFrame = 0;

  void applyAudio(const AudioAnalysisFrame& audio, uint32_t now) {
    if (!audio.ready) {
      settleTowardSilence();
      return;
    }

    if (audio.sequence == lastAudioSequence) {
      return;
    }

    lastAudioSequence = audio.sequence;
    rngState ^= audio.audioSeed + 0x9E3779B9UL + (rngState << 6) + (rngState >> 2);

    float kick = clamp01(audio.kick);
    float previousTrebleEnergy = trebleEnergy;
    float kickDelta = kick - previousKick;
    previousKick = kick;
    kickRise = smoothAttackRelease(kickRise, kickDelta > 0.0f ? kickDelta : 0.0f, 0.82f, 0.30f);

    subBassEnergy = smoothAttackRelease(subBassEnergy, audio.subBass, 0.58f, 0.22f);
    bassEnergy = smoothAttackRelease(bassEnergy, clamp01(audio.bass * 0.55f + audio.kick * 0.45f), 0.72f, 0.28f);
    kickEnergy = smoothAttackRelease(kickEnergy, kick, 0.82f, 0.34f);
    lowMidEnergy = smoothAttackRelease(lowMidEnergy, audio.lowMid, 0.42f, 0.16f);
    midEnergy = smoothAttackRelease(midEnergy, audio.mid, 0.40f, 0.15f);
    trebleEnergy = smoothAttackRelease(trebleEnergy, audio.treble, 0.60f, 0.22f);
    loudEnergy = smoothAttackRelease(loudEnergy, audio.loudness, 0.64f, 0.18f);
    roomHaze = smoothAttackRelease(roomHaze, clamp01(audio.loudness * 0.62f + audio.rms * 0.64f + audio.lowMid * 0.08f), 0.38f, 0.10f);
    motionEnergy = smoothAttackRelease(
      motionEnergy,
      clamp01(audio.transient * 0.24f + audio.trebleTransient * 0.18f + audio.audioChaos * 0.24f + kickRise * 0.80f + audio.loudness * 0.10f),
      0.72f,
      0.22f
    );
    stereoBalance = smoothToward(stereoBalance, audio.stereoBalance, 0.22f);
    spectralTilt = smoothToward(spectralTilt, audio.spectralTilt, 0.26f);
    audioFlowX = smoothToward(audioFlowX, audio.audioFlowX, 0.34f);
    audioFlowY = smoothToward(audioFlowY, audio.audioFlowY, 0.34f);
    audioCurl = smoothToward(audioCurl, audio.audioCurl, 0.26f);
    audioChaos = smoothToward(audioChaos, audio.audioChaos, 0.32f);

    float warmth = clamp01(subBassEnergy * 0.44f + bassEnergy * 0.34f + kickEnergy * 0.22f);
    float brightness = clamp01(trebleEnergy * 0.38f + spectralTilt * 0.34f + audio.spectralCentroidHz / 18000.0f * 0.18f);
    float colorTarget = wrap01(0.16f + audio.colorBase * 0.14f + warmth * 0.25f + brightness * 0.18f + audioCurl * 0.08f + stereoBalance * 0.05f);
    colorBase = smoothTone(colorBase, colorTarget, 0.18f + audio.transient * 0.06f + audio.trebleTransient * 0.04f);

    float bassHit = clamp01(kickRise * 1.85f + audio.bassTransient * (0.70f + kick * 0.34f) + audio.subBass * audio.bassTransient * 0.20f + audio.transient * 0.12f);
    if (bassHit > bassBloom) {
      bassBloom = bassHit;
    }

    float strobeSignal = audio.trebleTransient * 0.42f + audio.transient * 0.26f + kickRise * 0.16f + audio.bassTransient * 0.10f;
    float strobeHit = clamp01((strobeSignal - 0.42f) * 1.75f);
    if (strobeHit > strobeFlash && strobeHit > 0.08f && now - lastStrobeMs >= AFTERGLOW_MIN_STROBE_MS) {
      strobeFlash = strobeHit;
      lastStrobeMs = now;
    }

    float trebleRise = audio.treble - previousTrebleEnergy;
    if (trebleRise < 0.0f) {
      trebleRise = 0.0f;
    }
    float glintHit = clamp01(audio.trebleTransient * 0.92f + audio.transient * 0.22f + trebleRise * 0.92f + audio.audioChaos * 0.12f - 0.08f);
    if (glintHit > pendingTrebleGlint) {
      pendingTrebleGlint = glintHit;
    }
  }

  void settleTowardSilence() {
    subBassEnergy = smoothToward(subBassEnergy, 0.0f, 0.06f);
    bassEnergy = smoothToward(bassEnergy, 0.0f, 0.06f);
    kickEnergy = smoothToward(kickEnergy, 0.0f, 0.07f);
    lowMidEnergy = smoothToward(lowMidEnergy, 0.0f, 0.05f);
    midEnergy = smoothToward(midEnergy, 0.0f, 0.05f);
    trebleEnergy = smoothToward(trebleEnergy, 0.0f, 0.06f);
    loudEnergy = smoothToward(loudEnergy, 0.0f, 0.05f);
    roomHaze = smoothToward(roomHaze, 0.0f, 0.05f);
    motionEnergy = smoothToward(motionEnergy, 0.0f, 0.06f);
    bassBloom *= 0.90f;
    strobeFlash *= 0.25f;
    pendingTrebleGlint *= 0.76f;
  }

  float animationFrameScale(uint32_t now) {
    if (lastRenderMs == 0) {
      lastRenderMs = now;
      return 1.0f;
    }

    uint32_t elapsedMs = now - lastRenderMs;
    lastRenderMs = now;

    float scale = (float)elapsedMs / AFTERGLOW_FRAME_MS;
    if (scale < 0.62f) scale = 0.62f;
    if (scale > 1.35f) scale = 1.35f;
    return scale;
  }

  void fadeCanvas(float frameScale) {
    float fade = 0.890f + roomHaze * 0.048f + loudEnergy * 0.022f;
    if (fade > 0.962f) {
      fade = 0.962f;
    }
    fade = powf(fade, frameScale);

    for (uint16_t i = 0; i < LED_COUNT; i++) {
      canvasR[i] *= fade;
      canvasG[i] *= fade;
      canvasB[i] *= fade;

      if (canvasR[i] < 0.08f) canvasR[i] = 0.0f;
      if (canvasG[i] < 0.08f) canvasG[i] = 0.0f;
      if (canvasB[i] < 0.08f) canvasB[i] = 0.0f;
    }
  }

  void drawLightField() {
    float treblePop = clamp01(pendingTrebleGlint * 0.72f + strobeFlash * 0.40f + trebleEnergy * 0.18f);
    float stereoMotion = clamp01(fabsf(stereoBalance) * 0.55f + fabsf(audioFlowX) * 0.28f + fabsf(audioCurl) * 0.22f);
    float hazeEnergy = clamp01(roomHaze * 0.58f + loudEnergy * 0.14f + lowMidEnergy * 0.16f + bassEnergy * 0.06f);
    float floorEnergy = clamp01(subBassEnergy * 0.52f + bassEnergy * 0.36f + bassBloom * 0.50f + kickEnergy * 0.14f);
    float beamEnergy = clamp01(motionEnergy * 0.64f + treblePop * 0.46f + midEnergy * 0.22f + stereoMotion * 0.20f + loudEnergy * 0.18f + roomHaze * 0.12f);
    float bloomEnergy = bassBloom;
    float flash = clamp01(strobeFlash);
    flash = flash * flash * (0.070f + audioChaos * 0.025f + trebleEnergy * 0.020f);

    if (hazeEnergy < 0.010f && floorEnergy < 0.010f && beamEnergy < 0.018f && bloomEnergy < 0.035f && flash < 0.010f) {
      return;
    }

    float hazeR1 = 0.0f;
    float hazeG1 = 0.0f;
    float hazeB1 = 0.0f;
    float hazeR2 = 0.0f;
    float hazeG2 = 0.0f;
    float hazeB2 = 0.0f;
    float floorR = 0.0f;
    float floorG = 0.0f;
    float floorB = 0.0f;
    float bloomR = 0.0f;
    float bloomG = 0.0f;
    float bloomB = 0.0f;
    float backlightR = 0.0f;
    float backlightG = 0.0f;
    float backlightB = 0.0f;

    afterglowPalette(wrap01(colorBase + paletteDrift + treblePop * 0.035f), hazeR1, hazeG1, hazeB1);
    afterglowPalette(wrap01(colorBase + paletteDrift + 0.16f + trebleEnergy * 0.04f + stereoBalance * 0.035f), hazeR2, hazeG2, hazeB2);
    afterglowPalette(wrap01(colorBase + 0.22f + floorEnergy * 0.10f), floorR, floorG, floorB);
    afterglowPalette(wrap01(colorBase + 0.25f + bloomEnergy * 0.08f), bloomR, bloomG, bloomB);
    afterglowPalette(wrap01(colorBase + paletteDrift + 0.50f + treblePop * 0.12f + stereoBalance * 0.04f), backlightR, backlightG, backlightB);

    float beamCa[3];
    float beamSa[3];
    float beamCenter[3];
    float beamInvWidth[3];
    float beamLevel[3];
    float beamR[3];
    float beamG[3];
    float beamB[3];
    uint8_t beamCount = beamEnergy > 0.018f ? 3 : 0;

    for (uint8_t beam = 0; beam < beamCount; beam++) {
      float beamOffset = (float)beam * 2.13f;
      float angle = -0.78f + beamOffset * 0.72f + audioCurl * 0.54f + audioFlowY * 0.18f + sinf(sweepPhase * 0.28f + beamOffset) * 0.22f;
      beamCa[beam] = cosf(angle);
      beamSa[beam] = sinf(angle);
      beamCenter[beam] = (sinf(sweepPhase * (0.78f + beam * 0.19f) + beamOffset + stereoBalance * 0.80f) + stereoBalance * 0.42f + audioFlowX * 0.20f) * ((float)LED_DRIVER_GRID_WIDTH * 0.45f);
      beamInvWidth[beam] = 1.0f / (3.0f + beamEnergy * 4.6f + treblePop * 1.4f + (float)beam * 0.55f);
      beamLevel[beam] = (0.012f + beamEnergy * 0.042f + treblePop * 0.032f + bloomEnergy * 0.014f) * (1.0f - (float)beam * 0.10f);
      afterglowPalette(wrap01(colorBase + paletteDrift + 0.10f + (float)beam * 0.19f + treblePop * 0.11f + stereoBalance * 0.035f), beamR[beam], beamG[beam], beamB[beam]);
    }

    float centerX = ((float)LED_DRIVER_GRID_WIDTH - 1.0f) * 0.5f;
    float centerY = ((float)LED_DRIVER_GRID_HEIGHT - 1.0f) * 0.5f;
    float floorCenterX = clamp01(0.5f + stereoBalance * 0.25f + audioFlowX * 0.10f);
    float bloomCenterX = clamp01(0.5f + stereoBalance * 0.34f + audioFlowX * 0.11f) * (float)(LED_DRIVER_GRID_WIDTH - 1);
    float bloomCenterY = clamp01(0.12f + subBassEnergy * 0.18f + audioFlowY * 0.08f) * (float)(LED_DRIVER_GRID_HEIGHT - 1);
    float bloomRadius = 7.0f + bloomEnergy * 9.8f + subBassEnergy * 2.5f;
    float invBloomRadiusSq = 1.0f / (bloomRadius * bloomRadius + 0.001f);
    uint8_t hazePhase = (uint8_t)(sweepPhase * 28.0f + audioCurl * 19.0f);
    uint8_t floorPhase = (uint8_t)(sweepPhase * 36.0f + bassEnergy * 21.0f);

    for (uint16_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
      float yNorm = (float)y / (float)(LED_DRIVER_GRID_HEIGHT - 1);
      float dyNorm = ((float)y - centerY) / (float)LED_DRIVER_GRID_HEIGHT;
      float bottom = 1.0f - yNorm;
      float floorVertical = bottom * bottom * (1.45f - bottom * 0.32f);

      for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
        float xNorm = (float)x / (float)(LED_DRIVER_GRID_WIDTH - 1);
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;

        if (flash > 0.010f) {
          float dxNorm = ((float)x - centerX) / (float)LED_DRIVER_GRID_WIDTH;
          float softVignette = 1.0f - clamp01((dxNorm * dxNorm + dyNorm * dyNorm) * 1.55f);
          float level = flash * (0.018f + softVignette * 0.052f + treblePop * 0.018f);
          r += backlightR * level;
          g += backlightG * level;
          b += backlightB * level;
        }

        if (hazeEnergy > 0.010f) {
          float dxNorm = ((float)x - centerX) / (float)LED_DRIVER_GRID_WIDTH;
          float softVignette = 1.0f - clamp01((dxNorm * dxNorm + dyNorm * dyNorm) * 2.15f);
          float shimmer = triangleByte((uint8_t)(x * 11u + y * 17u + hazePhase));
          float level = hazeEnergy * (0.013f + softVignette * 0.015f + shimmer * 0.010f);
          float colorMix = 0.22f + shimmer * 0.34f + xNorm * 0.08f + yNorm * 0.06f + treblePop * 0.12f;
          r += mixFloat(hazeR1, hazeR2, colorMix) * level;
          g += mixFloat(hazeG1, hazeG2, colorMix) * level;
          b += mixFloat(hazeB1, hazeB2, colorMix) * level;
        }

        if (floorEnergy > 0.010f && floorVertical > 0.001f) {
          float spread = 1.0f - clamp01(fabsf(xNorm - floorCenterX) * (1.5f - bassEnergy * 0.45f));
          float ripple = 0.76f + 0.24f * triangleByte((uint8_t)(x * 13u + floorPhase));
          float level = floorVertical * spread * ripple * (0.018f + floorEnergy * 0.088f + bloomEnergy * 0.048f);
          r += floorR * level;
          g += floorG * level;
          b += floorB * level;
        }

        if (beamCount > 0) {
          float dx = (float)x - centerX;
          float dy = (float)y - centerY;
          for (uint8_t beam = 0; beam < beamCount; beam++) {
            float projection = dx * beamCa[beam] + dy * beamSa[beam];
            float edge = 1.0f - fabsf(projection - beamCenter[beam]) * beamInvWidth[beam];
            if (edge <= 0.0f) {
              continue;
            }

            edge = edge * edge * (3.0f - 2.0f * edge);
            float texture = 0.86f + 0.14f * triangleByte((uint8_t)(x * 19u + y * 7u + hazePhase + beam * 53u));
            float level = edge * beamLevel[beam] * texture;
            r += beamR[beam] * level;
            g += beamG[beam] * level;
            b += beamB[beam] * level;
          }
        }

        if (bloomEnergy > 0.035f) {
          float dx = ((float)x - bloomCenterX) * 0.88f;
          float dy = ((float)y - bloomCenterY) * 1.35f;
          float edge = 1.0f - (dx * dx + dy * dy) * invBloomRadiusSq;
          if (edge > 0.0f) {
            edge *= edge;
            float level = edge * (0.030f + bloomEnergy * 0.145f + kickEnergy * 0.032f);
            r += bloomR * level;
            g += bloomG * level;
            b += bloomB * level;
          }
        }

        if (r > 0.0f || g > 0.0f || b > 0.0f) {
          addPixel(ledIndexXY(x, y), r, g, b);
        }
      }
    }
  }

  void drawRoomHaze() {
    if (roomHaze < 0.012f && loudEnergy < 0.012f) {
      return;
    }

    float centerX = ((float)LED_DRIVER_GRID_WIDTH - 1.0f) * 0.5f;
    float centerY = ((float)LED_DRIVER_GRID_HEIGHT - 1.0f) * 0.5f;
    float pushX = stereoBalance * 0.18f + audioFlowX * 0.08f;
    float pushY = audioFlowY * 0.08f + (spectralTilt - 0.5f) * 0.08f;

    for (uint16_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
      float yNorm = (float)y / (float)(LED_DRIVER_GRID_HEIGHT - 1);
      for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
        float xNorm = (float)x / (float)(LED_DRIVER_GRID_WIDTH - 1);
        float dx = ((float)x - centerX) / (float)LED_DRIVER_GRID_WIDTH;
        float dy = ((float)y - centerY) / (float)LED_DRIVER_GRID_HEIGHT;
        float softVignette = 1.0f - clamp01(sqrtf(dx * dx + dy * dy) * 1.25f);
        float drift = sinf((float)x * 0.23f + (float)y * 0.31f + sweepPhase * 1.70f + audioCurl * 1.2f);
        float shimmer = 0.5f + 0.5f * sinf((xNorm + pushX) * 5.6f - (yNorm + pushY) * 3.8f + sweepPhase * 0.82f);
        float level = roomHaze * (0.014f + softVignette * 0.014f + shimmer * 0.010f) + loudEnergy * 0.006f + drift * roomHaze * 0.002f;
        if (level <= 0.0f) {
          continue;
        }

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float tone = wrap01(colorBase + paletteDrift + xNorm * 0.050f + yNorm * 0.060f + shimmer * 0.030f);
        afterglowPalette(tone, r, g, b);
        addPixel(ledIndexXY(x, y), r * level, g * level, b * level);
      }
    }
  }

  void drawBassFloor() {
    float floorEnergy = clamp01(subBassEnergy * 0.52f + bassEnergy * 0.36f + bassBloom * 0.50f + kickEnergy * 0.14f);
    if (floorEnergy < 0.010f) {
      return;
    }

    float centerXNorm = clamp01(0.5f + stereoBalance * 0.25f + audioFlowX * 0.10f);
    for (uint16_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
      float yNorm = (float)y / (float)(LED_DRIVER_GRID_HEIGHT - 1);
      float bottom = 1.0f - yNorm;
      float vertical = bottom * bottom * (1.45f - bottom * 0.32f);
      if (vertical <= 0.001f) {
        continue;
      }

      for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
        float xNorm = (float)x / (float)(LED_DRIVER_GRID_WIDTH - 1);
        float spread = 1.0f - clamp01(fabsf(xNorm - centerXNorm) * (1.5f - bassEnergy * 0.45f));
        float ripple = 0.72f + 0.28f * sinf((float)x * 0.30f + sweepPhase * 2.2f + bassEnergy * 1.4f);
        float level = vertical * spread * ripple * (0.020f + floorEnergy * 0.100f);

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        afterglowPalette(wrap01(colorBase + 0.22f + floorEnergy * 0.10f + xNorm * 0.040f), r, g, b);
        addPixel(ledIndexXY(x, y), r * level, g * level, b * level);
      }
    }
  }

  void drawMovingBeams() {
    float beamEnergy = clamp01(loudEnergy * 0.62f + motionEnergy * 0.58f + midEnergy * 0.22f + roomHaze * 0.26f);
    if (beamEnergy < 0.018f) {
      return;
    }

    float centerX = ((float)LED_DRIVER_GRID_WIDTH - 1.0f) * 0.5f;
    float centerY = ((float)LED_DRIVER_GRID_HEIGHT - 1.0f) * 0.5f;

    for (uint8_t beam = 0; beam < 3; beam++) {
      float beamOffset = (float)beam * 2.13f;
      float angle = -0.78f + beamOffset * 0.72f + audioCurl * 0.42f + sinf(sweepPhase * 0.28f + beamOffset) * 0.22f;
      float ca = cosf(angle);
      float sa = sinf(angle);
      float center = sinf(sweepPhase * (0.78f + beam * 0.19f) + beamOffset + stereoBalance * 0.80f) * ((float)LED_DRIVER_GRID_WIDTH * 0.45f);
      float width = 3.6f + beamEnergy * 5.2f + (float)beam * 0.70f;
      float beamLevel = (0.014f + beamEnergy * 0.046f + bassBloom * 0.018f) * (1.0f - (float)beam * 0.12f);

      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;
      float tone = wrap01(colorBase + paletteDrift + 0.11f + (float)beam * 0.19f + trebleEnergy * 0.08f);
      afterglowPalette(tone, r, g, b);

      for (uint16_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
        for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
          float dx = (float)x - centerX;
          float dy = (float)y - centerY;
          float projection = dx * ca + dy * sa;
          float edge = 1.0f - fabsf(projection - center) / width;
          if (edge <= 0.0f) {
            continue;
          }

          edge = edge * edge * (3.0f - 2.0f * edge);
          float shimmer = 0.82f + 0.18f * sinf((float)x * 0.46f - (float)y * 0.27f + sweepPhase * 2.6f + beamOffset);
          float level = edge * beamLevel * shimmer;
          addPixel(ledIndexXY(x, y), r * level, g * level, b * level);
        }
      }
    }
  }

  void drawBassBloom() {
    if (bassBloom < 0.035f) {
      return;
    }

    float centerX = clamp01(0.5f + stereoBalance * 0.34f + audioFlowX * 0.11f) * (float)(LED_DRIVER_GRID_WIDTH - 1);
    float centerY = clamp01(0.12f + subBassEnergy * 0.18f + audioFlowY * 0.08f) * (float)(LED_DRIVER_GRID_HEIGHT - 1);
    float radius = 7.0f + bassBloom * 9.8f + subBassEnergy * 2.5f;
    float warmth = clamp01(subBassEnergy * 0.35f + bassEnergy * 0.35f + bassBloom * 0.30f);

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    afterglowPalette(wrap01(colorBase + 0.25f + warmth * 0.08f), r, g, b);

    for (uint16_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
      for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
        float dx = ((float)x - centerX) * 0.88f;
        float dy = ((float)y - centerY) * 1.35f;
        float distance = sqrtf(dx * dx + dy * dy);
        float edge = 1.0f - distance / radius;
        if (edge <= 0.0f) {
          continue;
        }

        edge = edge * edge;
        float level = edge * (0.030f + bassBloom * 0.145f + kickEnergy * 0.032f);
        addPixel(ledIndexXY(x, y), r * level, g * level, b * level);
      }
    }
  }

  void spawnGlints(uint32_t now) {
    if (pendingTrebleGlint < 0.18f || now - lastGlintMs < AFTERGLOW_MIN_GLINT_MS) {
      return;
    }

    uint8_t count = 1;
    if (pendingTrebleGlint > 0.42f) count++;
    if (pendingTrebleGlint > 0.68f && audioChaos > 0.18f) count++;
    if (pendingTrebleGlint > 0.90f && nextRandomFloat() < pendingTrebleGlint) count++;

    for (uint8_t i = 0; i < count; i++) {
      spawnGlint(pendingTrebleGlint);
    }

    lastGlintMs = now;
    pendingTrebleGlint *= 0.42f;
  }

  void spawnGlint(float strength) {
    Glint& glint = glints[chooseGlintSlot()];
    float xNorm = nextRandomFloat();
    float yNorm = nextRandomFloat();
    float roll = nextRandomFloat();

    if (roll < 0.34f) {
      xNorm = clamp01(0.5f + stereoBalance * 0.42f + audioFlowX * 0.12f + (nextRandomFloat() - 0.5f) * (0.45f + trebleEnergy * 0.25f));
      yNorm = clamp01(0.52f + spectralTilt * 0.28f + audioFlowY * 0.10f + (nextRandomFloat() - 0.5f) * 0.34f);
    } else if (roll > 0.74f) {
      uint8_t edge = nextRandomU32() & 0x03;
      if (edge == 0) {
        xNorm = nextRandomFloat();
        yNorm = 0.02f + nextRandomFloat() * 0.10f;
      } else if (edge == 1) {
        xNorm = nextRandomFloat();
        yNorm = 0.88f + nextRandomFloat() * 0.10f;
      } else if (edge == 2) {
        xNorm = 0.02f + nextRandomFloat() * 0.10f;
        yNorm = nextRandomFloat();
      } else {
        xNorm = 0.88f + nextRandomFloat() * 0.10f;
        yNorm = nextRandomFloat();
      }
    }

    glint.x = xNorm * (float)(LED_DRIVER_GRID_WIDTH - 1);
    glint.y = yNorm * (float)(LED_DRIVER_GRID_HEIGHT - 1);
    glint.vx = (nextRandomFloat() - 0.5f) * (0.10f + audioChaos * 0.18f) + audioFlowX * 0.05f + stereoBalance * 0.025f;
    glint.vy = (nextRandomFloat() - 0.5f) * (0.08f + audioChaos * 0.15f) + audioFlowY * 0.05f + (spectralTilt - 0.5f) * 0.030f;
    glint.life = 0.0f;
    glint.maxLife = 4.0f + strength * 6.5f + trebleEnergy * 3.8f;
    glint.brightness = 0.34f + strength * 0.48f + trebleEnergy * 0.16f;
    glint.tone = wrap01(colorBase + 0.62f + nextRandomFloat() * 0.22f + trebleEnergy * 0.08f);
    glint.size = 0.50f + strength * 0.62f + audioChaos * 0.35f;
    glint.active = true;
  }

  void updateGlints(float frameScale) {
    for (uint8_t i = 0; i < AFTERGLOW_MAX_GLINTS; i++) {
      Glint& glint = glints[i];
      if (!glint.active) {
        continue;
      }

      glint.x += glint.vx * frameScale;
      glint.y += glint.vy * frameScale;
      glint.vx += (-glint.vy * audioCurl * 0.014f + audioFlowX * 0.006f) * frameScale;
      glint.vy += (glint.vx * audioCurl * 0.014f + audioFlowY * 0.006f) * frameScale;
      glint.life += frameScale;

      if (glint.life >= glint.maxLife || glint.x < -1.0f || glint.x > (float)LED_DRIVER_GRID_WIDTH || glint.y < -1.0f || glint.y > (float)LED_DRIVER_GRID_HEIGHT) {
        glint.active = false;
        continue;
      }

      float life = 1.0f - glint.life / glint.maxLife;
      float pop = life * life * (3.0f - 2.0f * life);
      float level = glint.brightness * pop * (0.46f + trebleEnergy * 0.30f + pendingTrebleGlint * 0.12f);

      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;
      afterglowPalette(wrap01(glint.tone + paletteDrift + glint.life * 0.010f), r, g, b);
      float white = clamp01(level * (0.16f + trebleEnergy * 0.16f)) * 58.0f;
      drawGlint(glint.x, glint.y, glint.size, r * level + white, g * level + white, b * level + white);
    }
  }

  void drawGlint(float x, float y, float size, float r, float g, float b) {
    int16_t centerX = (int16_t)roundf(x);
    int16_t centerY = (int16_t)roundf(y);

    addPixelSafe(centerX, centerY, r, g, b);

    float side = 0.22f + size * 0.12f;
    addPixelSafe(centerX - 1, centerY, r * side, g * side, b * side);
    addPixelSafe(centerX + 1, centerY, r * side, g * side, b * side);
    addPixelSafe(centerX, centerY - 1, r * side, g * side, b * side);
    addPixelSafe(centerX, centerY + 1, r * side, g * side, b * side);

    if (size > 0.90f) {
      float far = side * 0.32f;
      addPixelSafe(centerX - 2, centerY, r * far, g * far, b * far);
      addPixelSafe(centerX + 2, centerY, r * far, g * far, b * far);
      addPixelSafe(centerX, centerY - 2, r * far, g * far, b * far);
      addPixelSafe(centerX, centerY + 2, r * far, g * far, b * far);
    }
  }

  void afterglowPalette(float tone, float& r, float& g, float& b) const {
    tone = wrap01(tone);

    static constexpr uint8_t palette[][3] = {
      {  0, 210, 255},
      { 46,  88, 255},
      {154,  34, 255},
      {255,  32, 200},
      {255,  62,  88},
      {255, 166,  38},
      {160, 255,  44},
      { 28, 238, 184},
      {  0, 210, 255}
    };
    static constexpr uint8_t paletteCount = sizeof(palette) / sizeof(palette[0]);

    float scaled = tone * (float)(paletteCount - 1);
    uint8_t index = (uint8_t)scaled;
    if (index >= paletteCount - 1) {
      index = paletteCount - 2;
    }
    float mix = scaled - (float)index;

    r = (float)palette[index][0] + ((float)palette[index + 1][0] - (float)palette[index][0]) * mix;
    g = (float)palette[index][1] + ((float)palette[index + 1][1] - (float)palette[index][1]) * mix;
    b = (float)palette[index][2] + ((float)palette[index + 1][2] - (float)palette[index][2]) * mix;
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

  uint8_t chooseGlintSlot() const {
    uint8_t slot = 0;
    float oldestScore = -1.0f;

    for (uint8_t i = 0; i < AFTERGLOW_MAX_GLINTS; i++) {
      if (!glints[i].active) {
        return i;
      }

      float score = glints[i].life / glints[i].maxLife;
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

  float mixFloat(float a, float b, float amount) const {
    return a + (b - a) * clamp01(amount);
  }

  float triangleByte(uint8_t value) const {
    uint8_t folded = value < 128 ? value : (uint8_t)(255 - value);
    return (float)folded / 127.0f;
  }

  float smoothTone(float current, float target, float amount) const {
    float delta = target - current;
    if (delta > 0.5f) {
      delta -= 1.0f;
    } else if (delta < -0.5f) {
      delta += 1.0f;
    }

    return wrap01(current + delta * amount);
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
