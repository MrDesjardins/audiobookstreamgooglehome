#include <WiFi.h>
#include <HTTPClient.h>
#include <./secrets.h>

//const char* POST_URL = "http://10.0.0.181:8801/play";
const char* POST_URL = "http://127.0.0.1:8801/play";
const int POT_PIN = 34;
const int MIN_VALUE = 0;
const int MAX_VALUE = 12;

const unsigned long STABILIZE_DELAY_MS = 2000;

int lastValue = -1;
int stableValue = -1;

unsigned long lastChangeTime = 0;
bool postSent = false;

void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  analogSetPinAttenuation(POT_PIN, ADC_11db);


  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

int readAveragedPot(int samples = 16) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    int val = analogRead(POT_PIN);
    sum += val;
    delayMicroseconds(300);
  }
  return sum / samples;
}


void sendPost(int value) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping POST");
    return;
  }

  HTTPClient http;
  http.begin(POST_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"track\": \"" + String(value) + ".mp3\"}";

  int httpCode = http.POST(payload);

  Serial.print("POST sent, code: ");
  Serial.println(httpCode);

  http.end();
}

void loop() {
  int rawValue = readAveragedPot();
  // Serial.print("Value:");
  // Serial.println(rawValue);
  int mappedValue = map(rawValue, 0, 4095, MIN_VALUE, MAX_VALUE + 1);
  mappedValue = constrain(mappedValue, MIN_VALUE, MAX_VALUE);

  unsigned long now = millis();

  // Detect change
  if (mappedValue != lastValue) {
    lastValue = mappedValue;
    lastChangeTime = now;
    postSent = false;

    //Serial.print("Value changed to ");
    //Serial.println(mappedValue);
  }

  // Check stabilization
  if (!postSent && (now - lastChangeTime >= STABILIZE_DELAY_MS)) {
    if (mappedValue != stableValue) {
      stableValue = mappedValue;
      Serial.print("Value stabilized at ");
      Serial.println(stableValue);

      // sendPost(stableValue);
      postSent = true;
    }
  }

  delay(50);
}

