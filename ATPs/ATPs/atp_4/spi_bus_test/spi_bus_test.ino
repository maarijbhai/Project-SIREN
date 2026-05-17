#include <SPI.h>
#include <Wire.h>
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_Sensor.h>

Adafruit_LSM9DS1 lsm(PA4, -1);

void setup() {
  Serial.begin(115200);
  delay(3000);

  pinMode(PA4, OUTPUT);
  digitalWrite(PA4, HIGH);
  delay(200);

  SPI.setMOSI(PA7);
  SPI.setMISO(PA6);
  SPI.setSCLK(PA5);
  SPI.begin();
  delay(200);

  if (!lsm.begin()) {
    Serial.println("FAIL: IMU not found");
  } else {
    Serial.println("PASS: IMU found");
  }
}

void loop() {}