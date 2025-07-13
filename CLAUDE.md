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
- Basic RGB LED control with PWM
- Hue to RGB color conversion
- Color cycling demonstration
- Serial output for debugging

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
- **Filesystem**: LittleFS
- **Storage**: Dedicated partitions for Zigbee network data
- **Debug**: Available with detailed ESP Zigbee stack logging

## Next Steps
- [ ] Implement Zigbee light device profile
- [ ] Add Hue bridge discovery and pairing
- [ ] Integrate color temperature control
- [ ] Add brightness dimming
- [ ] Implement network configuration interface