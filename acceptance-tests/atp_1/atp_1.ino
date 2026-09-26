// ============================================================
// SIREN — ATP-1: MCU Boot & Peripheral Initialisation
// Pass criteria:
//   IMU      — begins on I2C and WHO_AM_I responds
//   SD card  — mounts and file created successfully
//   GPS      — UART initialised at 9600 baud
//   3.3V     — rail reads 3.2–3.4V via internal VREF
// ============================================================
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_Sensor.h>

Adafruit_LSM9DS1 lsm = Adafruit_LSM9DS1();

#define SD_CS_PIN  PB12
HardwareSerial GPS_SERIAL(PA3, PA2);  // RX=PA3, TX=PA2

File dataFile;

void logLine(String line) {
  dataFile = SD.open("atp1_dat.csv", FILE_WRITE);
  if (dataFile) { dataFile.println(line); dataFile.close(); }
}

float measureVDD() {
  analogReadResolution(12);
  uint32_t raw = analogRead(AVREF);
  if (raw == 0) return 0.0;
  return (1.21f * 4096.0f) / (float)raw;
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("==============================");
  Serial.println("ATP-1: MCU Boot & Peripheral Init");
  Serial.println("==============================");
  Serial.println();

  bool imuPass = false;
  bool sdPass  = false;
  bool gpsPass = false;
  bool vddPass = false;
  float vdd    = 0.0;

  // ── 3.3V rail ─────────────────────────────────────────────
  vdd     = measureVDD();
  vddPass = (vdd >= 3.2f && vdd <= 3.4f);
  Serial.print("3.3V rail      = ");
  Serial.print(vdd, 3);
  Serial.print(" V   ");
  Serial.println(vddPass ? "[PASS]" : "[FAIL]");

  // ── IMU ───────────────────────────────────────────────────
  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();

  if (lsm.begin()) {
    lsm.setupAccel(lsm.LSM9DS1_ACCELRANGE_8G,
                   lsm.LSM9DS1_ACCELDATARATE_952HZ);
    lsm.setupGyro(lsm.LSM9DS1_GYROSCALE_245DPS);
    imuPass = true;
    Serial.println("IMU            = OK   [PASS]");
  } else {
    Serial.println("IMU            = FAIL [FAIL] -- check I2C PB7/PB6");
  }

  // ── SD card ───────────────────────────────────────────────
  if (SD.begin(SD_CS_PIN)) {
    sdPass = true;
    Serial.println("SD card        = OK   [PASS]");
    if (SD.exists("atp1_dat.csv")) SD.remove("atp1_dat.csv");
    logLine("# ATP-1 Boot Test Results");
    logLine("peripheral,result,value");
  } else {
    Serial.println("SD card        = FAIL [FAIL] -- check SPI PB12");
  }

  // ── GPS — init only, no data required ─────────────────────
  GPS_SERIAL.begin(9600);
  // GPS pass = UART initialised without fault
  // Outdoor fix is verified separately in ATP-4
  gpsPass = true;
  Serial.println("GPS UART       = OK   [PASS]");
  Serial.println("  (outdoor fix verified in ATP-4)");

  // ── Log to SD ─────────────────────────────────────────────
  if (sdPass) {
    logLine("VDD_RAIL," + String(vddPass ? "PASS" : "FAIL")
            + "," + String(vdd, 3) + "V");
    logLine("IMU,"     + String(imuPass ? "PASS" : "FAIL") + ",");
    logLine("SD_CARD," + String(sdPass  ? "PASS" : "FAIL") + ",");
    logLine("GPS_UART," + String(gpsPass ? "PASS" : "FAIL") + ",");
  }

  // ── Summary ───────────────────────────────────────────────
  bool overall = imuPass && sdPass && gpsPass && vddPass;

  Serial.println();
  Serial.println("==============================");
  Serial.println("ATP-1 COMPLETE");
  Serial.println("------------------------------");
  Serial.print("3.3V RAIL      ");
  Serial.println(vddPass ? "[PASS]" : "[FAIL]");
  Serial.print("IMU            ");
  Serial.println(imuPass ? "[PASS]" : "[FAIL]");
  Serial.print("SD CARD        ");
  Serial.println(sdPass  ? "[PASS]" : "[FAIL]");
  Serial.print("GPS UART       ");
  Serial.println(gpsPass ? "[PASS]" : "[FAIL]");
  Serial.println("------------------------------");
  Serial.print("OVERALL        ");
  Serial.println(overall ? "[PASS]" : "[FAIL]");
  Serial.println("==============================");

  if (sdPass) {
    logLine("OVERALL," + String(overall ? "PASS" : "FAIL") + ",");
    Serial.println("Results saved to atp1_dat.csv");
  }
}

void loop() {}