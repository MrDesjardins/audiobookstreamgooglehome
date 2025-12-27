unsigned long lastUpdate = 0;
const unsigned long updateIntervalMs = 2000;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
}

void loop() {
  unsigned long now = millis();
  if (now - lastUpdate > updateIntervalMs) {
    lastUpdate = now;
    Serial.println("Test");
  }
}