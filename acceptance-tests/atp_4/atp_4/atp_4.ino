// ============================================================
// SIREN — ATP-4: GPS Position Accuracy
// NEO-6M via UART: RX=PA3, TX=PA2
//
// Pass criteria:
//   Fix acquired within 5 minutes
//   Position error < 10m from reference coordinate
//   NMEA sentences: 60 +/- 3 per minute
//
// Procedure:
//   1. Upload this sketch
//   2. Take board OUTSIDE with clear sky view
//   3. Open Serial Monitor at 115200
//   4. Wait for fix — board prints coordinates automatically
//   5. Screenshot the results
//   6. Compare coordinates to Google Maps
// ============================================================

#include <TinyGPSPlus.h>

TinyGPSPlus gps;

// ── State ────────────────────────────────────────────────────
unsigned long testStart        = 0;
unsigned long fixTime          = 0;
unsigned long lastSentenceCount = 0;
unsigned long sentenceCountStart = 0;
uint32_t      sentencesThisMin = 0;
uint32_t      totalSentences   = 0;
bool          fixAcquired      = false;
bool          resultsShown     = false;
bool          countingStarted  = false;

// ── Best fix recorded ────────────────────────────────────────
double bestLat  = 0.0;
double bestLon  = 0.0;
float  bestHDOP = 999.0;

void printResults() {
  unsigned long ttff = (fixTime - testStart) / 1000;

  Serial.println();
  Serial.println("==========================================");
  Serial.println("ATP-4: GPS POSITION ACCURACY — RESULTS");
  Serial.println("==========================================");

  // Time to first fix
  Serial.print("Time to fix:      ");
  Serial.print(ttff);
  Serial.print(" seconds  ");
  if (ttff <= 300) {
    Serial.println("[PASS — under 5 min]");
  } else {
    Serial.println("[FAIL — exceeded 5 min]");
  }

  // Coordinates
  Serial.println();
  Serial.println("--- Copy these into Google Maps ---");
  Serial.print("Latitude:         ");
  Serial.println(bestLat, 6);
  Serial.print("Longitude:        ");
  Serial.println(bestLon, 6);
  Serial.print("Google Maps fmt:  ");
  Serial.print(bestLat, 6);
  Serial.print(", ");
  Serial.println(bestLon, 6);
  Serial.println("-----------------------------------");

  // Quality
  Serial.print("HDOP:             ");
  Serial.print(bestHDOP, 1);
  if (bestHDOP < 2.0) {
    Serial.println("  [Excellent]");
  } else if (bestHDOP < 5.0) {
    Serial.println("  [Good]");
  } else {
    Serial.println("  [Poor — position error may be higher]");
  }

  Serial.print("Satellites:       ");
  Serial.println(gps.satellites.value());

  Serial.println();
  Serial.println("Counting NMEA sentences for 60 seconds...");
  Serial.println("Hold still — do not move the board");
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial1.setRx(PA3);
  Serial1.setTx(PA2);
  Serial1.begin(9600);

  testStart = millis();

  Serial.println("==============================");
  Serial.println("ATP-4: GPS Position Accuracy");
  Serial.println("==============================");
  Serial.println("Waiting for GPS fix...");
  Serial.println("Make sure you are OUTSIDE");
  Serial.println("with clear sky view.");
  Serial.println();
}

void loop() {
  // Feed GPS one byte at a time — non-blocking
  if (Serial1.available()) {
    char c = Serial1.read();
    if (gps.encode(c)) {
      totalSentences++;

      // Count sentences per minute after fix
      if (countingStarted) {
        sentencesThisMin++;
      }

      // Update best fix
      if (gps.location.isValid() && gps.hdop.isValid()) {
        float hdop = gps.hdop.hdop();
        if (!fixAcquired) {
          fixAcquired = true;
          fixTime     = millis();
          bestLat     = gps.location.lat();
          bestLon     = gps.location.lng();
          bestHDOP    = hdop;
          Serial.print("FIX ACQUIRED at t=");
          Serial.print((fixTime - testStart) / 1000);
          Serial.println("s");
          printResults();
          sentenceCountStart = millis();
          countingStarted    = true;
        }
        // Keep updating best position
        if (hdop < bestHDOP) {
          bestHDOP = hdop;
          bestLat  = gps.location.lat();
          bestLon  = gps.location.lng();
        }
      }
    }
  }

  // Warn if no fix after 5 minutes
  if (!fixAcquired &&
      millis() - testStart > 300000) {
    static bool warned = false;
    if (!warned) {
      Serial.println("WARNING: No fix after 5 minutes");
      Serial.println("Are you outside with clear sky?");
      warned = true;
    }
  }

  // Print sentence count after 60 seconds
  if (countingStarted &&
      !resultsShown &&
      millis() - sentenceCountStart >= 60000) {

    resultsShown = true;

    Serial.println();
    Serial.println("==========================================");
    Serial.println("ATP-4: FINAL RESULTS");
    Serial.println("==========================================");

    // NMEA sentence rate
    Serial.print("NMEA sentences/min: ");
    Serial.print(sentencesThisMin);
    Serial.print("  ");
    if (sentencesThisMin >= 57 && sentencesThisMin <= 63) {
      Serial.println("[PASS — 60 +/- 3]");
    } else {
      Serial.println("[FAIL — outside 60 +/- 3]");
    }

    // Final best position
    Serial.println();
    Serial.println("Best position recorded:");
    Serial.print("  Lat: "); Serial.println(bestLat, 6);
    Serial.print("  Lon: "); Serial.println(bestLon, 6);
    Serial.print("  HDOP: "); Serial.println(bestHDOP, 1);
    Serial.print("  Sats: ");
    Serial.println(gps.satellites.value());

    Serial.println();
    Serial.println("==========================================");
    Serial.println("Compare coordinates to Google Maps.");
    Serial.println("Calculate error using Haversine formula.");
    Serial.println("Screenshot this output for your report.");
    Serial.println("==========================================");
  }

  // Keep printing live position every 10s after fix
  if (fixAcquired && !resultsShown) {
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint >= 10000) {
      lastPrint = millis();
      unsigned long remaining =
        60 - (millis() - sentenceCountStart) / 1000;
      Serial.print("Live: ");
      Serial.print(gps.location.lat(), 6);
      Serial.print(", ");
      Serial.print(gps.location.lng(), 6);
      Serial.print("  HDOP=");
      Serial.print(gps.hdop.hdop(), 1);
      Serial.print("  Sats=");
      Serial.print(gps.satellites.value());
      Serial.print("  Sentence count: ");
      Serial.print(remaining);
      Serial.println("s remaining");
    }
  }
}