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

// Build endpoint URLs using SERVER_URL
String LIST_URL = String(SERVER_URL) + "/list";       // GET: returns {"tracks":["1.mp3","2.mp3"]}
String DEVICES_URL = String(SERVER_URL) + "/listdevices";  // GET: returns {"devices":["device1","device2"]}
String POST_URL = String(SERVER_URL) + "/play";        // POST: {"track":"1.mp3", "device":"device_name"}

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
int lastPlayedTrack = -1;         // Last track that was played (-1 = none)
int lastEncoderValue = 0;         // Previous encoder position
int previousTrackSelection = -1;  // For smooth scrolling (track previous selection)
int previousScrollOffset = -1;    // For smooth scrolling (track previous scroll position)

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
// BOOT ANIMATION - Character wakes up and goes to sleep
// ============================================================================
void drawCharacter(int x, int y, uint16_t color, bool inBed = false) {
  // Draw simple character: head + body + arms + legs
  if (inBed) {
    // Character lying down in bed
    tft.fillCircle(x, y, 12, color);  // Head
    tft.fillRect(x + 10, y - 6, 30, 12, color);  // Body horizontal
  } else {
    // Character standing
    tft.fillCircle(x, y, 12, color);  // Head
    tft.fillRect(x - 6, y + 12, 12, 25, color);  // Body
    tft.fillRect(x - 15, y + 15, 10, 5, color);  // Left arm
    tft.fillRect(x + 5, y + 15, 10, 5, color);   // Right arm
    tft.fillRect(x - 8, y + 37, 6, 20, color);   // Left leg
    tft.fillRect(x + 2, y + 37, 6, 20, color);   // Right leg
  }
}

void drawBed(int x, int y, uint16_t bedColor, uint16_t sheetColor) {
  // Draw bed frame
  tft.fillRect(x, y, 80, 10, bedColor);  // Mattress
  tft.fillRect(x - 5, y - 15, 5, 25, bedColor);  // Left post
  tft.fillRect(x + 80, y - 15, 5, 25, bedColor);  // Right post
  // Draw blanket
  tft.fillRect(x + 5, y - 8, 50, 15, sheetColor);  // Sheet
}

void drawSpeechBubble(int x, int y, const char* text) {
  // Draw thought bubble
  tft.fillCircle(x, y, 25, TFT_WHITE);
  tft.drawCircle(x, y, 25, TFT_BLACK);
  // Small bubble dots
  tft.fillCircle(x - 15, y + 22, 4, TFT_WHITE);
  tft.drawCircle(x - 15, y + 22, 4, TFT_BLACK);
  tft.fillCircle(x - 20, y + 30, 3, TFT_WHITE);
  tft.drawCircle(x - 20, y + 30, 3, TFT_BLACK);

  // Draw text
  tft.setTextColor(TFT_BLUE);
  tft.setTextSize(2);
  tft.setCursor(x - 20, y - 8);
  tft.print(text);
}

void drawSun(int x, int y) {
  // Draw sun with rays
  tft.fillCircle(x, y, 18, TFT_YELLOW);
  tft.drawCircle(x, y, 18, TFT_ORANGE);
  // Sun rays
  for (int i = 0; i < 8; i++) {
    float angle = i * 45 * 3.14159 / 180;
    int x1 = x + cos(angle) * 22;
    int y1 = y + sin(angle) * 22;
    int x2 = x + cos(angle) * 28;
    int y2 = y + sin(angle) * 28;
    tft.drawLine(x1, y1, x2, y2, TFT_ORANGE);
  }
}

void drawMoon(int x, int y) {
  // Draw crescent moon
  tft.fillCircle(x, y, 16, TFT_WHITE);
  tft.fillCircle(x + 8, y - 4, 14, 0x0841);  // Dark blue to create crescent
  // Add some stars near moon
  tft.fillCircle(x - 25, y - 10, 1, TFT_WHITE);
  tft.fillCircle(x + 22, y + 8, 1, TFT_WHITE);
  tft.fillCircle(x - 18, y + 15, 1, TFT_WHITE);
}

void drawMusicNote(int x, int y, uint16_t color) {
  // Draw a simple music note
  tft.fillCircle(x, y + 8, 4, color);       // Note head
  tft.fillRect(x + 3, y, 2, 10, color);     // Note stem
}

void drawCloud(int x, int y, uint16_t color) {
  // Draw a simple cloud shape
  tft.fillCircle(x, y, 12, color);
  tft.fillCircle(x + 15, y, 12, color);
  tft.fillCircle(x + 7, y - 8, 10, color);
  tft.fillRect(x - 12, y, 27, 12, color);
}

void playDownloadAnimation(String trackName) {
  // Dark night background for download animation
  uint16_t nightBlue = 0x0003;
  tft.fillScreen(nightBlue);

  // Draw stars in background
  for (int i = 0; i < 20; i++) {
    int sx = random(0, 320);
    int sy = random(0, 180);
    tft.fillCircle(sx, sy, 1, TFT_WHITE);
  }

  // Draw moon
  drawMoon(280, 80);

  // Draw bed with sleeping character at bottom
  int bedX = 130;
  int bedY = 200;
  drawBed(bedX, bedY, TFT_BROWN, TFT_DARKGREY);
  drawCharacter(bedX + 20, bedY - 4, TFT_ORANGE, true);

  // Draw cloud near moon
  int cloudX = 240;
  int cloudY = 80;
  drawCloud(cloudX, cloudY, TFT_WHITE);

  // Display track name at top
  tft.setTextColor(TFT_CYAN, nightBlue);
  tft.setTextSize(2);
  String displayName = trackName;
  displayName.replace("_", " ");
  if (displayName.length() > 23) {
    displayName = displayName.substring(0, 20) + "...";
  }
  tft.setCursor(10, 10);
  tft.print("Playing:");
  tft.setCursor(10, 30);
  tft.print(displayName);

  // Animate music notes falling from cloud to character
  // 10 waves of notes
  for (int wave = 0; wave < 6; wave++) {
    // Start positions for 3 notes
    int note1X = cloudX - 10;
    int note2X = cloudX + 5;
    int note3X = cloudX + 20;

    for (int step = 0; step < 25; step++) {
      // Calculate Y positions (falling down)
      int note1Y = cloudY + 20 + (step * 5);
      int note2Y = cloudY + 20 + (step * 5) - 20;  // Offset
      int note3Y = cloudY + 20 + (step * 5) - 40;  // More offset

      // Clear previous positions (carefully to avoid cloud area)
      if (step > 0) {
        int prevY1 = cloudY + 20 + ((step - 1) * 5);
        int prevY2 = prevY1 - 20;
        int prevY3 = prevY1 - 40;

        // Only clear note1 if it's below the cloud
        if (prevY1 > cloudY + 35) {
          tft.fillRect(note1X - 5, prevY1 - 5, 15, 20, nightBlue);
        }
        // Only clear note2 if it's below the cloud
        if (prevY2 > cloudY + 35) {
          tft.fillRect(note2X - 5, prevY2 - 5, 15, 20, nightBlue);
        }
        // Only clear note3 if it's below the cloud
        if (prevY3 > cloudY + 35) {
          tft.fillRect(note3X - 5, prevY3 - 5, 15, 20, nightBlue);
        }

        // Redraw any stars that were cleared
        if (random(0, 3) == 0) {
          int sx = random(note1X - 10, note3X + 10);
          int sy = random(max(prevY1 - 10, 120), min(prevY1 + 10, 180));
          if (sy < 180) tft.fillCircle(sx, sy, 1, TFT_WHITE);
        }
      }

      // Draw notes at new positions (only between cloud and bed)
      if (note1Y > cloudY + 30 && note1Y < bedY - 15) drawMusicNote(note1X, note1Y, TFT_YELLOW);
      if (note2Y > cloudY + 30 && note2Y < bedY - 15) drawMusicNote(note2X, note2Y, TFT_CYAN);
      if (note3Y > cloudY + 30 && note3Y < bedY - 15) drawMusicNote(note3X, note3Y, TFT_MAGENTA);

      delay(60);  // Animation speed
    }

    // Clear any remaining notes at the bottom after this wave
    tft.fillRect(note1X - 5, bedY - 25, 15, 20, nightBlue);
    tft.fillRect(note2X - 5, bedY - 25, 15, 20, nightBlue);
    tft.fillRect(note3X - 5, bedY - 25, 15, 20, nightBlue);
  }

  // Show "Downloading complete" message
  tft.fillRect(0, 100, 320, 40, nightBlue);
  tft.setTextColor(TFT_GREEN, nightBlue);
  tft.setTextSize(2);
  tft.setCursor(80, 110);
  tft.print("Loading...");
  delay(500);
}

void playBootAnimation() {
  // Color scheme - day to night transition (light to dark)
  uint16_t dayBlue = 0x5D1F;       // Bright sky blue (day)
  uint16_t afternoonBlue = 0x3C5F; // Medium blue (afternoon)
  uint16_t eveningBlue = 0x1C07;   // Darker blue (evening)
  uint16_t nightBlue = 0x0003;     // Very dark blue (night)

  // Start with bright daytime sky
  tft.fillScreen(dayBlue);

  // Title at top with dark text for light background
  tft.setTextColor(TFT_NAVY);
  tft.setTextSize(3);
  tft.setCursor(60, 10);
  tft.print("AUDIOBOOK");
  tft.setCursor(90, 40);
  tft.print("PLAYER");

  int bedX = 120;
  int bedY = 150;

  // Draw bright daytime sun
  drawSun(40, 85);

  // ===== FRAME 1: Character sleeping in bed (DAYTIME) =====
  drawBed(bedX, bedY, TFT_BROWN, TFT_BLUE);
  drawCharacter(bedX + 20, bedY - 4, TFT_ORANGE, true);  // In bed
  drawSpeechBubble(bedX + 30, bedY - 50, "Zzz");
  delay(800);

  // ===== FRAME 2: Character wakes up (sits up) - DAYTIME =====
  // Clear speech bubble area completely
  tft.fillRect(bedX - 30, bedY - 80, 120, 85, dayBlue);  // Clear with day sky
  drawBed(bedX, bedY, TFT_BROWN, TFT_BLUE);
  // Character sitting up in bed
  tft.fillCircle(bedX + 30, bedY - 20, 12, TFT_ORANGE);  // Head
  tft.fillRect(bedX + 24, bedY - 8, 12, 20, TFT_ORANGE);  // Body
  tft.fillRect(bedX + 15, bedY - 5, 10, 5, TFT_ORANGE);   // Left arm
  tft.fillRect(bedX + 36, bedY - 5, 10, 5, TFT_ORANGE);   // Right arm
  delay(500);

  // ===== FRAME 3: Character jumps out! - DAYTIME =====
  tft.fillRect(bedX - 10, bedY - 80, 100, 90, dayBlue);  // Clear area
  drawBed(bedX, bedY, TFT_BROWN, TFT_BLUE);
  drawCharacter(bedX + 50, bedY - 60, TFT_ORANGE);  // Jumping (orange = excited!)
  tft.setTextColor(TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(bedX + 70, bedY - 70);
  tft.print("!");
  delay(400);

  // ===== TRANSITION TO AFTERNOON =====
  tft.fillScreen(afternoonBlue);
  // Redraw title - still dark text
  tft.setTextColor(TFT_NAVY);
  tft.setTextSize(3);
  tft.setCursor(60, 10);
  tft.print("AUDIOBOOK");
  tft.setCursor(90, 40);
  tft.print("PLAYER");
  // Sun still visible
  drawSun(35, 90);

  // ===== FRAME 4: Character walks - AFTERNOON =====
  drawCharacter(100, bedY - 30, TFT_GREEN);  // Walking (green)
  delay(400);

  // ===== TRANSITION TO EVENING - Sun setting =====
  tft.fillScreen(eveningBlue);
  // Redraw title - lighter text for darker background
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(60, 10);
  tft.print("AUDIOBOOK");
  tft.setCursor(90, 40);
  tft.print("PLAYER");
  // Sun low on horizon
  drawSun(25, 110);

  // ===== FRAME 5: Character walks more - EVENING =====
  drawCharacter(60, bedY - 30, TFT_MAGENTA);  // Walking (magenta)
  delay(400);

  // ===== TRANSITION TO NIGHT - Moon appears =====
  tft.fillScreen(nightBlue);
  // Redraw title - bright text for dark background
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(3);
  tft.setCursor(60, 10);
  tft.print("AUDIOBOOK");
  tft.setCursor(90, 40);
  tft.print("PLAYER");
  // Moon in sky
  drawMoon(40, 85);
  // Add stars
  for (int i = 0; i < 15; i++) {
    int sx = random(0, 320);
    int sy = random(70, 240);
    tft.fillCircle(sx, sy, 1, TFT_WHITE);
  }

  // ===== FRAME 6: Character yawns and walks back - NIGHT =====
  drawCharacter(90, bedY - 30, TFT_PINK);  // Pink = sleepy
  // Yawn effect
  tft.drawCircle(90, bedY - 30 + 12, 8, TFT_WHITE);
  delay(500);

  // ===== FRAME 7: Back to bed - NIGHT =====
  tft.fillRect(0, bedY - 80, 320, 120, nightBlue);  // Clear with dark night
  // Redraw stars in the cleared area
  tft.fillCircle(50, bedY - 40, 1, TFT_WHITE);
  tft.fillCircle(130, bedY - 20, 1, TFT_WHITE);
  tft.fillCircle(180, bedY - 60, 1, TFT_WHITE);
  tft.fillCircle(95, bedY - 55, 1, TFT_WHITE);
  drawBed(bedX, bedY, TFT_BROWN, TFT_DARKGREY);  // Dark blanket for night
  drawCharacter(bedX + 20, bedY - 4, TFT_ORANGE, true);  // Back in bed
  delay(400);

  // ===== FRAME 8: Sleeping with Zzz - NIGHT =====
  drawSpeechBubble(bedX + 30, bedY - 50, "ZzZ");
  delay(600);

  // ===== FRAME 9: More Zzz - NIGHT =====
  // Clear old bubble completely
  tft.fillRect(bedX + 5, bedY - 80, 60, 65, nightBlue);  // Clear with dark night
  // Redraw stars that were cleared
  tft.fillCircle(bedX + 30, bedY - 60, 1, TFT_WHITE);
  tft.fillCircle(bedX + 45, bedY - 70, 1, TFT_WHITE);
  drawSpeechBubble(bedX + 30, bedY - 50, "ZZzz");
  delay(600);

  // Final message - dark background with bright text
  tft.fillRect(0, 200, 320, 40, nightBlue);
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(2);
  tft.setCursor(115, 210);
  tft.print("Ready!");
  delay(800);
}

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

  // Load last played track (default: -1 = none)
  lastPlayedTrack = preferences.getInt("lastPlayed", -1);
  Serial.print("Loaded last played track: ");
  Serial.println(lastPlayedTrack);

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

  // Play boot animation
  Serial.println("Playing boot animation...");
  playBootAnimation();
  Serial.println("Boot animation complete");

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
    String deviceDisplay = selectedDevice;
    deviceDisplay.replace("_", " ");
    tft.print(deviceDisplay);
    tft.print("]");
  }
}

// Helper function to draw a single track item
void drawTrackItem(int index, int yPos, bool isSelected) {
  // Draw background
  if (isSelected) {
    tft.fillRect(0, yPos - 2, tft.width() - 6, ITEM_HEIGHT, SELECTED_BG_COLOR);
    tft.setTextColor(SELECTED_TEXT_COLOR, SELECTED_BG_COLOR);
  } else {
    tft.fillRect(0, yPos - 2, tft.width() - 6, ITEM_HEIGHT, BACKGROUND_COLOR);
    tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
  }

  tft.setCursor(5, yPos);
  tft.setTextSize(TEXT_SIZE);

  // Show indicator if this is the last played track
  if (index == lastPlayedTrack) {
    tft.print("> ");  // Play indicator
  } else {
    tft.print("  ");  // Spacing
  }

  String displayName = audiobooks[index];
  // Replace underscores with spaces for prettier display
  displayName.replace("_", " ");

  // Adjust max length to account for indicator
  int maxLen = MAX_NAME_LENGTH - 2;
  if (displayName.length() > maxLen) {
    displayName = displayName.substring(0, maxLen - 3) + "...";
  }

  tft.print(displayName);
}

// Display track selection mode (optimized for smooth scrolling)
void displayTrackSelection() {
  int maxVisibleItems = (tft.height() - 25) / ITEM_HEIGHT;

  // Page-based scrolling: each page shows maxVisibleItems
  // Selection stays within a page until you cross the boundary
  int scrollOffset = (currentTrackSelection / maxVisibleItems) * maxVisibleItems;

  // Check if we need a full redraw or just partial update
  bool needFullRedraw = (previousScrollOffset != scrollOffset) ||
                        (previousScrollOffset == -1) ||
                        (previousTrackSelection == -1);

  if (needFullRedraw) {
    // Full redraw needed (scroll position changed or first draw)
    tft.fillScreen(BACKGROUND_COLOR);
    displayModeIndicator();

    if (audiobookCount == 0) {
      tft.setCursor(5, 20);
      tft.setTextSize(TEXT_SIZE);
      tft.setTextColor(TEXT_COLOR);
      tft.println("No audiobooks");
      previousScrollOffset = scrollOffset;
      previousTrackSelection = currentTrackSelection;
      return;
    }

    // Draw all visible items
    int yPos = 20;
    for (int i = scrollOffset; i < audiobookCount && i < scrollOffset + maxVisibleItems; i++) {
      drawTrackItem(i, yPos, i == currentTrackSelection);
      yPos += ITEM_HEIGHT;
    }

    // Draw scroll indicator
    if (audiobookCount > maxVisibleItems) {
      int indicatorHeight = ((tft.height() - 20) * maxVisibleItems) / audiobookCount;
      int indicatorY = 20 + ((tft.height() - 20) * scrollOffset) / audiobookCount;
      tft.fillRect(tft.width() - 5, indicatorY, 3, indicatorHeight, TFT_DARKGREY);
    }
  } else {
    // Partial update - only redraw changed items (much faster, no flicker)
    if (previousTrackSelection != currentTrackSelection) {
      // Redraw the previously selected item (remove highlight)
      if (previousTrackSelection >= scrollOffset &&
          previousTrackSelection < scrollOffset + maxVisibleItems) {
        int prevYPos = 20 + (previousTrackSelection - scrollOffset) * ITEM_HEIGHT;
        drawTrackItem(previousTrackSelection, prevYPos, false);
      }

      // Redraw the newly selected item (add highlight)
      int newYPos = 20 + (currentTrackSelection - scrollOffset) * ITEM_HEIGHT;
      drawTrackItem(currentTrackSelection, newYPos, true);
    }
  }

  // Update tracking variables
  previousScrollOffset = scrollOffset;
  previousTrackSelection = currentTrackSelection;
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
    // Replace underscores with spaces for prettier display
    displayName.replace("_", " ");

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

  // Save current track selection and last played to preferences
  preferences.putInt("trackIdx", index);
  lastSavedTrackIndex = index;
  preferences.putInt("lastPlayed", index);
  lastPlayedTrack = index;
  Serial.print("Saved track index: ");
  Serial.println(index);

  // PLAY DOWNLOAD ANIMATION (shows while WiFi connects)
  Serial.println("Playing download animation...");
  playDownloadAnimation(audiobooks[index]);

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
  Serial.println("Playback request sent");
  Serial.println("Recovering display without ESP restart...");

  // DISCONNECT WiFi completely
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(1000);  // Critical delay to let WiFi fully shut down
  Serial.println("WiFi disconnected");

  // HARDWARE RESET of TFT display via RST pin (GPIO4)
  Serial.println("Performing display hardware reset...");
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);   // Pull reset low
  delay(100);             // Hold reset for 100ms
  digitalWrite(4, HIGH);  // Release reset
  delay(200);             // Wait for display to initialize

  // RE-INITIALIZE display
  Serial.println("Re-initializing display...");
  tft.init();
  tft.setRotation(1);  // Landscape mode

  // RESTORE backlight PWM
  ledcAttach(BACKLIGHT_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(BACKLIGHT_PIN, brightnessValue);
  Serial.println("Backlight restored");

  // REDRAW current screen (maintains position in list!)
  Serial.println("Redrawing display...");
  updateDisplay();

  Serial.println("Display recovered - ready for interaction!");
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

      // Wrap-around with full refresh
      if (currentTrackSelection < 0) {
        // Wrapped from top to bottom - force full refresh
        currentTrackSelection = audiobookCount - 1;
        previousTrackSelection = -1;  // Force full redraw
        previousScrollOffset = -1;
      } else if (currentTrackSelection >= audiobookCount) {
        // Wrapped from bottom to top - force full refresh
        currentTrackSelection = 0;
        previousTrackSelection = -1;  // Force full redraw
        previousScrollOffset = -1;
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

        previousTrackSelection = -1;  // Force full redraw
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

        previousTrackSelection = -1;  // Force full redraw
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
        previousTrackSelection = -1;  // Force full redraw
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
