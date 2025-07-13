#include <Arduino.h>
#include <Zigbee.h>

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

ZigbeeHueLight *pelarboj;

// Static callback implementations
static void staticLightChangeCallback(bool state, uint8_t endpoint, uint8_t red, uint8_t green, uint8_t blue, uint8_t level, uint16_t temperature, esp_zb_zcl_color_control_color_mode_t color_mode)
{
  Serial.printf("state:%d level:%d R:%d G:%d B:%d", state, level, red, green, blue);
  if (!state)
  {
    ledcWrite(ledR, 0);
    ledcWrite(ledG, 0);
    ledcWrite(ledB, 0);
  }
  else
  {
    ledcWrite(ledR, red * (level / 255.0f));
    ledcWrite(ledG, green * (level / 255.0f));
    ledcWrite(ledB, blue * (level / 255.0f));
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

  uint8_t color = 0;        // a value from 0 to 255 representing the hue
  uint32_t R, G, B;         // the Red Green and Blue color components
  uint8_t brightness = 255; // 255 is maximum brightness, but can be changed.  Might need 256 for common anode to fully turn off.

  Serial.println("Send all LEDs a 255 and wait 2 seconds.");
  // If your RGB LED turns off instead of on here you should check if the LED is common anode or cathode.
  // If it doesn't fully turn off and is common anode try using 256.
  ledcWrite(ledR, 255);
  ledcWrite(ledG, 255);
  ledcWrite(ledB, 255);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(200);
  Serial.println("Send all LEDs a 0 and wait 2 seconds.");
  ledcWrite(ledR, 0);
  ledcWrite(ledG, 0);
  ledcWrite(ledB, 0);
  digitalWrite(LED_BUILTIN, LOW);
  delay(200);

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

  pelarboj->zbUpdateStateFromAttributes();
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