# HuePelarboj - Hue-Compatible IKEA Lamp

![HuePelarboj Project](images/1.jpg)

Convert your IKEA PELARBOJ lamp into a Philips Hue compatible smart light using ESP32-C6 and Zigbee.

## Acknowledgments

This project is inspired by and builds upon the excellent work by [n0rt0nthec4t](https://github.com/n0rt0nthec4t) and their [HomeSpan-IKEA-PELARBOJ](https://github.com/n0rt0nthec4t/HomeSpan-IKEA-PELARBOJ) project. Special thanks for the original concept, hardware design, and schematic that made this Hue-compatible version possible.

## Overview

This project transforms an IKEA PELARBOJ lamp into a smart RGB light that can be controlled through Philips Hue bridges and apps. Unlike the original HomeKit implementation, this version uses Zigbee protocol for seamless integration with existing Hue ecosystems.

## Features

- **Zigbee 3.0 Compatible**: Direct integration with Philips Hue bridges
- **Full RGB Control**: 16.7 million colors with smooth transitions
- **Brightness Dimming**: 0-100% brightness control
- **Energy Efficient**: PWM-based LED control
- **Easy Installation**: Fits inside original IKEA PELARBOJ housing

## Hardware Requirements

### Supported Boards
- Seeed XIAO ESP32-C6 (recommended)
- ESP32 Dev Module (alternative)

### Components
- RGB LED strip or individual RGB LEDs
- Appropriate resistors for LED current limiting
- Power supply (5V recommended)

### Connections
```
ESP32-C6 Pin | LED Component
-------------|---------------
D0           | Red LED
D1           | Green LED  
D2           | Blue LED
GND          | LED Common (Anode/Cathode)
```

## Software Setup

### Prerequisites
- [PlatformIO](https://platformio.org/) installed
- Git for version control

### Installation
```bash
git clone https://github.com/yourusername/HuePelarboj.git
cd HuePelarboj
```

### Building and Flashing
```bash
# Build for Seeed XIAO ESP32-C6
pio run -e seeed_xiao_esp32c6-common

# Upload to device
pio run -t upload -e seeed_xiao_esp32c6-common

# Monitor serial output
pio device monitor -b 115200
```

## Configuration

### LED Type
Modify the `invert` flag in `src/main.cpp` based on your LED configuration:
- `true` for common anode LEDs
- `false` for common cathode LEDs

### Pin Assignments
Update pin definitions in `src/main.cpp` if using different GPIO pins:
```cpp
uint8_t ledR = D0;  // Red LED pin
uint8_t ledG = D1;  // Green LED pin
uint8_t ledB = D2;  // Blue LED pin
```

## Usage

1. **Flash the firmware** to your ESP32-C6 device
2. **Install the hardware** inside your IKEA PELARBOJ lamp
3. **Power on** the device
4. **Pair with Hue Bridge** using the Philips Hue app
5. **Control via Hue app** or voice assistants

## Development

### Project Structure
```
HuePelarboj/
├── src/
│   └── main.cpp           # Main application code
├── include/               # Header files
├── lib/                   # Local libraries
├── platformio.ini         # Build configuration
├── zigbee_spiffs.csv      # Partition table
└── README.md             # This file
```

### Build Environments
- `seeed_xiao_esp32c6-common`: Production build for XIAO ESP32-C6
- `seeed_xiao_esp32c6-dev`: Debug build with verbose logging
- `esp32dev-common`: Production build for ESP32 Dev Module
- `esp32dev-dev`: Debug build for ESP32 Dev Module

## Contributing

Contributions are welcome! Please feel free to submit pull requests, report bugs, or suggest features.

## License

This project is open source. Please check the license file for details.

## Credits

- **Original Concept**: [n0rt0nthec4t](https://github.com/n0rt0nthec4t) for the [HomeSpan-IKEA-PELARBOJ](https://github.com/n0rt0nthec4t/HomeSpan-IKEA-PELARBOJ) project
- **Hardware Design**: Based on n0rt0nthec4t's schematic and PCB layout
- **IKEA**: For creating the beautiful PELARBOJ lamp that serves as our base

## Support

If you encounter issues or have questions:
1. Check the existing [Issues](../../issues)
2. Create a new issue with detailed information
3. Include serial monitor output for debugging

---

*Transform your ordinary lamp into something extraordinary!*