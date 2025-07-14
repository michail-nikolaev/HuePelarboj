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
- **Ultra-High Resolution PWM**: 12-bit (4096 levels) for professional-grade smoothness
- **RGB LED Control**: PWM-based control on pins D0, D1, D2 at 5kHz frequency
- **Dynamic Effects Layer**: Real-time effects applied on top of base colors
- **Smooth Color Interpolation**: FreeRTOS task with 50 FPS updates
- **Three-Tier State Management**: Target → Base → Final (with effects)
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
- **Ultra-Smooth Brightness**: 12-bit PWM provides 4096 brightness levels (16x smoother than 8-bit)
- **Professional Color Control**: 68.7 billion color combinations with imperceptible stepping
- **Dynamic Effects**: Two built-in effects with random selection at startup
- **Color Wander Effect**: Colors randomly drift around base color from coordinator
- **Level Pulse Effect**: Brightness smoothly pulsates around base level
- **State Reporting**: Device reports initial state to coordinator after startup
- **Race-Free Synchronization**: Coordinator and internal states perfectly synchronized
- **Premium Transitions**: All changes interpolate with professional-grade smoothness

## Implementation Details
- **Three-Tier Processing**: Target → Base → Final (with effects) → LED Output
- **Effects System**: `applyEffects(base_color, EFFECT_TYPE, time)` architecture
- **12-bit PWM Resolution**: 4096 levels per channel (0-4095) at 5kHz frequency
- **Professional Scaling**: `colorValue * (4095 / 255.0f)` for maximum smoothness
- **Interpolation Task**: Dedicated FreeRTOS task running at 50 FPS (20ms intervals)
- **State Structure**: Enhanced `LightState` with base, target, and final value sets
- **Thread Safety**: Mutex protection for concurrent access between task and callbacks
- **Transition Speed**: Configurable interpolation rate (0.1 = gradual, 1.0 = instant)
- **Effect Parameters**: Configurable speed and range for each effect type
- **LED Scaling**: Final output = `final_color * (final_level / 255.0f)`
- **Command Processing**: `staticLightChangeCallback` sets target values only
- **Hardware Updates**: LED outputs applied outside mutex for optimal performance
- **State Reporting**: Uses `setLightState()`, `setLightLevel()`, `setLightColor()` + `zbUpdateStateFromAttributes()`
- **Race-Free Init**: Both coordinator and internal states set to identical startup values

## Recent Updates
- ✅ **12-bit PWM Resolution**: Upgraded from 8-bit to 12-bit for ultra-smooth output
- ✅ **Professional Color Quality**: 68.7 billion colors vs 16.7 million (4000x improvement)
- ✅ **Optimized PWM Frequency**: 5kHz frequency for optimal 12-bit performance
- ✅ **Scaling Pipeline**: Automatic 8-bit to 12-bit conversion throughout codebase
- ✅ **Premium Smoothness**: Imperceptible stepping in all transitions and effects

## PWM & Effects Details
- **PWM Resolution**: 12-bit (4096 levels) at 5kHz frequency for ultra-smooth output
- **Color Range**: 68.7 billion total color combinations (4096³)
- **Smoothness Factor**: 16x smoother than standard 8-bit systems
- **EFFECT_COLOR_WANDER**: Colors drift ±10 RGB units around base using 3 sine waves
- **EFFECT_LEVEL_PULSE**: Brightness varies ±40% around base level with sine wave
- **Phase Counters**: Multiple phase timers for organic, non-repetitive movement
- **Configurable Timing**: COLOR_WANDER_SPEED (0.01f), LEVEL_PULSE_SPEED (0.01f)
- **Safe Boundaries**: constrain() ensures values stay within valid RGB/level ranges

## Next Steps
- [x] Implement Zigbee light device profile
- [x] Add Hue bridge discovery and pairing
- [x] Fix command response handling for proper Hue recognition
- [x] Add smooth color/brightness interpolation system
- [x] Add device state reporting to coordinator
- [x] Implement dynamic effects layer with two base effects
- [x] Upgrade to 12-bit PWM resolution for professional smoothness
- [ ] Add more effect types (strobe, rainbow, fireplace, etc.)
- [ ] Integrate color temperature control
- [ ] Add physical button for on/off state changes and effect switching
- [ ] Add brightness dimming ranges
- [ ] Implement network configuration interface