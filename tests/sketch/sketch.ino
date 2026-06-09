// Serial timing fixture used to verify delay() and Timer0 overflow handling.
int x;

void setup() {
  Serial.begin(9600);
}

void loop() {
  Serial.print(x);
  Serial.print(" second");
  Serial.println();
  x++;
  delay(1000);
}
