#include <SPI.h>
#include <SD.h>

#define SD_CS_PIN PB12

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("SD minimal test");

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  delay(500);

  SPI.setMOSI(PA7);
  SPI.setMISO(PA6);
  SPI.setSCLK(PA5);
  SPI.begin();
  delay(500);

  Serial.print("SD.begin... ");
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("FAILED");
  } else {
    Serial.println("OK");
    File f = SD.open("test.txt", FILE_WRITE);
    if (f) {
      f.println("hello");
      f.close();
      Serial.println("File written OK");
    } else {
      Serial.println("File open FAILED");
    }
  }
}

void loop() {}