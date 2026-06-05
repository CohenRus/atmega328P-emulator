void setup() {
  Serial.begin(9600);
  Serial.println("ready — type something:");
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    Serial.println("you said: " + line);
  }
}
