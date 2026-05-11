#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <TinyGPS++.h>
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_Sensor.h>

// ── Pin Definitions ──────────────────────────────────────────
#define SD_CS_PIN    PB12   // Your chosen CS pin
#define SPI_SCK      PA5
#define SPI_MISO     PA6
#define SPI_MOSI     PA7

// ── Serial ports ─────────────────────────────────────────────
HardwareSerial GpsSerial(PA3, PA2);  // RX=PA3, TX=PA2 (UART2)
TinyGPSPlus    gps;

// ── IMU (Adafruit instead of SparkFun) ───────────────────────
Adafruit_LSM9DS1 imu = Adafruit_LSM9DS1();

// ── Timing ───────────────────────────────────────────────────
#define IMU_INTERVAL_MS   10      // 100Hz
#define BEACON_INTERVAL_MS 900000 // 15 minutes

unsigned long lastIMU    = 0;
unsigned long lastBeacon = 0;

// ── GPS state ────────────────────────────────────────────────
float currentLat = 0.0;
float currentLon = 0.0;
bool  gpsFix     = false;

// ── High-pass filter state ───────────────────────────────────
float az_prev     = 0.0;
float az_filtered = 0.0;
float gx_prev     = 0.0;
float gx_filtered = 0.0;
float gy_prev     = 0.0;
float gy_filtered = 0.0;
const float alpha = 0.995;  // ~0.08Hz cutoff at 100Hz

// ── SD card file ─────────────────────────────────────────────
File dataFile;
bool sdReady = false;
int sdFlushCounter = 0; // Triggers the SD save every 100 samples

// ─────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  
  // DFU SURVIVAL DELAY
  delay(3000);
  
  Serial.println("==============================");
  Serial.println("SIREN Sensing Subsystem v1.0");
  Serial.println("==============================");

  // Explicitly set I2C for IMU — PB7(SDA), PB6(SCL)
  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();

  // Explicitly set GPS UART2 — PA3(RX), PA2(TX)
  GpsSerial.begin(9600);
  Serial.println("GPS UART2 started on PA2/PA3");

  // Explicitly set SPI1 for the SD Card
  SPI.setSCLK(SPI_SCK);
  SPI.setMISO(SPI_MISO);
  SPI.setMOSI(SPI_MOSI);
  SPI.begin();

  // IMU init
  if (!imu.begin()) {
    Serial.println("FATAL: IMU init failed. Check PB6/PB7 wiring.");
    while (1) { delay(100); }
  }
  
  // Setup IMU Ranges
  imu.setupAccel(imu.LSM9DS1_ACCELRANGE_4G);
  imu.setupGyro(imu.LSM9DS1_GYROSCALE_245DPS);
  Serial.println("IMU init OK");

  // SD card init
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("FATAL: SD init failed. Check SPI wiring.");
    while (1) { delay(100); }
  }

  // Open the file globally
  dataFile = SD.open("siren.csv", FILE_WRITE);
  if (dataFile) {
    if (dataFile.size() == 0) {
      dataFile.println("timestamp_ms,ax,ay,az_raw,az_filt,gx_raw,gx_filt,gy_raw,gy_filt,gz,lat,lon");
      dataFile.flush();
    }
    sdReady = true;
    Serial.println("SD init OK — File open and ready.");
  } else {
    Serial.println("FATAL: Could not open siren.csv");
    while (1) { delay(100); }
  }

  Serial.println("All systems GO. Logging started.");
}

// ─────────────────────────────────────────────────────────────
// MAIN LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  // Always feed GPS parser
  while (GpsSerial.available()) {
    if (gps.encode(GpsSerial.read())) {
      if (gps.location.isValid()) {
        currentLat = gps.location.lat();
        currentLon = gps.location.lng();
        if (!gpsFix) {
          Serial.println("GPS FIX ACQUIRED");
          gpsFix = true;
        }
      }
    }
  }

  // IMU at 100Hz
  unsigned long now = millis();
  if (now - lastIMU >= IMU_INTERVAL_MS) {
    lastIMU = now;
    
    imu.read();
    sensors_event_t a, m, g, temp;
    imu.getEvent(&a, &m, &g, &temp);

    float ax     = a.acceleration.x;
    float ay     = a.acceleration.y;
    float az_raw = a.acceleration.z;
    float gx_raw = g.gyro.x;
    float gy_raw = g.gyro.y;
    float gz     = g.gyro.z;

    // High-pass filter
    az_filtered = alpha * (az_filtered + az_raw - az_prev);
    az_prev     = az_raw;
    gx_filtered = alpha * (gx_filtered + gx_raw - gx_prev);
    gx_prev     = gx_raw;
    gy_filtered = alpha * (gy_filtered + gy_raw - gy_prev);
    gy_prev     = gy_raw;

    // Print to Serial (For PC Logger)
    Serial.print(now); Serial.print(",");
    Serial.print(ax, 4); Serial.print(",");
    Serial.print(ay, 4); Serial.print(",");
    Serial.print(az_raw, 4); Serial.print(",");
    Serial.print(az_filtered, 4); Serial.print(",");
    Serial.print(gx_raw, 4); Serial.print(",");
    Serial.print(gx_filtered, 4); Serial.print(",");
    Serial.print(gy_raw, 4); Serial.print(",");
    Serial.print(gy_filtered, 4); Serial.print(",");
    Serial.print(gz, 4); Serial.print(",");
    Serial.print(currentLat, 6); Serial.print(",");
    Serial.println(currentLon, 6);

    // Write to SD Card without closing it
    if (sdReady) {
      dataFile.print(now); dataFile.print(",");
      dataFile.print(ax, 4); dataFile.print(",");
      dataFile.print(ay, 4); dataFile.print(",");
      dataFile.print(az_raw, 4); dataFile.print(",");
      dataFile.print(az_filtered, 4); dataFile.print(",");
      dataFile.print(gx_raw, 4); dataFile.print(",");
      dataFile.print(gx_filtered, 4); dataFile.print(",");
      dataFile.print(gy_raw, 4); dataFile.print(",");
      dataFile.print(gy_filtered, 4); dataFile.print(",");
      dataFile.print(gz, 4); dataFile.print(",");
      dataFile.print(currentLat, 6); dataFile.print(",");
      dataFile.println(currentLon, 6);
      
      // Flush to physical memory every 100 samples (1 second)
      sdFlushCounter++;
      if (sdFlushCounter >= 100) {
        dataFile.flush();
        sdFlushCounter = 0;
      }
    }
  }

  // Beacon every 15 minutes
  if (millis() - lastBeacon >= BEACON_INTERVAL_MS) {
    lastBeacon = millis();
    Serial.println("=== BEACON ===");
    Serial.print("LAT: ");    Serial.println(currentLat, 6);
    Serial.print("LON: ");    Serial.println(currentLon, 6);
    Serial.print("BAT%: ");   Serial.println(100);
    Serial.print("TIME_MS: "); Serial.println(millis());
    Serial.println("==============");
  }
}