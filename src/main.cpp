#include <Arduino.h>
#include <Zigbee.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Courtesy http://www.instructables.com/id/How-to-Use-an-RGB-LED/?ALLSTEPS
// function to convert a color to its Red, Green, and Blue components.
void hueToRGB(uint8_t hue, uint8_t brightness, uint32_t &R, uint32_t &G, uint32_t &B)
{
  const boolean invert = true; // set true if common anode, false if common cathode

  uint16_t scaledHue = (hue * 6);
  uint8_t segment = scaledHue / 256;                    // segment 0 to 5 around the
                                                        // color wheel
  uint16_t segmentOffset = scaledHue - (segment * 256); // position within the segment

  uint8_t complement = 0;
  uint16_t prev = (brightness * (255 - segmentOffset)) / 256;
  uint16_t next = (brightness * segmentOffset) / 256;

  if (invert)
  {
    brightness = 255 - brightness;
    complement = 255;
    prev = 255 - prev;
    next = 255 - next;
  }

  switch (segment)
  {
  case 0: // red
    R = brightness;
    G = next;
    B = complement;
    break;
  case 1: // yellow
    R = prev;
    G = brightness;
    B = complement;
    break;
  case 2: // green
    R = complement;
    G = brightness;
    B = next;
    break;
  case 3: // cyan
    R = complement;
    G = prev;
    B = brightness;
    break;
  case 4: // blue
    R = next;
    G = complement;
    B = brightness;
    break;
  case 5: // magenta
  default:
    R = brightness;
    G = complement;
    B = prev;
    break;
  }
}

// Set up the rgb led names
const uint8_t ledR = D9;
const uint8_t ledG = D8;
const uint8_t ledB = D7;
const uint8_t EXTERNAL_BUTTON_PIN = D0;

const uint8_t ENDPOINT = 10;

// Button handling constants
const uint32_t DEBOUNCE_TIME_MS = 50;
const uint32_t DOUBLE_PRESS_WINDOW_MS = 300;
const uint32_t LONG_PRESS_TIME_MS = 5000;

// Button state machine
enum ButtonState
{
  BTN_IDLE,
  BTN_FIRST_PRESS,
  BTN_WAITING_SECOND,
  BTN_SECOND_PRESS,
  BTN_LONG_PRESS_ACTIVE
};

struct ButtonHandler
{
  ButtonState state;
  uint32_t pressStartTime;
  uint32_t releaseTime;
  bool isPressed;
  bool lastButtonReading;
};

ButtonHandler buttonHandler = {BTN_IDLE, 0, 0, false, HIGH};

// Effects system
enum EffectType
{
  EFFECT_NONE = 0,
  EFFECT_COLOR_WANDER = 1,
  EFFECT_LEVEL_PULSE = 2,
  EFFECT_COMBO = 3,
  EFFECT_SCENE_CHANGE = 4,
  EFFECT_FIREPLACE = 5,
  EFFECT_RAINBOW = 6,
  EFFECT_COLOR_STEPS = 7,
  EFFECT_BROKEN_ELECTRICITY = 8,
  EFFECT_BREATHING = 9,
  EFFECT_AUTO_CYCLE = 10,
  MAX_EFFECT_NUMBER = 11
};

// Special modes for LED control
enum SpecialMode
{
  MODE_NORMAL = 0,
  MODE_RESET_BLINKING = 1,
  MODE_EFFECT_BLINKING = 2
};

struct EffectState
{
  EffectType type;
  unsigned long startTime;
  float phase1, phase2, phase3; // Multiple phase counters for complex effects

  // Scene change effect state
  float sceneTargetR, sceneTargetG, sceneTargetB;    // Target color for scene change
  float sceneTargetLevel;                            // Target level for scene change
  float sceneCurrentR, sceneCurrentG, sceneCurrentB; // Current scene color during transition
  float sceneCurrentLevel;                           // Current scene level during transition
  unsigned long sceneChangeTime;                     // When the current scene change started
  unsigned long sceneHoldTime;                       // How long to hold current scene (5-10s random)
  unsigned long sceneTransitionTime;                 // How long transition should take (1-2s, set once)
  bool sceneTransitioning;                           // True if transitioning, false if holding
  
  // Auto-cycle effect state (separate from sub-effects)
  float autoCycleStartTime;                          // When current sub-effect started
  float autoCycleDuration;                           // How long current sub-effect should run
  int autoCycleSubEffect;                            // Current sub-effect (1-9)
  bool autoCycleNeedsReset;                          // Flag to reset sub-effect state
  
  // Auto-cycle transition state for smooth blending
  bool autoCycleInTransition;                        // True if transitioning between effects
  float autoCycleTransitionStart;                    // When transition started
  int autoCyclePrevEffect;                           // Previous effect (for blending from)
  float autoCyclePrevR, autoCyclePrevG, autoCyclePrevB, autoCyclePrevLevel; // Previous effect output
};

// Light state structure with current and target values
struct LightState
{
  // Base values (from Hue coordinator - the foundation for effects)
  float base_r, base_g, base_b; // Base RGB values (0.0-255.0)
  float base_level;             // Base brightness level (0.0-255.0)
  bool base_state;              // Base on/off state

  // Target values (set by Hue commands - interpolated to base)
  uint8_t target_r, target_g, target_b; // Target RGB values (0-255)
  uint8_t target_level;                 // Target brightness level (0-255)
  bool target_state;                    // Target on/off state

  // Final output values (base + effects - sent to LEDs)
  float final_r, final_g, final_b; // Final RGB after effects (0.0-255.0)
  float final_level;               // Final brightness after effects (0.0-255.0)

  // Special modes
  SpecialMode specialMode;      // Current special mode
  uint32_t modeStartTime;       // When special mode started
  uint8_t blinkCount;           // Number of blinks remaining (for effect blinking)
  uint32_t lastBlinkTime;       // Last blink toggle time
  bool blinkOn;                 // Current blink state
  float savedR, savedG, savedB; // Saved current color for blinking
  EffectType savedEffect;       // Saved effect type during blinking
};

LightState lightState = {
    // Initialize base values
    0.0f, 0.0f, 0.0f, // base RGB
    0.0f,             // base level
    false,            // base state

    // Initialize target values
    0, 0, 0, // target RGB
    255,     // target level
    false,   // target state

    // Initialize final values
    0.0f, 0.0f, 0.0f, // final RGB
    0.0f,             // final level

    // Initialize special modes
    MODE_NORMAL,      // specialMode
    0,                // modeStartTime
    0,                // blinkCount
    0,                // lastBlinkTime
    false,            // blinkOn
    0.0f, 0.0f, 0.0f, // savedR, savedG, savedB
    EFFECT_NONE       // savedEffect
};

EffectState effectState = {
    EFFECT_COLOR_WANDER, // Start with color wander effect
    0,                   // Will be set when effect starts
    0.0f, 0.0f, 0.0f,    // Phase counters

    // Scene change effect initialization
    0.0f, 0.0f, 0.0f, // sceneTargetR, sceneTargetG, sceneTargetB
    0.0f,             // sceneTargetLevel
    0.0f, 0.0f, 0.0f, // sceneCurrentR, sceneCurrentG, sceneCurrentB
    0.0f,             // sceneCurrentLevel
    0,                // sceneChangeTime
    0,                // sceneHoldTime
    0,                // sceneTransitionTime
    false             // sceneTransitioning
};

SemaphoreHandle_t colorMutex;

ZigbeeHueLight *pelarboj;

const int LED_UPDATE_RATE_MS = 20;   // 50 FPS update rate
const float TRANSITION_SPEED = 0.1f; // Interpolation speed (0.0-1.0)

// LED PWM configuration
const int LED_PWM_FREQUENCY = 5000; // 5 kHz PWM frequency for 12-bit resolution
const int LED_PWM_RESOLUTION = 12;  // 12-bit resolution (0-4095)
const int LED_PWM_MAX_VALUE = 4095; // Maximum PWM value for 12-bit

// Effect parameters
const float COLOR_WANDER_RANGE = 10.0f; // How far colors can wander from base (0-255)
const float COLOR_WANDER_SPEED = 0.01f; // Speed of color wandering
const float COLOR_STEPS_RANGE = 30.0f; // Range for rapid color steps (0-255)
const float COLOR_STEPS_INTERVAL = 1.0f; // Time between steps in seconds
const float LEVEL_PULSE_RANGE = 0.4f;   // Pulse range as fraction of base level (0.0-1.0)
const float LEVEL_PULSE_SPEED = 0.01f;  // Speed of level pulsation

// Fireplace effect parameters
const float FIREPLACE_FLICKER_SPEED = 0.08f;  // Speed of flame flickering (faster)
const float FIREPLACE_INTENSITY_RANGE = 0.3f; // How much brightness can vary (reduced range)
const float FIREPLACE_RED_BOOST = 1.1f;       // Subtle red boost for warm fire colors
const float FIREPLACE_ORANGE_MIX = 0.15f;     // Subtle orange mix to stay closer to base

// Rainbow effect parameters
const float RAINBOW_CYCLE_SPEED = 0.02f; // Speed of color spectrum cycling (faster)

// Broken electricity effect parameters
const float ELECTRICITY_STABLE_MIN = 5.0f;    // Minimum stable time (2s)
const float ELECTRICITY_STABLE_MAX = 20.0f;    // Maximum stable time (8s)
const float ELECTRICITY_BLACKOUT_CHANCE = 0.05f; // 5% chance of complete blackout
const float ELECTRICITY_SURGE_CHANCE = 0.1f;     // 10% chance of bright surge
const float ELECTRICITY_FLICKER_CHANCE = 0.85f;  // 85% chance of normal flicker
const float ELECTRICITY_BLACKOUT_DURATION = 0.15f; // Duration of blackouts (150ms)
const float ELECTRICITY_SURGE_MULTIPLIER = 1.6f;   // Brightness multiplier for surges

// Breathing effect parameters
const float BREATHING_SPEED = 0.01f;        // Speed of breathing cycle (very slow)
const float BREATHING_MIN_LEVEL = 0.2f;      // Minimum brightness (20% of base level)
const float BREATHING_MAX_LEVEL = 1.0f;      // Maximum brightness (100% of base level)
const float BREATHING_COLOR_VARIATION = 5.0f; // Subtle color warmth variation (±5 RGB units)

// Auto-cycle effect parameters
const float AUTO_CYCLE_MIN_TIME = 30.0f;   // Minimum time per effect
const float AUTO_CYCLE_MAX_TIME = 300.0f;  // Maximum time per effect
const float AUTO_CYCLE_TRANSITION_TIME = 2.0f; // Smooth transition duration between effects (2s)

const float RAINBOW_SATURATION = 0.8f;   // How vivid the rainbow colors are (0.0-1.0)

// Effect management functions
void switchToNextEffect()
{
  effectState.type = (EffectType)((effectState.type + 1) % MAX_EFFECT_NUMBER);
  effectState.startTime = millis();
  effectState.phase1 = 0.0f;
  effectState.phase2 = 0.0f;
  effectState.phase3 = 0.0f;

  // Reset scene change state when switching to/from scene change effect
  effectState.sceneChangeTime = 0;
  effectState.sceneHoldTime = 0;
  effectState.sceneTransitionTime = 0;
  effectState.sceneTransitioning = false;

  Serial.printf("Switched to effect: %d\n", effectState.type);
}

void blinkEffectNumber(uint8_t effectNum)
{
  if (xSemaphoreTake(colorMutex, pdMS_TO_TICKS(50)) == pdTRUE)
  {
    // Save current state
    lightState.savedR = lightState.final_r;
    lightState.savedG = lightState.final_g;
    lightState.savedB = lightState.final_b;
    lightState.savedEffect = effectState.type;

    // Start effect blinking mode
    lightState.specialMode = MODE_EFFECT_BLINKING;
    lightState.modeStartTime = millis();
    lightState.blinkCount = effectNum; // Number of complete pulse cycles
    lightState.lastBlinkTime = millis();
    lightState.blinkOn = true;

    // Temporarily disable effects
    effectState.type = EFFECT_NONE;

    Serial.printf("Starting blink mode: %d blinks (count=%d)\n", effectNum, lightState.blinkCount);
    xSemaphoreGive(colorMutex);
  }
}

void toggleLightState()
{
  bool newState;

  // Update internal state first
  if (xSemaphoreTake(colorMutex, pdMS_TO_TICKS(50)) == pdTRUE)
  {
    lightState.target_state = !lightState.target_state;
    lightState.target_level = lightState.target_state ? 255 : 0;
    newState = lightState.target_state;
    xSemaphoreGive(colorMutex);
  }
  else
  {
    Serial.println("Failed to acquire mutex for light toggle");
    return;
  }

  // Update coordinator state outside mutex to avoid deadlock
  pelarboj->setLightState(newState);
  pelarboj->zbUpdateStateFromAttributes();

  Serial.printf("Toggled light: %s\n", newState ? "ON" : "OFF");
}

void performFactoryReset()
{
  Serial.println("=== Factory Reset Initiated ===");

  // Start reset blinking mode - LED task will handle blinking
  if (xSemaphoreTake(colorMutex, pdMS_TO_TICKS(50)) == pdTRUE)
  {
    lightState.specialMode = MODE_RESET_BLINKING;
    lightState.modeStartTime = millis();
    xSemaphoreGive(colorMutex);
  }

  // Wait for 5 seconds while checking if button is still pressed
  for (int i = 0; i < 50; i++)
  { // Check every 100ms for 5 seconds
    if (digitalRead(BOOT_PIN) == HIGH && digitalRead(EXTERNAL_BUTTON_PIN) == HIGH)
    { // Button released
      Serial.println("Button released - reset cancelled");

      // Stop reset mode and restore normal operation
      if (xSemaphoreTake(colorMutex, pdMS_TO_TICKS(50)) == pdTRUE)
      {
        lightState.specialMode = MODE_NORMAL;
        xSemaphoreGive(colorMutex);
      }

      digitalWrite(LED_BUILTIN, LOW);
      return; // Exit without resetting
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // If we get here, button was held for full 5 seconds - proceed with reset
  Serial.println("Reset confirmed - proceeding with factory reset");

  // Stop reset mode and turn off LEDs
  if (xSemaphoreTake(colorMutex, pdMS_TO_TICKS(50)) == pdTRUE)
  {
    lightState.specialMode = MODE_NORMAL;
    lightState.final_r = 0.0f;
    lightState.final_g = 0.0f;
    lightState.final_b = 0.0f;
    lightState.final_level = 0.0f;
    xSemaphoreGive(colorMutex);
  }

  digitalWrite(LED_BUILTIN, LOW);

  Serial.println("Resetting Zigbee network...");
  // Zigbee.factoryReset();

  Serial.println("System reset complete - device will restart");
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP.restart();
}

// Apply effects to base color and return final output values
void applyEffects(float baseR, float baseG, float baseB, float baseLevel,
                  float &finalR, float &finalG, float &finalB, float &finalLevel)
{

  if (effectState.startTime == 0)
  {
    effectState.startTime = millis();
  }

  unsigned long elapsed = millis() - effectState.startTime;
  float time = elapsed / 1000.0f; // Convert to seconds

  // Start with base values
  finalR = baseR;
  finalG = baseG;
  finalB = baseB;
  finalLevel = baseLevel;

  switch (effectState.type)
  {
  case EFFECT_COLOR_WANDER:
  {
    // Update phase counters at different speeds for organic movement
    effectState.phase1 += COLOR_WANDER_SPEED * 1.0f;
    effectState.phase2 += COLOR_WANDER_SPEED * 1.3f;
    effectState.phase3 += COLOR_WANDER_SPEED * 0.7f;

    // Generate smooth wandering offsets using sine waves
    float offsetR = sin(effectState.phase1) * COLOR_WANDER_RANGE;
    float offsetG = sin(effectState.phase2) * COLOR_WANDER_RANGE;
    float offsetB = sin(effectState.phase3) * COLOR_WANDER_RANGE;

    // Apply offsets to base color
    finalR = constrain(baseR + offsetR, 0.0f, 255.0f);
    finalG = constrain(baseG + offsetG, 0.0f, 255.0f);
    finalB = constrain(baseB + offsetB, 0.0f, 255.0f);
  }
  break;

  case EFFECT_LEVEL_PULSE:
  {
    // Update phase counter for pulsation
    effectState.phase1 += LEVEL_PULSE_SPEED;

    // Generate smooth pulsation using sine wave
    float pulseMultiplier = 1.0f + (sin(effectState.phase1) * LEVEL_PULSE_RANGE);

    // Apply pulsation to level
    finalLevel = constrain(baseLevel * pulseMultiplier, 0.0f, 255.0f);
  }
  break;

  case EFFECT_COMBO:
  {
    // Combine color wandering and level pulsation
    // Update phase counters at different speeds for organic movement
    effectState.phase1 += COLOR_WANDER_SPEED * 1.0f; // For color wander R
    effectState.phase2 += COLOR_WANDER_SPEED * 1.3f; // For color wander G
    effectState.phase3 += COLOR_WANDER_SPEED * 0.7f; // For color wander B

    // Generate smooth wandering offsets using sine waves
    float offsetR = sin(effectState.phase1) * COLOR_WANDER_RANGE;
    float offsetG = sin(effectState.phase2) * COLOR_WANDER_RANGE;
    float offsetB = sin(effectState.phase3) * COLOR_WANDER_RANGE;

    // Apply offsets to base color
    finalR = constrain(baseR + offsetR, 0.0f, 255.0f);
    finalG = constrain(baseG + offsetG, 0.0f, 255.0f);
    finalB = constrain(baseB + offsetB, 0.0f, 255.0f);

    // Add level pulsation using a different phase counter
    // Use time-based calculation to avoid phase counter conflicts
    float pulsePhase = elapsed * LEVEL_PULSE_SPEED * 0.001f; // Convert to phase
    float pulseMultiplier = 1.0f + (sin(pulsePhase) * LEVEL_PULSE_RANGE);

    // Apply pulsation to level
    finalLevel = constrain(baseLevel * pulseMultiplier, 0.0f, 255.0f);
  }
  break;

  case EFFECT_SCENE_CHANGE:
  {
    // Initialize scene change if needed
    if (effectState.sceneChangeTime == 0)
    {
      // Start with current base values
      effectState.sceneCurrentR = baseR;
      effectState.sceneCurrentG = baseG;
      effectState.sceneCurrentB = baseB;
      effectState.sceneCurrentLevel = baseLevel;

      // Generate first target based on base color variations
      effectState.sceneTargetR = constrain(baseR + random(-50, 51), 0, 255);
      effectState.sceneTargetG = constrain(baseG + random(-50, 51), 0, 255);
      effectState.sceneTargetB = constrain(baseB + random(-50, 51), 0, 255);
      effectState.sceneTargetLevel = constrain(baseLevel + random(-50, 51), 50, 255);

      effectState.sceneChangeTime = millis();
      effectState.sceneHoldTime = random(5000, 10000);      // 5-10 seconds hold
      effectState.sceneTransitionTime = random(1000, 2000); // 1-2 seconds transition
      effectState.sceneTransitioning = true;

      Serial.printf("Scene change: New target R=%d G=%d B=%d L=%d\n",
                    (int)effectState.sceneTargetR, (int)effectState.sceneTargetG,
                    (int)effectState.sceneTargetB, (int)effectState.sceneTargetLevel);
    }

    unsigned long sceneElapsed = millis() - effectState.sceneChangeTime;

    if (effectState.sceneTransitioning)
    {
      // Transition phase (1-2 seconds, time set once at start)
      if (sceneElapsed < effectState.sceneTransitionTime)
      {
        // Smooth interpolation to target using fixed transition time
        float progress = (float)sceneElapsed / effectState.sceneTransitionTime;
        progress = min(progress, 1.0f);

        // Use exponential interpolation for smoother transitions
        float smoothProgress = progress * progress * (3.0f - 2.0f * progress); // Smoothstep

        effectState.sceneCurrentR = effectState.sceneCurrentR + (effectState.sceneTargetR - effectState.sceneCurrentR) * smoothProgress * 0.1f;
        effectState.sceneCurrentG = effectState.sceneCurrentG + (effectState.sceneTargetG - effectState.sceneCurrentG) * smoothProgress * 0.1f;
        effectState.sceneCurrentB = effectState.sceneCurrentB + (effectState.sceneTargetB - effectState.sceneCurrentB) * smoothProgress * 0.1f;
        effectState.sceneCurrentLevel = effectState.sceneCurrentLevel + (effectState.sceneTargetLevel - effectState.sceneCurrentLevel) * smoothProgress * 0.1f;

        finalR = effectState.sceneCurrentR;
        finalG = effectState.sceneCurrentG;
        finalB = effectState.sceneCurrentB;
        finalLevel = effectState.sceneCurrentLevel;
      }
      else
      {
        // Transition complete - switch to hold phase
        effectState.sceneCurrentR = effectState.sceneTargetR;
        effectState.sceneCurrentG = effectState.sceneTargetG;
        effectState.sceneCurrentB = effectState.sceneTargetB;
        effectState.sceneCurrentLevel = effectState.sceneTargetLevel;
        effectState.sceneTransitioning = false;
        effectState.sceneChangeTime = millis(); // Reset timer for hold phase

        finalR = effectState.sceneCurrentR;
        finalG = effectState.sceneCurrentG;
        finalB = effectState.sceneCurrentB;
        finalLevel = effectState.sceneCurrentLevel;
      }
    }
    else
    {
      // Hold phase (5-10 seconds)
      if (sceneElapsed < effectState.sceneHoldTime)
      {
        // Hold current scene
        finalR = effectState.sceneCurrentR;
        finalG = effectState.sceneCurrentG;
        finalB = effectState.sceneCurrentB;
        finalLevel = effectState.sceneCurrentLevel;
      }
      else
      {
        // Hold complete - generate new target based on base color variations
        effectState.sceneTargetR = constrain(baseR + random(-50, 51), 0, 255);
        effectState.sceneTargetG = constrain(baseG + random(-50, 51), 0, 255);
        effectState.sceneTargetB = constrain(baseB + random(-50, 51), 0, 255);
        effectState.sceneTargetLevel = constrain(baseLevel + random(-50, 51), 50, 255);

        effectState.sceneChangeTime = millis();
        effectState.sceneHoldTime = random(5000, 10000);      // New hold time
        effectState.sceneTransitionTime = random(1000, 2000); // New transition time
        effectState.sceneTransitioning = true;

        Serial.printf("Scene change: New target R=%d G=%d B=%d L=%d\n",
                      (int)effectState.sceneTargetR, (int)effectState.sceneTargetG,
                      (int)effectState.sceneTargetB, (int)effectState.sceneTargetLevel);

        // Start interpolating toward new target
        finalR = effectState.sceneCurrentR;
        finalG = effectState.sceneCurrentG;
        finalB = effectState.sceneCurrentB;
        finalLevel = effectState.sceneCurrentLevel;
      }
    }
  }
  break;

  case EFFECT_FIREPLACE:
  {
    // Simulate realistic fireplace flickering with warm colors
    // Update multiple phase counters for organic flame movement
    effectState.phase1 += FIREPLACE_FLICKER_SPEED * 1.0f; // Main flicker
    effectState.phase2 += FIREPLACE_FLICKER_SPEED * 1.7f; // Secondary flicker
    effectState.phase3 += FIREPLACE_FLICKER_SPEED * 0.6f; // Slow ember glow

    // Generate multiple sine waves for realistic flame behavior
    float mainFlicker = sin(effectState.phase1);
    float secondaryFlicker = sin(effectState.phase2) * 0.4f;
    float emberGlow = sin(effectState.phase3) * 0.2f;

    // Combine flickers with bias toward brighter flames
    float totalFlicker = (mainFlicker + secondaryFlicker + emberGlow + 1.5f) / 3.5f;
    totalFlicker = constrain(totalFlicker, 0.0f, 1.0f);

    // Create subtle warm fire colors closer to base
    float fireRed = baseR * FIREPLACE_RED_BOOST;
    float fireGreen = baseG * (0.9f + FIREPLACE_ORANGE_MIX * totalFlicker); // Subtle orange tint
    float fireBlue = baseB * 0.8f;                                          // Slightly reduce blue for warmth

    // Apply intensity variations for flickering (reduced range)
    float intensity = 1.0f - (FIREPLACE_INTENSITY_RANGE * (1.0f - totalFlicker));

    // Constrain colors to valid range
    finalR = constrain(fireRed, 0.0f, 255.0f);
    finalG = constrain(fireGreen, 0.0f, 255.0f);
    finalB = constrain(fireBlue, 0.0f, 255.0f);
    finalLevel = constrain(baseLevel * intensity, baseLevel * 0.7f, baseLevel);
  }
  break;

  case EFFECT_RAINBOW:
  {
    // Smooth rainbow color cycling based on base color
    effectState.phase1 += RAINBOW_CYCLE_SPEED;

    // Cycle hue around base color (±120 degrees for variety while staying related)
    float hueOffset = sin(effectState.phase1) * 120.0f; // -120 to +120 degrees

    // Convert base color to approximate hue for starting point
    float baseHue = 0.0f;
    if (baseR >= baseG && baseR >= baseB)
    {
      // Red dominant
      baseHue = 0.0f + (baseG - baseB) / (baseR - min(baseG, baseB)) * 60.0f;
    }
    else if (baseG >= baseR && baseG >= baseB)
    {
      // Green dominant
      baseHue = 120.0f + (baseB - baseR) / (baseG - min(baseR, baseB)) * 60.0f;
    }
    else
    {
      // Blue dominant
      baseHue = 240.0f + (baseR - baseG) / (baseB - min(baseR, baseG)) * 60.0f;
    }

    // Calculate final hue with offset
    uint8_t finalHue = (uint8_t)constrain((baseHue + hueOffset) * 255.0f / 360.0f, 0, 255);

    // Use existing hueToRGB function with base brightness
    uint32_t rainbowR, rainbowG, rainbowB;
    hueToRGB(finalHue, (uint8_t)baseLevel, rainbowR, rainbowG, rainbowB);

    // Blend with base color to maintain base characteristics
    float blendFactor = 0.08f; // 8% rainbow, 92% base color
    finalR = rainbowR * blendFactor + baseR * (1.0f - blendFactor);
    finalG = rainbowG * blendFactor + baseG * (1.0f - blendFactor);
    finalB = rainbowB * blendFactor + baseB * (1.0f - blendFactor);
    finalLevel = baseLevel; // Keep original brightness level

    // Constrain to valid range
    finalR = constrain(finalR, 0.0f, 255.0f);
    finalG = constrain(finalG, 0.0f, 255.0f);
    finalB = constrain(finalB, 0.0f, 255.0f);
  }
  break;

  case EFFECT_COLOR_STEPS:
  {
    // Rapid color steps - like color wander but with sudden jumps at intervals
    // Check if enough time has passed for a new step
    if (effectState.sceneChangeTime == 0 || (time - effectState.sceneChangeTime) >= COLOR_STEPS_INTERVAL)
    {
      // Time for a new color step
      effectState.sceneChangeTime = time;
      
      // Generate new random offsets for each channel, similar to color wander but larger range
      float offsetR = (random(0, 2001) - 1000) * COLOR_STEPS_RANGE / 1000.0f; // -30 to +30
      float offsetG = (random(0, 2001) - 1000) * COLOR_STEPS_RANGE / 1000.0f; // -30 to +30  
      float offsetB = (random(0, 2001) - 1000) * COLOR_STEPS_RANGE / 1000.0f; // -30 to +30
      
      // Store the new target in scene variables (reusing existing structure)
      effectState.sceneTargetR = constrain(baseR + offsetR, 0.0f, 255.0f);
      effectState.sceneTargetG = constrain(baseG + offsetG, 0.0f, 255.0f);
      effectState.sceneTargetB = constrain(baseB + offsetB, 0.0f, 255.0f);
    }
    
    // Apply the current step values (instant change - no interpolation)
    finalR = effectState.sceneTargetR;
    finalG = effectState.sceneTargetG;
    finalB = effectState.sceneTargetB;
  }
  break;

  case EFFECT_BROKEN_ELECTRICITY:
  {
    // Horror movie broken electricity - mostly stable with rare dramatic flickers
    // Use phase1 as state: 0=stable, 1=in_event, 2=returning_to_stable
    
    // Initialize with stable state if first time
    if (effectState.sceneChangeTime == 0)
    {
      effectState.phase1 = 0; // Start in stable state
      effectState.sceneTargetR = baseR;
      effectState.sceneTargetG = baseG;
      effectState.sceneTargetB = baseB;
      effectState.sceneTargetLevel = baseLevel;
      
      // Set next event time (2-8 seconds from now)
      effectState.sceneTransitionTime = ELECTRICITY_STABLE_MIN + 
        (random(0, 1001) / 1000.0f) * (ELECTRICITY_STABLE_MAX - ELECTRICITY_STABLE_MIN);
      effectState.sceneChangeTime = time;
    }
    
    float timeSinceLastChange = time - effectState.sceneChangeTime;
    
    if (effectState.phase1 == 0) // Stable state - waiting for next event
    {
      if (timeSinceLastChange >= effectState.sceneTransitionTime)
      {
        // Time for an electrical event - roll for type
        float eventRoll = random(0, 1001) / 1000.0f;
        effectState.phase1 = 1; // Switch to event state
        
        if (eventRoll < ELECTRICITY_BLACKOUT_CHANCE)
        {
          // Complete blackout
          effectState.sceneTargetR = 0.0f;
          effectState.sceneTargetG = 0.0f;
          effectState.sceneTargetB = 0.0f;
          effectState.sceneTargetLevel = 0.0f;
          effectState.sceneTransitionTime = ELECTRICITY_BLACKOUT_DURATION;
        }
        else if (eventRoll < ELECTRICITY_BLACKOUT_CHANCE + ELECTRICITY_SURGE_CHANCE)
        {
          // Bright surge
          effectState.sceneTargetR = min(255.0f, baseR * ELECTRICITY_SURGE_MULTIPLIER);
          effectState.sceneTargetG = min(255.0f, baseG * ELECTRICITY_SURGE_MULTIPLIER);
          effectState.sceneTargetB = min(255.0f, baseB * ELECTRICITY_SURGE_MULTIPLIER);
          effectState.sceneTargetLevel = min(255.0f, baseLevel * ELECTRICITY_SURGE_MULTIPLIER);
          effectState.sceneTransitionTime = 0.1f;
        }
        else if (eventRoll < ELECTRICITY_BLACKOUT_CHANCE + ELECTRICITY_SURGE_CHANCE + ELECTRICITY_FLICKER_CHANCE)
        {
          // Quick flicker
          float variation = 0.4f + (random(0, 601) / 1000.0f);
          effectState.sceneTargetR = baseR * variation;
          effectState.sceneTargetG = baseG * variation;
          effectState.sceneTargetB = baseB * variation;
          effectState.sceneTargetLevel = baseLevel * variation;
          effectState.sceneTransitionTime = 0.05f + (random(0, 101) / 1000.0f);
        }
        else
        {
          // No event this time - stay stable
          effectState.phase1 = 0;
          effectState.sceneTransitionTime = ELECTRICITY_STABLE_MIN + 
            (random(0, 1001) / 1000.0f) * (ELECTRICITY_STABLE_MAX - ELECTRICITY_STABLE_MIN);
        }
        
        effectState.sceneChangeTime = time;
      }
    }
    else if (effectState.phase1 == 1) // In event state
    {
      if (timeSinceLastChange >= effectState.sceneTransitionTime)
      {
        // Event duration over - return to stable
        effectState.phase1 = 0;
        effectState.sceneTargetR = baseR;
        effectState.sceneTargetG = baseG;
        effectState.sceneTargetB = baseB;
        effectState.sceneTargetLevel = baseLevel;
        
        // Set next stable duration
        effectState.sceneTransitionTime = ELECTRICITY_STABLE_MIN + 
          (random(0, 1001) / 1000.0f) * (ELECTRICITY_STABLE_MAX - ELECTRICITY_STABLE_MIN);
        effectState.sceneChangeTime = time;
      }
    }
    
    // Apply current electrical state
    finalR = effectState.sceneTargetR;
    finalG = effectState.sceneTargetG;
    finalB = effectState.sceneTargetB;
    finalLevel = effectState.sceneTargetLevel;
  }
  break;

  case EFFECT_BREATHING:
  {
    // Slow organic breathing effect - like the light is alive and sleeping
    // Update breathing phase very slowly for calm, meditative rhythm
    effectState.phase1 += BREATHING_SPEED;
    
    // Create breathing curve using sine wave - smooth inhale and exhale
    float breathingCycle = sin(effectState.phase1);
    
    // Map breathing cycle to brightness range (20% to 100% of base level)
    float breathingMultiplier = BREATHING_MIN_LEVEL + 
      (BREATHING_MAX_LEVEL - BREATHING_MIN_LEVEL) * (breathingCycle * 0.5f + 0.5f);
    
    // Apply breathing to brightness level
    finalLevel = constrain(baseLevel * breathingMultiplier, 0.0f, 255.0f);
    
    // Add subtle color warmth variation synchronized with breathing
    // Warmer (more red/yellow) on exhale, cooler (more blue) on inhale
    float colorVariation = breathingCycle * BREATHING_COLOR_VARIATION;
    
    // Slightly increase red/decrease blue on exhale for warmth
    finalR = constrain(baseR + colorVariation * 0.6f, 0.0f, 255.0f);
    finalG = constrain(baseG + colorVariation * 0.3f, 0.0f, 255.0f);
    finalB = constrain(baseB - colorVariation * 0.4f, 0.0f, 255.0f);
  }
  break;

  case EFFECT_AUTO_CYCLE:
  {
    // Auto-cycle through all other effects randomly with smooth transitions
    // Uses dedicated auto-cycle variables to avoid conflicts with sub-effects
    
    // Initialize auto-cycle if first time
    if (effectState.autoCycleStartTime == 0)
    {
      // Pick random first effect (exclude EFFECT_NONE=0 and EFFECT_AUTO_CYCLE=10)
      effectState.autoCycleSubEffect = 1 + random(0, 9); // Random from 1-9
      effectState.autoCycleNeedsReset = true;
      effectState.autoCycleInTransition = false;
      
      // Set random duration for first effect
      effectState.autoCycleDuration = AUTO_CYCLE_MIN_TIME + 
        (random(0, 1001) / 1000.0f) * (AUTO_CYCLE_MAX_TIME - AUTO_CYCLE_MIN_TIME);
      effectState.autoCycleStartTime = time;
    }
    
    // Check if it's time to start transition to next effect
    if (!effectState.autoCycleInTransition && 
        (time - effectState.autoCycleStartTime) >= (effectState.autoCycleDuration - AUTO_CYCLE_TRANSITION_TIME))
    {
      // Start transition - capture current effect output for blending
      EffectType originalType = effectState.type;
      effectState.type = (EffectType)effectState.autoCycleSubEffect;
      applyEffects(baseR, baseG, baseB, baseLevel, 
                   effectState.autoCyclePrevR, effectState.autoCyclePrevG, 
                   effectState.autoCyclePrevB, effectState.autoCyclePrevLevel);
      effectState.type = originalType;
      
      // Set up transition
      effectState.autoCyclePrevEffect = effectState.autoCycleSubEffect;
      effectState.autoCycleInTransition = true;
      effectState.autoCycleTransitionStart = time;
      
      // Pick new effect (different from current)
      int newEffect;
      do {
        newEffect = 1 + random(0, 9); // Random from 1-9
      } while (newEffect == effectState.autoCycleSubEffect);
      
      effectState.autoCycleSubEffect = newEffect;
      effectState.autoCycleNeedsReset = true;
    }
    
    // Check if transition is complete
    if (effectState.autoCycleInTransition && 
        (time - effectState.autoCycleTransitionStart) >= AUTO_CYCLE_TRANSITION_TIME)
    {
      // Transition complete - start new effect duration
      effectState.autoCycleInTransition = false;
      effectState.autoCycleDuration = AUTO_CYCLE_MIN_TIME + 
        (random(0, 1001) / 1000.0f) * (AUTO_CYCLE_MAX_TIME - AUTO_CYCLE_MIN_TIME);
      effectState.autoCycleStartTime = time;
    }
    
    // Reset sub-effect state if needed (when switching effects)
    if (effectState.autoCycleNeedsReset)
    {
      // Reset all effect state variables for clean sub-effect start
      effectState.phase1 = 0;
      effectState.phase2 = 0;
      effectState.phase3 = 0;
      effectState.sceneCurrentR = baseR;
      effectState.sceneCurrentG = baseG;
      effectState.sceneCurrentB = baseB;
      effectState.sceneCurrentLevel = baseLevel;
      effectState.sceneTargetR = baseR;
      effectState.sceneTargetG = baseG;
      effectState.sceneTargetB = baseB;
      effectState.sceneTargetLevel = baseLevel;
      effectState.sceneChangeTime = 0;
      effectState.sceneTransitionTime = 0;
      effectState.sceneHoldTime = 0;
      effectState.sceneTransitioning = false;
      effectState.autoCycleNeedsReset = false;
    }
    
    if (effectState.autoCycleInTransition)
    {
      // During transition - blend between previous and current effects
      float transitionProgress = (time - effectState.autoCycleTransitionStart) / AUTO_CYCLE_TRANSITION_TIME;
      transitionProgress = constrain(transitionProgress, 0.0f, 1.0f);
      
      // Get current effect output
      EffectType originalType = effectState.type;
      effectState.type = (EffectType)effectState.autoCycleSubEffect;
      float currentR, currentG, currentB, currentLevel;
      applyEffects(baseR, baseG, baseB, baseLevel, currentR, currentG, currentB, currentLevel);
      effectState.type = originalType;
      
      // Smooth interpolation using smoothstep for natural feel
      float smoothProgress = transitionProgress * transitionProgress * (3.0f - 2.0f * transitionProgress);
      
      // Blend between previous and current effects
      finalR = effectState.autoCyclePrevR * (1.0f - smoothProgress) + currentR * smoothProgress;
      finalG = effectState.autoCyclePrevG * (1.0f - smoothProgress) + currentG * smoothProgress;
      finalB = effectState.autoCyclePrevB * (1.0f - smoothProgress) + currentB * smoothProgress;
      finalLevel = effectState.autoCyclePrevLevel * (1.0f - smoothProgress) + currentLevel * smoothProgress;
    }
    else
    {
      // Not in transition - run current effect normally
      EffectType originalType = effectState.type;
      effectState.type = (EffectType)effectState.autoCycleSubEffect;
      applyEffects(baseR, baseG, baseB, baseLevel, finalR, finalG, finalB, finalLevel);
      effectState.type = originalType;
    }
  }
  break;

  case EFFECT_NONE:
  default:
    // No effects - final = base
    break;
  }
}

// LED update task that handles smooth color interpolation and effects
// Button handling task - runs independently
void buttonTask(void *parameter)
{
  while (true)
  {
    uint32_t currentTime = millis();
    bool currentReading = (digitalRead(BOOT_PIN) == LOW) || (digitalRead(EXTERNAL_BUTTON_PIN) == LOW);

    // Debounce logic
    if (currentReading != buttonHandler.lastButtonReading)
    {
      buttonHandler.lastButtonReading = currentReading;
      vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));
      continue;
    }

    // State machine for button handling
    switch (buttonHandler.state)
    {
    case BTN_IDLE:
      if (currentReading && !buttonHandler.isPressed)
      {
        buttonHandler.isPressed = true;
        buttonHandler.pressStartTime = currentTime;
        buttonHandler.state = BTN_FIRST_PRESS;
        Serial.println("Button pressed - first press detected");
      }
      break;

    case BTN_FIRST_PRESS:
      if (!currentReading && buttonHandler.isPressed)
      {
        // Button released after first press
        buttonHandler.isPressed = false;
        buttonHandler.releaseTime = currentTime;
        buttonHandler.state = BTN_WAITING_SECOND;
        Serial.println("Button released - waiting for second press");
      }
      else if (currentReading && (currentTime - buttonHandler.pressStartTime) >= LONG_PRESS_TIME_MS)
      {
        // Long press detected
        buttonHandler.state = BTN_LONG_PRESS_ACTIVE;
        Serial.println("Long press detected - factory reset");
        performFactoryReset();
        buttonHandler.state = BTN_IDLE;
        buttonHandler.isPressed = false;
      }
      break;

    case BTN_WAITING_SECOND:
      if (currentReading && !buttonHandler.isPressed)
      {
        // Second press detected
        buttonHandler.isPressed = true;
        buttonHandler.pressStartTime = currentTime;
        buttonHandler.state = BTN_SECOND_PRESS;
        Serial.println("Second press detected - double press");
      }
      else if ((currentTime - buttonHandler.releaseTime) >= DOUBLE_PRESS_WINDOW_MS)
      {
        // Timeout - single press confirmed
        Serial.println("Single press confirmed - toggling light");
        toggleLightState();
        buttonHandler.state = BTN_IDLE;
      }
      break;

    case BTN_SECOND_PRESS:
      if (!currentReading && buttonHandler.isPressed)
      {
        // Second press completed - double press confirmed
        buttonHandler.isPressed = false;
        Serial.println("Double press confirmed - switching effect");
        switchToNextEffect();
        // Blink the effect number (1-7) instead of enum value (0-6)
        uint8_t effectNumber = effectState.type + 1; // Convert 0-6 to 1-7
        blinkEffectNumber(effectNumber);
        buttonHandler.state = BTN_IDLE;
      }
      else if (currentReading && (currentTime - buttonHandler.pressStartTime) >= LONG_PRESS_TIME_MS)
      {
        // Long press during second press
        buttonHandler.state = BTN_LONG_PRESS_ACTIVE;
        Serial.println("Long press during second press - factory reset");
        performFactoryReset();
        buttonHandler.state = BTN_IDLE;
        buttonHandler.isPressed = false;
      }
      break;

    case BTN_LONG_PRESS_ACTIVE:
      // This state is handled above, reset when button released
      if (!currentReading)
      {
        buttonHandler.state = BTN_IDLE;
        buttonHandler.isPressed = false;
      }
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // Check every 10ms
  }
}

void ledUpdateTask(void *parameter)
{
  while (true)
  {
    if (xSemaphoreTake(colorMutex, pdMS_TO_TICKS(5)) == pdTRUE)
    {
      // Check special modes first
      if (lightState.specialMode == MODE_RESET_BLINKING)
      {
        uint32_t elapsed = millis() - lightState.modeStartTime;

        // Slow pulsation during reset (1Hz pulse, 30%-100% range)
        float pulse = (sin(elapsed * 0.006283f) + 1.0f) * 0.5f; // 0.006283 = 2*PI/1000 for 1Hz
        float level = 0.3f + (pulse * 0.7f);                    // 30% to 100% range

        lightState.final_r = 255.0f;
        lightState.final_g = 0.0f;
        lightState.final_b = 0.0f;
        lightState.final_level = level * 255.0f;

        // Also pulse built-in LED
        digitalWrite(LED_BUILTIN, pulse > 0.5f ? HIGH : LOW);
      }
      else if (lightState.specialMode == MODE_EFFECT_BLINKING)
      {
        uint32_t elapsed = millis() - lightState.modeStartTime;

        // Fast pulsation for effect indication (2Hz pulse, 30%-100% range)
        float pulse = (sin(elapsed * 0.012566f) + 1.0f) * 0.5f; // 0.012566 = 2*PI/500 for 2Hz
        float level = 0.3f + (pulse * 0.7f);                    // 30% to 100% range

        // Use saved current color
        lightState.final_r = lightState.savedR;
        lightState.final_g = lightState.savedG;
        lightState.final_b = lightState.savedB;
        lightState.final_level = level * 255.0f;

        // Count pulses instead of discrete blinks
        uint32_t pulseCount = elapsed / 500; // One complete pulse cycle every 500ms
        if (pulseCount >= lightState.blinkCount)
        {
          Serial.printf("Pulse mode finished, restoring effect: %d\n", lightState.savedEffect);
          lightState.specialMode = MODE_NORMAL;
          effectState.type = lightState.savedEffect; // Restore effect
        }
      }
      else
      {
        // Normal operation
        // Smooth interpolation toward target values (creates base color)
        lightState.base_r += (lightState.target_r - lightState.base_r) * TRANSITION_SPEED;
        lightState.base_g += (lightState.target_g - lightState.base_g) * TRANSITION_SPEED;
        lightState.base_b += (lightState.target_b - lightState.base_b) * TRANSITION_SPEED;
        lightState.base_level += (lightState.target_level - lightState.base_level) * TRANSITION_SPEED;
        lightState.base_state = lightState.target_state;

        // Apply effects to base values to get final values
        applyEffects(lightState.base_r, lightState.base_g, lightState.base_b, lightState.base_level,
                     lightState.final_r, lightState.final_g, lightState.final_b, lightState.final_level);
      }

      xSemaphoreGive(colorMutex);

      // Calculate final RGB values with brightness applied
      // In special modes, ignore base_state and use final values directly
      float outputR, outputG, outputB;
      if (lightState.specialMode != MODE_NORMAL)
      {
        outputR = lightState.final_r * (lightState.final_level / 255.0f);
        outputG = lightState.final_g * (lightState.final_level / 255.0f);
        outputB = lightState.final_b * (lightState.final_level / 255.0f);
      }
      else
      {
        outputR = lightState.base_state ? (lightState.final_r * (lightState.final_level / 255.0f)) : 0.0f;
        outputG = lightState.base_state ? (lightState.final_g * (lightState.final_level / 255.0f)) : 0.0f;
        outputB = lightState.base_state ? (lightState.final_b * (lightState.final_level / 255.0f)) : 0.0f;
      }

      // Scale to 12-bit PWM range (0-4095) for ultra-smooth output
      uint16_t pwmR = (uint16_t)constrain(outputR * (LED_PWM_MAX_VALUE / 255.0f), 0, LED_PWM_MAX_VALUE);
      uint16_t pwmG = (uint16_t)constrain(outputG * (LED_PWM_MAX_VALUE / 255.0f), 0, LED_PWM_MAX_VALUE);
      uint16_t pwmB = (uint16_t)constrain(outputB * (LED_PWM_MAX_VALUE / 255.0f), 0, LED_PWM_MAX_VALUE);

      // Apply to LED hardware with 12-bit resolution
      ledcWrite(ledR, pwmR);
      ledcWrite(ledG, pwmG);
      ledcWrite(ledB, pwmB);
    }

    vTaskDelay(pdMS_TO_TICKS(LED_UPDATE_RATE_MS));
  }
}

// Static callback implementations
static void staticLightChangeCallback(bool state, uint8_t endpoint, uint8_t red, uint8_t green, uint8_t blue, uint8_t level, uint16_t temperature, esp_zb_zcl_color_control_color_mode_t color_mode)
{
  // Serial.printf("Command received - state:%d level:%d R:%d G:%d B:%d\n", state, level, red, green, blue);
  //  Update target values for smooth interpolation
  if (xSemaphoreTake(colorMutex, pdMS_TO_TICKS(10)) == pdTRUE)
  {
    lightState.target_state = state;
    lightState.target_r = red;
    lightState.target_g = green;
    lightState.target_b = blue;
    lightState.target_level = level;
    xSemaphoreGive(colorMutex);
  }
}

static void staticIdentifyCallback(uint16_t time)
{
  // Static identify callback - implementation could be added if needed
}

// the setup routine runs once when you press reset:
void setup()
{
  Serial.begin(115200);
  delay(10);

  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(EXTERNAL_BUTTON_PIN, INPUT_PULLUP);

  // Initialize pins as LEDC channels with high resolution
  // 12-bit resolution provides 4096 levels for ultra-smooth transitions
  ledcAttach(ledR, LED_PWM_FREQUENCY, LED_PWM_RESOLUTION); // 5 kHz PWM, 12-bit resolution
  ledcAttach(ledG, LED_PWM_FREQUENCY, LED_PWM_RESOLUTION);
  ledcAttach(ledB, LED_PWM_FREQUENCY, LED_PWM_RESOLUTION);

  pinMode(LED_BUILTIN, OUTPUT);

  // Initialize mutex for thread-safe color updates
  colorMutex = xSemaphoreCreateMutex();
  if (colorMutex == NULL)
  {
    Serial.println("Failed to create color mutex!");
    ESP.restart();
  }

  uint8_t color = 0;        // a value from 0 to 255 representing the hue
  uint32_t R, G, B;         // the Red Green and Blue color components
  uint8_t brightness = 255; // 255 is maximum brightness, but can be changed.  Might need 256 for common anode to fully turn off.

  uint8_t phillips_hue_key[] = {0x81, 0x45, 0x86, 0x86, 0x5D, 0xC6, 0xC8, 0xB1, 0xC8, 0xCB, 0xC4, 0x2E, 0x5D, 0x65, 0xD3, 0xB9};
  Zigbee.setEnableJoiningToDistributed(true);
  Zigbee.setStandardDistributedKey(phillips_hue_key);

  pelarboj = new ZigbeeHueLight(ENDPOINT, ESP_ZB_HUE_LIGHT_TYPE_COLOR);

  // Configure the light
  pelarboj->onLightChange(staticLightChangeCallback);
  pelarboj->onIdentify(staticIdentifyCallback);

  pelarboj->setManufacturerAndModel("nkey", "Pelarboj");
  pelarboj->setSwBuild("0.0.1");
  pelarboj->setOnOffOnTime(0);
  pelarboj->setOnOffGlobalSceneControl(false);

  Zigbee.addEndpoint(pelarboj);

  if (!Zigbee.begin(ZIGBEE_ROUTER, false))
  {
    Serial.println("Zigbee failed to start!");
    Serial.println("Rebooting...");
    ESP.restart();
  }

  Serial.println("Connecting Zigbee to network");

  while (!Zigbee.connected())
  {
    color++;
    hueToRGB(color, brightness, R, G, B); // call function to convert hue to RGB

    // write the RGB values to the pins (scale to 12-bit resolution)
    ledcWrite(ledR, R * (LED_PWM_MAX_VALUE / 255)); // Scale to 12-bit range
    ledcWrite(ledG, G * (LED_PWM_MAX_VALUE / 255));
    ledcWrite(ledB, B * (LED_PWM_MAX_VALUE / 255));
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }

  effectState.type = EFFECT_AUTO_CYCLE;
  effectState.startTime = millis();
  // Generate random color for startup
  uint8_t startR = random(255);
  uint8_t startG = random(255);
  uint8_t startB = random(255);
  uint8_t startLevel = 255;
  bool startState = true;

  // Set coordinator state
  pelarboj->setLightState(startState);
  pelarboj->setLightLevel(startLevel);
  pelarboj->setLightColor(startR, startG, startB);
  pelarboj->zbUpdateStateFromAttributes();

  // Set internal state to match to avoid race conditions
  if (xSemaphoreTake(colorMutex, pdMS_TO_TICKS(100)) == pdTRUE)
  {
    lightState.target_state = startState;
    lightState.target_r = startR;
    lightState.target_g = startG;
    lightState.target_b = startB;
    lightState.target_level = startLevel;

    // Also set base values directly for immediate effect
    lightState.base_state = startState;
    lightState.base_r = startR;
    lightState.base_g = startG;
    lightState.base_b = startB;
    lightState.base_level = startLevel;

    xSemaphoreGive(colorMutex);
  }

  // Start button handling task (higher priority to avoid inheritance issues)
  if (xTaskCreate(buttonTask, "Button_Handler", 2048, NULL, 3, NULL) != pdPASS)
  {
    Serial.println("Failed to create button handling task!");
    ESP.restart();
  }

  // Start LED update task
  if (xTaskCreate(ledUpdateTask, "LED_Update", 4096, NULL, 2, NULL) != pdPASS)
  {
    Serial.println("Failed to create LED update task!");
    ESP.restart();
  }

  Serial.println("Button and LED tasks started - enhanced button functionality enabled");
}

// void loop runs over and over again
void loop()
{
  // Button handling is now done in async task
  // Just keep the built-in LED heartbeat
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}