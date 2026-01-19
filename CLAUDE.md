# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based audiobook player with TFT display and rotary encoder interface. The device fetches an audiobook list from a server via WiFi, displays it on a 2.4" TFT screen, and allows selection via rotary encoder. Pressing the encoder button sends an HTTP POST request to play the selected audiobook on a Google Home device.

**Hardware:**
- ESP32 WROOM microcontroller
- 2.4" TFT Display (ST7789, 320x240, SPI) with integrated EC11 rotary encoder module
- Two buttons: encoder switch (play) and K0 (brightness control)

## Architecture

**Startup Flow:**
1. Load saved preferences (brightness, track selection, device)
2. Connect to WiFi FIRST (credentials from secrets.h)
3. HTTP GET /list and /listdevices to fetch data
4. Disconnect WiFi before display init
5. Initialize backlight PWM (GPIO26, 5kHz, 8-bit resolution)
6. Initialize TFT display (landscape mode, 320x240, using HSPI)
7. Play boot animation (day-to-night transition)
8. Initialize EC11 encoder (half-quad mode with internal pull-ups)
9. Display scrollable list with blue selection highlight

**Main Loop:**
1. Poll encoder for rotation (updates selection with wrap-around)
2. Poll encoder button (debounced, triggers playback)
3. Poll K0 button (debounced, cycles brightness 20%→50%→80%)
4. Redraw display only when selection changes (optimization)

**Key Functions:**
- `fetchAudiobookList()`: HTTP GET, JSON parsing, populates audiobooks[] array
- `displayAudiobookList()`: Renders scrollable list with selection highlight and scroll indicator
- `sendPlayRequest(index)`: HTTP POST with track name, shows "Playing!" feedback

## Code Structure

**File:** `audiobookstreamgooglehome.ino` (single file, well-commented sections)

**Critical Constants:**
- `ITEM_HEIGHT = 20`: Pixels per list item (text size 2 = 16px tall)
- `MAX_NAME_LENGTH = 18`: Character limit before truncation
- Max 50 audiobooks in memory (String array)

**Display Layout:**
- 320x240 landscape orientation (rotation=1)
- ~11 visible items (240-10)/20
- Auto-scrolls to keep selection visible
- Scroll indicator on right edge when needed

## Hardware Pin Configuration

**TFT Display (HSPI - avoids WiFi conflicts):**
- SCK → GPIO14 (HSPI SCLK)
- MOSI → GPIO13 (HSPI MOSI)
- RST → GPIO4, DC → GPIO2, CS → GPIO5
- BL (backlight) → GPIO26 (PWM controlled)
- K0 (button) → GPIO15

**EC11 Rotary Encoder:**
- CLK (Phase A) → GPIO27
- DT (Phase B) → GPIO25 (moved from GPIO14 to avoid HSPI conflict)
- SW (button) → GPIO19 (moved from GPIO13 to avoid HSPI conflict)
- Uses ESP32 internal pull-ups

**Why HSPI?** WiFi uses VSPI internally. Using HSPI for the display completely separates the two SPI buses, eliminating display corruption during WiFi operations.

## TFT_eSPI Library Configuration

**CRITICAL:** TFT_eSPI requires manual configuration before compilation.

Copy `Custom_Setup.h` to the TFT_eSPI library folder, or edit `User_Setup.h`:
```cpp
#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_MOSI 13    // HSPI MOSI
#define TFT_SCLK 14    // HSPI SCLK
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
#define USE_HSPI_PORT           // Use HSPI instead of VSPI
#define SUPPORT_TRANSACTIONS    // Enable SPI transaction support
```

See SETUP.md for detailed configuration instructions.

## Libraries

- **TFT_eSPI**: Display driver (Bodmer)
- **ESP32Encoder**: Rotary encoder handling (Kevin Harrington)
- **ArduinoJson**: JSON parsing (v6 or v7, code has compatibility layer)
- **WiFi, HTTPClient**: Built-in ESP32 libraries

## Server API

**Endpoint:** `http://10.0.0.181:8801`

**GET /list** - Returns audiobook list:
```json
{"tracks": ["1.mp3", "2.mp3", "3.mp3"]}
```

**POST /play** - Triggers playback:
```json
{"track": "1.mp3"}
```

To change server IP, edit constants at top of .ino file.

## Development Workflow

1. Copy `secrets.template.h` → `secrets.h` (add WiFi credentials)
2. Install libraries via Arduino IDE Library Manager
3. Configure TFT_eSPI User_Setup.h for ST7789 driver
4. Select board: ESP32 Dev Module
5. Upload and monitor Serial (115200 baud)

## Important Implementation Details

**Encoder Reading:**
- Uses `attachHalfQuad()` for EC11 20-detent encoder
- Divides count by 2 to match detents: `encoder.getCount() / 2`
- Wrap-around at list boundaries for circular navigation

**Button Debouncing:**
- 50ms debounce delay for both buttons
- INPUT_PULLUP mode (LOW when pressed)
- Edge detection (press on HIGH→LOW transition)

**Display Optimization:**
- Only redraws on selection change
- Scrolls to keep selected item visible
- Truncates long names with "..." suffix

**PWM Backlight:**
- GPIO26, 5kHz, 8-bit (0-255)
- Adjustable via encoder in brightness mode
- Saved to preferences, persists across reboots
- Controlled via ESP32 LEDC peripheral

**JSON Compatibility:**
- Preprocessor checks ArduinoJson version
- v7: `JsonDocument doc;`
- v6: `StaticJsonDocument<2048> doc;`

## Common Issues

**Display blank:** Check TFT_eSPI configuration, verify ST7789 driver enabled, check HSPI pins (GPIO13/14)

**Display corruption after WiFi:** Ensure USE_HSPI_PORT is enabled in TFT_eSPI config. HSPI separates display from WiFi's VSPI bus.

**Encoder backwards:** Swap ENCODER_CLK and ENCODER_DT pin assignments

**WiFi fails:** ESP32 only supports 2.4GHz networks

**List empty:** Verify server is running and accessible, check Serial output for HTTP errors

**Button not responding:** Check INPUT_PULLUP wiring (button should connect pin to GND). Encoder SW is GPIO19, K0 is GPIO15.

## Debugging

Serial output (115200 baud) shows:
- Startup sequence with IP address
- Received JSON from server
- Each loaded audiobook with index
- Encoder rotation events with selection
- Button presses
- HTTP request/response codes
- Error messages for WiFi, HTTP, JSON failures

Serial monitor is essential for troubleshooting - all major operations log detailed information.
