// ============================================================
// SIREN — ATP-5: SD Card Data Integrity
// IMU LSM9DS1  I2C:  SDA=PB7, SCL=PB6
// SD card      SPI1: CS=PA4, SCK=PA5, MISO=PA6, MOSI=PA7
// Target: ~71 Hz for 10 min = ~42600 rows
// Pass:   fs*600 +/- 0.5%, zero nulls, no gaps > 15ms
// Validate: copy atp5_dat.csv to atp_5/ then run validate.py
// ============================================================

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_Sensor.h>

#define SD_CS_PIN           PA4
#define SAMPLE_INTERVAL_MS  14UL
#define TEST_DURATION_MS    600000UL
#define PROGRESS_INTERVAL   30000UL

Adafruit_LSM9DS1 lsm = Adafruit_LSM9DS1();

float       az_prev     = 0.0f;
float       az_filtered = 0.0f;
const float alpha       = 0.995f;

unsigned long lastSample   = 0;
unsigned long lastProgress = 0;
unsigned long testStart    = 0;
uint32_t      totalRows    = 0;
bool          testDone     = false;

File dataFile;

void setup() {
  Serial.begin(115200);
  delay(2000);

  // ── IMU — I2C ─────────────────────────────────────────────
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

  // ── SD — SPI1: CS=PA4, SCK=PA5, MISO=PA6, MOSI=PA7 ───────
  SPI.setMOSI(PA7);
  SPI.setMISO(PA6);
  SPI.setSCLK(PA5);

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
  // No flush here — let the SD library write sectors on its own schedule
  Serial.println("SD OK -- recording to atp5_dat.csv");
  Serial.println("Recording 10 min -- do not remove SD card");

  testStart    = millis();
  lastSample   = millis();
  lastProgress = millis();
}

void loop() {
  if (testDone) return;

  unsigned long now     = millis();
  uint32_t      elapsed = (uint32_t)(now - testStart);

  // ── End of test ───────────────────────────────────────────
  if (elapsed >= TEST_DURATION_MS) {
    dataFile.close();   // single flush at the very end
    float finalHz = (float)totalRows / (elapsed / 1000.0f);
    Serial.print("DONE -- ");
    Serial.print(totalRows);
    Serial.print(" rows, ");
    Serial.print(elapsed / 1000);
    Serial.print(" s, ");
    Serial.print(finalHz, 1);
    Serial.println(" Hz -- pull SD card and run validate.py");
    testDone = true;
    return;
  }

  // ── Progress every 30 s ───────────────────────────────────
  if (now - lastProgress >= PROGRESS_INTERVAL) {
    lastProgress += PROGRESS_INTERVAL;
    float hz = (elapsed > 0) ? ((float)totalRows / (elapsed / 1000.0f)) : 0.0f;
    Serial.print(elapsed / 1000);
    Serial.print(" s -- ");
    Serial.print(totalRows);
    Serial.print(" samples -- ");
    Serial.print(hz, 1);
    Serial.println(" Hz");
  }

  // ── Sample at ~71 Hz (drift-free timing) ──────────────────
  if (now - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample += SAMPLE_INTERVAL_MS;

    lsm.read();
    sensors_event_t a, m, g, temp;
    lsm.getEvent(&a, &m, &g, &temp);

    float ax     = a.acceleration.x;
    float ay     = a.acceleration.y;
    float az_raw = a.acceleration.z;

    az_filtered = alpha * (az_filtered + az_raw - az_prev);
    az_prev     = az_raw;

    // Write one row directly — no RAM buffer, no explicit flush.
    // The SD library sector-buffers internally (~10 rows per sector).
    // Each sector write (~1–5 ms) is absorbed within the 14 ms interval.
    dataFile.print(elapsed);        dataFile.print(',');
    dataFile.print(ax, 4);          dataFile.print(',');
    dataFile.print(ay, 4);          dataFile.print(',');
    dataFile.print(az_raw, 4);      dataFile.print(',');
    dataFile.print(az_filtered, 4); dataFile.print(',');
    dataFile.print(g.gyro.x, 4);    dataFile.print(',');
    dataFile.print(g.gyro.y, 4);    dataFile.print(',');
    dataFile.println(g.gyro.z, 4);

    totalRows++;
  }
}
