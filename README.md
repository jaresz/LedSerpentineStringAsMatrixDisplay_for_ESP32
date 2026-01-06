# ESP32-S3 Configurable NeoPixel Matrix

This project runs colorful lighting effects on a configurable NeoPixel LED matrix driven by an ESP32

## Overview
- Configurable LED matrix with serpentine (zig-zag) wiring order
- Default configuration: 32 columns x 7 rows (224 LEDs)
- Matrix layout: Top-left is LED 0, serpentine pattern alternates row direction
  - Row 0 (even): Left→Right (LEDs 0 to cols-1)
  - Row 1 (odd): Right→Left (LEDs cols to 2*cols-1)
  - Row 2 (even): Left→Right (LEDs 2*cols to 3*cols-1)
  - And so on...

## Matrix Configuration Limits
- **Maximum LED strip length**: 256 LEDs
- **Columns**: 20-40 (configurable in code)
- **Rows**: 5-12 (configurable in code)
- **Total LEDs**: Must not exceed 256 (cols × rows ≤ 256)

## Customizing Matrix Dimensions
To change the matrix size, modify these variables in `main.cpp`:
```cpp
int matrixCols = 32;  // Your desired column count (20-40)
int matrixRows = 7;   // Your desired row count (5-12)
```
The code will validate these values on startup and fall back to safe defaults (20×5) if invalid.

## Wiring
- Data (middle wire) → GPIO2 (LED strip data pin)
- +5V (power) → LED V+ (external power supply recommended for >50 LEDs)
- GND → LED GND
- GPIO6 (as SDA) to AHT10 SDA, GPIO5 (AS SCL) to AHT10 SCL, 3.3V to AHT10 VCC, GND to AHT10 GND
- **Important**: Connect ESP32 GND and LED strip GND together (common ground)

## Hardware Features
- **Temperature/Humidity Sensor**: AHT10 on I2C (SDA: GPIO6, SCL: GPIO5)
- **Button**: GPIO9 for configuration (long press for WiFi setup)
- **Built-in RGB LED**: Status indication
- **WiFi**: Web interface for configuration and sensor data
- **OTA Updates**: Over-the-air firmware updates

## Software Features
- **WiFi Configuration**: Captive portal setup mode
- **Web Interface**: Real-time sensor data and configuration
- **Animation System**: Multiple LED effect modes
- **Temperature Display**: Visual temperature representation on LEDs
- **Matrix Drawing**: Configurable pixel grid system

## Build & Upload
- This project is a PlatformIO/Arduino project. Build and upload with PlatformIO in VS Code or run:

```bash
platformio run --target upload
```

