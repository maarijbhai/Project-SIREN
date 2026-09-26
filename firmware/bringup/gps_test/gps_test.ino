#include <TinyGPS++.h>

TinyGPSPlus gps;

void setup() {
  Serial.begin(115200); // USB Serial Monitor
  
  // ==========================================
  // CRITICAL DFU SAFETY BUFFER
  // ==========================================
  delay(3000); 

  // Explicitly route Hardware Serial 1 to your pins
  Serial1.setRx(PA3);
  Serial1.setTx(PA2);
  Serial1.begin(9600);  
  
  Serial.println(F("--- STM32 GPS Test ---"));
  Serial.println(F("Initializing Hardware UART on PB6 & PB7..."));
}

void loop() {
  // Read data from the hardware Serial1 and feed it to TinyGPS++
  while (Serial1.available() > 0) {
    if (gps.encode(Serial1.read())) {
      displayInfo();
    }
  }

  // Warning if no signal is detected after 5 seconds
  if (millis() > 5000 && gps.charsProcessed() < 10) {
    Serial.println(F("ERROR: No GPS data received. Check your PB6/PB7 wiring!"));
    while(true) {
      delay(10); // Halt the board
    }
  }
}

void displayInfo() {
  Serial.print(F("Status: ")); 
  if (gps.location.isValid()) {
    // This will only trigger once you plug in the antenna and get a lock
    Serial.print(gps.location.lat(), 6);
    Serial.print(F(", "));
    Serial.print(gps.location.lng(), 6);
  } else {
    // This proves the STM32 is successfully reading the empty NMEA sentences!
    Serial.print(F("Wiring good! STM32 sees the GPS. (Antenna/Satellite lock needed for location)"));
  }
  Serial.println();
}