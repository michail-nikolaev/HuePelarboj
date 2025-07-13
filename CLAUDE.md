# HuePelarboj Project

Project inspired by [HomeSpan-IKEA-PELARBOJ](https://github.com/n0rt0nthec4t/HomeSpan-IKEA-PELARBOJ) to convert an IKEA lamp to a Hue-compatible device using ESP32-C6 with Zigbee support.

## Hardware
- **Target Board**: Seeed XIAO ESP32-C6 or ESP32 Dev Module
- **LED Pins**: 
  - Red: D0
  - Green: D1  
  - Blue: D2
- **LED Type**: Common anode RGB LED (configurable via `invert` flag)

## Current Implementation
- **Zigbee Hue Light**: Full ZigbeeHueLight implementation with color support
- **RGB LED Control**: PWM-based control on pins D0, D1, D2
- **Hue Integration**: Uses Phillips Hue distributed network key
- **Device Profile**: Configured as "nkey Pelarboj" color light
- **Factory Reset**: Boot button (3+ seconds) for network reset
- **Connection Status**: LED indicators during pairing and operation

## Build Commands
```bash
# Build for Seeed XIAO ESP32-C6 (production)
pio run -e seeed_xiao_esp32c6-common

# Build for Seeed XIAO ESP32-C6 (debug)
pio run -e seeed_xiao_esp32c6-dev

# Build for ESP32 Dev Module (production)
pio run -e esp32dev-common

# Build for ESP32 Dev Module (debug)
pio run -e esp32dev-dev

# Upload firmware
pio run -t upload -e [environment]

# Monitor serial output
pio device monitor -b 115200
```

## Zigbee Configuration
- **Mode**: ZCZR (Zigbee Coordinator/Router)
- **Device Type**: ESP_ZB_HUE_LIGHT_TYPE_COLOR
- **Endpoint**: 10
- **Distributed Key**: Phillips Hue standard key
- **Filesystem**: LittleFS with dedicated Zigbee partitions
- **Debug**: Available with detailed ESP Zigbee stack logging

## Connection Status
- ✅ **Zigbee Network**: Successfully connects to Hue bridge
- ✅ **Command Handling**: Properly responds to Hue commands with LED control
- ✅ **State Synchronization**: Calls zbUpdateStateFromAttributes() after setup
- **Raw Commands Handled**: Basic (0), Level Control (8), Color Control (768)

## Features Working
- **On/Off Control**: Device responds to power state commands
- **Brightness Control**: Level control with proper scaling
- **Color Control**: RGB color commands are processed and applied to LEDs
- **State Reporting**: Device updates Zigbee attributes after initialization

## Implementation Details
- **Command Callback**: `staticLightChangeCallback` processes all light commands
- **LED Scaling**: Brightness level applied to RGB values: `color * (level / 255.0f)`
- **Power States**: LEDs turn off when state=false, apply colors when state=true
- **Attribute Sync**: `zbUpdateStateFromAttributes()` ensures proper state reporting

## Next Steps
- [x] Implement Zigbee light device profile
- [x] Add Hue bridge discovery and pairing
- [x] Fix command response handling for proper Hue recognition
- [ ] Integrate color temperature control
- [ ] Add brightness dimming ranges
- [ ] Implement network configuration interface