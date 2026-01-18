/*
 * Audiobook Player for ESP32 with TFT Display and Rotary Encoder
 *
 * Hardware:
 * - ESP32 WROOM
 * - 2.4" TFT Display (ST7789 driver, 320x240 resolution)
 * - EC11 Rotary Encoder (integrated with display module)
 *
 * Features:
 * - Fetches audiobook list from server via WiFi
 * - Displays scrollable list on TFT screen
 * - Navigate with rotary encoder
 * - Press encoder button to play selected audiobook
 * - Press K0 button to cycle display brightness (20%, 50%, 80%)
 *
 * Libraries Required:
 * - TFT_eSPI (configure User_Setup.h for ST7789, 320x240, SPI pins)
 * - ESP32Encoder
 * - ArduinoJson (v6 or v7)
 * - Built-in: WiFi, HTTPClient
 *
 * Pin Configuration: See readme.md
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <ESP32Encoder.h>
#include <Preferences.h>  // For persistent storage across reboots
#include "secrets.h"  // Contains WIFI_SSID and WIFI_PASS

// ============================================================================
// SERVER CONFIGURATION
// ============================================================================
const char* SERVER_URL = "http://10.0.0.181:8801";
const char* LIST_URL = "http://10.0.0.181:8801/list";       // GET: returns {"tracks":["1.mp3","2.mp3"]}
const char* DEVICES_URL = "http://10.0.0.181:8801/listdevices";  // GET: returns {"devices":["device1","device2"]}
const char* POST_URL = "http://10.0.0.181:8801/play";        // POST: {"track":"1.mp3", "device":"device_name"}

// ============================================================================
// PIN DEFINITIONS
// ============================================================================
// Rotary Encoder (EC11)
const int ENCODER_CLK = 27;  // Phase A (CLK)
const int ENCODER_DT = 14;   // Phase B (DT)
const int ENCODER_SW = 13;   // Encoder push button (plays audiobook) - MOVED FROM GPIO12 TO GPIO13

// Additional Controls
const int BRIGHTNESS_BTN = 15;  // K0 button (cycles brightness)
const int BACKLIGHT_PIN = 26;   // TFT backlight PWM control (TEMPORARY TEST - moved from GPIO25 to GPIO26)

// TFT SPI pins are configured in TFT_eSPI library User_Setup.h:
// SCK=18, MOSI=23, RST=4, DC=2, CS=5

// ============================================================================
// DISPLAY SETTINGS
// ============================================================================
#define BACKGROUND_COLOR TFT_BLACK
#define TEXT_COLOR TFT_WHITE
#define SELECTED_BG_COLOR TFT_BLUE
#define SELECTED_TEXT_COLOR TFT_WHITE
#define ITEM_HEIGHT 20           // Pixels per list item
#define TEXT_SIZE 2              // Font size (1=6x8px, 2=12x16px)
#define MAX_NAME_LENGTH 24       // Characters to display before truncating

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================
TFT_eSPI tft = TFT_eSPI();       // TFT display object
ESP32Encoder encoder;             // Rotary encoder object
Preferences preferences;          // Persistent storage object

// ============================================================================
// MODE SYSTEM
// ============================================================================
enum DisplayMode {
  MODE_TRACK_SELECTION,
  MODE_BRIGHTNESS_ADJUST,
  MODE_OUTPUT_SELECTION
};

DisplayMode currentMode = MODE_TRACK_SELECTION;

// ============================================================================
// AUDIOBOOK DATA
// ============================================================================
String audiobooks[50];            // Array to store audiobook names (max 50)
int audiobookCount = 0;           // Number of audiobooks loaded
int currentTrackSelection = 0;    // Currently selected audiobook index
int lastEncoderValue = 0;         // Previous encoder position

// ============================================================================
// AUDIO OUTPUT DEVICE DATA
// ============================================================================
String audioDevices[20];          // Array to store device names (max 20)
int audioDeviceCount = 0;         // Number of devices loaded
int currentDeviceSelection = 0;   // Currently selected device index
String selectedDevice = "";       // Currently active output device

// ============================================================================
// BRIGHTNESS DATA
// ============================================================================
int brightnessValue = 128;        // Current brightness (0-255), starts at 50%

// ============================================================================
// BUTTON DEBOUNCING
// ============================================================================
// Encoder button (play)
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

// Brightness button
bool lastBrightnessButtonState = HIGH;
unsigned long lastBrightnessDebounceTime = 0;

const unsigned long DEBOUNCE_DELAY = 100;  // Milliseconds - balance between responsiveness and debouncing

// ============================================================================
// DELAYED SAVE FOR TRACK SELECTION (to avoid excessive flash writes)
// ============================================================================
unsigned long lastTrackChangeTime = 0;
int lastSavedTrackIndex = -1;
const unsigned long TRACK_SAVE_DELAY = 3000;  // Save 3 seconds after last change

// ============================================================================
// PWM Configuration for backlight
// ============================================================================
const int PWM_CHANNEL = 0;       // ESP32 PWM channel (0-15)
const int PWM_FREQ = 5000;       // 5kHz frequency
const int PWM_RESOLUTION = 8;    // 8-bit resolution (0-255)

// ============================================================================
// SETUP - Runs once at startup
// ============================================================================
void setup() {
  // Initialize serial communication for debugging
  Serial.begin(115200);
  Serial.println("\n\n=== Audiobook Player Starting ===");

  // *** LOAD SAVED PREFERENCES ***
  Serial.println("Loading saved preferences...");
  preferences.begin("audiobook", false);  // Open preferences with namespace "audiobook"

  // Load saved brightness (default: 128 = 50%)
  brightnessValue = preferences.getInt("brightness", 128);
  Serial.print("Loaded brightness: ");
  Serial.println(brightnessValue);

  // Load saved track selection (default: 0)
  currentTrackSelection = preferences.getInt("trackIdx", 0);
  Serial.print("Loaded track index: ");
  Serial.println(currentTrackSelection);

  // Load saved device selection (default: 0)
  currentDeviceSelection = preferences.getInt("deviceIdx", 0);
  Serial.print("Loaded device index: ");
  Serial.println(currentDeviceSelection);

  // Load saved device name (default: "")
  selectedDevice = preferences.getString("deviceName", "");
  Serial.print("Loaded device name: ");
  Serial.println(selectedDevice);

  // *** CONNECT WIFI FIRST - BEFORE DISPLAY INITIALIZATION ***
  Serial.println("Connecting WiFi BEFORE display init...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Fetch audiobook list from server
  Serial.println("Fetching audiobook list...");
  fetchAudiobookList();
  Serial.println("Audiobook list fetch complete");

  // Fetch audio device list from server
  fetchDeviceList();
  Serial.println("Audio device list fetch complete");

  // DISCONNECT WiFi NOW - before display init
  Serial.println("Disconnecting WiFi BEFORE display init...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(1000);  // Wait for WiFi to fully shut down
  Serial.println("WiFi disconnected");

  // *** NOW INITIALIZE DISPLAY - WiFi is completely off ***
  Serial.println("Initializing display AFTER WiFi is off...");

  // Configure and initialize backlight PWM with saved brightness
  ledcAttach(BACKLIGHT_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(BACKLIGHT_PIN, brightnessValue);
  Serial.print("Backlight set to saved value: ");
  Serial.print((brightnessValue * 100) / 255);
  Serial.println("%");

  // Initialize TFT display
  tft.init();
  tft.setRotation(1);  // Landscape mode (320x240)
  Serial.println("TFT initialized");

  // Draw startup screen
  Serial.println("Drawing startup screen...");
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.println("AUDIOBOOK");
  tft.setCursor(10, 50);
  tft.println("PLAYER");
  tft.setTextSize(2);
  tft.setCursor(10, 100);
  tft.println("Ready!");
  Serial.println("Startup screen drawn");
  delay(2000);

  // Initialize rotary encoder
  ESP32Encoder::useInternalWeakPullResistors = puType::up;  // Enable pull-up resistors
  encoder.attachHalfQuad(ENCODER_DT, ENCODER_CLK);  // Attach encoder pins
  encoder.setCount(0);  // Reset counter
  lastEncoderValue = 0;
  Serial.println("Encoder initialized");

  // Initialize buttons with pull-up resistors
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(BRIGHTNESS_BTN, INPUT_PULLUP);
  Serial.println("Buttons initialized");

  // Initialize track save tracking
  lastSavedTrackIndex = currentTrackSelection;

  // Display initial screen (track selection mode)
  Serial.println("Displaying initial screen...");
  currentMode = MODE_TRACK_SELECTION;
  updateDisplay();

  Serial.println("=== Setup Complete ===\n");
}

// ============================================================================
// FETCH AUDIOBOOK LIST FROM SERVER
// ============================================================================
void fetchAudiobookList() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR: WiFi not connected");
    tft.fillScreen(BACKGROUND_COLOR);
    tft.setCursor(0, 0);
    tft.println("WiFi Error!");
    return;
  }

  // Show loading message
  tft.fillScreen(BACKGROUND_COLOR);
  tft.setCursor(0, 0);
  tft.println("Fetching list...");
  Serial.println("Fetching audiobook list...");

  // Make HTTP GET request
  HTTPClient http;
  http.begin(LIST_URL);
  int httpCode = http.GET();

  if (httpCode == 200) {
    // Successfully received response
    String payload = http.getString();
    Serial.println("Server response:");
    Serial.println(payload);

    // Parse JSON response
    // Expected format: {"tracks":["1.mp3","2.mp3","3.mp3"]}
    #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument doc;  // ArduinoJson v7
    #else
      StaticJsonDocument<2048> doc;  // ArduinoJson v6 (adjust size if needed)
    #endif

    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      // JSON parsing failed
      Serial.print("ERROR: JSON parse failed: ");
      Serial.println(error.c_str());
      tft.fillScreen(BACKGROUND_COLOR);
      tft.setCursor(0, 0);
      tft.println("JSON Error!");
      http.end();
      return;
    }

    // Extract tracks array from JSON
    JsonArray tracks = doc["tracks"].as<JsonArray>();
    audiobookCount = 0;

    // Populate audiobooks array
    for (JsonVariant track : tracks) {
      if (audiobookCount >= 50) {
        Serial.println("WARNING: Maximum 50 audiobooks reached");
        break;
      }
      audiobooks[audiobookCount] = track.as<String>();
      Serial.print("  [");
      Serial.print(audiobookCount);
      Serial.print("] ");
      Serial.println(audiobooks[audiobookCount]);
      audiobookCount++;
    }

    Serial.print("Successfully loaded ");
    Serial.print(audiobookCount);
    Serial.println(" audiobooks");

    // Reset selection if out of bounds
    if (currentTrackSelection >= audiobookCount) {
      currentTrackSelection = 0;
    }

  } else {
    // HTTP request failed
    Serial.print("ERROR: HTTP request failed, code: ");
    Serial.println(httpCode);
    tft.fillScreen(BACKGROUND_COLOR);
    tft.setCursor(0, 0);
    tft.print("HTTP Error: ");
    tft.println(httpCode);
  }

  http.end();
}

// ============================================================================
// FETCH AUDIO DEVICE LIST FROM SERVER
// ============================================================================
void fetchDeviceList() {
  Serial.println("Fetching audio device list...");

  // Make HTTP GET request
  HTTPClient http;
  http.begin(DEVICES_URL);
  int httpCode = http.GET();

  if (httpCode == 200) {
    // Successfully received response
    String payload = http.getString();
    Serial.println("Device list response:");
    Serial.println(payload);

    // Parse JSON response
    // Expected format: {"devices":["device1","device2","device3"]}
    #if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument doc;  // ArduinoJson v7
    #else
      StaticJsonDocument<1024> doc;  // ArduinoJson v6
    #endif

    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("ERROR: JSON parse failed: ");
      Serial.println(error.c_str());
      http.end();
      return;
    }

    // Extract devices array from JSON
    JsonArray devices = doc["devices"].as<JsonArray>();
    audioDeviceCount = 0;

    // Populate audioDevices array
    for (JsonVariant device : devices) {
      if (audioDeviceCount >= 20) {
        Serial.println("WARNING: Maximum 20 devices reached");
        break;
      }
      audioDevices[audioDeviceCount] = device.as<String>();
      Serial.print("  [");
      Serial.print(audioDeviceCount);
      Serial.print("] ");
      Serial.println(audioDevices[audioDeviceCount]);
      audioDeviceCount++;
    }

    Serial.print("Successfully loaded ");
    Serial.print(audioDeviceCount);
    Serial.println(" audio devices");

    // Reset selection if out of bounds
    if (currentDeviceSelection >= audioDeviceCount) {
      currentDeviceSelection = 0;
    }

    // Set default selected device if not set
    if (selectedDevice == "" && audioDeviceCount > 0) {
      selectedDevice = audioDevices[0];
      Serial.print("Default device set to: ");
      Serial.println(selectedDevice);
    }

  } else {
    Serial.print("ERROR: HTTP request failed, code: ");
    Serial.println(httpCode);
  }

  http.end();
}

// ============================================================================
// DISPLAY FUNCTIONS FOR EACH MODE
// ============================================================================

// Display mode indicator bar at top
void displayModeIndicator() {
  // Draw mode bar at top
  tft.fillRect(0, 0, tft.width(), 15, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(5, 4);

  if (currentMode == MODE_TRACK_SELECTION) {
    tft.print("MODE: TRACK");
  } else if (currentMode == MODE_BRIGHTNESS_ADJUST) {
    tft.print("MODE: BRIGHTNESS");
  } else if (currentMode == MODE_OUTPUT_SELECTION) {
    tft.print("MODE: OUTPUT [");
    tft.print(selectedDevice);
    tft.print("]");
  }
}

// Display track selection mode
void displayTrackSelection() {
  tft.fillScreen(BACKGROUND_COLOR);
  displayModeIndicator();

  if (audiobookCount == 0) {
    tft.setCursor(5, 20);
    tft.setTextSize(TEXT_SIZE);
    tft.setTextColor(TEXT_COLOR);
    tft.println("No audiobooks");
    return;
  }

  int maxVisibleItems = (tft.height() - 25) / ITEM_HEIGHT;
  int scrollOffset = 0;
  if (currentTrackSelection >= maxVisibleItems) {
    scrollOffset = currentTrackSelection - maxVisibleItems + 1;
  }

  int yPos = 20;
  for (int i = scrollOffset; i < audiobookCount && i < scrollOffset + maxVisibleItems; i++) {
    if (i == currentTrackSelection) {
      tft.fillRect(0, yPos - 2, tft.width(), ITEM_HEIGHT, SELECTED_BG_COLOR);
      tft.setTextColor(SELECTED_TEXT_COLOR, SELECTED_BG_COLOR);
    } else {
      tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
    }

    tft.setCursor(5, yPos);
    tft.setTextSize(TEXT_SIZE);

    String displayName = audiobooks[i];
    if (displayName.length() > MAX_NAME_LENGTH) {
      displayName = displayName.substring(0, MAX_NAME_LENGTH - 3) + "...";
    }

    tft.println(displayName);
    yPos += ITEM_HEIGHT;
  }

  // Scroll indicator
  if (audiobookCount > maxVisibleItems) {
    int indicatorHeight = ((tft.height() - 20) * maxVisibleItems) / audiobookCount;
    int indicatorY = 20 + ((tft.height() - 20) * scrollOffset) / audiobookCount;
    tft.fillRect(tft.width() - 5, indicatorY, 3, indicatorHeight, TFT_DARKGREY);
  }

  Serial.print("Track mode: showing ");
  Serial.print(scrollOffset);
  Serial.print("-");
  Serial.print(min(scrollOffset + maxVisibleItems - 1, audiobookCount - 1));
  Serial.print(" (selected: ");
  Serial.print(currentTrackSelection);
  Serial.println(")");
}

// Display brightness adjustment mode
void displayBrightnessAdjust() {
  tft.fillScreen(BACKGROUND_COLOR);
  displayModeIndicator();

  // Draw brightness bar
  int barWidth = 280;
  int barHeight = 40;
  int barX = (tft.width() - barWidth) / 2;
  int barY = 80;

  // Border
  tft.drawRect(barX - 2, barY - 2, barWidth + 4, barHeight + 4, TFT_WHITE);

  // Fill bar based on brightness (0-255)
  int fillWidth = (barWidth * brightnessValue) / 255;
  tft.fillRect(barX, barY, fillWidth, barHeight, TFT_YELLOW);
  tft.fillRect(barX + fillWidth, barY, barWidth - fillWidth, barHeight, TFT_DARKGREY);

  // Display percentage
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE, BACKGROUND_COLOR);
  int percentage = (brightnessValue * 100) / 255;
  String percentText = String(percentage) + "%";
  int textWidth = percentText.length() * 18;
  tft.setCursor((tft.width() - textWidth) / 2, 140);
  tft.print(percentText);

  // Instructions
  tft.setTextSize(2);
  tft.setCursor(20, 180);
  tft.print("Rotate to adjust");
  tft.setCursor(20, 200);
  tft.print("Press to confirm");

  Serial.print("Brightness mode: ");
  Serial.print(percentage);
  Serial.println("%");
}

// Display output device selection mode
void displayOutputSelection() {
  tft.fillScreen(BACKGROUND_COLOR);
  displayModeIndicator();

  if (audioDeviceCount == 0) {
    tft.setCursor(5, 20);
    tft.setTextSize(TEXT_SIZE);
    tft.setTextColor(TEXT_COLOR);
    tft.println("No devices");
    return;
  }

  int maxVisibleItems = (tft.height() - 25) / ITEM_HEIGHT;
  int scrollOffset = 0;
  if (currentDeviceSelection >= maxVisibleItems) {
    scrollOffset = currentDeviceSelection - maxVisibleItems + 1;
  }

  int yPos = 20;
  for (int i = scrollOffset; i < audioDeviceCount && i < scrollOffset + maxVisibleItems; i++) {
    if (i == currentDeviceSelection) {
      tft.fillRect(0, yPos - 2, tft.width(), ITEM_HEIGHT, SELECTED_BG_COLOR);
      tft.setTextColor(SELECTED_TEXT_COLOR, SELECTED_BG_COLOR);
    } else {
      tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
    }

    tft.setCursor(5, yPos);
    tft.setTextSize(TEXT_SIZE);

    String displayName = audioDevices[i];

    // Show checkmark for currently active device
    if (audioDevices[i] == selectedDevice) {
      tft.print("* ");
    } else {
      tft.print("  ");
    }

    if (displayName.length() > (MAX_NAME_LENGTH - 2)) {
      displayName = displayName.substring(0, MAX_NAME_LENGTH - 5) + "...";
    }

    tft.println(displayName);
    yPos += ITEM_HEIGHT;
  }

  Serial.print("Output mode: showing devices, selected: ");
  Serial.println(currentDeviceSelection);
}

// Main display update function
void updateDisplay() {
  if (currentMode == MODE_TRACK_SELECTION) {
    displayTrackSelection();
  } else if (currentMode == MODE_BRIGHTNESS_ADJUST) {
    displayBrightnessAdjust();
  } else if (currentMode == MODE_OUTPUT_SELECTION) {
    displayOutputSelection();
  }
}

// ============================================================================
// SEND PLAY REQUEST TO SERVER
// ============================================================================
void sendPlayRequest(int index) {
  // Validate index
  if (index < 0 || index >= audiobookCount) {
    Serial.print("ERROR: Invalid index ");
    Serial.println(index);
    return;
  }

  Serial.print("Playing audiobook [");
  Serial.print(index);
  Serial.print("]: ");
  Serial.println(audiobooks[index]);

  // Save current track selection to preferences
  preferences.putInt("trackIdx", index);
  lastSavedTrackIndex = index;
  Serial.print("Saved track index: ");
  Serial.println(index);

  // DO NOT TOUCH DISPLAY - WiFi will corrupt it
  // Just reconnect WiFi in background

  // RECONNECT WiFi
  Serial.println("Reconnecting WiFi for playback...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(300);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nERROR: WiFi reconnection failed!");
    // Don't draw error - WiFi failed so display might be corrupted anyway
    // Just disconnect and restore
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500);

    // Reset and restore display
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
    delay(100);
    digitalWrite(4, HIGH);
    delay(200);

    tft.init();
    tft.setRotation(1);
    ledcAttach(BACKLIGHT_PIN, PWM_FREQ, PWM_RESOLUTION);
    ledcWrite(BACKLIGHT_PIN, brightnessValue);

    updateDisplay();
    return;
  }

  Serial.println("\nWiFi reconnected!");

  // Prepare HTTP POST request
  HTTPClient http;
  http.begin(POST_URL);
  http.addHeader("Content-Type", "application/json");

  // Build JSON payload: {"track":"filename.mp3", "device":"device_name"}
  String payload = "{\"track\":\"" + audiobooks[index] + "\"";
  if (selectedDevice != "") {
    payload += ",\"device\":\"" + selectedDevice + "\"";
  }
  payload += "}";

  Serial.print("Sending: ");
  Serial.println(payload);

  // Send POST request
  int httpCode = http.POST(payload);
  Serial.print("Response code: ");
  Serial.println(httpCode);

  http.end();

  // Playback request sent successfully!
  Serial.println("Playback request sent (HTTP 200)");
  Serial.println("Restarting ESP32 to recover display...");

  delay(500);

  // RESTART ESP32 completely - this will run setup() again with fresh WiFi/display init
  ESP.restart();
}

// ============================================================================
// MAIN LOOP - Runs continuously
// ============================================================================
void loop() {
  // -------------------------------------------------------------------------
  // ROTARY ENCODER - Behavior changes based on mode
  // -------------------------------------------------------------------------
  int encoderValue = encoder.getCount() / 2;

  if (encoderValue != lastEncoderValue) {
    int change = encoderValue - lastEncoderValue;
    lastEncoderValue = encoderValue;

    if (currentMode == MODE_TRACK_SELECTION) {
      // Track selection mode - navigate through audiobooks
      currentTrackSelection += change;
      if (currentTrackSelection < 0) {
        currentTrackSelection = audiobookCount - 1;
      } else if (currentTrackSelection >= audiobookCount) {
        currentTrackSelection = 0;
      }
      Serial.print("Track selection: ");
      Serial.println(audiobooks[currentTrackSelection]);

      // Mark time for delayed save to preferences
      lastTrackChangeTime = millis();

      updateDisplay();

    } else if (currentMode == MODE_BRIGHTNESS_ADJUST) {
      // Brightness mode - adjust brightness value
      brightnessValue += (change * 5);  // 5 units per detent for smoother control
      if (brightnessValue < 0) brightnessValue = 0;
      if (brightnessValue > 255) brightnessValue = 255;

      // Apply brightness immediately
      ledcWrite(BACKLIGHT_PIN, brightnessValue);

      Serial.print("Brightness adjusted to: ");
      Serial.println((brightnessValue * 100) / 255);
      updateDisplay();

    } else if (currentMode == MODE_OUTPUT_SELECTION) {
      // Output selection mode - navigate through devices
      currentDeviceSelection += change;
      if (currentDeviceSelection < 0) {
        currentDeviceSelection = audioDeviceCount - 1;
      } else if (currentDeviceSelection >= audioDeviceCount) {
        currentDeviceSelection = 0;
      }
      Serial.print("Output selection: ");
      Serial.println(audioDevices[currentDeviceSelection]);
      updateDisplay();
    }
  }

  // -------------------------------------------------------------------------
  // ENCODER BUTTON - Action changes based on mode
  // -------------------------------------------------------------------------
  bool buttonState = digitalRead(ENCODER_SW);
  static bool encoderDebouncing = false;

  if (buttonState == LOW && !encoderDebouncing) {
    lastDebounceTime = millis();
    encoderDebouncing = true;
  }

  if (encoderDebouncing && (millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (buttonState == LOW) {
      if (currentMode == MODE_TRACK_SELECTION) {
        // Track mode - play selected audiobook
        Serial.println("*** PLAYING TRACK ***");
        sendPlayRequest(currentTrackSelection);
      } else if (currentMode == MODE_BRIGHTNESS_ADJUST) {
        // Brightness mode - confirm and return to track selection
        Serial.println("*** BRIGHTNESS CONFIRMED ***");

        // Save brightness to preferences
        preferences.putInt("brightness", brightnessValue);
        Serial.print("Saved brightness: ");
        Serial.println(brightnessValue);

        currentMode = MODE_TRACK_SELECTION;
        updateDisplay();
      } else if (currentMode == MODE_OUTPUT_SELECTION) {
        // Output mode - select device and return to track selection
        selectedDevice = audioDevices[currentDeviceSelection];
        Serial.print("*** OUTPUT DEVICE SELECTED: ");
        Serial.print(selectedDevice);
        Serial.println(" ***");

        // Save device selection to preferences
        preferences.putInt("deviceIdx", currentDeviceSelection);
        preferences.putString("deviceName", selectedDevice);
        Serial.print("Saved device: ");
        Serial.println(selectedDevice);

        currentMode = MODE_TRACK_SELECTION;
        updateDisplay();
      }
    }
    encoderDebouncing = false;
  }

  if (buttonState == HIGH) {
    encoderDebouncing = false;
  }

  // -------------------------------------------------------------------------
  // MODE SWITCH BUTTON (K0) - Cycle between modes
  // -------------------------------------------------------------------------
  bool modeButtonState = digitalRead(BRIGHTNESS_BTN);
  static bool modeDebouncing = false;
  static unsigned long lastModeAction = 0;
  const unsigned long MODE_COOLDOWN = 500;  // 500ms cooldown between mode changes

  if (modeButtonState == LOW && !modeDebouncing && (millis() - lastModeAction) > MODE_COOLDOWN) {
    lastBrightnessDebounceTime = millis();
    modeDebouncing = true;
  }

  if (modeDebouncing && (millis() - lastBrightnessDebounceTime) > DEBOUNCE_DELAY) {
    if (modeButtonState == LOW) {
      // Cycle to next mode
      if (currentMode == MODE_TRACK_SELECTION) {
        // Leaving track selection mode - save current selection
        if (currentTrackSelection != lastSavedTrackIndex) {
          preferences.putInt("trackIdx", currentTrackSelection);
          lastSavedTrackIndex = currentTrackSelection;
          Serial.print("Saved track index on mode switch: ");
          Serial.println(currentTrackSelection);
        }

        currentMode = MODE_BRIGHTNESS_ADJUST;
        Serial.println("*** SWITCHED TO BRIGHTNESS MODE ***");
      } else if (currentMode == MODE_BRIGHTNESS_ADJUST) {
        currentMode = MODE_OUTPUT_SELECTION;
        Serial.println("*** SWITCHED TO OUTPUT SELECTION MODE ***");
      } else if (currentMode == MODE_OUTPUT_SELECTION) {
        currentMode = MODE_TRACK_SELECTION;
        Serial.println("*** SWITCHED TO TRACK SELECTION MODE ***");
      }

      updateDisplay();
      lastModeAction = millis();
    }
    modeDebouncing = false;
  }

  if (modeButtonState == HIGH) {
    modeDebouncing = false;
  }

  // -------------------------------------------------------------------------
  // DELAYED SAVE - Save track selection after user stops rotating
  // -------------------------------------------------------------------------
  if (lastTrackChangeTime > 0 &&
      (millis() - lastTrackChangeTime) > TRACK_SAVE_DELAY &&
      currentTrackSelection != lastSavedTrackIndex) {
    // User hasn't changed track for 3 seconds, save it
    preferences.putInt("trackIdx", currentTrackSelection);
    lastSavedTrackIndex = currentTrackSelection;
    Serial.print("Auto-saved track index: ");
    Serial.println(currentTrackSelection);
    lastTrackChangeTime = 0;  // Reset timer
  }

  // Small delay to prevent excessive CPU usage
  delay(10);
}
