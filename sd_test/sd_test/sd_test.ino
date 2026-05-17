#include <SPI.h>
#include <SD.h>

#define SD_CS_PIN PB12

void setup() {
  Serial.begin(115200);
  delay(3000);

  // Deselect BOTH devices before touching SPI
  pinMode(PA4,  OUTPUT); digitalWrite(PA4,  HIGH); // IMU CS
  pinMode(PB12, OUTPUT); digitalWrite(PB12, HIGH); // SD CS
  delay(200);

  SPI.setMOSI(PA7);
  SPI.setMISO(PA6);
  SPI.setSCLK(PA5);
  SPI.begin();
  delay(200);

  // IMU FIRST
  Adafruit_LSM9DS1 lsm(PA4, -1);
  if (!lsm.begin()) {
    Serial.println("FATAL: IMU not found");
    while (1) {}
  }
  Serial.println("IMU OK");

  // SD SECOND
  if (!SD.begin(PB12)) {
    Serial.println("FATAL: SD not found");
    while (1) {}
  }
  Serial.println("SD OK");
}
void loop() {}