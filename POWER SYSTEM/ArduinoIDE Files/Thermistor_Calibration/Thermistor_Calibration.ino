#include <math.h>

// --- Pin Definition ---
#define NTC_PIN PA0

// --- Thermistor & ADC Constants ---
#define ADC_VREF       3.3f
#define ADC_RESOLUTION 4095.0f   // 12-bit ADC max value
#define NTC_R_FIXED    10000.0f  // 10k voltage divider resistor
#define NTC_R25        10000.0f  // 10k nominal resistance at 25°C
#define NTC_B          3900.0f   // Beta value for TTC3A103
#define NTC_T25_K      298.15f   // 25°C in Kelvin

void setup() {
  // Initialize the serial port for USB debugging
  Serial.begin(115200);
  
  // Force the STM32 core to use its full 12-bit ADC resolution
  analogReadResolution(12);
  
  // Give the serial port a moment to connect
  delay(2000); 
  Serial.println("--- SIREN Thermistor Test Started ---");
}

void loop() {
  // 1. Read the raw 12-bit ADC value
  int raw_adc = analogRead(NTC_PIN);
  
  // 2. Convert the raw reading to a voltage
  float vadc = ((float)raw_adc / ADC_RESOLUTION) * ADC_VREF;
  
  // 3. Safety Check: Prevent divide-by-zero if the pin is shorted or floating
  if (vadc <= 0.01f || vadc >= (ADC_VREF - 0.01f)) {
    Serial.println("FAULT: Thermistor disconnected or shorted!");
    delay(1000);
    return;
  }
  
  // 4. Calculate the actual resistance of the NTC
  float r_ntc = NTC_R_FIXED * vadc / (ADC_VREF - vadc);
  
  // 5. Apply the Steinhart-Hart (Beta) equation to get Kelvin, then convert to Celsius
  float inv_T = (1.0f / NTC_T25_K) + (1.0f / NTC_B) * log(r_ntc / NTC_R25);
  float temp_c = (1.0f / inv_T) - 273.15f;
  
  // 6. Print the formatted data to the Serial Monitor
  Serial.print("Raw ADC: ");
  Serial.print(raw_adc);
  Serial.print(" | Voltage: ");
  Serial.print(vadc, 2);
  Serial.print("V | Temp: ");
  Serial.print(temp_c, 2);
  Serial.println(" °C");
  
  // Wait 1 second before taking the next reading
  delay(1000);
}