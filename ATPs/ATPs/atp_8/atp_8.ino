// ============================================================
// SIREN — ATP-8: 30-Minute Endurance Test
// IMU LSM9DS1  I2C:  SDA=PB7, SCL=PB6, 14ms intervals
// GPS NEO-6M   UART: RX=PA3, TX=PA2, 9600 baud
// SD card      SPI:  CS=PB12, SCK=PA5, MISO=PA6, MOSI=PA7
// ============================================================
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_Sensor.h>
#include <TinyGPSPlus.h>

// ── Pins ─────────────────────────────────────────────────────
#define SD_CS_PIN    PA4  // PA4

// ── Timing ───────────────────────────────────────────────────
#define SAMPLE_INTERVAL_MS  14UL
#define BUFFER_SIZE         500
#define TEST_DURATION_MS    900000UL
#define PROGRESS_INTERVAL   60000UL

// ── Objects ──────────────────────────────────────────────────
Adafruit_LSM9DS1 lsm = Adafruit_LSM9DS1();
TinyGPSPlus      gps;

// ── Filter ───────────────────────────────────────────────────
float       az_prev     = 0.0f;
float       az_filtered = 0.0f;
const float alpha       = 0.995f;

// ── GPS state ────────────────────────────────────────────────
float lastLat   = 0.0f;
float lastLon   = 0.0f;
bool  hasFix    = false;

// ── Buffer ───────────────────────────────────────────────────
struct Sample {
  uint32_t timestamp;
  float ax, ay, az_raw, az_filt;
  float gx, gy, gz;
  float lat, lon;
};
Sample   buffer[BUFFER_SIZE];
uint16_t bufferIndex = 0;

// ── Expected rows ────────────────────────────────────────────
const uint32_t EXPECTED_ROWS = TEST_DURATION_MS / SAMPLE_INTERVAL_MS;
const uint32_t TOLERANCE     = EXPECTED_ROWS / 200;  // 0.5%

// ── State ────────────────────────────────────────────────────
unsigned long lastSample   = 0;
unsigned long lastProgress = 0;
unsigned long testStart    = 0;
uint32_t      totalRows    = 0;
uint32_t      errorCount   = 0;
bool          testDone     = false;

File dataFile;

// ── Flush buffer to SD ───────────────────────────────────────
void flushBuffer() {
  for (uint16_t i = 0; i < bufferIndex; i++) {
    if (dataFile.print(buffer[i].timestamp) == 0) {
      errorCount++;
      break;
    }
    dataFile.print(',');
    dataFile.print(buffer[i].ax, 4);       dataFile.print(',');
    dataFile.print(buffer[i].ay, 4);       dataFile.print(',');
    dataFile.print(buffer[i].az_raw, 4);   dataFile.print(',');
    dataFile.print(buffer[i].az_filt, 4);  dataFile.print(',');
    dataFile.print(buffer[i].gx, 4);       dataFile.print(',');
    dataFile.print(buffer[i].gy, 4);       dataFile.print(',');
    dataFile.print(buffer[i].gz, 4);       dataFile.print(',');
    dataFile.print(buffer[i].lat, 6);      dataFile.print(',');
    dataFile.println(buffer[i].lon, 6);
  }
  dataFile.flush();
  bufferIndex = 0;
}

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("==============================");
  Serial.println("SIREN ATP-8: 30-Min Endurance");
  Serial.println("==============================");

  // ── I2C for IMU ──────────────────────────────────────────
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

  // ── UART for GPS ─────────────────────────────────────────
  Serial1.setRx(PA3);
  Serial1.setTx(PA2);
  Serial1.begin(9600);
  Serial.println("GPS OK");

  // ── SPI for SD ───────────────────────────────────────────
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  delay(200);

  SPI.setMOSI(PA7);
  SPI.setMISO(PA6);
  SPI.setSCLK(PA5);
  SPI.begin();

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("FATAL: SD not found");
    Serial.println("  CS=PB12, MOSI=PA7, MISO=PA6, SCK=PA5");
    while (1) {}
  }

  if (SD.exists("atp8.csv")) SD.remove("atp8.csv");
  dataFile = SD.open("atp8.csv", FILE_WRITE);
  if (!dataFile) {
    Serial.println("FATAL: cannot create atp8.csv");
    while (1) {}
  }
  dataFile.println("timestamp_ms,ax,ay,az_raw,az_filt,"
                   "gx,gy,gz,lat,lon");
  dataFile.flush();
  Serial.println("SD OK");

  Serial.println();
  Serial.print("Expected rows: "); Serial.println(EXPECTED_ROWS);
  Serial.print("Tolerance:     +/- "); Serial.println(TOLERANCE);
  Serial.println("Running 30 minutes — do not touch board");
  Serial.println("Take board outside or near window for GPS");
  Serial.println();

  testStart    = millis();
  lastSample   = millis();
  lastProgress = millis();
}

// ─────────────────────────────────────────────────────────────
void loop() {

  // ── GPS — ONE byte per loop, never blocks ─────────────────
  if (Serial1.available()) {
    if (gps.encode(Serial1.read())) {
      if (gps.location.isValid()) {
        lastLat = (float)gps.location.lat();
        lastLon = (float)gps.location.lng();
        hasFix  = true;
      }
    }
  }

  if (testDone) return;

  unsigned long now     = millis();
  uint32_t      elapsed = (uint32_t)(now - testStart);

  // ── End of test ──────────────────────────────────────────
  if (elapsed >= TEST_DURATION_MS) {
    if (bufferIndex > 0) flushBuffer();
    dataFile.close();

    float    hz   = (float)totalRows / (elapsed / 1000.0f);
    uint32_t diff = (totalRows > EXPECTED_ROWS)
                    ? (totalRows - EXPECTED_ROWS)
                    : (EXPECTED_ROWS - totalRows);
    bool rowPass = diff <= TOLERANCE;
    bool passed  = (errorCount == 0) && rowPass;

    Serial.println();
    Serial.println("==============================");
    Serial.println(passed ? "ATP-8  PASS" : "ATP-8  FAIL");
    Serial.println("------------------------------");
    Serial.print("Total rows:   "); Serial.println(totalRows);
    Serial.print("Expected:     "); Serial.print(EXPECTED_ROWS);
    Serial.print(" +/- ");          Serial.println(TOLERANCE);
    Serial.print("Sample rate:  "); Serial.print(hz, 1);
    Serial.println(" Hz");
    Serial.print("SD errors:    "); Serial.println(errorCount);
    Serial.print("GPS fix:      ");
    Serial.println(hasFix ? "acquired" : "never acquired");
    Serial.println("==============================");
    Serial.println("Pull SD card and run validate.py");

    testDone = true;
    return;
  }

  // ── Progress every 60s ───────────────────────────────────
  if (now - lastProgress >= PROGRESS_INTERVAL) {
    lastProgress += PROGRESS_INTERVAL;
    float hz = (elapsed > 0)
               ? (float)totalRows / (elapsed / 1000.0f)
               : 0.0f;
    Serial.print(elapsed / 60000);
    Serial.print(" min — ");
    Serial.print(totalRows);
    Serial.print(" rows — ");
    Serial.print(hz, 1);
    Serial.print(" Hz — GPS: ");
    Serial.print(hasFix ? "FIX" : "searching");
    Serial.print(" — errors: ");
    Serial.println(errorCount);
  }

  // ── Sample at 71Hz — drift-free timing ───────────────────
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

    buffer[bufferIndex++] = {
      elapsed,
      ax, ay, az_raw, az_filtered,
      g.gyro.x, g.gyro.y, g.gyro.z,
      lastLat, lastLon
    };
    totalRows++;

    if (bufferIndex >= BUFFER_SIZE) flushBuffer();
  }
}