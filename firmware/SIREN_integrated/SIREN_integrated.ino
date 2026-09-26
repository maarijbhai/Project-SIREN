// ============================================================
// SIREN — Full Integrated Firmware
// STM32F401CE Black Pill  |  EEE4113F Group 9  |  UCT 2026
//
// IMU:   LSM9DS1  I2C   SDA=PB7  SCL=PB6
// GPS:   NEO-6M   UART2 RX=PA3   TX=PA2  9600 baud
// SD:    Module   SPI   CS=PB12  MOSI=PA7 MISO=PA6 SCK=PA5
// COMMS: ESP32    UART1 TX=PA9   RX=PA10 9600 baud
//
// Beacon format every 15 min:
//   ts,lat,lon\n
//   END_OF_DATA\n
// ============================================================

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_LSM9DS1.h>
#include <Adafruit_Sensor.h>

// ── Pins ────────────────────────────────────────────────────
#define SD_CS_PIN       PB12

// ── Timing ──────────────────────────────────────────────────
#define SAMPLE_INTERVAL_MS  14UL        // ~71 Hz
#define BEACON_INTERVAL_MS  60000UL    // 15 min — use 60000 for testing
#define BUFFER_SIZE         500
char gps_utc[24] = "NO_FIX";

// ── Filter ──────────────────────────────────────────────────
#define HP_ALPHA  0.995f

// ── Peripherals ─────────────────────────────────────────────
Adafruit_LSM9DS1  imu;
HardwareSerial    GPS_Serial(PA3, PA2);   // RX, TX
HardwareSerial    Comms(PA10, PA9); 

// ── GPS state ───────────────────────────────────────────────
float   currentLat  = 0.0f;
float   currentLon  = 0.0f;
bool    gps_fix     = false;
String  nmeaBuffer  = "";

// ── IMU filter state ────────────────────────────────────────
float   az_prev     = 0.0f;
float   az_filt     = 0.0f;
bool    filter_init = false;

// ── Timing state ────────────────────────────────────────────
uint32_t lastSample = 0;
uint32_t lastBeacon = 0;
uint32_t rowCount   = 0;

// ── CSV row buffer ───────────────────────────────────────────
struct Row {
  uint32_t ts;
  float ax, ay, az_raw, az_filt;
  float gx, gy, gz;
  float lat, lon;
};
Row      buf[BUFFER_SIZE];
uint16_t bufHead = 0;

File dataFile;
bool sd_ok = false;

// ============================================================
// GPS — NMEA parser (GPRMC only, non-blocking)
// ============================================================
float parseCoord(String raw, String dir) {
  if (raw.length() < 4) return 0.0f;
  int dot = raw.indexOf('.');
  if (dot < 2) return 0.0f;
  float deg = raw.substring(0, dot - 2).toFloat();
  float min = raw.substring(dot - 2).toFloat();
  float result = deg + min / 60.0f;
  if (dir == "S" || dir == "W") result = -result;
  return result;
}

void parseGPRMC(String s) {
  // $GPRMC,hhmmss,A/V,lat,N/S,lon,E/W,...
  int idx[12];
  int n = 0;
  idx[n++] = 0;
  for (int i = 0; i < (int)s.length() && n < 12; i++) {
    if (s[i] == ',') idx[n++] = i + 1;
  }
  if (n < 7) return;
  String status = s.substring(idx[2], idx[3] - 1);
  if (status != "A") { gps_fix = false; return; }
  currentLat = parseCoord(s.substring(idx[3], idx[4]-1), s.substring(idx[4], idx[5]-1));
  currentLon = parseCoord(s.substring(idx[5], idx[6]-1), s.substring(idx[6], idx[7]-1));
  gps_fix = true;
}

void readGPS() {
  while (GPS_Serial.available()) {
    char c = GPS_Serial.read();
    if (c == '$') {
      nmeaBuffer = "$";
    } else if (c == '\n') {
      if (nmeaBuffer.startsWith("$GPRMC")) parseGPRMC(nmeaBuffer);
      nmeaBuffer = "";
    } else {
      nmeaBuffer += c;
      if (nmeaBuffer.length() > 100) nmeaBuffer = "";
    }
  }
}

// ============================================================
// SD — flush buffer to CSV
// ============================================================
void flushBuffer() {
  if (!sd_ok || bufHead == 0) return;
  for (uint16_t i = 0; i < bufHead; i++) {
    Row &r = buf[i];
    dataFile.print(r.ts);        dataFile.print(',');
    dataFile.print(r.ax,   4);   dataFile.print(',');
    dataFile.print(r.ay,   4);   dataFile.print(',');
    dataFile.print(r.az_raw,4);  dataFile.print(',');
    dataFile.print(r.az_filt,4); dataFile.print(',');
    dataFile.print(r.gx,   4);   dataFile.print(',');
    dataFile.print(r.gy,   4);   dataFile.print(',');
    dataFile.print(r.gz,   4);   dataFile.print(',');
    dataFile.print(r.lat,  6);   dataFile.print(',');
    dataFile.println(r.lon, 6);
    rowCount++;
  }
  dataFile.flush();
  bufHead = 0;
}

// ============================================================
// COMMS — send beacon to ESP32
// Format:
//   ts,lat,lon\n
//   END_OF_DATA\n
// ============================================================
void sendBeacon() {
  uint32_t ts = millis() / 1000;

  float lat = gps_fix ? currentLat : -33.95878f;
  float lon = gps_fix ? currentLon :  18.46029f;

  Comms.print(ts);
  Comms.print(',');
  Comms.print(lat, 6);
  Comms.print(',');
  Comms.print(lon, 6);
  Comms.print('\n');
  Comms.println("END_OF_DATA");

  Serial.print(F("[BEACON] ts="));
  Serial.print(ts);
  Serial.print(F(" lat="));
  Serial.print(lat, 6);
  Serial.print(F(" lon="));
  Serial.print(lon, 6);
  Serial.print(F(" fix="));
  Serial.println(gps_fix ? "YES — real GPS" : "NO — using fallback UCT coords");
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println(F("=== SIREN Boot ==="));

  // GPS
  GPS_Serial.begin(9600);
  Comms.begin(115200);

  // Comms to ESP32
  Comms.begin(115200);
  Serial.println(F("Comms UART OK"));

  // IMU — I2C
  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();
  if (!imu.begin()) {
    Serial.println(F("FATAL: IMU not found"));
    while (1) {}
  }
  imu.setupAccel(imu.LSM9DS1_ACCELRANGE_8G,
                 imu.LSM9DS1_ACCELDATARATE_952HZ);
  imu.setupGyro(imu.LSM9DS1_GYROSCALE_245DPS);
  imu.setupMag(imu.LSM9DS1_MAGGAIN_4GAUSS);
  Serial.println(F("IMU init OK"));

  // SD — SPI
  SPI.setMOSI(PB15);
  SPI.setMISO(PB14);
  SPI.setSCLK(PB13);
  SPI.begin();
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  delay(200);

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("FATAL: SD not found"));
    while (1) {}
  }
  dataFile = SD.open("siren.csv", FILE_WRITE);
  if (!dataFile) {
    Serial.println(F("FATAL: Cannot open siren.csv"));
    while (1) {}
  }
  if (dataFile.size() == 0) {
    dataFile.println(F("timestamp_ms,ax,ay,az_raw,az_filt,gx,gy,gz,lat,lon"));
  }
  sd_ok = true;
  Serial.println(F("SD init OK"));

  lastSample = millis();
  lastBeacon = millis();
  Serial.println(F("=== All systems OK — logging ==="));
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  uint32_t now = millis();

  // GPS — non-blocking read
  readGPS();

  // Listen for GET_DATA from ESP32
  if (Comms.available()) {
    String cmd = Comms.readStringUntil('\n');
    cmd.trim();

    // Strip non-printable boot garbage
    String clean = "";
    for (int i = 0; i < cmd.length(); i++) {
      if (isPrintable(cmd[i])) clean += cmd[i];
    }

    Serial.print(F("[COMMS] RX: ")); Serial.println(clean);

    if (clean == "GET_DATA") {
      Serial.println(F("[COMMS] GET_DATA received — sending beacon"));
      flushBuffer();
      sendBeacon();
      Serial.println(F("[COMMS] Beacon sent"));
    }
  }

  // IMU sample at ~71Hz
  if (now - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample += SAMPLE_INTERVAL_MS;

    imu.read();
    sensors_event_t a, m, g, temp;
    imu.getEvent(&a, &m, &g, &temp);

    float ax = a.acceleration.x;
    float ay = a.acceleration.y;
    float az = a.acceleration.z;
    float gx = g.gyro.x;
    float gy = g.gyro.y;
    float gz = g.gyro.z;

    if (!filter_init) {
      az_prev     = az;
      az_filt     = 0.0f;
      filter_init = true;
    } else {
      az_filt = HP_ALPHA * (az_filt + az - az_prev);
      az_prev = az;
    }

    // Tap detection print for demo
    if (abs(az_filt) > 10.0f) {
      Serial.print(F("[TAP DETECTED] az_filt="));
      Serial.println(az_filt, 3);
    }

    buf[bufHead++] = { now, ax, ay, az, az_filt, gx, gy, gz, currentLat, currentLon };

    if (bufHead >= BUFFER_SIZE) flushBuffer();
  }

  // Periodic SD flush only — no auto beacon
  if (now - lastBeacon >= BEACON_INTERVAL_MS) {
    lastBeacon = now;
    flushBuffer();
    Serial.println(F("[SD] Periodic flush"));
  }
}