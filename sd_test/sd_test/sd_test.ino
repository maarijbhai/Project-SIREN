#include <SPI.h>
#include <SD.h>

// Define our Chip Select pin
const int chipSelect = PA4; 

void setup() {
  Serial.begin(115200);
  
  // ==========================================
  // CRITICAL DFU SAFETY BUFFER
  // Gives the laptop 3 seconds to establish the USB CDC connection.
  // ==========================================
  delay(3000); 

  Serial.println("\n--- STM32 Black Pill SD Card Test ---");
  Serial.print("Initializing SD card...");

  // 1. Check if the card is present and can be initialized
  if (!SD.begin(chipSelect)) {
    Serial.println(" FAILED!");
    Serial.println("\nTroubleshooting Checklist:");
    Serial.println("1. Is the card firmly inserted?");
    Serial.println("2. Is your wiring correct (PA4, PA5, PA6, PA7)?");
    Serial.println("3. Is the SD card formatted to FAT32? (CRITICAL)");
    while (1) {
      delay(10); // Halt the system if it fails
    }
  }
  Serial.println(" SUCCESS!");

  // 2. Open a file for writing (it creates it if it doesn't exist)
  Serial.print("Writing to test.txt...");
  File myFile = SD.open("test.txt", FILE_WRITE);

  if (myFile) {
    // Write some data to the file
    myFile.println("SYSTEM BOOT SUCCESS.");
    myFile.println("The STM32 can successfully write to this SD card.");
    // Close the file to save the data
    myFile.close();
    Serial.println(" done.");
  } else {
    // If the file didn't open, print an error
    Serial.println(" ERROR: Could not open test.txt for writing.");
  }

  // 3. Re-open the file for reading
  Serial.println("Reading back from test.txt:\n");
  myFile = SD.open("test.txt");
  
  if (myFile) {
    // Read from the file until there's nothing else in it
    Serial.println("=== FILE CONTENTS START ===");
    while (myFile.available()) {
      Serial.write(myFile.read());
    }
    Serial.println("=== FILE CONTENTS END ===");
    
    // Close the file
    myFile.close();
  } else {
    // If the file didn't open, print an error
    Serial.println(" ERROR: Could not open test.txt for reading.");
  }
  
  Serial.println("\nTest complete. You can unplug the board.");
}

void loop() {
  // Nothing happens after setup
}