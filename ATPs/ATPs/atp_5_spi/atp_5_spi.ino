// ============================================================
// SIREN — ATP-5: SD Card Data Integrity
// IMU LSM9DS1  I2C:  SDA=PB7, SCL=PB6
// SD card      SPI1: CS=PB12
// GPS NEO-6M   UART2: RX=PA3, TX=PA2
//
// Pass criteria (inferred from data):
//   Row count:  fs*600 +/- 0.5%
//   Nulls:      0
//   Gaps >15ms: 0
// ============================================================

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_Sensor.h>

// ── Pins ─────────────────────────────────────────────────────
#define SD_CS    PB12

// ── IMU — I2C ────────────────────────────────────────────────
Adafruit_LSM9DS1 lsm = Adafruit_LSM9DS1();

// ── GPS ──────────────────────────────────────────────────────
HardwareSerial GPS_SERIAL(PA3, PA2);

// ── Timing ───────────────────────────────────────────────────
#define SAMPLE_INTERVAL_MS  14UL       // ~71Hz target
#define BUFFER_SIZE         500        // flush every ~5s
#define TEST_DURATION_MS    600000UL   // 10 minutes
#define PROGRESS_INTERVAL   30000UL    // heartbeat every 30s

// ── Sample buffer ────────────────────────────────────────────
struct Sample {
  uint32_t timestamp;
  float ax, ay, az_raw, az_filt;
  float gx, gy, gz;
};
Sample   buf[BUFFER_SIZE];
uint16_t bufIdx = 0;

// ── High-pass filter ─────────────────────────────────────────
float       az_prev = 0.0f;
float       az_filt = 0.0f;
const float alpha   = 0.995f;

// ── State ────────────────────────────────────────────────────
unsigned long lastSample   = 0;
unsigned long lastProgress = 0;
unsigned long testStart    = 0;
uint32_t      totalRows    = 0;
bool          testDone     = false;

File dataFile;

// ── Flush buffer → SD ────────────────────────────────────────
void flushBuffer() {
  for (uint16_t i = 0; i < bufIdx; i++) {
    dataFile.print(buf[i].timestamp);  dataFile.print(',');
    dataFile.print(buf[i].ax, 4);      dataFile.print(',');
    dataFile.print(buf[i].ay, 4);      dataFile.print(',');
    dataFile.print(buf[i].az_raw, 4);  dataFile.print(',');
    dataFile.print(buf[i].az_filt, 4); dataFile.print(',');
    dataFile.print(buf[i].gx, 4);      dataFile.print(',');
    dataFile.print(buf[i].gy, 4);      dataFile.print(',');
    dataFile.println(buf[i].gz, 4);
  }
  dataFile.flush();
  bufIdx = 0;
}

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("==============================");
  Serial.println("ATP-5: SD Card Data Integrity");
  Serial.println("==============================");

  // ── IMU — I2C ────────────────────────────────────────────
  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();

  if (!lsm.begin()) {
    Serial.println("FATAL: IMU not found");
    Serial.println("  Check: SDA→PB7, SCL→PB6");
    while (1) {}
  }
  lsm.setupAccel(lsm.LSM9DS1_ACCELRANGE_8G,
                 lsm.LSM9DS1_ACCELDATARATE_952HZ);
  lsm.setupGyro(lsm.LSM9DS1_GYROSCALE_245DPS);
  Serial.println("IMU OK");

  // ── GPS ──────────────────────────────────────────────────
  GPS_SERIAL.begin(9600);
  Serial.println("GPS OK");

  // ── SD card ──────────────────────────────────────────────
  if (!SD.begin(SD_CS)) {
    Serial.println("FATAL: SD not found");
    Serial.println("  Check: CS→PB12, shared SPI1 bus");
    while (1) {}
  }
  if (SD.exists("atp5.csv")) SD.remove("atp5.csv");
  dataFile = SD.open("atp5.csv", FILE_WRITE);
  if (!dataFile) {
    Serial.println("FATAL: cannot create atp5.csv");
    while (1) {}
  }
  dataFile.println("timestamp_ms,ax,ay,az_raw,az_filt,"
                   "gx,gy,gz");
  dataFile.flush();
  Serial.println("SD OK");
  Serial.println();
  Serial.println("Recording 10 minutes...");
  Serial.println("Do NOT touch the board.");

  testStart    = millis();
  lastSample   = millis();
  lastProgress = millis();
}

// ─────────────────────────────────────────────────────────────
void loop() {
  // Drain GPS buffer — keep UART from overflowing
  while (GPS_SERIAL.available()) GPS_SERIAL.read();

  if (testDone) return;

  unsigned long now     = millis();
  uint32_t      elapsed = (uint32_t)(now - testStart);

  // ── End of test ──────────────────────────────────────────
  if (elapsed >= TEST_DURATION_MS) {
    if (bufIdx > 0) flushBuffer();
    dataFile.close();

    float hz = (float)totalRows / (elapsed / 1000.0f);

    Serial.println();
    Serial.println("==============================");
    Serial.println("RECORDING COMPLETE");
    Serial.println("------------------------------");
    Serial.print("Total rows:    ");
    Serial.println(totalRows);
    Serial.print("Duration:      ");
    Serial.print(elapsed / 1000);
    Serial.println(" s");
    Serial.print("Sample rate:   ");
    Serial.print(hz, 1);
    Serial.println(" Hz");
    Serial.println("------------------------------");
    Serial.println("Pull SD card");
    Serial.println("Run: python validate.py");
    Serial.println("==============================");

    testDone = true;
    return;
  }

  // ── Progress heartbeat ───────────────────────────────────
  if (now - lastProgress >= PROGRESS_INTERVAL) {
    lastProgress += PROGRESS_INTERVAL;
    float hz = (elapsed > 0) ?
               (float)totalRows / (elapsed / 1000.0f) : 0;
    Serial.print("t=");    Serial.print(elapsed / 1000);
    Serial.print("s  rows="); Serial.print(totalRows);
    Serial.print("  rate="); Serial.print(hz, 1);
    Serial.println(" Hz");
  }

  // ── Sample at 100Hz ──────────────────────────────────────
  if (now - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample += SAMPLE_INTERVAL_MS;  // drift-free timing

    lsm.read();
    sensors_event_t a, m, g, temp;
    lsm.getEvent(&a, &m, &g, &temp);

    float ax     = a.acceleration.x;
    float ay     = a.acceleration.y;
    float az_raw = a.acceleration.z;

    az_filt = alpha * (az_filt + az_raw - az_prev);
    az_prev = az_raw;

    buf[bufIdx++] = {
      elapsed,
      ax, ay, az_raw, az_filt,
      g.gyro.x, g.gyro.y, g.gyro.z
    };
    totalRows++;

    if (bufIdx >= BUFFER_SIZE) flushBuffer();
  }
}


// Sponsored by GoonCode 2026. C