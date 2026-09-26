// ============================================================
// SIREN — ATP-3 TAP TEST
// Logs 45s of IMU data to tap_dat.csv (TAP_DAT.CSV on SD card)
// Let filter settle for 10s, then perform taps
// Pass: spike >2×RMS, ≥2 consecutive samples above threshold
// ============================================================
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_Sensor.h>

Adafruit_LSM9DS1 lsm = Adafruit_LSM9DS1();

#define SD_CS_PIN           PB12
#define SAMPLE_INTERVAL_MS  10
#define BUFFER_SIZE         100
#define TEST_DURATION_MS    45000   // 45s: 10s settle + 35s tapping
#define SETTLE_TIME_MS      10000

struct Sample {
  uint32_t timestamp;
  float ax, ay, az_raw, az_filt;
  float gx, gy, gz;
};
Sample buffer[BUFFER_SIZE];
uint16_t bufferIndex = 0;

float az_prev     = 0.0;
float az_filtered = 0.0;
const float alpha = 0.995;

unsigned long lastSample = 0;
unsigned long testStart  = 0;
bool testDone   = false;
bool settling   = true;

File dataFile;

void flushBuffer() {
  dataFile = SD.open("tap_dat.csv", FILE_WRITE);
  if (!dataFile) {
    Serial.println("ERR: SD open failed");
    return;
  }
  for (uint16_t i = 0; i < bufferIndex; i++) {
    dataFile.print(buffer[i].timestamp);   dataFile.print(",");
    dataFile.print(buffer[i].ax, 4);       dataFile.print(",");
    dataFile.print(buffer[i].ay, 4);       dataFile.print(",");
    dataFile.print(buffer[i].az_raw, 4);   dataFile.print(",");
    dataFile.print(buffer[i].az_filt, 4);  dataFile.print(",");
    dataFile.print(buffer[i].gx, 4);       dataFile.print(",");
    dataFile.print(buffer[i].gy, 4);       dataFile.print(",");
    dataFile.println(buffer[i].gz, 4);
  }
  dataFile.close();
  bufferIndex = 0;
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();

  if (!lsm.begin()) {
    Serial.println("FATAL: IMU not found — check I2C address (0x6A/0x6B)");
    while (1) {}
  }
  lsm.setupAccel(lsm.LSM9DS1_ACCELRANGE_8G,
                 lsm.LSM9DS1_ACCELDATARATE_952HZ);
  lsm.setupGyro(lsm.LSM9DS1_GYROSCALE_245DPS);
  Serial.println("IMU OK");

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("FATAL: SD not found — check wiring and FAT32 format");
    while (1) {}
  }
  if (SD.exists("tap_dat.csv")) SD.remove("tap_dat.csv");
  dataFile = SD.open("tap_dat.csv", FILE_WRITE);
  if (dataFile) {
    dataFile.println("# ATP-3 TAP TEST");
    dataFile.println("timestamp_ms,ax,ay,az_raw,az_filt,gx,gy,gz");
    dataFile.close();
  }
  Serial.println("SD OK — recording to tap_data.csv");
  Serial.println("─────────────────────────────────────");
  Serial.println("Hold device STILL for 10s (filter settling)");
  Serial.println("Then perform clear single taps on the device");
  Serial.println("─────────────────────────────────────");

  testStart  = millis();
  lastSample = millis();
}

void loop() {
  if (testDone) return;

  unsigned long now     = millis();
  uint32_t      elapsed = now - testStart;

  if (elapsed >= TEST_DURATION_MS) {
    flushBuffer();
    Serial.println("DONE — remove SD card and run tap_analysis.py");
    testDone = true;
    return;
  }

  // Print countdown prompts
  if (settling && elapsed >= SETTLE_TIME_MS) {
    settling = false;
    Serial.println("Filter settled — START TAPPING NOW");
  }

  if (now - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample = now;

    lsm.read();
    sensors_event_t a, m, g, temp;
    lsm.getEvent(&a, &m, &g, &temp);

    float ax     = a.acceleration.x;
    float ay     = a.acceleration.y;
    float az_raw = a.acceleration.z;

    az_filtered = alpha * (az_filtered + az_raw - az_prev);
    az_prev     = az_raw;

    buffer[bufferIndex++] = {
      elapsed, ax, ay, az_raw, az_filtered,
      g.gyro.x, g.gyro.y, g.gyro.z
    };

    if (bufferIndex >= BUFFER_SIZE) {
      uint8_t sec = elapsed / 1000;
      Serial.print("t="); Serial.print(sec); Serial.println("s — flushing");
      flushBuffer();
    }
  }
}
