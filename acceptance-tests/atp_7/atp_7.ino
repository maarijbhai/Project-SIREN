// ============================================================
// SIREN — ATP-7: Beacon Packet
// MCU: STM32F401CE Black Pill
// GPS: u-blox NEO-6M  Serial2  RX=PA3  TX=PA2  9600 baud
//
// Procedure:
//   1. Flash and open Serial Monitor at 115200
//   2. Take unit outside with clear sky view
//   3. First beacon fires on GPS fix; next every 900 s
//   4. Capture full serial output to atp7_log.txt
//   5. Run validate.py for pass/fail verdict
//      (minimum 2 beacons required for interval check)
//
// Packet format (13 bytes, all little-endian):
//   [0]     0xBE        sync byte
//   [1-4]   float32     latitude  (degrees)
//   [5-8]   float32     longitude (degrees)
//   [9-12]  uint32_t    seconds since boot
//
// Pass: 13 bytes, sync=0xBE, coords match GPS ±0.001°,
//       interval = 900 ± 5 s
// ============================================================

#include <TinyGPS++.h>

HardwareSerial Serial2(PA3, PA2);      // RX=PA3, TX=PA2

#define GPS_SERIAL          Serial2
#define GPS_BAUD            9600

#define BEACON_INTERVAL_MS  60000UL    // 60 s — 1 minute
#define FIX_REPORT_MS        30000UL   // how often to print "waiting" status
#define COUNTDOWN_MS         60000UL   // how often to print countdown

TinyGPSPlus gps;

// Packed struct — 1+4+4+4 = 13 bytes, no padding
struct __attribute__((packed)) BeaconPacket {
  uint8_t  sync;   // 0xBE
  float    lat;
  float    lon;
  uint32_t ts;     // seconds since boot
};

unsigned long lastBeacon    = 0;
unsigned long lastFixReport = 0;
unsigned long lastCountdown = 0;
bool          fixed         = false;


// ── Transmit beacon ───────────────────────────────────────

void transmitBeacon() {
  BeaconPacket pkt;
  pkt.sync = 0xBE;
  pkt.lat  = (float)gps.location.lat();
  pkt.lon  = (float)gps.location.lng();
  pkt.ts   = millis() / 1000UL;

  // GPS snapshot on the line just before BEACON — used by validate.py
  Serial.print("# GPS lat=");
  Serial.print(pkt.lat, 6);
  Serial.print(" lon=");
  Serial.print(pkt.lon, 6);
  Serial.print(" sats=");
  Serial.print(gps.satellites.isValid() ? (int)gps.satellites.value() : 0);
  Serial.print(" hdop=");
  Serial.println(gps.hdop.isValid() ? gps.hdop.hdop() : -1.0f, 2);

  // Machine-parseable BEACON line
  Serial.print("BEACON ts=");
  Serial.print(pkt.ts);
  Serial.print(' ');
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(&pkt);
  for (uint8_t i = 0; i < sizeof(BeaconPacket); i++) {
    if (raw[i] < 0x10) Serial.print('0');
    Serial.print(raw[i], HEX);
    if (i < sizeof(BeaconPacket) - 1) Serial.print(' ');
  }
  Serial.println();
}


// ── Setup ─────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(2000);

  GPS_SERIAL.begin(GPS_BAUD);

  Serial.println("========================================");
  Serial.println("  SIREN ATP-7  Beacon Packet Test");
  Serial.println("  Waiting for GPS fix...");
  Serial.println("  Take unit outside, clear sky view.");
  Serial.println("========================================");

  lastFixReport = millis();
}


// ── Loop ──────────────────────────────────────────────────

void loop() {
  if (GPS_SERIAL.available()) {
  gps.encode(GPS_SERIAL.read());
}

  unsigned long now = millis();

  if (!fixed) {
    // ── Waiting for fix ──────────────────────────────────
    if (gps.location.isValid()) {
      fixed = true;
      Serial.print("# Fix acquired: sats=");
      Serial.println(gps.satellites.isValid() ? (int)gps.satellites.value() : 0);

      transmitBeacon();
      lastBeacon    = now;
      lastCountdown = now;
      Serial.println("# Next beacon in 900 s");

    } else if (now - lastFixReport >= FIX_REPORT_MS) {
      lastFixReport += FIX_REPORT_MS;
      Serial.print("# Waiting for fix — chars processed: ");
      Serial.println(gps.charsProcessed());
    }

  } else {
    // ── Fixed — countdown and beacon ─────────────────────
    if (now - lastCountdown >= COUNTDOWN_MS) {
      lastCountdown += COUNTDOWN_MS;
      uint32_t remaining = (BEACON_INTERVAL_MS - (now - lastBeacon)) / 1000UL;
      Serial.print("# Next beacon in ");
      Serial.print(remaining);
      Serial.println(" s");
    }

    if (now - lastBeacon >= BEACON_INTERVAL_MS) {
      lastBeacon += BEACON_INTERVAL_MS;
      transmitBeacon();
      Serial.println("# Next beacon in 900 s");
      lastCountdown = now;
    }
  }
}
