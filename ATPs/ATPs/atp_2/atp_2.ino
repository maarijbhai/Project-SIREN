// ============================================================
// SIREN — ATP-2: IMU Static Accuracy
// Phase 1: flat  → mean az_raw = 9.81 +/- 0.5 m/s²
// Phase 2: 45°   → mean az_raw = 6.94 +/- 0.5 m/s²
// Phase 3: still → max az_filt < 0.1 m/s²
// Logs all samples + summary to atp2_dat.csv on SD card
// ============================================================
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_Sensor.h>

Adafruit_LSM9DS1 lsm = Adafruit_LSM9DS1();

#define SD_CS_PIN        PB12
#define NUM_SAMPLES      100
#define SAMPLE_DELAY_MS  10     // 100 Hz

// Filter state
float az_prev     = 0.0;
float az_filtered = 0.0;
const float alpha = 0.995;

// Phase control
int   phase       = 0;
int   sampleCount = 0;
float sumRaw      = 0.0;
float maxFilt     = 0.0;

unsigned long lastSample = 0;
unsigned long phaseStart = 0;

// Results (stored for SD summary at end)
float meanFlat = 0.0;
float mean45   = 0.0;
bool  flatPass = false;
bool  tiltPass = false;
bool  filtPass = false;

File dataFile;

void logLine(String line) {
  dataFile = SD.open("atp2_dat.csv", FILE_WRITE);
  if (dataFile) {
    dataFile.println(line);
    dataFile.close();
  }
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
  if (SD.exists("atp2_dat.csv")) SD.remove("atp2_dat.csv");
  logLine("# ATP-2 IMU Static Accuracy");
  logLine("phase,sample,az_raw,az_filt,timestamp_ms");
  Serial.println("SD OK -- logging to atp2_dat.csv");

  Serial.println();
  Serial.println("==============================");
  Serial.println("ATP-2: IMU Static Accuracy");
  Serial.println("==============================");
  Serial.println();
  Serial.println("PHASE 1: Place board FLAT on desk");
  Serial.println("Waiting 5 seconds to settle...");

  phaseStart = millis();
  lastSample = millis();
}

void loop() {
  unsigned long now = millis();

  // Wait 5s before phase 1
  if (phase == 0 && now - phaseStart >= 5000) {
    Serial.println("Recording 10 flat samples...");
    phase = 1;
    sampleCount = 0;
    sumRaw = 0;
  }

  if (now - lastSample < SAMPLE_DELAY_MS) return;
  lastSample = now;

  lsm.read();
  sensors_event_t a, m, g, temp;
  lsm.getEvent(&a, &m, &g, &temp);

  float az_raw = a.acceleration.z;
  az_filtered  = alpha * (az_filtered + az_raw - az_prev);
  az_prev      = az_raw;

  // ── Phase 1 — flat ───────────────────────────────────────
  if (phase == 1) {
    sumRaw += az_raw;
    sampleCount++;

    String row = "FLAT," + String(sampleCount) + "," +
                 String(az_raw, 4) + "," +
                 String(az_filtered, 4) + "," +
                 String(now);
    logLine(row);

    if (sampleCount % 10 == 0) {
      Serial.print("  Sample "); Serial.print(sampleCount);
      Serial.print(": az_raw = "); Serial.print(az_raw, 3);
      Serial.println(" m/s^2");
    }

    if (sampleCount >= NUM_SAMPLES) {
      meanFlat = sumRaw / NUM_SAMPLES;
      flatPass = abs(meanFlat - 9.81) <= 0.5;

      Serial.println();
      Serial.print("FLAT MEAN az_raw = "); Serial.print(meanFlat, 3);
      Serial.println(" m/s^2");
      Serial.print("Expected:          9.810 +/- 0.500 m/s^2  ");
      Serial.println(flatPass ? "[PASS]" : "[FAIL]");

      logLine("# FLAT_MEAN," + String(meanFlat, 4) +
              "," + (flatPass ? "PASS" : "FAIL"));

      Serial.println();
      Serial.println("PHASE 2: Tilt board to 45 degrees");
      Serial.println("Use phone inclinometer app");
      Serial.println("Hold steady then send any character in Serial Monitor");
      phase = 2;
      sampleCount = 0;
      sumRaw = 0;
    }
  }

  // ── Phase 2 — wait for tilt confirmation ─────────────────
  if (phase == 2) {
    if (Serial.available()) {
      Serial.read();
      Serial.println("Recording 10 tilted samples...");
      phase = 3;
      sampleCount = 0;
      sumRaw = 0;
    }
  }

  // ── Phase 3 — 45 degree ──────────────────────────────────
  if (phase == 3) {
    sumRaw += az_raw;
    sampleCount++;

    String row = "45DEG," + String(sampleCount) + "," +
                 String(az_raw, 4) + "," +
                 String(az_filtered, 4) + "," +
                 String(now);
    logLine(row);

    if (sampleCount % 10 == 0) {
      Serial.print("  Sample "); Serial.print(sampleCount);
      Serial.print(": az_raw = "); Serial.print(az_raw, 3);
      Serial.println(" m/s^2");
    }

    if (sampleCount >= NUM_SAMPLES) {
      mean45   = sumRaw / NUM_SAMPLES;
      tiltPass = abs(mean45 - 6.94) <= 0.5;

      Serial.println();
      Serial.print("45 DEG MEAN az_raw = "); Serial.print(mean45, 3);
      Serial.println(" m/s^2");
      Serial.print("Expected:           6.940 +/- 0.500 m/s^2  ");
      Serial.println(tiltPass ? "[PASS]" : "[FAIL]");

      logLine("# 45DEG_MEAN," + String(mean45, 4) +
              "," + (tiltPass ? "PASS" : "FAIL"));

      Serial.println();
      Serial.println("PHASE 4: Place board FLAT again and hold still");
      Serial.println("Re-settling filter for 40 seconds...");
      phase = 4;
      sampleCount = 0;
      phaseStart = millis();
    }
  }

  // ── Phase 4 — re-settle (40s) ────────────────────────────
  if (phase == 4) {
    if (millis() - phaseStart >= 40000) {
      Serial.println("Filter re-settled -- starting stationary check");
      Serial.println("PHASE 5: Hold board completely still for 30 seconds...");
      phase = 5;
      sampleCount = 0;
      maxFilt = 0;
      phaseStart = millis();
    }
  }

  // ── Phase 5 — stationary filter check (30s) ──────────────
  if (phase == 5) {
    float absFilt = abs(az_filtered);
    if (absFilt > maxFilt) maxFilt = absFilt;
    sampleCount++;

    String row = "STATIC," + String(sampleCount) + "," +
                 String(az_raw, 4) + "," +
                 String(az_filtered, 4) + "," +
                 String(now);
    logLine(row);

    if (millis() - phaseStart >= 30000) {
      filtPass = maxFilt < 0.1;

      Serial.println();
      Serial.print("Max az_filt (stationary) = ");
      Serial.print(maxFilt, 4); Serial.println(" m/s^2");
      Serial.print("Required:                  < 0.100 m/s^2  ");
      Serial.println(filtPass ? "[PASS]" : "[FAIL]");

      logLine("# STATIC_MAX," + String(maxFilt, 4) +
              "," + (filtPass ? "PASS" : "FAIL"));

      // Final summary to SD
      bool overall = flatPass && tiltPass && filtPass;
      logLine("# SUMMARY");
      logLine("# FLAT," + String(meanFlat, 4) + "," + (flatPass ? "PASS" : "FAIL"));
      logLine("# 45DEG," + String(mean45, 4)  + "," + (tiltPass ? "PASS" : "FAIL"));
      logLine("# FILTER," + String(maxFilt, 4) + "," + (filtPass ? "PASS" : "FAIL"));
      logLine("# OVERALL," + String(overall ? "PASS" : "FAIL"));

      Serial.println();
      Serial.println("==============================");
      Serial.println("ATP-2 COMPLETE");
      Serial.println("------------------------------");
      Serial.print("FLAT    "); Serial.println(flatPass ? "[PASS]" : "[FAIL]");
      Serial.print("45 DEG  "); Serial.println(tiltPass ? "[PASS]" : "[FAIL]");
      Serial.print("FILTER  "); Serial.println(filtPass ? "[PASS]" : "[FAIL]");
      Serial.println("------------------------------");
      Serial.print("OVERALL ");
      Serial.println((flatPass && tiltPass && filtPass) ? "[PASS]" : "[FAIL]");
      Serial.println("==============================");
      Serial.println("Pull SD card and check atp2_dat.csv");

      phase = 99;
    }
  }
}

