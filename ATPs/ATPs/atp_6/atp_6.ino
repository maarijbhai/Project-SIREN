// ============================================================
// SIREN — ATP-6: Current Draw Characterisation
// MCU: STM32F401CE Black Pill
//
// Procedure:
//   1. Place ammeter in series with Vcc (USB 5V rail)
//   2. Open Serial Monitor at 115200
//   3. Read ammeter at each MEASURE NOW prompt
//   4. Record the 4 readings in atp6_measurements.csv
//   5. Run validate.py for pass/fail verdict
//
// States (10 s each):
//   0  MCU only          — baseline, no peripherals
//   1  MCU + IMU         — IMU sampling at ~71 Hz
//   2  MCU + IMU + GPS   — GPS UART active
//   3  Full system       — IMU + GPS + SD writing
//
// Pass: IMU delta 4–6 mA, GPS delta 35–50 mA
// ============================================================

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_Sensor.h>

HardwareSerial Serial2(PA3, PA2);      // GPS: RX=PA3, TX=PA2

#define SD_CS_PIN    PA4
#define STATE_DUR_MS 10000UL   // 10 s measurement window per state
#define SAMPLE_MS    14UL      // ~71 Hz when IMU is active

Adafruit_LSM9DS1 lsm = Adafruit_LSM9DS1();

uint8_t       state      = 0;
unsigned long stateStart = 0;
unsigned long lastSample = 0;
bool          imuReady   = false;
bool          sdReady    = false;
File          dataFile;

float       az_prev     = 0.0f;
float       az_filtered = 0.0f;
const float alpha       = 0.995f;


// ── Helpers ───────────────────────────────────────────────

void printBanner(uint8_t n, const char* desc) {
  Serial.println();
  Serial.print("── State ");
  Serial.print(n);
  Serial.print(": ");
  Serial.println(desc);
  Serial.println("   >>> MEASURE NOW — read ammeter and record value <<<");
  Serial.println("   (10 s window)");
}

bool initIMU() {
  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();
  if (!lsm.begin()) return false;
  lsm.setupAccel(lsm.LSM9DS1_ACCELRANGE_8G,
                 lsm.LSM9DS1_ACCELDATARATE_952HZ);
  lsm.setupGyro(lsm.LSM9DS1_GYROSCALE_245DPS);
  return true;
}

bool initSD() {
  SPI.setMOSI(PA7);
  SPI.setMISO(PA6);
  SPI.setSCLK(PA5);
  if (!SD.begin(SD_CS_PIN)) return false;
  if (SD.exists("atp6_sd.csv")) SD.remove("atp6_sd.csv");
  dataFile = SD.open("atp6_sd.csv", FILE_WRITE);
  return (bool)dataFile;
}

void sampleIMU(unsigned long now) {
  if (!imuReady) return;
  if (now - lastSample < SAMPLE_MS) return;
  lastSample += SAMPLE_MS;

  lsm.read();
  sensors_event_t a, m, g, temp;
  lsm.getEvent(&a, &m, &g, &temp);

  float az_raw = a.acceleration.z;
  az_filtered  = alpha * (az_filtered + az_raw - az_prev);
  az_prev      = az_raw;

  if (sdReady) {
    uint32_t elapsed = (uint32_t)(now - stateStart);
    dataFile.print(elapsed);              dataFile.print(',');
    dataFile.print(a.acceleration.x, 4); dataFile.print(',');
    dataFile.print(a.acceleration.y, 4); dataFile.print(',');
    dataFile.print(az_raw, 4);           dataFile.print(',');
    dataFile.print(az_filtered, 4);      dataFile.print(',');
    dataFile.print(g.gyro.x, 4);         dataFile.print(',');
    dataFile.print(g.gyro.y, 4);         dataFile.print(',');
    dataFile.println(g.gyro.z, 4);
  }
}


// ── Setup ─────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("========================================");
  Serial.println("  SIREN ATP-6  Current Draw Test");
  Serial.println("  Place ammeter in series with Vcc.");
  Serial.println("  4 states x 10 s each.");
  Serial.println("  Record reading at each MEASURE NOW.");
  Serial.println("========================================");

  printBanner(0, "MCU only  (no peripherals)");
  stateStart = millis();
}


// ── Loop ──────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  switch (state) {

    // ── State 0: MCU only ─────────────────────────────────
    case 0:
      if (now - stateStart >= STATE_DUR_MS) {
        imuReady = initIMU();
        Serial.println(imuReady ? "IMU OK" : "IMU FAIL");
        printBanner(1, "MCU + IMU");
        lastSample = now;
        state      = 1;
        stateStart = now;
      }
      break;

    // ── State 1: MCU + IMU ────────────────────────────────
    case 1:
      sampleIMU(now);
      if (now - stateStart >= STATE_DUR_MS) {
        Serial2.begin(9600);   // PA3=RX, PA2=TX — GPS powers on
        Serial.println("GPS OK");
        printBanner(2, "MCU + IMU + GPS");
        state      = 2;
        stateStart = now;
      }
      break;

    // ── State 2: MCU + IMU + GPS ──────────────────────────
    case 2:
      sampleIMU(now);
      if (now - stateStart >= STATE_DUR_MS) {
        sdReady = initSD();
        Serial.println(sdReady ? "SD OK" : "SD FAIL");
        printBanner(3, "Full system  (MCU + IMU + GPS + SD writing)");
        state      = 3;
        stateStart = now;
      }
      break;

    // ── State 3: Full system ──────────────────────────────
    case 3:
      sampleIMU(now);
      if (now - stateStart >= STATE_DUR_MS) {
        if (sdReady) dataFile.close();
        state = 4;
      }
      break;

    // ── Done ─────────────────────────────────────────────
    case 4:
      Serial.println();
      Serial.println("========================================");
      Serial.println("  Test complete.");
      Serial.println("  Fill atp6_measurements.csv:");
      Serial.println();
      Serial.println("  state,description,current_mA");
      Serial.println("  0,MCU only,<reading>");
      Serial.println("  1,MCU + IMU,<reading>");
      Serial.println("  2,MCU + IMU + GPS,<reading>");
      Serial.println("  3,Full system,<reading>");
      Serial.println();
      Serial.println("  Then run: python validate.py");
      Serial.println("========================================");
      while (1) {}
  }
}
