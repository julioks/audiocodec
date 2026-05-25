#pragma once

#include "AudioVisualizer.h"
#include <math.h>

static constexpr bool LED_CANDY_SPECTRUM_REVERSE_X = true;
static constexpr float LED_CANDY_SPECTRUM_DB_FLOOR = -78.0f;
static constexpr float LED_CANDY_SPECTRUM_DB_CEILING = -3.0f;
static constexpr float LED_CANDY_SPECTRUM_SILENCE_DB = -96.0f;
static constexpr uint8_t LED_CANDY_MAX_STARS = 220;
static constexpr float LED_CANDY_COLUMN_TONE_SPAN = 0.14f;
static constexpr float LED_CANDY_BAR_FILL_GRADIENT_SPAN = 0.66f;
static constexpr float LED_CANDY_COLD_BAR_BASE = 0.50f;
static constexpr float LED_CANDY_COLD_BAR_SPAN = 0.40f;
static constexpr float LED_CANDY_WARM_BAR_BASE = 0.94f;
static constexpr float LED_CANDY_WARM_BAR_SPAN = 0.40f;

class CandyPeakSpectrumVisualizer : public AudioVisualizer {
public:
  const char* name() const override {
    return "candy";
  }

  void begin() override {
    reset();
  }

  void reset() override {
    ditherFrame = 0;
    highAverage = 0.0f;
    highSpark = 0.0f;
    highHit = 0.0f;
    bassGradientEnergy = 0.0f;
    starCooldown = 0;
    starSeed = 0xA53C9E41UL;
    for (uint16_t column = 0; column < LED_DRIVER_GRID_WIDTH; column++) {
      columnDb[column] = LED_CANDY_SPECTRUM_SILENCE_DB;
      columnLevel[column] = 0.0f;
      peakLevel[column] = 0.0f;
      peakFall[column] = 0.0f;
      columnAverage[column] = 0.0f;
    }

    for (uint8_t i = 0; i < LED_CANDY_MAX_STARS; i++) {
      stars[i].active = false;
      stars[i].brightness = 0.0f;
    }
  }

  void render(Adafruit_NeoPixel& pixels, const AudioAnalysisFrame& audio) override {
    if (!audio.ready) {
      pixels.clear();
      return;
    }

    updateColumns(audio);
    updatePeaks(audio);
    updateStars(audio);
    updateBassGradient(audio);

    float paletteWarmth = bassGradientWarmth();

    for (uint16_t x = 0; x < LED_DRIVER_GRID_WIDTH; x++) {
      uint16_t column = LED_CANDY_SPECTRUM_REVERSE_X
        ? (LED_DRIVER_GRID_WIDTH - 1 - x)
        : x;

      float level = columnLevel[column];
      float peak = peakLevel[column];
      uint16_t barHeight = (uint16_t)roundf(level * (float)LED_DRIVER_GRID_HEIGHT);
      if (barHeight > LED_DRIVER_GRID_HEIGHT) {
        barHeight = LED_DRIVER_GRID_HEIGHT;
      }

      uint16_t peakY = 0;
      if (peak > 0.01f) {
        peakY = (uint16_t)roundf(peak * (float)(LED_DRIVER_GRID_HEIGHT - 1));
        if (peakY >= LED_DRIVER_GRID_HEIGHT) {
          peakY = LED_DRIVER_GRID_HEIGHT - 1;
        }
      }

      for (uint16_t y = 0; y < LED_DRIVER_GRID_HEIGHT; y++) {
        uint16_t pixelIndex = ledIndexXY(x, y);

        if (y < barHeight) {
          float fill = (barHeight <= 1) ? 1.0f : (float)y / (float)(barHeight - 1);
          pixels.setPixelColor(pixelIndex, barColor(pixels, pixelIndex, column, level, fill, paletteWarmth));
        } else if (y == peakY && peak > 0.01f) {
          pixels.setPixelColor(pixelIndex, peakColor(pixels, pixelIndex, column, peak, paletteWarmth));
        } else if (y < peakY && y + 3 >= peakY && peak > level + 0.05f) {
          float trail = 1.0f - ((float)(peakY - y) / 3.0f);
          pixels.setPixelColor(pixelIndex, peakTrailColor(pixels, pixelIndex, column, trail, paletteWarmth));
        } else {
          int8_t starIndex = starAt(x, y);
          if (starIndex >= 0) {
            pixels.setPixelColor(pixelIndex, starColor(pixels, pixelIndex, (uint8_t)starIndex, paletteWarmth));
          } else {
            pixels.setPixelColor(pixelIndex, 0);
          }
        }
      }
    }

    ditherFrame++;
  }

private:
  float columnDb[LED_DRIVER_GRID_WIDTH];
  float columnLevel[LED_DRIVER_GRID_WIDTH];
  float peakLevel[LED_DRIVER_GRID_WIDTH];
  float peakFall[LED_DRIVER_GRID_WIDTH];
  float columnAverage[LED_DRIVER_GRID_WIDTH];
  float highAverage = 0.0f;
  float highSpark = 0.0f;
  float highHit = 0.0f;
  float bassGradientEnergy = 0.0f;
  uint32_t starSeed = 0xA53C9E41UL;
  uint8_t ditherFrame = 0;
  uint8_t starCooldown = 0;

  struct Star {
    bool active;
    uint8_t x;
    uint8_t y;
    float brightness;
    float tone;
  };

  Star stars[LED_CANDY_MAX_STARS];

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

  void updatePeaks(const AudioAnalysisFrame& audio) {
    float highNow = 0.0f;
    float highLift = 0.0f;
    uint16_t highColumns = 0;
    uint16_t highStart = (LED_DRIVER_GRID_WIDTH * 5) / 8;

    for (uint16_t column = highStart; column < LED_DRIVER_GRID_WIDTH; column++) {
      highNow += columnLevel[column];
      float lift = columnLevel[column] - columnAverage[column];
      if (lift > 0.0f) {
        highLift += lift;
      }
      highColumns++;
    }

    if (highColumns > 0) {
      highNow /= (float)highColumns;
      highLift /= (float)highColumns;
    }

    float highPop = clamp01((highNow - highAverage) * 3.0f + highLift * 4.0f + audio.trebleTransient * 1.2f);
    highHit = highPop;
    highAverage = highAverage * 0.95f + highNow * 0.05f;
    if (highPop > highSpark) {
      highSpark = highPop;
    } else {
      highSpark *= 0.82f;
    }
    for (uint16_t column = 0; column < LED_DRIVER_GRID_WIDTH; column++) {
      float level = columnLevel[column];
      float average = columnAverage[column];
      columnAverage[column] = average * 0.94f + level * 0.06f;

      if (level >= peakLevel[column]) {
        peakLevel[column] = level;
        peakFall[column] = 0.006f;
      } else {
        peakFall[column] += 0.0018f + audio.loudness * 0.0012f;
        peakLevel[column] -= peakFall[column];
        if (peakLevel[column] < level) {
          peakLevel[column] = level;
          peakFall[column] = 0.004f;
        }
      }
    }
  }

  void updateStars(const AudioAnalysisFrame& audio) {
    for (uint8_t i = 0; i < LED_CANDY_MAX_STARS; i++) {
      if (!stars[i].active) {
        continue;
      }

      uint16_t column = LED_CANDY_SPECTRUM_REVERSE_X
        ? (LED_DRIVER_GRID_WIDTH - 1 - stars[i].x)
        : stars[i].x;
      uint16_t peakY = peakYForColumn(column);
      if (stars[i].y <= peakY || stars[i].brightness < 0.035f) {
        stars[i].active = false;
        continue;
      }

      stars[i].brightness *= 0.88f;
    }

    if (starCooldown > 0) {
      starCooldown--;
      return;
    }

    if (highHit < 0.16f) {
      return;
    }

    uint8_t spawnCount = 1;
    if (highHit > 0.30f) spawnCount++;
    if (highHit > 0.50f) spawnCount++;
    if (highHit > 0.72f) spawnCount++;

    for (uint8_t i = 0; i < spawnCount; i++) {
      spawnStar(audio.audioSeed);
    }

    starCooldown = highHit > 0.42f ? 1 : 2;
  }

  void spawnStar(uint32_t audioSeed) {
    uint16_t column = 0;
    uint16_t x = 0;
    uint16_t peakY = 0;

    for (uint8_t attempt = 0; attempt < 8; attempt++) {
      column = (uint16_t)(nextRandom(audioSeed) % LED_DRIVER_GRID_WIDTH);
      x = LED_CANDY_SPECTRUM_REVERSE_X
        ? (LED_DRIVER_GRID_WIDTH - 1 - column)
        : column;
      peakY = peakYForColumn(column);

      if (peakY + 1 < LED_DRIVER_GRID_HEIGHT) {
        break;
      }
    }

    if (peakY + 1 >= LED_DRIVER_GRID_HEIGHT) {
      return;
    }

    uint16_t available = LED_DRIVER_GRID_HEIGHT - peakY - 1;
    uint16_t y = peakY + 1 + (uint16_t)(nextRandom(audioSeed) % available);
    uint8_t slot = findStarSlot();

    stars[slot].active = true;
    stars[slot].x = (uint8_t)x;
    stars[slot].y = (uint8_t)y;
    stars[slot].brightness = 0.68f + highHit * 0.32f;
    stars[slot].tone = wrap01(0.02f + (float)(nextRandom(audioSeed) & 0xFF) / 255.0f * 0.30f);
  }

  uint8_t findStarSlot() const {
    uint8_t dimmest = 0;
    for (uint8_t i = 0; i < LED_CANDY_MAX_STARS; i++) {
      if (!stars[i].active) {
        return i;
      }
      if (stars[i].brightness < stars[dimmest].brightness) {
        dimmest = i;
      }
    }
    return dimmest;
  }

  uint32_t nextRandom(uint32_t audioSeed) {
    starSeed = starSeed * 1664525UL + 1013904223UL + audioSeed + ditherFrame;
    return starSeed;
  }

  uint16_t peakYForColumn(uint16_t column) const {
    float peak = peakLevel[column];
    if (peak <= 0.01f) {
      return 0;
    }

    uint16_t peakY = (uint16_t)roundf(peak * (float)(LED_DRIVER_GRID_HEIGHT - 1));
    if (peakY >= LED_DRIVER_GRID_HEIGHT) {
      peakY = LED_DRIVER_GRID_HEIGHT - 1;
    }
    return peakY;
  }

  int8_t starAt(uint16_t x, uint16_t y) const {
    for (uint8_t i = 0; i < LED_CANDY_MAX_STARS; i++) {
      if (stars[i].active && stars[i].x == x && stars[i].y == y) {
        return (int8_t)i;
      }
    }
    return -1;
  }

  void updateBassGradient(const AudioAnalysisFrame& audio) {
    float bassTarget = clamp01(audio.bass * 0.74f + audio.subBass * 0.18f + audio.lowMid * 0.08f + audio.kick * 0.08f);
    float amount = bassTarget > bassGradientEnergy ? 0.16f : 0.045f;
    bassGradientEnergy = smoothToward(bassGradientEnergy, bassTarget, amount);
  }

  float strongestBandDbInRange(const AudioAnalysisFrame& audio, float lowHz, float highHz) const {
    float strongestDb = LED_CANDY_SPECTRUM_SILENCE_DB;
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
    return clamp01((db - LED_CANDY_SPECTRUM_DB_FLOOR) / (LED_CANDY_SPECTRUM_DB_CEILING - LED_CANDY_SPECTRUM_DB_FLOOR));
  }

  float smoothToward(float current, float target, float amount) const {
    return current * (1.0f - amount) + target * amount;
  }

  float bassGradientWarmth() const {
    float warmth = clamp01(bassGradientEnergy);
    return warmth * warmth * (3.0f - 2.0f * warmth);
  }

  float columnToneOffset(uint16_t column) const {
    float t = (float)column / (float)(LED_DRIVER_GRID_WIDTH - 1);
    return t * LED_CANDY_COLUMN_TONE_SPAN;
  }

  void candyPalette(float t, float& r, float& g, float& b) const {
    t = wrap01(t);

    static constexpr uint8_t palette[][3] = {
      {255,  34, 180},
      {255, 108,  70},
      {255, 218,  74},
      { 66, 255, 172},
      { 52, 220, 255},
      {122,  92, 255},
      {255,  72, 226}
    };
    static constexpr uint8_t paletteCount = sizeof(palette) / sizeof(palette[0]);

    float scaled = t * (float)(paletteCount - 1);
    uint8_t index = (uint8_t)scaled;
    if (index >= paletteCount - 1) {
      index = paletteCount - 2;
    }
    float mix = scaled - (float)index;

    r = (float)palette[index][0] + ((float)palette[index + 1][0] - (float)palette[index][0]) * mix;
    g = (float)palette[index][1] + ((float)palette[index + 1][1] - (float)palette[index][1]) * mix;
    b = (float)palette[index][2] + ((float)palette[index + 1][2] - (float)palette[index][2]) * mix;
  }

  void mixCandyPalette(float coldTone, float warmTone, float warmth, float& r, float& g, float& b) const {
    float coldR = 0.0f;
    float coldG = 0.0f;
    float coldB = 0.0f;
    float warmR = 0.0f;
    float warmG = 0.0f;
    float warmB = 0.0f;

    candyPalette(coldTone, coldR, coldG, coldB);
    candyPalette(warmTone, warmR, warmG, warmB);

    warmth = clamp01(warmth);
    r = coldR + (warmR - coldR) * warmth;
    g = coldG + (warmG - coldG) * warmth;
    b = coldB + (warmB - coldB) * warmth;
  }

  void barGradientCandy(uint16_t column, float fill, float level, float warmth, float& r, float& g, float& b) const {
    float gradient = clamp01(fill * LED_CANDY_BAR_FILL_GRADIENT_SPAN + columnToneOffset(column) + level * 0.08f);
    float coldTone = LED_CANDY_COLD_BAR_BASE + gradient * LED_CANDY_COLD_BAR_SPAN;
    float warmTone = wrap01(LED_CANDY_WARM_BAR_BASE + gradient * LED_CANDY_WARM_BAR_SPAN);
    mixCandyPalette(coldTone, warmTone, warmth, r, g, b);
  }

  uint32_t barColor(Adafruit_NeoPixel& pixels, uint16_t pixelIndex, uint16_t column, float level, float fill, float warmth) {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    barGradientCandy(column, fill, level, warmth, r, g, b);

    float glow = 0.30f + level * 0.58f + fill * (0.18f + warmth * 0.06f);
    return visualizerColor(pixels, pixelIndex, r * glow, g * glow, b * glow, ditherFrame);
  }

  uint32_t peakColor(Adafruit_NeoPixel& pixels, uint16_t pixelIndex, uint16_t column, float peak, float warmth) {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    barGradientCandy(column, 0.92f, peak, warmth, r, g, b);

    float flash = 0.82f + peak * 0.36f;
    return visualizerColor(pixels, pixelIndex, r * flash + 80.0f, g * flash + 55.0f, b * flash + 70.0f, ditherFrame);
  }

  uint32_t peakTrailColor(Adafruit_NeoPixel& pixels, uint16_t pixelIndex, uint16_t column, float trail, float warmth) {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    barGradientCandy(column, 0.70f + clamp01(trail) * 0.20f, trail, warmth, r, g, b);

    float glow = clamp01(trail) * 0.16f;
    return visualizerColor(pixels, pixelIndex, r * glow, g * glow, b * glow, ditherFrame);
  }

  uint32_t starColor(Adafruit_NeoPixel& pixels, uint16_t pixelIndex, uint8_t starIndex, float warmth) {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float coldTone = 0.58f + stars[starIndex].tone * 0.70f;
    float warmTone = wrap01(0.98f + stars[starIndex].tone * 0.75f);
    mixCandyPalette(coldTone, warmTone, warmth * 0.45f + highHit * 0.20f, r, g, b);

    float glow = clamp01(stars[starIndex].brightness);
    return visualizerColor(pixels, pixelIndex, r * glow + 80.0f * glow, g * glow + 70.0f * glow, b * glow + 95.0f * glow, ditherFrame);
  }
};
