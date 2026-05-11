// ============================================================
// SIREN — IMU Data Logger with SD card
// ATP-2, ATP-3, ATP-5 data collection
// ============================================================
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_Sensor.h>

Adafruit_LSM9DS1 lsm = Adafruit_LSM9DS1();

#define SD_CS_PIN           PB12
#define SAMPLE_INTERVAL_MS  10
#define BUFFER_SIZE         100     // flush every 1 second
#define TEST_DURATION_MS    60000   // 60 seconds

// Buffer
struct Sample {
  uint32_t timestamp;
  float ax, ay, az_raw, az_filt;
  float gx, gy, gz;
};
Sample buffer[BUFFER_SIZE];
uint16_t bufferIndex = 0;

// Filter
float az_prev     = 0.0;
float az_filtered = 0.0;
const float alpha = 0.995;

// Timing
unsigned long lastSample = 0;
unsigned long testStart  = 0;
bool testDone = false;

File dataFile;

// ── Flush buffer to SD ───────────────────────────────────────
void flushBuffer() {
  dataFile = SD.open("siren.csv", FILE_WRITE);
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

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(3000);

  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();

  if (!lsm.begin()) {
    Serial.println("FATAL: IMU not found");
    while (1) {}
  }
  lsm.setupAccel(lsm.LSM9DS1_ACCELRANGE_8G,
                 lsm.LSM9DS1_ACCELDATARATE_952HZ);
  lsm.setupGyro(lsm.LSM9DS1_GYROSCALE_245DPS);
  lsm.setupMag(lsm.LSM9DS1_MAGGAIN_4GAUSS);
  Serial.println("IMU OK");

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("FATAL: SD not found");
    while (1) {}
  }

  // Fresh file every run
  if (SD.exists("siren.csv")) SD.remove("siren.csv");
  dataFile = SD.open("siren.csv", FILE_WRITE);
  if (dataFile) {
    dataFile.println("timestamp_ms,ax,ay,az_raw,az_filt,"
                     "gx,gy,gz");
    dataFile.close();
  }
  Serial.println("SD OK — recording to siren.csv");
  Serial.println("60 seconds — do taps and shakes freely");

  testStart  = millis();
  lastSample = millis();
}

// ─────────────────────────────────────────────────────────────
void loop() {
  if (testDone) return;

  unsigned long now     = millis();
  uint32_t      elapsed = now - testStart;

  if (elapsed >= TEST_DURATION_MS) {
    flushBuffer();
    Serial.println("DONE — pull SD card");
    testDone = true;
    return;
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
      Serial.print("t=");
      Serial.print(elapsed / 1000);
      Serial.println("s — flushing buffer");
      flushBuffer();
    }
  }
}