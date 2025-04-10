# Jiggla - ESP32 Mouse Jiggler

Jiggla is an advanced, WiFi-configurable mouse jiggler device based on the ESP32-S2/S3 microcontroller. It prevents screen locks and sleep modes by simulating subtle mouse movements at configurable intervals.

![Jiggla Web Interface](https://raw.githubusercontent.com/yourusername/jiggla/main/docs/images/jiggla-ui.png)

## Features

- **USB Mouse Emulation**: Appears as a standard USB mouse when connected to a computer
- **Configurable Movement Patterns**:
  - Linear or circular mouse movements
  - Adjustable movement distance (X/Y pixels)
  - Variable movement speed and intervals
  - Random delay option to create more natural patterns
  - Movement trail effect
- **Wireless Control Interface**:
  - Web-based configuration UI
  - Mobile-friendly responsive design
  - Real-time status monitoring
  - Secure authentication
  - Activity logging
- **Network Connectivity**:
  - WiFi Station mode for connection to existing networks
  - Fallback to Access Point mode if WiFi connection fails
  - Optional hidden AP for increased security
  - mDNS support for easy device discovery (jiggla.local)
- **Persistence & Security**:
  - Settings saved to flash memory
  - Secure credential management
  - Session persistence across device reboots

## Hardware Requirements

- ESP32-S2 or ESP32-S3 development board
- USB-C cable for power and data
- Computer with USB port

## Setup Instructions

### 1. Clone & Configure

1. Clone this repository:
   ```
   git clone https://github.com/yourusername/jiggla.git
   cd jiggla
   ```

2. Create your credentials file:
   ```
   cp credentials.h.template credentials.h
   ```

3. Edit `credentials.h` with your WiFi network information:
   ```c
   #define WIFI_SSID "your_wifi_ssid"
   #define WIFI_PASSWORD "your_wifi_password"
   ```

### 2. Building & Flashing

1. Install [PlatformIO](https://platformio.org/) if you haven't already

2. Build and upload the firmware:
   ```
   platformio run -e esp32-s2 --target upload
   ```
   (Use `esp32-s3-zero` instead of `esp32-s2` if you have an S3 board)

3. Upload the filesystem data (web interface):
   ```
   platformio run -e esp32-s2 --target uploadfs
   ```

### 3. First-Time Use

1. Connect the ESP32 to your computer via USB
2. The device will appear as a USB mouse
3. Connect to the WiFi network:
   - If your configured WiFi is available, it will connect automatically
   - If not, it will create an access point named "jiggla"
4. Access the web interface:
   - Browse to http://jiggla.local:8787
   - Or use the IP address shown on the serial monitor
5. Default login credentials:
   - Username: admin
   - Password: your_password (from credentials.h)

## Web Interface

The web interface provides control over all Jiggla's features:

### Main Controls

- **Enable/Disable**: Toggle mouse movement functionality
- **Movement Interval**: Time between mouse movements (in seconds)
- **Movement Speed**: How fast the mouse pointer moves
- **Movement Pattern**: Linear or circular movement
- **Distance Controls**: Set X/Y movement distance in pixels
- **Test Button**: Trigger a movement immediately

### Advanced Settings

- **Random Delay**: Adds ±30% random variation to movement intervals
- **Movement Trail**: Creates multiple movements in sequence for a more complex pattern

### Admin Settings

- **Authentication**: Change admin username and password
- **WiFi Access Point**: Configure AP SSID, password, and visibility
- **Hostname**: Set the mDNS hostname for the device
- **Web Server Port**: Configure the HTTP server port

## Command Line Options

For advanced users, you can modify build flags in `platformio.ini`:

```ini
build_flags = 
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=0
    -D DEVICE_NAME="jiggla"
    -D ASYNC_TCP_STACK_SIZE=10240
    -D MAX_HEADER_LENGTH=1024
    -D CORE_DEBUG_LEVEL=5
```

## Troubleshooting

### Connection Issues

If you can't connect to the device:
1. Check the serial monitor for IP address information
2. Verify WiFi connection status
3. Try accessing via IP address instead of mDNS hostname

### Movement Problems

If mouse movements aren't working:
1. Check that the device is recognized as a USB mouse
2. Verify Jiggla is enabled in the web interface
3. Try the "Test Movement" button to force immediate action

### Web Interface Not Loading

If the web interface isn't accessible:
1. Verify WiFi connection
2. Try rebooting the device
3. Check for interference from security software

## License

This project is released under the MIT License. See the LICENSE file for details.

## Acknowledgments

- Based on the ESP32 Arduino framework
- Uses AsyncWebServer for the web interface
- Inspired by commercial mouse jiggler products

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

---

© 2024 Jiggla Project 