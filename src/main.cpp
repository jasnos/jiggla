void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-S2 Recovery Mode");
  
  // Initialize USB so you can see it on your computer
  USB.begin();
}

void loop() {
  Serial.println("Device is in recovery mode");
  delay(5000);
}