// ============================================================
// SIREN — ATP-5: SD Card Data Integrity
// Records 10 minutes at 10ms intervals = 60000 rows
// Writes to atp5_dat.csv — validate with validate.py
// Pass: 60000 +/- 300 rows, zero nulls, no gaps > 15ms
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
#define TEST_DURATION_MS    600000UL   // 10 minutes = 60000 rows

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
bool testDone = false;

File dataFile;

void flushBuffer() {
  for (uint16_t i = 0; i < bufferIndex; i++) {
    dataFile.print(buffer[i].timestamp);  dataFile.print(",");
    dataFile.print(buffer[i].ax, 4);      dataFile.print(",");
    dataFile.print(buffer[i].ay, 4);      dataFile.print(",");
    dataFile.print(buffer[i].az_raw, 4);  dataFile.print(",");
    dataFile.print(buffer[i].az_filt, 4); dataFile.print(",");
    dataFile.print(buffer[i].gx, 4);      dataFile.print(",");
    dataFile.print(buffer[i].gy, 4);      dataFile.print(",");
    dataFile.println(buffer[i].gz, 4);
  }
  dataFile.flush();
  bufferIndex = 0;
}

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
  Serial.println("IMU OK");

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("FATAL: SD not found");
    while (1) {}
  }
  if (SD.exists("atp5_dat.csv")) SD.remove("atp5_dat.csv");
  dataFile = SD.open("atp5_dat.csv", FILE_WRITE);
  if (!dataFile) {
    Serial.println("FATAL: could not create atp5_dat.csv");
    while (1) {}
  }
  dataFile.println("# ATP-5 SD Integrity Test");
  dataFile.println("timestamp_ms,ax,ay,az_raw,az_filt,gx,gy,gz");
  dataFile.flush();
  Serial.println("SD OK -- recording to atp5_dat.csv");
  Serial.println("Recording for 10 minutes -- do not touch");
  Serial.println("Progress printed every 60 seconds");

  testStart  = millis();
  lastSample = millis();
}

void loop() {
  if (testDone) return;

  unsigned long now     = millis();
  uint32_t      elapsed = now - testStart;

  if (elapsed >= TEST_DURATION_MS) {
    flushBuffer();
    dataFile.close();
    Serial.println("DONE -- pull SD card and run validate.py");
    testDone = true;
    return;
  }

  // Print progress every 60s
  static uint8_t lastMinute = 0;
  uint8_t minute = elapsed / 60000;
  if (minute != lastMinute) {
    lastMinute = minute;
    Serial.print(minute);
    Serial.print(" min elapsed -- ");
    Serial.print(elapsed / SAMPLE_INTERVAL_MS);
    Serial.println(" rows written");
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

    if (bufferIndex >= BUFFER_SIZE) flushBuffer();
  }
}
