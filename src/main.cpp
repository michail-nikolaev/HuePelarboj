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
const uint8_t ledR = D0;
const uint8_t ledG = D1;
const uint8_t ledB = D2;

const uint8_t ENDPOINT = 10;

// Effects system
enum EffectType
{
  EFFECT_NONE = 0,
  EFFECT_COLOR_WANDER = 1,
  EFFECT_LEVEL_PULSE = 2
};

struct EffectState
{
  EffectType type;
  unsigned long startTime;
  float phase1, phase2, phase3; // Multiple phase counters for complex effects
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
    0.0f              // final level
};

EffectState effectState = {
    EFFECT_COLOR_WANDER, // Start with color wander effect
    0,                   // Will be set when effect starts
    0.0f, 0.0f, 0.0f     // Phase counters
};

SemaphoreHandle_t colorMutex;

ZigbeeHueLight *pelarboj;

const int LED_UPDATE_RATE_MS = 20;   // 50 FPS update rate
const float TRANSITION_SPEED = 0.1f; // Interpolation speed (0.0-1.0)

// Effect parameters
const float COLOR_WANDER_RANGE = 20.0f; // How far colors can wander from base (0-255)
const float COLOR_WANDER_SPEED = 0.01f; // Speed of color wandering
const float LEVEL_PULSE_RANGE = 0.4f;   // Pulse range as fraction of base level (0.0-1.0)
const float LEVEL_PULSE_SPEED = 0.01f;  // Speed of level pulsation

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

  case EFFECT_NONE:
  default:
    // No effects - final = base
    break;
  }
}

// LED update task that handles smooth color interpolation and effects
void ledUpdateTask(void *parameter)
{
  while (true)
  {
    if (xSemaphoreTake(colorMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
      // Smooth interpolation toward target values (creates base color)
      lightState.base_r += (lightState.target_r - lightState.base_r) * TRANSITION_SPEED;
      lightState.base_g += (lightState.target_g - lightState.base_g) * TRANSITION_SPEED;
      lightState.base_b += (lightState.target_b - lightState.base_b) * TRANSITION_SPEED;
      lightState.base_level += (lightState.target_level - lightState.base_level) * TRANSITION_SPEED;
      lightState.base_state = lightState.target_state;

      // Apply effects to base values to get final values
      applyEffects(lightState.base_r, lightState.base_g, lightState.base_b, lightState.base_level,
                   lightState.final_r, lightState.final_g, lightState.final_b, lightState.final_level);

      xSemaphoreGive(colorMutex);

      // Calculate final RGB values with brightness applied
      float outputR = lightState.base_state ? (lightState.final_r * (lightState.final_level / 255.0f)) : 0.0f;
      float outputG = lightState.base_state ? (lightState.final_g * (lightState.final_level / 255.0f)) : 0.0f;
      float outputB = lightState.base_state ? (lightState.final_b * (lightState.final_level / 255.0f)) : 0.0f;

      // Apply to LED hardware
      ledcWrite(ledR, (uint8_t)constrain(outputR, 0, 255));
      ledcWrite(ledG, (uint8_t)constrain(outputG, 0, 255));
      ledcWrite(ledB, (uint8_t)constrain(outputB, 0, 255));
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

  // Initialize pins as LEDC channels
  // resolution 1-16 bits, freq limits depend on resolution, channel is automatically selected
  ledcAttach(ledR, 12000, 8); // 12 kHz PWM, 8-bit resolution
  ledcAttach(ledG, 12000, 8);
  ledcAttach(ledB, 12000, 8);

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

    // write the RGB values to the pins
    ledcWrite(ledR, R); // write red component to channel 1, etc.
    ledcWrite(ledG, G);
    ledcWrite(ledB, B);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }

  // Select random effect for demonstration
  effectState.type = (EffectType)(random(1, 3)); // Random between EFFECT_COLOR_WANDER and EFFECT_LEVEL_PULSE
  effectState.startTime = millis();

  Serial.printf("Selected effect: %s\n",
                effectState.type == EFFECT_COLOR_WANDER ? "COLOR_WANDER" : "LEVEL_PULSE");

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

  // Start LED update task
  if (xTaskCreate(ledUpdateTask, "LED_Update", 4096, NULL, 2, NULL) != pdPASS)
  {
    Serial.println("Failed to create LED update task!");
    ESP.restart();
  }

  Serial.println("LED update task started - smooth color transitions enabled");
}

void resetSystem()
{
  Serial.println("=== System Reset ===");

  // Reset Zigbee network
  Serial.println("Resetting Zigbee network...");
  Zigbee.factoryReset();

  Serial.println("System reset complete - device will restart");
  delay(500); // Additional delay to ensure all operations complete
  ESP.restart();
}

void checkForReset()
{
  // Checking button for factory reset
  if (digitalRead(BOOT_PIN) == LOW)
  { // Push button pressed
    // Key debounce handling
    vTaskDelay(pdMS_TO_TICKS(100));
    int startTime = millis();
    bool ledState = false;
    unsigned long lastBlink = millis();
    const int BLINK_INTERVAL = 100; // Fast blink every 100ms

    while (digitalRead(BOOT_PIN) == LOW)
    {
      // Fast blink built-in LED while button is held
      if (millis() - lastBlink >= BLINK_INTERVAL)
      {
        ledState = !ledState;
        digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
        lastBlink = millis();
      }

      vTaskDelay(pdMS_TO_TICKS(10));
      ; // Short delay to prevent excessive CPU usage

      if ((millis() - startTime) > 3000)
      {
        // If key pressed for more than 3secs, perform unified system reset
        Serial.println("Button held for 3+ seconds - performing full system reset");
        digitalWrite(LED_BUILTIN, LOW); // Turn off LED before reset
        resetSystem();
      }
    }

    // Button released - turn off LED
    digitalWrite(LED_BUILTIN, LOW);
  }
}

// void loop runs over and over again
void loop()
{
  checkForReset();
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}