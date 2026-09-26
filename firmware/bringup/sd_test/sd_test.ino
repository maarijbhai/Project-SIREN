#include <SPI.h>
#include <SD.h>

#define SD_CS_PIN PB12

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("Trying SPI2: PB15/PB14/PB13");

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  delay(200);

  SPI.setMOSI(PB15);
  SPI.setMISO(PB14);
  SPI.setSCLK(PB13);
  SPI.begin();
  delay(200);

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("FAIL: SPI2 also failed");
    while (1) {}
  }
  Serial.println("PASS: SD found on SPI2 PB15/PB14/PB13");
}

void loop() {}