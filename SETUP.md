# Setup Guide for Audiobook Player

This guide walks through the complete setup process for the ESP32 audiobook player.

## Hardware Requirements

- **ESP32 WROOM** microcontroller
- **2.4" TFT Display** with EC11 Rotary Encoder Module
  - Display Driver: ST7789
  - Resolution: 320x240 pixels
  - Interface: SPI
  - EC11 Encoder: 20 pulses/20 positioning with switch

## Library Installation

### Arduino IDE

1. Open Arduino IDE
2. Go to **Tools > Manage Libraries**
3. Install the following libraries:
   - **TFT_eSPI** by Bodmer
   - **ESP32Encoder** by Kevin Harrington
   - **ArduinoJson** by Benoit Blanchon (v6 or v7)

## TFT_eSPI Configuration

**CRITICAL:** The TFT_eSPI library must be configured for your specific display and pin setup.

### Option 1: Use the Provided Custom_Setup.h (Easiest)

This repository includes a pre-configured `Custom_Setup.h` file with all the correct settings.

1. Locate the TFT_eSPI library folder:
   - **Arduino IDE (Windows)**: `C:\Users\[YourName]\Documents\Arduino\libraries\TFT_eSPI\`
   - **Arduino IDE (Mac/Linux)**: `~/Documents/Arduino/libraries/TFT_eSPI/`
   - **PlatformIO**: `.pio/libdeps/esp32dev/TFT_eSPI/`

2. Copy `Custom_Setup.h` from this project to the TFT_eSPI library folder

3. Open `User_Setup_Select.h` in the TFT_eSPI folder

4. Comment out the default setup (around line 23):
   ```cpp
   // #include <User_Setup.h>           // Default setup (comment this out)
   ```

5. Add this line to include the custom setup:
   ```cpp
   #include <Custom_Setup.h>            // Our custom setup for ST7789
   ```

6. Save and close the file

### Option 2: Edit User_Setup.h Directly

If you prefer to modify the library's default configuration file:

1. Locate and open `TFT_eSPI/User_Setup.h`

2. Find and **comment out** all driver definitions, then enable ST7789:
   ```cpp
   #define ST7789_DRIVER      // Enable ST7789 driver
   #define ST7789_240x320     // Specific init for 240x320 displays
   ```

3. Set display dimensions:
   ```cpp
   #define TFT_WIDTH  240
   #define TFT_HEIGHT 320
   ```

4. Configure SPI pins (HSPI):
   ```cpp
   #define TFT_MISO -1   // Not connected
   #define TFT_MOSI 13   // HSPI MOSI
   #define TFT_SCLK 14   // HSPI SCLK
   #define TFT_CS   5
   #define TFT_DC   2
   #define TFT_RST  4
   ```

5. **IMPORTANT:** Enable HSPI port to avoid WiFi conflicts:
   ```cpp
   #define USE_HSPI_PORT
   #define SUPPORT_TRANSACTIONS
   ```

6. **IMPORTANT:** Do NOT define TFT_BL (backlight is controlled by code):
   ```cpp
   // #define TFT_BL 26  // Leave commented out!
   ```

7. Set SPI frequency:
   ```cpp
   #define SPI_FREQUENCY  10000000  // 10MHz (safe speed for this display)
   ```

8. Save the file

## Pin Connections

**Important:** We use HSPI (not VSPI) for the TFT display to avoid conflicts with WiFi. This requires specific pin assignments.

### TFT Display (HSPI)

| Display Pin | ESP32 GPIO | Notes |
|-------------|------------|-------|
| GND | GND | Ground |
| VCC | 3.3V | Power (3.3V only!) |
| SCL | **14** | SPI Clock (HSPI SCLK) |
| SDA | **13** | SPI MOSI (HSPI MOSI) |
| RST | 4 | Display Reset |
| DC | 2 | Data/Command |
| CS | 5 | Chip Select |
| BL | **26** | Backlight (PWM controlled) |
| K0 | 15 | Brightness button |

### EC11 Rotary Encoder

| Encoder Pin | ESP32 GPIO | Notes |
|-------------|------------|-------|
| CLK (A) | 27 | Phase A |
| DT (B) | **25** | Phase B (moved from 14 for HSPI) |
| SW | **19** | Push button (moved from 13 for HSPI) |
| VCC | 3.3V | Shared with display |
| GND | GND | Ground |

### Why HSPI?

The ESP32 has two SPI buses: VSPI and HSPI. WiFi uses VSPI internally, which caused display corruption when both WiFi and the TFT were active on the same bus. By moving the TFT to HSPI, they operate on completely separate buses, eliminating interference.

## WiFi Configuration

1. Copy `secrets.template.h` to `secrets.h`:
   ```bash
   cp secrets.template.h secrets.h
   ```

2. Edit `secrets.h` with your WiFi credentials:
   ```cpp
   const char* WIFI_SSID = "YourWiFiName";
   const char* WIFI_PASS = "YourWiFiPassword";
   ```

3. **Important:** `secrets.h` is in `.gitignore` to prevent committing credentials

## Server Configuration

The code expects a server running on `http://10.0.0.181:8801` with two endpoints:

### GET /list
Returns JSON with audiobook tracks:
```json
{
  "tracks": ["1.mp3", "2.mp3", "3.mp3"]
}
```

### POST /play
Accepts JSON to play a track:
```json
{
  "track": "1.mp3"
}
```

**To change the server address**, edit `audiobookstreamgooglehome.ino`:
```cpp
const char* SERVER_URL = "http://YOUR_SERVER_IP:PORT";
```

## Upload to ESP32

### Arduino IDE

1. Go to **Tools > Board** → Select **ESP32 Dev Module**
2. Go to **Tools > Port** → Select your ESP32's COM port
3. Click **Upload**

## Troubleshooting

### Display shows nothing
- Check all SPI pin connections
- Verify TFT_eSPI is configured for ST7789 driver
- Check that backlight (BL) is connected to GPIO25
- Try increasing brightness (press K0 button)

### Display shows garbage
- Wrong driver selected in TFT_eSPI setup
- SPI pins incorrectly mapped
- Check display rotation setting (should be 1 for landscape)

### Encoder doesn't respond
- Verify CLK and DT pins are correct
- Check for loose connections
- Try swapping CLK and DT if rotation direction is inverted

### WiFi won't connect
- Verify credentials in `secrets.h`
- ESP32 only supports 2.4GHz WiFi (not 5GHz)
- Check Serial Monitor (115200 baud) for connection status

### Compilation errors
- Ensure all libraries are installed
- Check ArduinoJson version compatibility
- Verify ESP32 board support is installed

## Testing

1. Open **Serial Monitor** at **115200 baud**
2. Watch for startup messages:
   ```
   === Audiobook Player Starting ===
   Backlight initialized (50%)
   Display initialized
   Connecting to WiFi...
   WiFi connected!
   ```
3. Server should be contacted and list displayed
4. Rotate encoder to navigate
5. Press encoder button to play
6. Press K0 button to cycle brightness

## Operation

- **Rotate encoder**: Navigate through audiobook list
- **Press encoder button (SW)**: Play selected audiobook
- **Press K0 button**: Cycle brightness (20% → 50% → 80% → 20%...)

The display shows:
- Scrollable list of audiobooks
- Blue highlight on selected item
- Scroll indicator on right side (when needed)
- "Playing!" message when sending request
